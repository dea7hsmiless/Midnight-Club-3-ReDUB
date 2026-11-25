/*
 * kernel_thread.c - Xbox Threading Subsystem
 *
 * Implements Xbox thread creation, termination, delays, and priority
 * management using Win32 threading APIs.
 *
 * Xbox threading model:
 *   - PsCreateSystemThreadEx creates kernel-mode threads (→ CreateThread)
 *   - Thread start routines are __stdcall with a single PVOID context
 *   - Time intervals use NT 100-nanosecond units (negative = relative)
 *   - Thread priorities use NT KPRIORITY increments
 */

#include <stdio.h>
#include "kernel.h"
#include "xbox_memory_layout.h"   /* XBOX_WORKER_STACK_* + worker-stack decls */

/* ============================================================================
 * Thread Start Wrapper
 *
 * Xbox start routines are __stdcall void(*)(PVOID), but Win32 CreateThread
 * expects DWORD WINAPI (*)(LPVOID). We wrap the Xbox routine to bridge
 * the calling convention and return type.
 * ============================================================================ */

typedef struct _XBOX_THREAD_START_INFO {
    PXBOX_SYSTEM_ROUTINE StartRoutine;
    PVOID                StartContext;
} XBOX_THREAD_START_INFO;

static DWORD WINAPI xbox_thread_wrapper(LPVOID lpParameter)
{
    XBOX_THREAD_START_INFO info = *(XBOX_THREAD_START_INFO*)lpParameter;

    /* Free the start info before calling the routine - the routine may
     * never return (calling PsTerminateSystemThread instead) */
    HeapFree(GetProcessHeap(), 0, lpParameter);

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD, "Thread %u starting at %p",
        GetCurrentThreadId(), info.StartRoutine);

    info.StartRoutine(info.StartContext);

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD, "Thread %u returned normally",
        GetCurrentThreadId());

    xbox_kernel_guest_unregister();
    return 0;
}

/* ============================================================================
 * PsCreateSystemThreadEx
 *
 * Xbox signature:
 *   PsCreateSystemThreadEx(
 *     OUT PHANDLE ThreadHandle,
 *     IN ULONG ThreadExtraSize,      // extra bytes in thread object (ignored)
 *     IN ULONG KernelStackSize,      // stack size (0 = default)
 *     IN ULONG TlsDataSize,          // TLS data size (Xbox-specific, ignored)
 *     OUT PULONG ThreadId,           // optional thread ID
 *     IN PVOID StartContext1,        // context passed to StartRoutine
 *     IN PVOID StartContext2,        // alternate context (unused by game code)
 *     IN BOOLEAN CreateSuspended,
 *     IN BOOLEAN DebugStack,         // debug stack (ignored)
 *     IN PXBOX_SYSTEM_ROUTINE StartRoutine
 *   )
 *
 * Maps to: CreateThread with a wrapper for calling convention adaptation.
 * ============================================================================ */

NTSTATUS __stdcall xbox_PsCreateSystemThreadEx(
    PHANDLE ThreadHandle,
    ULONG ThreadExtraSize,
    ULONG KernelStackSize,
    ULONG TlsDataSize,
    PULONG ThreadId,
    PVOID StartContext1,
    PVOID StartContext2,
    BOOLEAN CreateSuspended,
    BOOLEAN DebugStack,
    PXBOX_SYSTEM_ROUTINE StartRoutine)
{
    XBOX_THREAD_START_INFO* info;
    HANDLE hThread;
    DWORD dwThreadId;
    DWORD dwCreationFlags;

    (void)ThreadExtraSize;
    (void)TlsDataSize;
    (void)StartContext2;
    (void)DebugStack;

    if (!ThreadHandle || !StartRoutine)
        return STATUS_INVALID_PARAMETER;

    /* Allocate start info - freed by the wrapper thread */
    info = (XBOX_THREAD_START_INFO*)HeapAlloc(GetProcessHeap(), 0, sizeof(XBOX_THREAD_START_INFO));
    if (!info)
        return STATUS_NO_MEMORY;

    info->StartRoutine = StartRoutine;
    info->StartContext = StartContext1;

    dwCreationFlags = CreateSuspended ? CREATE_SUSPENDED : 0;

    /* Use default stack size if 0 (Xbox default is 64KB) */
    if (KernelStackSize == 0)
        KernelStackSize = 65536;

    hThread = CreateThread(NULL, KernelStackSize, xbox_thread_wrapper, info,
                           dwCreationFlags, &dwThreadId);
    if (!hThread) {
        HeapFree(GetProcessHeap(), 0, info);
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_THREAD,
            "PsCreateSystemThreadEx: CreateThread failed (error %u)", GetLastError());
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *ThreadHandle = hThread;
    if (ThreadId)
        *ThreadId = dwThreadId;

    xbox_log(XBOX_LOG_INFO, XBOX_LOG_THREAD,
        "PsCreateSystemThreadEx: created thread %u (handle=%p, routine=%p, suspended=%d)",
        dwThreadId, hThread, StartRoutine, CreateSuspended);

    return STATUS_SUCCESS;
}

/* ============================================================================
 * PsTerminateSystemThread
 *
 * Terminates the calling thread. On Xbox this is the standard way for
 * system threads to exit. Maps directly to ExitThread.
 * ============================================================================ */

NTSTATUS __stdcall xbox_PsTerminateSystemThread(NTSTATUS ExitStatus)
{
    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "PsTerminateSystemThread: thread %u exiting with status 0x%08X",
        GetCurrentThreadId(), ExitStatus);

    xbox_kernel_guest_unregister();
    ExitThread((DWORD)ExitStatus);

    /* ExitThread never returns, but the compiler needs this */
    return STATUS_SUCCESS;
}

/* ============================================================================
 * KeDelayExecutionThread
 *
 * Delays the current thread. The interval uses NT 100-nanosecond units:
 *   - Negative values = relative delay (most common)
 *   - Positive values = absolute time (rare)
 *   - Zero = yield
 *
 * Maps to: SleepEx (for alertable waits) or Sleep
 * ============================================================================ */

NTSTATUS __stdcall xbox_KeDelayExecutionThread(
    KPROCESSOR_MODE WaitMode,
    BOOLEAN Alertable,
    PLARGE_INTEGER Interval)
{
    DWORD ms;

    (void)WaitMode;

    if (!Interval)
        return STATUS_INVALID_PARAMETER;

    if (Interval->QuadPart == 0) {
        /* Zero interval = yield the thread's time slice */
        SwitchToThread();
        return STATUS_SUCCESS;
    }

    if (Interval->QuadPart < 0) {
        /* Negative = relative time in 100ns units. Convert to milliseconds. */
        LONGLONG relative_100ns = -Interval->QuadPart;
        ms = (DWORD)(relative_100ns / 10000);
        /* Ensure at least 1ms for very short intervals */
        if (ms == 0 && relative_100ns > 0)
            ms = 1;
    } else {
        /* Positive = absolute time. Calculate relative delay from now. */
        LARGE_INTEGER now;
        GetSystemTimeAsFileTime((LPFILETIME)&now);
        LONGLONG diff = Interval->QuadPart - now.QuadPart;
        if (diff <= 0)
            return STATUS_SUCCESS; /* Already past */
        ms = (DWORD)(diff / 10000);
    }

    xbox_kernel_busy(-1);               /* sleeping holds nothing: a safe point */
    if (Alertable) {
        DWORD result = SleepEx(ms, TRUE);
        xbox_kernel_busy(1);
        if (result == WAIT_IO_COMPLETION)
            return STATUS_ALERTED;
    } else {
        Sleep(ms);
        xbox_kernel_busy(1);
    }

    return STATUS_SUCCESS;
}

/* ============================================================================
 * Thread Priority
 *
 * Xbox uses NT KPRIORITY base priority increments relative to the process.
 * We map these to Win32 thread priority levels.
 * ============================================================================ */

/*
 * Map Xbox priority increment to Win32 priority level.
 * Xbox base priorities typically range from -2 to +2 for game threads.
 */
static int xbox_priority_to_win32(LONG increment)
{
    if (increment <= -15)       return THREAD_PRIORITY_IDLE;
    else if (increment <= -2)   return THREAD_PRIORITY_LOWEST;
    else if (increment == -1)   return THREAD_PRIORITY_BELOW_NORMAL;
    else if (increment == 0)    return THREAD_PRIORITY_NORMAL;
    else if (increment == 1)    return THREAD_PRIORITY_ABOVE_NORMAL;
    else if (increment <= 2)    return THREAD_PRIORITY_HIGHEST;
    else                        return THREAD_PRIORITY_TIME_CRITICAL;
}

static LONG win32_priority_to_xbox(int priority)
{
    switch (priority) {
        case THREAD_PRIORITY_IDLE:          return -15;
        case THREAD_PRIORITY_LOWEST:        return -2;
        case THREAD_PRIORITY_BELOW_NORMAL:  return -1;
        case THREAD_PRIORITY_NORMAL:        return 0;
        case THREAD_PRIORITY_ABOVE_NORMAL:  return 1;
        case THREAD_PRIORITY_HIGHEST:       return 2;
        case THREAD_PRIORITY_TIME_CRITICAL: return 15;
        default:                            return 0;
    }
}

LONG __stdcall xbox_KeSetBasePriorityThread(PVOID Thread, LONG Increment)
{
    HANDLE hThread = (HANDLE)Thread;
    LONG previous;

    /* Get previous priority before setting new one */
    int prev_win32 = GetThreadPriority(hThread);
    previous = win32_priority_to_xbox(prev_win32);

    int new_win32 = xbox_priority_to_win32(Increment);
    SetThreadPriority(hThread, new_win32);

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "KeSetBasePriorityThread: thread=%p, increment=%d (win32=%d), prev=%d",
        Thread, Increment, new_win32, previous);

    return previous;
}

LONG __stdcall xbox_KeQueryBasePriorityThread(PVOID Thread)
{
    HANDLE hThread = (HANDLE)Thread;
    int win32_priority = GetThreadPriority(hThread);
    return win32_priority_to_xbox(win32_priority);
}

/* ============================================================================
 * KeAlertThread
 *
 * Sends an alert to a thread, which can wake it from an alertable wait.
 * On Xbox, this sets the alerted flag on the thread object.
 * We approximate this with QueueUserAPC using a no-op APC routine.
 * ============================================================================ */

static VOID CALLBACK xbox_alert_apc(ULONG_PTR dwParam)
{
    (void)dwParam;
    /* No-op - the purpose is just to wake the thread from alertable wait */
}

NTSTATUS __stdcall xbox_KeAlertThread(PVOID Thread, KPROCESSOR_MODE AlertMode)
{
    HANDLE hThread = (HANDLE)Thread;

    (void)AlertMode;

    if (!QueueUserAPC(xbox_alert_apc, hThread, 0)) {
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_THREAD,
            "KeAlertThread: QueueUserAPC failed (error %u)", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

/* ============================================================================
 * NtYieldExecution
 *
 * Yields the current thread's remaining time slice.
 * Maps directly to SwitchToThread.
 * ============================================================================ */

NTSTATUS __stdcall xbox_NtYieldExecution(void)
{
    SwitchToThread();
    return STATUS_SUCCESS;
}

/* ============================================================================
 * NtDuplicateObject
 *
 * Duplicates a kernel handle. On Xbox this is simpler than Win32 since
 * there's only one process. Maps to DuplicateHandle within the same process.
 * ============================================================================ */

NTSTATUS __stdcall xbox_NtDuplicateObject(
    HANDLE SourceHandle,
    PHANDLE TargetHandle,
    ULONG Options)
{
    HANDLE hProcess = GetCurrentProcess();
    DWORD dwOptions = 0;

    if (!TargetHandle)
        return STATUS_INVALID_PARAMETER;

    /* Xbox DUPLICATE_CLOSE_SOURCE = 0x1, same as Win32 */
    if (Options & 0x1)
        dwOptions |= DUPLICATE_CLOSE_SOURCE;
    /* Xbox DUPLICATE_SAME_ACCESS = 0x2, same as Win32 */
    if (Options & 0x2)
        dwOptions |= DUPLICATE_SAME_ACCESS;

    if (!DuplicateHandle(hProcess, SourceHandle, hProcess, TargetHandle,
                         0, FALSE, dwOptions)) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_THREAD,
            "NtDuplicateObject: DuplicateHandle failed (error %u)", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "NtDuplicateObject: source=%p → target=%p (options=0x%X)",
        SourceHandle, *TargetHandle, Options);

    return STATUS_SUCCESS;
}

/* Guest threads park cooperatively after releasing the execution lock.
 * A native SuspendThread can freeze that lock or the scheduler/CRT locks;
 * kernel busy depth alone cannot identify a safe instruction to freeze. */
typedef struct KThreadSlot {
    volatile LONG tid;
    volatile LONG busy;          /* kernel depth + raised IRQL */
    volatile LONG guest;         /* 0 host, 1 guest, 2 retired guest */
    volatile LONG suspend_count;
    LONG native_start;           /* the initial CREATE_SUSPENDED host gate */
    struct KThreadSlot *next;
} KThreadSlot;
static KThreadSlot *g_kbusy;
static XBOX_THREAD_LOCAL KThreadSlot *t_kslot;
static CRITICAL_SECTION s_suspend_lock;
static CONDITION_VARIABLE s_suspend_cv;
static volatile LONG s_suspend_init;
extern volatile int g_gil_contended;
extern int xbox_thread_holds_dispatch(void);
extern int xbox_gil_suspend(void);
extern void xbox_gil_resume(int depth);

static void suspend_lock(void)
{
    if (InterlockedCompareExchange(&s_suspend_init, 1, 0) == 0) {
        InitializeCriticalSection(&s_suspend_lock);
        InitializeConditionVariable(&s_suspend_cv);
        InterlockedExchange(&s_suspend_init, 2);
    }
    while (InterlockedCompareExchange(&s_suspend_init, 2, 2) != 2) Sleep(0);
    EnterCriticalSection(&s_suspend_lock);
}

static KThreadSlot *kbusy_slot(DWORD tid, int create)
{
    KThreadSlot *k;
    if (!tid) return NULL;
    suspend_lock();
    for (k = g_kbusy; k; k = k->next) {
        if ((DWORD)k->tid == tid) break;
    }
    if (!k && create) {
        k = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*k));
        if (k) { k->next = g_kbusy; g_kbusy = k; }
        if (k) k->tid = (LONG)tid;
    }
    /* A native ID cannot be reused until its old thread has exited. Keep
     * retired entries so a guest is never natively frozen during teardown. */
    if (k && create && k->guest == 2) {
        k->guest = k->busy = k->suspend_count = 0;
        k->native_start = 0;
    }
    LeaveCriticalSection(&s_suspend_lock);
    return k;
}

void xbox_kernel_busy(int delta)
{
    if (!t_kslot)
        t_kslot = kbusy_slot(GetCurrentThreadId(), 1);
    if (t_kslot)
        t_kslot->busy += delta;
}

BOOL xbox_kernel_guest_prepare(HANDLE thread)
{
    KThreadSlot *k = kbusy_slot(GetThreadId(thread), 1);
    if (!k) return FALSE;
    suspend_lock();
    k->guest = 1;
    k->native_start = 1;
    LeaveCriticalSection(&s_suspend_lock);
    return TRUE;
}

void xbox_kernel_guest_register(void)
{
    if (!t_kslot) t_kslot = kbusy_slot(GetCurrentThreadId(), 1);
    if (t_kslot) InterlockedExchange(&t_kslot->guest, 1);
}

void xbox_kernel_guest_unregister(void)
{
    if (!t_kslot) return;
    suspend_lock();
    t_kslot->busy = t_kslot->suspend_count = 0;
    t_kslot->native_start = 0;
    t_kslot->guest = 2; /* retired; retain the native ID through host teardown */
    t_kslot = NULL;
    WakeAllConditionVariable(&s_suspend_cv);
    LeaveCriticalSection(&s_suspend_lock);
}

int xbox_kernel_suspend_pending(void)
{
    return t_kslot && t_kslot->guest && t_kslot->busy <= 0 &&
        InterlockedCompareExchange(&t_kslot->suspend_count, 0, 0) > 0 &&
        !xbox_thread_holds_dispatch();
}

void xbox_kernel_suspend_checkpoint(void)
{
    if (!xbox_kernel_suspend_pending()) return;
    suspend_lock();
    while (t_kslot->suspend_count > 0)
        SleepConditionVariableCS(&s_suspend_cv, &s_suspend_lock, INFINITE);
    LeaveCriticalSection(&s_suspend_lock);
}

NTSTATUS __stdcall xbox_NtSuspendThread(
    HANDLE ThreadHandle,
    PULONG PreviousSuspendCount)
{
    DWORD prev, exit_status, tid = GetThreadId(ThreadHandle);
    KThreadSlot *k = kbusy_slot(tid, 0);

    suspend_lock();
    if (k && (DWORD)k->tid == tid && k->guest &&
        (k->guest == 2 || (GetExitCodeThread(ThreadHandle, &exit_status) &&
                          exit_status != STILL_ACTIVE))) {
        LeaveCriticalSection(&s_suspend_lock);
        return (NTSTATUS)0xC000004Bu; /* STATUS_THREAD_IS_TERMINATING */
    }
    if (k && (DWORD)k->tid == tid && k->guest) {
        prev = (DWORD)k->suspend_count + (DWORD)k->native_start;
        if (prev >= 127) {
            LeaveCriticalSection(&s_suspend_lock);
            return (NTSTATUS)0xC000004Au; /* STATUS_SUSPEND_COUNT_EXCEEDED */
        }
        InterlockedIncrement(&k->suspend_count);
        g_gil_contended = 1; /* request the next generated pre-emption point */
        LeaveCriticalSection(&s_suspend_lock);
        if (PreviousSuspendCount) *PreviousSuspendCount = prev;
        if (tid == GetCurrentThreadId()) {
            int depth = xbox_gil_suspend();
            xbox_kernel_busy(-1);
            xbox_kernel_suspend_checkpoint();
            xbox_kernel_busy(1);
            xbox_gil_resume(depth);
        }
        return STATUS_SUCCESS;
    }
    LeaveCriticalSection(&s_suspend_lock);

    /*
     * SuspendThread returns the previous suspend count, or (DWORD)-1 on
     * failure. The Xbox call reports that count through an out-parameter, so
     * the two are not interchangeable: -1 must become an error status rather
     * than a suspend count of 0xFFFFFFFF.
     */
    /* Unregistered host threads still use the host API. Guest threads are
     * registered before their initial host gate is opened. */
    prev = SuspendThread(ThreadHandle);
    if (prev == (DWORD)-1) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_THREAD,
            "NtSuspendThread: SuspendThread failed (error %u)", GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    if (PreviousSuspendCount)
        *PreviousSuspendCount = (ULONG)prev;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "NtSuspendThread: thread=%p previous_count=%u", ThreadHandle, prev);

    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtResumeThread(
    HANDLE ThreadHandle,
    PULONG PreviousSuspendCount)
{
    DWORD prev, exit_status;
    DWORD tid = GetThreadId(ThreadHandle);
    KThreadSlot *k = kbusy_slot(tid, 0);

    suspend_lock();
    if (k && (DWORD)k->tid == tid && k->guest &&
        (k->guest == 2 || (GetExitCodeThread(ThreadHandle, &exit_status) &&
                          exit_status != STILL_ACTIVE))) {
        LeaveCriticalSection(&s_suspend_lock);
        return (NTSTATUS)0xC000004Bu;
    }
    if (k && (DWORD)k->tid == tid && k->guest) {
        prev = (DWORD)k->suspend_count + (DWORD)k->native_start;
        if (k->suspend_count > 0) InterlockedDecrement(&k->suspend_count);
        else if (k->native_start) {
            if (ResumeThread(ThreadHandle) == (DWORD)-1) {
                LeaveCriticalSection(&s_suspend_lock);
                return STATUS_UNSUCCESSFUL;
            }
            k->native_start = 0;
        }
        WakeAllConditionVariable(&s_suspend_cv);
        LeaveCriticalSection(&s_suspend_lock);
        if (PreviousSuspendCount) *PreviousSuspendCount = prev;
        return STATUS_SUCCESS;
    }
    LeaveCriticalSection(&s_suspend_lock);

    /* Mirror of NtSuspendThread: (DWORD)-1 is failure, not a count. */
    prev = ResumeThread(ThreadHandle);
    if (prev == (DWORD)-1) {
        xbox_log(XBOX_LOG_ERROR, XBOX_LOG_THREAD,
            "NtResumeThread: ResumeThread failed (error %u)", GetLastError());
        {   /* diagnostic: which handle, and is it a thread at all */
            static int shown;
            if (shown++ < 10)
                fprintf(stderr, "  [KERNEL] NtResumeThread(%p) failed: error %lu, tid %lu\n",
                        ThreadHandle, GetLastError(), GetThreadId(ThreadHandle));
        }
        return STATUS_UNSUCCESSFUL;
    }
    {   /* diagnostic: a resume loop that never wakes anything */
        static XBOX_THREAD_LOCAL HANDLE last;
        static XBOX_THREAD_LOCAL unsigned idle;
        if (prev == 0 && ThreadHandle == last) {
            if (++idle == 20000)
                fprintf(stderr, "  [KERNEL] NtResumeThread: tid %lu resumes tid %lu (handle %p)"
                                " 20000 times; it is never suspended\n",
                        GetCurrentThreadId(), GetThreadId(ThreadHandle), ThreadHandle);
        } else {
            idle = 0;
            last = ThreadHandle;
        }
    }
    if (PreviousSuspendCount)
        *PreviousSuspendCount = (ULONG)prev;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_THREAD,
        "NtResumeThread: thread=%p previous_count=%u", ThreadHandle, prev);

    return STATUS_SUCCESS;
}


/* ================================================================
 * Worker stack slices + game-thread tracking (host-tick-driven titles)
 * ================================================================
 *
 * Ported from the Burnout 3 fork as the runtimes reunite. A host-driven title
 * returns from its entry after spawning an init thread and expects the host's
 * own thread to drive the per-frame tick; to call recompiled code from there it
 * needs a guest stack, which a worker slice provides. Additive: a default-model
 * title never calls any of this, so it is inert for Halo, Crimson Skies, etc.
 * See docs/technical/burnout3-reunification.md and XBOX_WORKER_STACK_* in
 * xbox_memory_layout.h.
 */

/* The game's own thread, kept so a wedged boot can be inspected from the host
 * watchdog. Set when a title's game thread is spawned; NULL under the default
 * inline model, where there is no separate thread to sample. */
static HANDLE g_game_thread = NULL;

void  xbox_set_game_thread(void *h) { g_game_thread = (HANDLE)h; }
void *xbox_thread_debug_handle(void) { return (void *)g_game_thread; }

/* One bit per worker stack slice. Interlocked because a host-driven title can
 * allocate a slice from the host thread while recompiled code allocates one for
 * a spawned worker, so the allocator itself must be thread-safe. */
static volatile LONG g_worker_stack_used[XBOX_WORKER_STACK_COUNT];

int xbox_worker_stack_alloc(void)
{
    int i;
    for (i = 0; i < XBOX_WORKER_STACK_COUNT; i++) {
        if (InterlockedCompareExchange(&g_worker_stack_used[i], 1, 0) == 0)
            return i;
    }
    return -1;  /* all slices in use */
}

void xbox_worker_stack_free(int slot)
{
    if (slot >= 0 && slot < XBOX_WORKER_STACK_COUNT)
        InterlockedExchange(&g_worker_stack_used[slot], 0);
}

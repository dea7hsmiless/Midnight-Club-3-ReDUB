#include "kernel.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef void (*guest_fn)(void);
guest_fn recomp_lookup(uint32_t a) { (void)a; return NULL; }
guest_fn recomp_lookup_manual(uint32_t a) { (void)a; return NULL; }
void xbox_gil_enter(void);
void xbox_gil_leave(void);
int xbox_gil_suspend(void);
void xbox_gil_resume(int);
void recomp_preempt(void);
extern volatile int g_gil_contended;
static HANDLE ready,release_kernel,after_kernel,callback_done;
static volatile LONG stop,counter;
static int failures;
static void check(int ok,const char *name)
{
    printf("%s: %s\n",ok?"PASS":"FAIL",name);
    if(!ok)failures++;
}
static void tick(void)
{
    if(g_gil_contended)recomp_preempt();
    InterlockedIncrement(&counter);
    Sleep(1);
}
static DWORD WINAPI owner(void *unused)
{
    (void)unused;
    xbox_gil_enter();SetEvent(ready);
    while(!InterlockedCompareExchange(&stop,0,0))tick();
    xbox_gil_leave();xbox_kernel_guest_unregister();return 0;
}
static DWORD WINAPI probe(void *unused)
{
    (void)unused;xbox_gil_enter();xbox_gil_leave();
    xbox_kernel_guest_unregister();return 0;
}
static void other_guest_runs(void)
{
    HANDLE thread=CreateThread(NULL,0,probe,NULL,0,NULL);
    check(thread&&WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,
          "unrelated guest executes while target is suspended");
    CloseHandle(thread);
}
static HANDLE start(LPTHREAD_START_ROUTINE routine)
{
    HANDLE thread=CreateThread(NULL,0,routine,NULL,CREATE_SUSPENDED,NULL);
    check(thread&&xbox_kernel_guest_prepare(thread),"register thread before opening host start gate");
    return thread;
}
static void finish(HANDLE thread)
{
    check(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,"guest worker completes");
    check((uint32_t)xbox_NtSuspendThread(thread,NULL)==0xC000004Bu,"exited guest is not passed to native suspension during teardown");
    CloseHandle(thread);
}
static DWORD WINAPI self_suspend(void *unused)
{
    (void)unused;xbox_gil_enter();
    int depth=xbox_gil_suspend();xbox_kernel_busy(1);
    SetEvent(ready);
    NTSTATUS status=xbox_NtSuspendThread(GetCurrentThread(),NULL);
    xbox_kernel_busy(-1);xbox_gil_resume(depth);
    if(status==0)SetEvent(after_kernel);
    xbox_gil_leave();xbox_kernel_guest_unregister();return 0;
}
static DWORD WINAPI kernel_callback(void *unused)
{
    (void)unused;xbox_gil_enter();
    int depth=xbox_gil_suspend();xbox_kernel_busy(1);
    SetEvent(ready);WaitForSingleObject(release_kernel,INFINITE);
    /* Guest callbacks within a bridge must finish before it can park. */
    xbox_gil_enter();xbox_gil_leave();
    SetEvent(callback_done);
    xbox_kernel_busy(-1);xbox_gil_resume(depth);
    SetEvent(after_kernel);
    xbox_gil_leave();xbox_kernel_guest_unregister();return 0;
}
static DWORD WINAPI queued_guest(void *unused)
{
    (void)unused;SetEvent(ready);
    xbox_gil_enter();SetEvent(after_kernel);xbox_gil_leave();
    xbox_kernel_guest_unregister();return 0;
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    ready=CreateEventW(NULL,TRUE,FALSE,NULL);
    release_kernel=CreateEventW(NULL,TRUE,FALSE,NULL);
    after_kernel=CreateEventW(NULL,TRUE,FALSE,NULL);
    callback_done=CreateEventW(NULL,TRUE,FALSE,NULL);
    ULONG previous=99;
    HANDLE thread=start(owner);
    check(xbox_NtSuspendThread(thread,&previous)==0&&previous==1,"suspend initially stopped guest reports previous count 1");
    check(xbox_NtResumeThread(thread,&previous)==0&&previous==2,"first resume preserves initial host gate");
    check(WaitForSingleObject(ready,30)==WAIT_TIMEOUT,"initially suspended guest has not started");
    check(xbox_NtResumeThread(thread,&previous)==0&&previous==1,"second resume opens host gate once");
    check(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0,"owner acquires guest execution lock");
    check(xbox_NtSuspendThread(thread,&previous)==0&&previous==0,"running guest suspension reports previous count 0");
    other_guest_runs();
    LONG saved=InterlockedCompareExchange(&counter,0,0);Sleep(30);
    check(InterlockedCompareExchange(&counter,0,0)==saved,"suspended owner stops at a generated pre-emption point");
    check(xbox_NtSuspendThread(thread,&previous)==0&&previous==1,"nested suspend preserves count");
    check(xbox_NtResumeThread(thread,&previous)==0&&previous==2,"nested resume decrements count");
    Sleep(30);check(InterlockedCompareExchange(&counter,0,0)==saved,"one remaining suspension keeps owner parked");
    check(xbox_NtResumeThread(thread,&previous)==0&&previous==1,"final resume releases owner");
    ULONGLONG deadline=GetTickCount64()+5000;
    while(InterlockedCompareExchange(&counter,0,0)==saved&&GetTickCount64()<deadline)Sleep(1);
    check(InterlockedCompareExchange(&counter,0,0)>saved,"resumed owner executes again");
    InterlockedExchange(&stop,1);finish(thread);

    ResetEvent(ready);thread=start(self_suspend);xbox_NtResumeThread(thread,NULL);
    check(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0,"self-suspension reached");
    /* Wait until the request is visible before resuming, rather than racing
     * the deliberately asynchronous signal immediately before the call. */
    deadline=GetTickCount64()+5000;
    do {xbox_NtSuspendThread(thread,&previous);xbox_NtResumeThread(thread,NULL);if(previous)break;Sleep(1);}while(GetTickCount64()<deadline);
    check(previous==1,"self-suspension request is active");
    other_guest_runs();
    check(WaitForSingleObject(after_kernel,30)==WAIT_TIMEOUT,"self-suspending call waits for resume");
    xbox_NtResumeThread(thread,NULL);finish(thread);
    check(WaitForSingleObject(after_kernel,0)==WAIT_OBJECT_0,"self-suspension resumes normally");

    ResetEvent(ready);ResetEvent(after_kernel);
    thread=start(kernel_callback);xbox_NtResumeThread(thread,NULL);
    check(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0,"worker entered native kernel region");
    xbox_NtSuspendThread(thread,NULL);SetEvent(release_kernel);
    check(WaitForSingleObject(callback_done,5000)==WAIT_OBJECT_0,"pending suspension does not freeze a kernel callback");
    other_guest_runs();
    check(WaitForSingleObject(after_kernel,30)==WAIT_TIMEOUT,"suspension deferred until kernel callback releases runtime locks");
    xbox_NtResumeThread(thread,NULL);finish(thread);
    check(WaitForSingleObject(after_kernel,0)==WAIT_OBJECT_0,"kernel region completes after resume");

    ResetEvent(ready);ResetEvent(after_kernel);
    xbox_gil_enter();
    thread=start(queued_guest);xbox_NtResumeThread(thread,NULL);
    check(WaitForSingleObject(ready,5000)==WAIT_OBJECT_0,"guest queued behind execution lock");
    xbox_NtSuspendThread(thread,NULL);
    xbox_gil_leave();other_guest_runs();
    check(WaitForSingleObject(after_kernel,30)==WAIT_TIMEOUT,"queued guest checks suspension again before executing");
    xbox_NtResumeThread(thread,NULL);finish(thread);
    check(WaitForSingleObject(after_kernel,0)==WAIT_OBJECT_0,"queued guest executes after resume");
    CloseHandle(ready);CloseHandle(release_kernel);CloseHandle(after_kernel);CloseHandle(callback_done);
    return failures?1:0;
}

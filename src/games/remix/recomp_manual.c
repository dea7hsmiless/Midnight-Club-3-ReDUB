/* Ported by tools/port_overrides.py from src/games/dub/recomp_manual.c: every guest address
 * rewritten for this build. Places marked PORT? could not be mapped;
 * each needs a look before its override is trusted. */

/**
 * Midnight Club 3: DUB Edition -- hand-written overrides of lifted functions
 * and ICALL diagnostics. Started from xboxrecomp's templates/new-game (MIT).
 *
 * tools/regen.sh passes this file to the recompiler (--exclude-manual): any
 * sub_XXXXXXXX defined here is not generated, so the hand-written body links
 * instead. After editing, LIFT_ONLY=1 tools/regen.sh.
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wchar.h>
#include <stdatomic.h>

/* The generated register model (gen/recomp_types.h): the guest registers,
 * MEM32 and friends, and the ICALL trace ring the runtime owns. Include it
 * rather than re-declaring registers by hand: they are thread-local
 * (RECOMP_TLS), and a plain `extern uint32_t g_eax` is a TLS mismatch that
 * GCC refuses to link on Linux and the Switch. */
#include "recomp_types.h"
#if defined(_WIN32) || defined(__linux__)
static void port_media_frame(void);
#endif
static atomic_int port_gameplay_active;
static void port_gameplay_update(void);

/* Free roam (MC3_ROAM=1): the game layer included at the end of this file.
 * These forward declarations let the overrides below call it; each entry
 * point does nothing unless roam_running() (the transport started). */
int  roam_running(void);                        /* net/roam/roam_api.h */
static void roam_build(void);                   /* roam_game.c: the cruise's remote slots */
static void roam_built(void);
static void roam_pool_load(void);               /* roam_pool.c: standalone cars after the load */
static void roam_pool_frame(void);              /* after the physics step */
static void roam_pool_pass(uint32_t obj, int after);    /* the state machine's pass */
static void roam_player(uint32_t p);            /* before a player's update */
static void roam_tag_frame(void);               /* roam_tagdraw.c: name tags this frame */
static void roam_tag_projection(uint32_t vp);   /* roam_tagdraw.c: the main view's projection */
static void roam_blips(uint32_t map, uint32_t top);     /* roam_blips.c: the map's peer blips */
static void dri_ambient_pass(void);                     /* roam_ambient.c: the ambient probe */
static void dri_ambient_draw(void);                     /* roam_ambient.c: ghost poses at draw time */
static void amb_note(int tag, uint32_t va, uint32_t arg);  /* roam_ambient.c: capture the live ambient objects */

/* ── Native replacements ───────────────────────────────────── */

/* memmove (CRT), 0x002DB4C0 -- func_id: memmove, confidence 0.9.
 *
 * The lifted body dispatches its head and tail copies through two jump tables
 * (0x002AB670 (PORT?), 0x002AB76C (PORT?)) whose targets are labels inside the function. The
 * vendored lifter emits those as indirect calls to other functions, which
 * resolve to nothing ([ICALL] Failed to resolve VA 0x002AB8B4 (PORT?), first run): the
 * copy silently stops short. Upstream fixed this class of jump table after our
 * base; until it is merged, the host's memmove does the job (faster, too).
 *
 * cdecl: dest, src, count on the guest stack above the return address;
 * returns dest in eax; the caller pops the arguments, we pop the return
 * address as `ret` would. */
void sub_002DB4C0(void)
{
    uint32_t dst = MEM32(g_esp + 4);
    uint32_t src = MEM32(g_esp + 8);
    uint32_t len = MEM32(g_esp + 12);

    if (len)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), len);
    g_eax = dst;
    g_esp += 4;
}

/* Bink's MMX detection, 0x002E9AC0 (called by the blit set-up sub_003B3D80).
 *
 * The classic test: flip EFLAGS.ID with pushfd/popfd to see whether CPUID
 * exists, then CPUID leaf 1, EDX bit 23 = MMX. The vendored lifter emits
 * pushfd, popfd and cpuid as silent no-ops, so the ID bit never "sticks",
 * Bink decides the CPU has no MMX, never installs its pixel routines
 * (0x55FD70.. stay 0), and the blit loop -- whose stdcall calls through those
 * null pointers are skipped without popping their argument -- drifts its
 * stack and never ends (2026-10-14 runs, [BINKLOOP] trace).
 *
 * The Xbox CPU is a Pentium III: CPUID and MMX are always there. Same
 * results and side effects as the original:
 *   [0x4BA740] = cached "has MMX" (-1 until detected)
 *   [0x5697DC] = "use MMX", unless the caller's flags carry 0x40000
 * cdecl: flags at [esp+4]; returns the detection result in eax. */
void sub_002E9AC0(void)
{
    uint32_t flags = MEM32(g_esp + 4);
    uint32_t has_mmx = MEM32(0x4BA740);

    if ((int32_t)has_mmx < 0) {
        has_mmx = 1;
        MEM32(0x4BA740) = has_mmx;
    }
    MEM32(0x5697DC) = (has_mmx && !(flags & 0x40000u)) ? 1u : 0u;
    g_eax = has_mmx;
    g_esp += 4;
}

/* D3D fence wait, 0x004165A0 (D3D section): waits until the GPU-written
 * semaphore at *(device + 0x30) reaches a fence. The NV2A executor writes
 * BACK_END_WRITE_SEMAPHORE_RELEASE values there only once the project names
 * the address (nv2a_pb_set_semaphore_target, xboxrecomp nv2a_backend.h);
 * until then every release is dropped and the wait never ends (2026-10-14
 * run 15: main thread here with PUT == GET). MC3's D3D keeps its device
 * pointer at 0x00422D78 (mov esi, [0x422D78] at the top of this function).
 * Name the semaphore on the first wait, then run the original: its kick-off
 * queues new releases, which now land. */
/*
 * The wait's last loop (0x004166D7) reads the time handed out once
 * (+0x2C, into a register) and spins while
 *     handed - target < handed - gpu      (unsigned)
 * i.e. while the GPU is behind the fence inside the window [.., handed].
 * Its poll-loop yield lets the other guest threads run -- MC3's loading
 * screen keeps drawing from its own thread -- and when they push and the
 * GPU runs past `handed` before this thread looks again, handed - gpu
 * wraps to ~4 G and the loop never ends: the career loading-screen hang
 * (2026-10-21; snapshot: fence 000F6AEF, GPU at 000F90B5, main thread
 * spinning there with the loading screen still animating). On the Xbox the
 * window is microseconds and nothing pushes in it. So while the original
 * runs, a spin hook checks at each yield: once the GPU has passed the fence
 * it stores the fence itself in the semaphore, which ends the loop on its
 * next read, and puts the GPU's value back when the wait returns (unless
 * the GPU has written a newer one). Lower is the safe direction: a reader
 * in between waits, never runs ahead. MC3_FENCE_FIX=0 turns it off.
 */
static uint32_t s_fence_sem_va;
static RECOMP_TLS uint32_t t_fence_target, t_fence_saved;
static RECOMP_TLS int t_fence_fixed;
extern void recomp_set_spin_hook(void (*fn)(void));
typedef int (*PortWaitRecheck)(uint32_t);
extern PortWaitRecheck xbox_kernel_wait_recheck_set(PortWaitRecheck fn);
extern uint32_t nv2a_pb_last_fence_release(void);
extern uint32_t nv2a_pb_desync_count(void);

/* Publish the fence target when the release that would have reached it was
 * discarded with the rest of a lost batch. Same CAS discipline as the other
 * fence paths: only ever move the value forward, and only from the value
 * that was actually observed. */
static uint32_t fence_cas(uint32_t va, uint32_t expect, uint32_t want);
static void fence_release_lost(uint32_t target)
{
    uint32_t gpu;
    if (!s_fence_sem_va)
        return;
    gpu = __atomic_load_n((uint32_t *)((uintptr_t)s_fence_sem_va + g_xbox_mem_offset),
                          __ATOMIC_SEQ_CST);
    if ((int32_t)(target - gpu) <= 0)
        return;                                /* already there or past it */
    fence_cas(s_fence_sem_va, gpu, target);
}

extern uint32_t nv2a_pb_desync_count(void);
/* Set when a fence wait should give up because the stream was lost under it.
 * The wait began before a desync and is short of its target: the executor
 * discarded the batch that carried the release, so no timeout will ever
 * arrive. Releasing to the target is the only way out, and it costs one
 * frame's rendering rather than the session. */
static int fence_event_recheck(uint32_t object)
{
    uint32_t device=MEM32(0x422D78u);
    if(!device||object!=device+0x1DCCu||!s_fence_sem_va)return 0;
    uint32_t gpu=__atomic_load_n((uint32_t *)XBOX_PTR(s_fence_sem_va),__ATOMIC_SEQ_CST);
    if((int32_t)(gpu-t_fence_target)<0){
        static RECOMP_TLS uint32_t watched;
        static RECOMP_TLS uint64_t began,last;
        static RECOMP_TLS uint32_t desync_at_start;
        struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
        uint64_t now=(uint64_t)ts.tv_sec*1000u+ts.tv_nsec/1000000u;
        if(watched!=t_fence_target){watched=t_fence_target;began=last=now;
            desync_at_start=nv2a_pb_desync_count();}
        /* The pushbuffer walk lost the command stream since this wait began.
         * The release it needs was in the part that was discarded, so the
         * value being polled here will never change. Publish the target so
         * the guest thread can run: this is a lost frame, not a lost game. */
        if(nv2a_pb_desync_count()!=desync_at_start){
            static int logged;
            if(logged++<8)
                fprintf(stderr,"[D3D] fence wait ended after the pushbuffer lost the stream: "
                        "GPU=%08X target=%08X (released; the frame's remaining commands were lost)\n",
                        gpu,t_fence_target);
            fence_release_lost(t_fence_target);
            return 1;
        }
        if(now-began>=1000&&now-last>=2000){last=now;fprintf(stderr,"[D3D] fence event still pending: GPU=%08X target=%08X PUT=%08X GET=%08X lastRelease=%08X\n",gpu,t_fence_target,MEM32(0xFD800040u),MEM32(0xFD800044u),nv2a_pb_last_fence_release());}
        return 0;
    }
    static int logged;
    if(logged++<8)fprintf(stderr,"[D3D] fence event notification missed: GPU=%08X target=%08X; completion verified\n",gpu,t_fence_target);
    return 1;
}

/* Compare-and-swap on guest memory: the executor writes it from its thread. */
static uint32_t fence_cas(uint32_t va, uint32_t expect, uint32_t want)
{
    uint32_t *p = (uint32_t *)((uintptr_t)va + g_xbox_mem_offset);
    __atomic_compare_exchange_n(p, &expect, want, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expect;                            /* the value found */
}

static void fence_spin_hook(void)
{
    uint32_t gpu;
    if (!s_fence_sem_va)
        return;
    if (t_fence_fixed) {
        /* Still spinning after the fix: some other loop -- give it the
         * real value back. */
        fence_cas(s_fence_sem_va, t_fence_target, t_fence_saved);
        t_fence_fixed = 0;
        return;
    }
    gpu = __atomic_load_n((uint32_t *)((uintptr_t)s_fence_sem_va + g_xbox_mem_offset), __ATOMIC_SEQ_CST);
    if (gpu == t_fence_target || (int32_t)(gpu - t_fence_target) < 0)
        return;                                /* not there yet, or exact */
    if (fence_cas(s_fence_sem_va, gpu, t_fence_target) == gpu) {
        static int logged;
        t_fence_saved = gpu;
        t_fence_fixed = 1;
        if (logged++ < 5)
            fprintf(stderr, "[D3D] fence wait: GPU at %08X ran past fence %08X and the "
                            "window -- released\n", gpu, t_fence_target);
    }
}

extern void sub_004165A0_gen(void);
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);
void sub_004165A0(void)
{
    static uint32_t sem_va;
    static int fix = -1;
    uint32_t save_target = t_fence_target, save_saved = t_fence_saved;
    int save_fixed = t_fence_fixed;

    if (!sem_va) {
        uint32_t device = MEM32(0x00422D78u);
        uint32_t sem = device ? MEM32(device + 0x30u) : 0;
        if (sem) {
            sem_va = sem;
            nv2a_pb_set_semaphore_target(sem_va);
            fprintf(stderr, "[D3D] GPU semaphore at 0x%08X (device 0x%08X)\n",
                    sem_va, device);
            s_fence_sem_va = sem_va;
        }
    }
    if (fix < 0) {
        const char *e = getenv("MC3_FENCE_FIX");
        fix = !(e && e[0] == '0');
    }
    if (!fix || !s_fence_sem_va) {
        sub_004165A0_gen();
        return;
    }
    t_fence_target = MEM32(g_esp + 4);         /* (time, flags), stdcall */
    t_fence_fixed = 0;
    recomp_set_spin_hook(fence_spin_hook);
    PortWaitRecheck save_wait=xbox_kernel_wait_recheck_set(fence_event_recheck);
    sub_004165A0_gen();
    xbox_kernel_wait_recheck_set(save_wait);
    if (t_fence_fixed)
        fence_cas(s_fence_sem_va, t_fence_target, t_fence_saved);
    t_fence_target = save_target;
    t_fence_saved = save_saved;
    t_fence_fixed = save_fixed;
    recomp_set_spin_hook(save_target ? fence_spin_hook : NULL);
}

/* ── Differential tests (src/difftest.c, tools/difftest.py) ── */

/* Wrapped so one call can be captured and replayed on real x86 (make difftest
 * FUNC=<addr> CALL=<n>). Unarmed, a wrapper costs one counter and a compare.
 * Movie frames and movie audio decode wrongly (2026-10-15); BinkDoFrame
 * (0x003AD2C0) itself waits on Bink's I/O thread, so capture below it:
 *   0x003B2F40  Bink video: decodes the frame's planes (4 x sub_003B1180,
 *               block decoders 0x003AFB30.., transforms 0x003B7060..);
 *               called from BinkDoFrame at 0x003AD82A
 *   0x003AEDB0  Bink audio: decodes the frame's audio (sub_003AE8C0 and the
 *               .text maths at 0x002E9930 / 0x002E97F0); at 0x003AD5AD */
extern void mc3_difftest_call(uint32_t func, void (*lifted)(void), long *calls);
extern long mc3_difftest_wanted(void);

/* ── Music: the WMA decoder XACT drives (MC3_WMA_TRACE=1) ──
 *
 * The music (audio/XStreams/mx.xwb, 110 tracks) is WMA, stereo 44.1 kHz;
 * XACT decodes it on the CPU through the WMADEC section's media object and
 * plays the PCM from a looping buffer voice. On 2026-10-16 that voice ran
 * but every sample it read was zero, so the menu song was silent. This logs
 * what the decoder is asked and answers: XACT creates it with
 * sub_004260AD (0x0040CAE8, when the wave's format tag is 2 = WMA); its
 * vtable is at 0x004A5594 (constructor sub_00426003). Per method: the first
 * calls in full, then every 500th, with stdcall arguments (this, a, b), the
 * HRESULT, and for any argument that looks like an XMEDIAPACKET
 * {pvBuffer, dwMaxSize, pdwCompletedSize, ...} the completed size and the
 * peak 16-bit sample in it. Each is difftest-capable: Process is slot 4,
 * `make difftest FUNC=003EFCE7 CALL=<n>`. */
static int wma_trace_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_WMA_TRACE");
        on = e && e[0] == '1';
    }
    return on;
}

/* An XMEDIAPACKET's completed size and 16-bit peak, or 0 if `va` is not one. */
static int wma_packet(uint32_t va, uint32_t *done, uint32_t *max, int *peak)
{
    uint32_t buf, size, done_va, n, i;
    if (va < 0x10000u || va >= 0x84000000u)
        return 0;
    buf = MEM32(va); size = MEM32(va + 4); done_va = MEM32(va + 8);
    if (buf < 0x10000u || buf >= 0x84000000u || !size || size > 0x400000u)
        return 0;
    n = done_va && done_va < 0x84000000u ? MEM32(done_va) : size;
    if (n > size) n = size;
    *done = n; *max = size; *peak = 0;
    for (i = 0; i + 1 < n; i += 2) {
        int v = (int16_t)MEM16(buf + i);
        if (v < 0) v = -v;
        if (v > *peak) *peak = v;
    }
    return 1;
}

static void wma_call(uint32_t addr, int method, void (*fn)(void))
{
    static long calls[11], dcalls[11];
    uint32_t a0, a1, a2;
    long n;
    /* Creation is rare and the question is often whether it happens at
     * all: logged (first 4) with or without the trace. */
    if (!wma_trace_on() && (method != 10 || calls[10] >= 4)) {
        mc3_difftest_call(addr, fn, &dcalls[method]);
        return;
    }
    a0 = MEM32(g_esp + 4); a1 = MEM32(g_esp + 8); a2 = MEM32(g_esp + 12);
    n = ++calls[method];
    mc3_difftest_call(addr, fn, &dcalls[method]);
    if (n <= 12 || n % 500 == 0) {
        uint32_t d, m;
        int pk;
        fprintf(stderr, "[WMA] %s #%ld (%08X, %08X, %08X) -> %08X",
                method == 10 ? "create" : "method", n, a0, a1, a2, g_eax);
        if (method < 10)
            fprintf(stderr, " [vtable +0x%02X]", method * 4);
        if (wma_packet(a1, &d, &m, &pk))
            fprintf(stderr, " | a: %u/%u bytes peak %d", d, m, pk);
        if (wma_packet(a2, &d, &m, &pk))
            fprintf(stderr, " | b: %u/%u bytes peak %d", d, m, pk);
        fprintf(stderr, "\n");
    }
}

/* Written out, not by macro: tools/recomp/manual_scan.py reads the text. */
extern void sub_004260AD_gen(void);
void sub_004260AD(void) { wma_call(0x004260ADu, 10, sub_004260AD_gen); }   /* create: (out object, ...) */
extern void sub_00425C5F_gen(void);
void sub_00425C5F(void) { wma_call(0x00425C5Fu, 2, sub_00425C5F_gen); }
extern void sub_00425CA1_gen(void);
void sub_00425CA1(void) { wma_call(0x00425CA1u, 3, sub_00425CA1_gen); }
extern void sub_00425F87_gen(void);
void sub_00425F87(void) { wma_call(0x00425F87u, 4, sub_00425F87_gen); }
extern void sub_00425C9C_gen(void);
void sub_00425C9C(void) { wma_call(0x00425C9Cu, 5, sub_00425C9C_gen); }
extern void sub_00425F23_gen(void);
void sub_00425F23(void) { wma_call(0x00425F23u, 6, sub_00425F23_gen); }
extern void sub_00425D5A_gen(void);
void sub_00425D5A(void) { wma_call(0x00425D5Au, 7, sub_00425D5A_gen); }
extern void sub_00425DC0_gen(void);
void sub_00425DC0(void) { wma_call(0x00425DC0u, 8, sub_00425DC0_gen); }
extern void sub_00425B8F_gen(void);
void sub_00425B8F(void) { wma_call(0x00425B8Fu, 9, sub_00425B8F_gen); }

/* ── Music: XACT's API as the game calls it (MC3_XACT_TRACE=1) ──
 *
 * With MC3_WMA_TRACE on, XACT never created a WMA decoder (2026-10-16):
 * the songs fail before decoding, and the in-game music player shuffles
 * through them forever. These are the 37 XACT functions the game calls
 * directly (0x004073D9..0x00407A14; the last ten are jmp thunks). Each
 * logs its first 3 calls, and every call returning a failure HRESULT (bit
 * 31 set; a few may return counts or pointers instead), with ecx and four
 * stack arguments. */
extern void mc3_difftest_call(uint32_t func, void (*lifted)(void), long *calls);

static int xact_trace_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_XACT_TRACE");
        on = e && e[0] == '1';
    }
    return on;
}

static void xact_call(uint32_t addr, int slot, void (*fn)(void))
{
    static long calls[64], failures, dcalls[64];
    uint32_t c, a0, a1, a2, a3;
    long n;
    /* Every XACT wrapper can also be captured: make difftest FUNC=<addr>. */
    if (!xact_trace_on()) { mc3_difftest_call(addr, fn, &dcalls[slot]); return; }
    c = g_ecx;
    a0 = MEM32(g_esp + 4); a1 = MEM32(g_esp + 8);
    a2 = MEM32(g_esp + 12); a3 = MEM32(g_esp + 16);
    uint32_t desc[4] = { 0, 0, 0, 0 };
    n = ++calls[slot];
    /* sub_004078AD / sub_0040EDB3's second argument is a descriptor:
     * flags (bit 2: streamed), then three words (+0xC: the stream's
     * source). Read before the call, while it is the caller's. */
    if ((addr == 0x004078ADu || addr == 0x0040EDB3u) && a1 >= 0x10000u && a1 < 0x84000000u) {
        desc[0] = MEM32(a1); desc[1] = MEM32(a1 + 4);
        desc[2] = MEM32(a1 + 8); desc[3] = MEM32(a1 + 12);
    }
    mc3_difftest_call(addr, fn, &dcalls[slot]);
    if (n <= 3 || ((g_eax & 0x80000000u) && failures++ < 400)) {
        fprintf(stderr, "[XACT] sub_%08X #%ld (ecx %08X; %08X %08X %08X %08X) -> %08X%s",
                addr, n, c, a0, a1, a2, a3, g_eax,
                (g_eax & 0x80000000u) ? "  FAILED" : "");
        if (desc[0] | desc[1] | desc[2] | desc[3])
            fprintf(stderr, " desc {%08X %08X %08X %08X}", desc[0], desc[1], desc[2], desc[3]);
        fprintf(stderr, "\n");
    }
}

/* Written out, not by macro: tools/recomp/manual_scan.py reads the text. */
/* XACT's variation choice, 0x0040DBEF: thiscall (table offset, flags,
 * float), returns the chosen entry or 0. sub_0040E0F6 turns 0 into wave
 * index 0xFFFF and sub_0040EC76 into E_FAIL (0x0040ED25) -- every song in
 * the music player. Pure computation (mode (flags >> 13) & 15, an RNG at
 * sub_00408459 / sub_00408462): wrapped for difftest, and logged with
 * MC3_XACT_TRACE. The first call is at boot (the menu song). */
extern void sub_0040DBEF_gen(void);
void sub_0040DBEF(void)
{
    static long calls, logged;
    uint32_t c = g_ecx, a0 = MEM32(g_esp + 4), a1 = MEM32(g_esp + 8), a2 = MEM32(g_esp + 12);
    uint32_t entry = 0;
    if (c >= 0x10000u && c < 0x84000000u)
        entry = MEM32(MEM32(c + 4) + a0);
    mc3_difftest_call(0x0040DBEFu, sub_0040DBEF_gen, &calls);
    if (xact_trace_on() && logged++ < 40)
        fprintf(stderr, "[XACT] variation sub_0040DBEF #%ld (ecx %08X; %08X %08X %08X)"
                " table word %08X (mode %u, count %u) -> %08X\n", logged, c, a0, a1, a2,
                entry, (entry >> 13) & 15, entry & 0x1FFF, g_eax);
}

/* XACT sound states (2026-10-17). Starting a sound moves it to state 5
 * (sub_0040D9D0 -> sub_0040B71F(5)); the only refused transition is from
 * state 7, "stopped", and that refusal is the music player's E_FAIL: every
 * song is stopped before it starts. sub_0040BE8F is XACT's stop (state 7 +
 * cleanup), called from nine places. With MC3_XACT_TRACE these log who stops
 * a sound (the return address names the caller) and every move to state 7 or
 * refused transition, with the sound object and its state before. */
extern void sub_0040BE8F_gen(void);
void sub_0040BE8F(void)
{
    static long n;
    uint32_t snd = g_ecx, ret = MEM32(g_esp), flags = MEM32(g_esp + 4);
    uint32_t state = snd >= 0x10000u && snd < 0x84000000u ? MEM16(snd + 0x30) : 0xFFFF;
    if (xact_trace_on() && n++ < 200)
        fprintf(stderr, "[XACT] stop sound %08X (state %u, flags %08X) called from %08X\n",
                snd, state, flags, ret);
    sub_0040BE8F_gen();
}

extern void sub_0040B71F_gen(void);
void sub_0040B71F(void)
{
    static long n;
    uint32_t snd = g_ecx, ret = MEM32(g_esp), want = MEM32(g_esp + 4);
    uint32_t before = snd >= 0x10000u && snd < 0x84000000u ? MEM16(snd + 0x30) : 0xFFFF;
    sub_0040B71F_gen();
    if (xact_trace_on() && (want == 7 || !g_eax) && n++ < 300)
        fprintf(stderr, "[XACT] sound %08X state %u -> %u %s (from %08X)\n",
                snd, before, want, g_eax ? "ok" : "REFUSED", ret);
}

/* One level down, inside sub_004078AD -> sub_0040EDB3 (2026-10-16: E_FAIL
 * 403x while the music player shuffled): which step fails. */
extern void sub_0040EDB3_gen(void);
void sub_0040EDB3(void) { xact_call(0x0040EDB3u, 40, sub_0040EDB3_gen); }
extern void sub_00407084_gen(void);
void sub_00407084(void) { xact_call(0x00407084u, 41, sub_00407084_gen); }   /* streamed (flag 4) */
extern void sub_0040EC76_gen(void);
void sub_0040EC76(void) { xact_call(0x0040EC76u, 42, sub_0040EC76_gen); }
extern void sub_0040DEB0_gen(void);
void sub_0040DEB0(void) { xact_call(0x0040DEB0u, 43, sub_0040DEB0_gen); }
extern void sub_00410DDB_gen(void);
void sub_00410DDB(void) { xact_call(0x00410DDBu, 44, sub_00410DDB_gen); }
extern void sub_004073D9_gen(void);
void sub_004073D9(void) { xact_call(0x004073D9u, 0, sub_004073D9_gen); }
extern void sub_0040740B_gen(void);
void sub_0040740B(void) { xact_call(0x0040740Bu, 1, sub_0040740B_gen); }
extern void sub_004074B0_gen(void);
void sub_004074B0(void) { xact_call(0x004074B0u, 2, sub_004074B0_gen); }
extern void sub_004074C8_gen(void);
void sub_004074C8(void) { xact_call(0x004074C8u, 3, sub_004074C8_gen); }
extern void sub_0040750E_gen(void);
void sub_0040750E(void) { xact_call(0x0040750Eu, 4, sub_0040750E_gen); }
extern void sub_0040754D_gen(void);
void sub_0040754D(void) { xact_call(0x0040754Du, 5, sub_0040754D_gen); }
extern void sub_00407590_gen(void);
void sub_00407590(void) { xact_call(0x00407590u, 6, sub_00407590_gen); }
extern void sub_004075D3_gen(void);
void sub_004075D3(void) { xact_call(0x004075D3u, 7, sub_004075D3_gen); }
extern void sub_00407612_gen(void);
void sub_00407612(void) { xact_call(0x00407612u, 8, sub_00407612_gen); }
extern void sub_0040764D_gen(void);
void sub_0040764D(void) { xact_call(0x0040764Du, 9, sub_0040764D_gen); }
extern void sub_0040768C_gen(void);
void sub_0040768C(void) { xact_call(0x0040768Cu, 10, sub_0040768C_gen); }
extern void sub_004076AC_gen(void);
void sub_004076AC(void) { xact_call(0x004076ACu, 11, sub_004076AC_gen); }
extern void sub_004076EB_gen(void);
void sub_004076EB(void) { xact_call(0x004076EBu, 12, sub_004076EB_gen); }
extern void sub_00407726_gen(void);
void sub_00407726(void) { xact_call(0x00407726u, 13, sub_00407726_gen); }
extern void sub_00407761_gen(void);
void sub_00407761(void) { xact_call(0x00407761u, 14, sub_00407761_gen); }
extern void sub_004077A0_gen(void);
void sub_004077A0(void) { xact_call(0x004077A0u, 15, sub_004077A0_gen); }
extern void sub_004077E3_gen(void);
void sub_004077E3(void) { xact_call(0x004077E3u, 16, sub_004077E3_gen); }
extern void sub_0040781B_gen(void);
void sub_0040781B(void) { xact_call(0x0040781Bu, 17, sub_0040781B_gen); }
extern void sub_00407833_gen(void);
void sub_00407833(void) { xact_call(0x00407833u, 18, sub_00407833_gen); }
extern void sub_0040784F_gen(void);
void sub_0040784F(void) { xact_call(0x0040784Fu, 19, sub_0040784F_gen); }
extern void sub_0040787A_gen(void);
void sub_0040787A(void) { xact_call(0x0040787Au, 20, sub_0040787A_gen); }
extern void sub_004078AD_gen(void);
void sub_004078AD(void) { xact_call(0x004078ADu, 21, sub_004078AD_gen); }
extern void sub_004078E0_gen(void);
void sub_004078E0(void) { xact_call(0x004078E0u, 22, sub_004078E0_gen); }
extern void sub_00407917_gen(void);
void sub_00407917(void) { xact_call(0x00407917u, 23, sub_00407917_gen); }
extern void sub_0040794E_gen(void);
void sub_0040794E(void) { xact_call(0x0040794Eu, 24, sub_0040794E_gen); }
extern void sub_00407981_gen(void);
void sub_00407981(void) { xact_call(0x00407981u, 25, sub_00407981_gen); }
extern void sub_004079B4_gen(void);
void sub_004079B4(void) { xact_call(0x004079B4u, 26, sub_004079B4_gen); }
extern void sub_004079E7_gen(void);
void sub_004079E7(void) { xact_call(0x004079E7u, 27, sub_004079E7_gen); }
extern void sub_004079EC_gen(void);
void sub_004079EC(void) { xact_call(0x004079ECu, 28, sub_004079EC_gen); }
extern void sub_004079F1_gen(void);
void sub_004079F1(void) { xact_call(0x004079F1u, 29, sub_004079F1_gen); }
extern void sub_004079F6_gen(void);
void sub_004079F6(void) { xact_call(0x004079F6u, 30, sub_004079F6_gen); }
extern void sub_004079FB_gen(void);
void sub_004079FB(void) { xact_call(0x004079FBu, 31, sub_004079FB_gen); }
extern void sub_00407A00_gen(void);
void sub_00407A00(void) { xact_call(0x00407A00u, 32, sub_00407A00_gen); }
extern void sub_00407A05_gen(void);
void sub_00407A05(void) { xact_call(0x00407A05u, 33, sub_00407A05_gen); }
extern void sub_00407A0A_gen(void);
void sub_00407A0A(void) { xact_call(0x00407A0Au, 34, sub_00407A0A_gen); }
extern void sub_00407A0F_gen(void);
void sub_00407A0F(void) { xact_call(0x00407A0Fu, 35, sub_00407A0F_gen); }
extern void sub_00407A14_gen(void);
void sub_00407A14(void) { xact_call(0x00407A14u, 36, sub_00407A14_gen); }

/* ── Movies: where a frame's time goes (MC3_BINK_TRACE=1) ──
 *
 * The movie loop (sub_003638A0) per frame: BinkDoFrame, lock the texture,
 * BinkCopyToBuffer, BinkNextFrame, then spin on BinkWait until the next frame
 * is due; its caller draws and presents. Once a second this prints the
 * average milliseconds per frame in each phase -- "rest" is everything from
 * the end of the wait to the next BinkDoFrame (draw, Present, game). */

enum { BT_DO, BT_VIDEO, BT_AUDIO, BT_LOCK, BT_COPY, BT_NEXT, BT_WAIT, BT_REST, BT_N };
static const char *const s_bt_name[BT_N] =
    { "doframe", "video", "audio", "lock", "copy", "next", "wait", "rest" };
static double s_bt_sum[BT_N], s_bt_mark[BT_N], s_bt_report;
static long s_bt_frames, s_bt_polls;
static double s_bt_wait_start;       /* 0 when not in the wait loop */

static double bt_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static int bt_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_BINK_TRACE");
        on = e && e[0] == '1';
    }
    return on;
}

static void bt_report(double now)
{
    char line[256];
    int n = 0, i;
    if (!s_bt_report) { s_bt_report = now; return; }
    if (now - s_bt_report < 1000.0 || !s_bt_frames) return;
    n += snprintf(line + n, sizeof line - n, "[BINKT] %.1f fps, ms/frame:",
                  s_bt_frames * 1000.0 / (now - s_bt_report));
    for (i = 0; i < BT_N; i++)
        n += snprintf(line + n, sizeof line - n, " %s %.1f", s_bt_name[i], s_bt_sum[i] / s_bt_frames);
    snprintf(line + n, sizeof line - n, " (%ld polls/frame)", s_bt_polls / s_bt_frames);
    fprintf(stderr, "%s\n", line);
    memset(s_bt_sum, 0, sizeof s_bt_sum);
    s_bt_frames = s_bt_polls = 0;
    s_bt_report = now;
}

/* Time one lifted call into phase `p`. */
static void bt_time(int p, void (*fn)(void))
{
    double t0;
    if (!bt_on()) { fn(); return; }
    t0 = bt_now();
    fn();
    s_bt_sum[p] += bt_now() - t0;
}

/* BinkDoFrame, 0x003AD2C0: stdcall (hbink). Opens a frame. */

/* Remix bring-up: does a stdcall wrapper hand back the stack it was given?
 * (MC3_ABI_CHECK=1; the movie function's pops came back 0, 2026-10-24) */
static void esp_check(const char *name, uint32_t esp0, uint32_t want)
{
    static int on = -1, said;
    if (on < 0) {
        const char *e = getenv("MC3_ABI_CHECK");
        on = e && e[0] == '1';
    }
    if (on && g_esp - esp0 != want && said++ < 20)
        fprintf(stderr, "[ABI] %s returned esp %+d, want %+u\n", name, (int)(g_esp - esp0), want);
}
/* MC3_SKIP_MOVIES=hard (see movie_skipped below): the movie being skipped.
 * Its BinkDoFrame, BinkCopyToBuffer, BinkNextFrame and BinkWait return at
 * once (nothing decoded, the game's cleared texture stays black) with the
 * frame number at the last frame, so the movie loop (sub_003638A0) reports
 * the end (3) and the game closes it and goes on as after a whole movie. */
static uint32_t s_hard_hbink;
static int hard_skip(unsigned args)
{
    uint32_t h = MEM32(g_esp + 4);
    if (!s_hard_hbink || h != s_hard_hbink)
        return 0;
    MEM32(h + 0x0C) = MEM32(h + 0x08);          /* FrameNum = Frames */
    g_eax = 0;
    g_esp += 4 + 4 * args;                      /* stdcall: return address + arguments */
    return 1;
}

extern void sub_003AD2C0_gen(void);
void sub_003AD2C0(void)
{
    double t0;
    uint32_t e0 = g_esp;
    if (hard_skip(1)) return;
    if (!bt_on()) { sub_003AD2C0_gen(); esp_check("BinkDoFrame", e0, 8); return; }
    t0 = bt_now();
    if (s_bt_mark[BT_REST]) s_bt_sum[BT_REST] += t0 - s_bt_mark[BT_REST];
    bt_report(t0);
    s_bt_frames++;
    sub_003AD2C0_gen();
    s_bt_mark[BT_LOCK] = bt_now();
    s_bt_sum[BT_DO] += s_bt_mark[BT_LOCK] - t0;
}

/* BinkCopyToBuffer, 0x003AD280: the YUV -> RGB copy into the locked texture.
 * stdcall (hbink, dest, pitch, height, x, y, flags), returns 0 when copied.
 * With the renderer upscaling the frame (bink_native.c mc3_bink_native_copy)
 * the copy is skipped: nothing but the drawn texture reads it. */
extern int mc3_bink_native_copy(uint32_t hbink, uint32_t dest_va, const uint8_t *dest,
                                uint32_t pitch, uint32_t height, uint32_t x, uint32_t y, int after);
extern void sub_003AD280_gen(void);
void sub_003AD280(void)
{
    uint32_t hbink = MEM32(g_esp + 4), dest = MEM32(g_esp + 8), pitch = MEM32(g_esp + 12);
    uint32_t height = MEM32(g_esp + 16), x = MEM32(g_esp + 20), y = MEM32(g_esp + 24);
    if (hard_skip(7)) return;
    if (bt_on() && s_bt_mark[BT_LOCK]) {
        s_bt_sum[BT_LOCK] += bt_now() - s_bt_mark[BT_LOCK];
        s_bt_mark[BT_LOCK] = 0;
    }
    if (dest && mc3_bink_native_copy(hbink, dest, (const uint8_t *)XBOX_PTR(dest),
                                     pitch, height, x, y, 0)) {
        g_eax = 0;
        g_esp += 4 + 28;                     /* return address + 7 arguments */
        return;
    }
    {
        uint32_t e0 = g_esp;
        bt_time(BT_COPY, sub_003AD280_gen);
        esp_check("BinkCopyToBuffer", e0, 32);
    }
    if (dest)
        mc3_bink_native_copy(hbink, dest, (const uint8_t *)XBOX_PTR(dest), pitch, height, x, y, 1);
}

/* BinkNextFrame, 0x003AD8D0. */
extern void sub_003AD8D0_gen(void);
void sub_003AD8D0(void)
{
    uint32_t e0 = g_esp;
    if (hard_skip(1)) return;
    bt_time(BT_NEXT, sub_003AD8D0_gen);
    esp_check("BinkNextFrame", e0, 8);
}

/* BinkWait, 0x003ACDF0: stdcall (hbink), nonzero = too early, call again. */
extern void sub_003ACDF0_gen(void);
void sub_003ACDF0(void)
{
    double now;
    uint32_t e0 = g_esp;
    if (hard_skip(1)) return;
    if (!bt_on()) { sub_003ACDF0_gen(); esp_check("BinkWait", e0, 8); return; }
    now = bt_now();
    if (!s_bt_wait_start) s_bt_wait_start = now;
    s_bt_polls++;
    sub_003ACDF0_gen();
    if (!g_eax) {
        now = bt_now();
        s_bt_sum[BT_WAIT] += now - s_bt_wait_start;
        s_bt_wait_start = 0;
        s_bt_mark[BT_REST] = now;
    }
}

/* ── Movies: Bink video decoded by FFmpeg (src/bink_native.c) ── */

/* Bink's HBINK, as far as the native path needs it. */
#define BINK_WIDTH      0x00u
#define BINK_HEIGHT     0x04u
#define BINK_FRAMENUM   0x0Cu       /* 1-based frame being decoded */
#define BINK_DIRTY_MAP  0x3A0u      /* changed-block map, 1 byte per 16x16 block;
                                     * HBINK+0xDC points here, the decoder's 5th
                                     * argument */

extern int  mc3_bink_native_enabled(void);
extern void mc3_bink_native_open(uint32_t hbink, const char *xbox_name);
extern int  mc3_bink_native_frame(uint32_t hbink, int frame, uint8_t *dest, int w, int h);
extern void mc3_bink_native_close(uint32_t hbink);
extern void mc3_bink_native_audio(uint32_t hbink, int frame);
extern int  mc3_bink_native_audio_active(void);

/* MC3_SKIP_MOVIES: for unattended runs (headless, recordings) where nobody
 * is there to press Enter. Off by default: in a window, press it yourself.
 * A comma list of file names without the folder ("mc3intro.bik"), 1 = the
 * boot intro (rockstar, sdlogo, mc3intro), or hard = the same with nothing
 * decoded at all (hard_skip above). While such a movie is open, pad 1 pulses
 * A + Start (3 frames held, 3 released: fresh presses) -- input like a
 * player's, through the title's own handling, so only what the game lets you
 * skip is skipped: with 1 the Rockstar and Rockstar San Diego logos play in
 * full (as on an Xbox) and mc3intro is cut short. MC3_AUTOSTART implies 1,
 * since nobody is there to press through the intro either. */
extern int mc3_autostart_on(void);              /* src/autostart.c */
extern void usb_gamepad_set_held(int dev, uint8_t digital, uint8_t analog);
#define PAD_START 0x10u                         /* digital mask */
#define PAD_A     0x01u                         /* analog mask */
static uint32_t s_skip_hbink;                   /* the movie being pressed through */
static unsigned s_skip_frames;

static int movie_skipped(const char *path)
{
    const char *list = getenv("MC3_SKIP_MOVIES"), *base = path, *p;
    size_t n;
    int hard = 0;

    if ((!list || !*list) && mc3_autostart_on())
        list = "1";                             /* MC3_AUTOSTART skips the intro too */
    if (!list || !*list || !strcmp(list, "0"))
        return 0;
    if (!strcmp(list, "hard"))
        hard = 1, list = "1";
    if (!strcmp(list, "1"))
        list = "rockstar.bik,sdlogo.bik,mc3intro.bik";
    for (p = path; *p; p++)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    n = strlen(base);
    for (p = list; *p; ) {
        const char *e = strchr(p, ',');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == n && !strncasecmp(p, base, n))
            return 1 + hard;
        p += len + (e ? 1 : 0);
    }
    return 0;
}

/* BinkOpen, 0x003AB590: stdcall (name, flags), returns the HBINK. */
extern void sub_003AB590_gen(void);
void sub_003AB590(void)
{
    char name[260] = "";
    uint32_t name_va = MEM32(g_esp + 4);
    int skip;

    if (name_va)
        strncpy(name, (const char *)XBOX_PTR(name_va), sizeof name - 1);
    skip = name[0] ? movie_skipped(name) : 0;
    sub_003AB590_gen();
    if (skip == 2 && g_eax) {
        s_hard_hbink = g_eax;
        fprintf(stderr, "[BINK] skipping %s (MC3_SKIP_MOVIES=hard)\n", name);
        return;
    }
    if (skip) {
        s_skip_hbink = g_eax;
        s_skip_frames = 0;
        fprintf(stderr, "[BINK] pressing A+Start through %s (MC3_SKIP_MOVIES)\n", name);
    }
    if (name[0] && g_eax)
        mc3_bink_native_open(g_eax, name);
}

/* BinkClose, 0x003ADA70: stdcall (hbink). */
extern void sub_003ADA70_gen(void);
void sub_003ADA70(void)
{
    if (s_hard_hbink && MEM32(g_esp + 4) == s_hard_hbink) {
        fprintf(stderr, "[BINK] skipped, no frame played\n");
        s_hard_hbink = 0;
    }
    if (s_skip_hbink && MEM32(g_esp + 4) == s_skip_hbink) {
        usb_gamepad_set_held(0, 0, 0);
        fprintf(stderr, "[BINK] skipped after %u frames\n", s_skip_frames);
        s_skip_hbink = 0;
    }
    mc3_bink_native_close(MEM32(g_esp + 4));
    sub_003ADA70_gen();
}

/* Bink video decode, 0x003B2F40: stdcall, 14 arguments -- (dest frame,
 * previous frame, 0, 0, changed-block map = HBINK+0x3A0, w, h, padded w, padded h,
 * packet, ...),
 * returns 1. Called from BinkDoFrame at 0x003AD82A. The lifted decoder runs
 * when FFmpeg has no frame for this movie, and for difftest captures. */
extern void sub_003B2F40_gen(void);
static void bink_video_decode(void)
{
    static long calls;
    uint32_t map = MEM32(g_esp + 20);
    uint32_t hbink = map - BINK_DIRTY_MAP;

    if (s_skip_hbink && hbink == s_skip_hbink) {
        int down = (s_skip_frames++ % 6) < 3;
        usb_gamepad_set_held(0, down ? PAD_START : 0, down ? PAD_A : 0);
    }

    if (!mc3_difftest_wanted() && mc3_bink_native_enabled()) {
        int w = (int)MEM32(hbink + BINK_WIDTH), h = (int)MEM32(hbink + BINK_HEIGHT);
        int frame = (int)MEM32(hbink + BINK_FRAMENUM) - 1;
        mc3_bink_native_audio(hbink, frame);
        if (mc3_bink_native_frame(hbink, frame, (uint8_t *)XBOX_PTR(MEM32(g_esp + 4)), w, h)) {
            /* Every block changed, so BinkCopyToBuffer copies the whole frame:
             * it counts the map's leading non-zero bytes against the block
             * total. Same size as BinkDoFrame's own clear (0x003AD72F): the
             * 8th and 9th arguments (padded width, height) over 16. */
            uint32_t blocks = (MEM32(g_esp + 32) >> 4) * (MEM32(g_esp + 36) >> 4);
            memset((void *)XBOX_PTR(map), 1, blocks);
            g_eax = 1;
            g_esp += 4 + 0x38;
            return;
        }
    }
    mc3_difftest_call(0x003B2F40u, sub_003B2F40_gen, &calls);
}

void sub_003B2F40(void)
{
    bt_time(BT_VIDEO, bink_video_decode);
}

/* Bink inverse DCT for one 8x8 block, 0x003B5880: (dest, pitch, coeffs) on
 * the stack. In frame 600 its call #59 writes the one wrong block the
 * video difftest found (dest 0x80F3D400, coefficients at 0x00F7FADC). */
extern void sub_003B5880_gen(void);
void sub_003B5880(void)
{
    static long calls;
    mc3_difftest_call(0x003B5880u, sub_003B5880_gen, &calls);
}

extern void sub_003AEDB0_gen(void);
/* Bink audio decode, 0x003AEDB0: stdcall (state, &out_ptr, &out_bytes,
 * packet, &packet_pos), called from BinkDoFrame at 0x003AD5AD, which then
 * copies *out_bytes of PCM from *out_ptr into its DirectSound ring. While
 * FFmpeg plays the movie's sound (src/bink_native.c), that PCM is zeroed:
 * Bink's streaming and clock carry on, on silence. */
static void bink_audio_decode(void)
{
    static long calls;
    uint32_t out_ptr_va = MEM32(g_esp + 8), out_bytes_va = MEM32(g_esp + 12);

    mc3_difftest_call(0x003AEDB0u, sub_003AEDB0_gen, &calls);
    if (mc3_bink_native_audio_active() && !mc3_difftest_wanted()) {
        uint32_t out = MEM32(out_ptr_va), bytes = MEM32(out_bytes_va);
        if (out && bytes && bytes <= 0x100000u /* PORT? */)
            memset((void *)XBOX_PTR(out), 0, bytes);
    }
}

void sub_003AEDB0(void)
{
    bt_time(BT_AUDIO, bink_audio_decode);
}

/* ── Game timing: the clocks MC3 reads (MC3_TIME_TRACE=1) ──
 *
 * 2026-10-17: gameplay ran ~10x fast at a steady 60 fps while music and
 * movies kept time, and every host clock checked out at the source. These
 * wrap the title's own timer entry points and print once a second, per
 * clock, how often it was read and how far it moved (in its own units):
 *   sub_001A1C00  ms since boot: rdtsc / 733333        (1000 per second)
 *   sub_0032633B  QueryPerformanceCounter (rdtsc)      (733.33 M per second)
 *   sub_0032634C  QueryPerformanceFrequency            (733333333)
 *   sub_0020E140  rdtsc, low 32 bits                   (733.33 M per second) */
static int time_trace_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_TIME_TRACE");
        on = e && e[0] == '1';
    }
    return on;
}

static struct { const char *name; long calls; uint64_t first, last; } s_clk[4] = {
    { "ms clock (0x1A1C00)" }, { "QPC (0x32633B)" }, { "QPF (0x32634C)" }, { "rdtsc lo (0x20E140)" },
};

static void qpc_callers_print(void);

static void time_note(int k, uint64_t v)
{
    static long long next;
    struct timespec ts;
    long long now;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (!s_clk[k].calls++) s_clk[k].first = v;
    s_clk[k].last = v;
    if (!next) next = now + 1000;
    if (now < next) return;
    next = now + 1000;
    for (int i = 0; i < 4; i++) {
        if (!s_clk[i].calls) continue;
        fprintf(stderr, "[TIME] %-20s %6ld reads/s, moved %llu (last %llu)\n", s_clk[i].name,
                s_clk[i].calls, (unsigned long long)(s_clk[i].last - s_clk[i].first),
                (unsigned long long)s_clk[i].last);
        s_clk[i].calls = 0;
    }
    qpc_callers_print();
}

extern void sub_001A1C00_gen(void);
void sub_001A1C00(void)
{
    sub_001A1C00_gen();
    if (time_trace_on()) time_note(0, ((uint64_t)g_edx << 32) | g_eax);
}
/* QPC's callers: the return addresses of the last second, top 4 printed
 * with the [TIME] lines (2026-10-17: 600k reads/s during the intro
 * flythrough, none in the garage -- who polls it). */
typedef struct { uint32_t ret; long n; } CallerCount;
static CallerCount s_qpc_by[16], s_bink_by[16];

static void caller_note(CallerCount *t, uint32_t ret)
{
    int i, low = 0;
    for (i = 0; i < 16; i++) {
        if (t[i].ret == ret) { t[i].n++; return; }
        if (t[i].n < t[low].n) low = i;
    }
    t[low].ret = ret;
    t[low].n = 1;
}
static void qpc_caller(uint32_t ret) { caller_note(s_qpc_by, ret); }

static void callers_print(CallerCount *t, const char *what)
{
    for (int k = 0; k < 4; k++) {
        int best = -1;
        for (int i = 0; i < 16; i++)
            if (t[i].n > 0 && (best < 0 || t[i].n > t[best].n)) best = i;
        if (best < 0) break;
        fprintf(stderr, "[TIME]   %s called from %08X: %ld\n", what, t[best].ret, t[best].n);
        t[best].n = 0;                              /* printed */
    }
    memset(t, 0, 16 * sizeof *t);
}

static void qpc_callers_print(void)
{
    callers_print(s_qpc_by, "QPC");
    callers_print(s_bink_by, "Bink timer");
}

/* Bink's millisecond timer (RADTimerRead): its callers, for the loop that
 * reads it ~500k times a second during the intro flythrough. */
extern void sub_003AE3E0_gen(void);
void sub_003AE3E0(void)
{
    if (time_trace_on()) caller_note(s_bink_by, MEM32(g_esp));
    sub_003AE3E0_gen();
}

extern void sub_0032633B_gen(void);
void sub_0032633B(void)
{
    uint32_t out = MEM32(g_esp + 4), ret = MEM32(g_esp);
    sub_0032633B_gen();
    if (time_trace_on()) {
        qpc_caller(ret);
        time_note(1, ((uint64_t)MEM32(out + 4) << 32) | MEM32(out));
    }
}
extern void sub_0032634C_gen(void);
void sub_0032634C(void)
{
    uint32_t out = MEM32(g_esp + 4);
    sub_0032634C_gen();
    if (time_trace_on()) time_note(2, ((uint64_t)MEM32(out + 4) << 32) | MEM32(out));
}
extern void sub_0020E140_gen(void);
void sub_0020E140(void)
{
    sub_0020E140_gen();
    if (time_trace_on()) time_note(3, g_eax);
}

/* ── D3D's vblank handling, counted (MC3_PERF reports it) ──
 *
 * sub_0041B250 is D3D's vblank work: it advances the device's vblank count
 * (+0x1C0) and retires flips. Its callers run it when PMC_INTR_0's display
 * bit (0x01000000) is set -- on any interrupt, so a pushbuffer trap raised
 * while that bit is still up is counted as one more vblank. Should be 60 a
 * second; the report prints the real rate (2026-10-17: the intro flythrough
 * runs fast on Linux and reads no clock, so its time base is suspect). */
volatile long g_mc3_d3d_vblanks;
volatile uint32_t g_mc3_d3d_device;       /* ecx of the vblank handler: D3D's device */
extern void sub_0041B250_gen(void);
extern void xbox_Nv2aVblankTaken(void);
void sub_0041B250(void)
{
    g_mc3_d3d_vblanks++;
    g_mc3_d3d_device = g_ecx;
    /* The caller has consumed the pending interrupt. Acknowledge the plain
     * MMIO backing now, before D3D spins waiting for PMC bit 24 to drop. */
    xbox_Nv2aVblankTaken();
    sub_0041B250_gen();
}

/* ── The main loop's passes, counted (MC3_PERF reports them) ──
 *
 * sub_0001FA11 is the game's main loop; it calls sub_002D5400 once per
 * pass, before the update. On the console a pass is paced by the display
 * (60 a second); the intro flythrough runs ~10x fast on Linux while the
 * presented frame rate, the frame time and every clock read are right
 * (2026-10-18) -- so: passes per second against frames presented. */
volatile long g_mc3_loop_passes;
extern void sub_002D5400_gen(void);
void sub_002D5400(void)
{
    g_mc3_loop_passes++;
    sub_002D5400_gen();
    port_gameplay_update();
#if defined(_WIN32) || defined(__linux__)
    port_media_frame();
#endif
}

/* ── D3D's PGRAPH handler: the trap protocol with an acknowledgement ──
 *
 * sub_0041B7C0 is D3D's PGRAPH interrupt handler: it reads PGRAPH_INTR and
 * NSOURCE (0xFD400100/0108), and for a software method (NOP) hands the
 * trapped parameter to the pushbuffer callback sub_0041B550. It acknowledges
 * by writing the status back -- write-one-to-clear on the chip, a no-op on
 * the plain memory here -- so the trap stayed visible, and D3D's interrupt
 * routine ran the handler again and again on it before the executor saw it
 * taken: 412,082 flips queued in one run (2026-10-18), D3D's flip targets
 * millions of vblanks ahead and its frame bookkeeping far ahead of the
 * screen. The toolkit's documented cure for that (NFSU2 hit the same): wrap
 * the handler under the executor's trap lock and report a trap it found as
 * taken, which clears what the hardware would (nv2a_pb_exec.c). */
extern void nv2a_pb_trap_lock(void);
extern void nv2a_pb_trap_unlock(void);
extern void nv2a_pb_trap_taken(void);
extern void sub_0041B7C0_gen(void);
void sub_0041B7C0(void)
{
    static int on = -1;
    uint32_t nsource;
    if (on < 0) {                      /* MC3_TRAP_HOOK=0: the old protocol, to compare */
        const char *e = getenv("MC3_TRAP_HOOK");
        on = !(e && e[0] == '0');
    }
    if (!on) {
        sub_0041B7C0_gen();
        return;
    }
    nv2a_pb_trap_lock();
    nsource = MEM32(0xFD400108u);
    sub_0041B7C0_gen();
    if (nsource)
        nv2a_pb_trap_taken();
    nv2a_pb_trap_unlock();
}

/* ── Frame pacing: D3D's flip retirement (vblank) ──
 *
 * D3D queues each flip with a target vblank: previous target + the
 * presentation interval the title asked for (the low bits of the callback's
 * parameter; MC3's menus use 2, i.e. 30 fps). sub_0041B170, called from the
 * vblank handler, retires the flips whose target has come and returns how
 * many. The executor's FLIP_STALL waits for those retirements once the
 * project reports them (nv2a_pb_flip_retired); unreported, it waited one
 * vblank whatever the interval, and the 30 fps menus, flythrough and garage
 * played at 60 -- the game advances them per frame. A first attempt
 * (2026-10-17) stuck at 4 fps: the trap storm above had wrecked D3D's flip
 * queue (targets millions of vblanks ahead). With the trap acknowledged,
 * the queue is sane and retirement can pace the flips. MC3_FLIP_PACE=0
 * falls back to one flip per vblank. */
extern void nv2a_pb_flip_retired(unsigned n);
extern void nv2a_pb_flip_retired_one_if_empty(void);
volatile long g_mc3_flip_callbacks;

/* The title's frame rate for this run. 30 is the shipped cadence; a user asks
 * for 60 by putting MC3_FRAME_RATE=60 in mcdub3remix_env.txt (the older
 * MC3_FRAME_RATE_TEST_FPS name still works). Resolved once: the flip and
 * simulation wrappers below ask on every frame. */
static int mc3_frame_rate_mode(void)
{
    static int mode = -1;
    if (mode < 0) {
        const char *e = getenv("MC3_FRAME_RATE");
        if (!e || !e[0]) e = getenv("MC3_FRAME_RATE_TEST_FPS");
        mode = e && e[0] ? atoi(e) : 30;
#ifdef MC3_ALLOW_120
        /* Experimental frame-rate build: 30, 60 or 120. */
        if (mode != 30 && mode != 60 && mode != 120) mode = 30;
#else
        /* Release builds: exactly two cadences, the Xbox's 30 and the
         * optional 60. Nothing else exists in this binary. */
        mode = mode >= 60 ? 60 : 30;
#endif
    }
    return mode;
}

/* A pedestrian switch that defaults on in 60 Hz mode and off in 30 Hz mode:
 * 60 fps needs the corrected animation cadence and pose blending, while 30
 * must keep the published path untouched. =0 turns it off for A/B tests. */static int mc3_pedestrian_switch(const char *name)
{
    const char *e;
    if (mc3_frame_rate_mode() < 60)
        return 0;
    e = getenv(name);
    return !(e && e[0] == '0');
}

extern void sub_0041B170_gen(void);
void sub_0041B170(void)
{
    static int on = -1;
    static uint32_t last_retire_vblank;
    static unsigned retires_this_vblank;
    if (on < 0) {
        const char *e = getenv("MC3_FLIP_PACE");
        on = !(e && e[0] == '0');
    }
    int frame_rate_mode = mc3_frame_rate_mode();
    sub_0041B170_gen();
    if (frame_rate_mode < 60) {
        if (on) nv2a_pb_flip_retired(g_eax);
        return;
    }
    if (on) {
        int gameplay = 0;
#if (defined(_WIN32) || defined(__linux__)) && !defined(__ANDROID__)
        extern int windows_media_gameplay_get(void);
        gameplay = windows_media_gameplay_get();
#else
        gameplay = atomic_load(&port_gameplay_active);
#endif
        {
            uint32_t vblank = (uint32_t)g_mc3_d3d_vblanks;
            /* D3D checks retirement several times per vblank. In the 60 Hz
             * mode gameplay consumes one frame per vblank; the 120 Hz mode
             * consumes two. Menus and pause keep the title's original
             * interval-two cadence (30) in both, so the menu does not turn
             * into a 120 Hz pinball table.
             *
             * Supplying the bounded fallback in every state also prevents a
             * gameplay -> pause transition from stranding D3D's queue. */
#ifdef MC3_ALLOW_120
            unsigned per_vblank = gameplay ? (frame_rate_mode >= 120 ? 2u : 1u) : 0u;
#else
            unsigned per_vblank = gameplay ? 1u : 0u;
#endif
            unsigned want = per_vblank;
            if (!gameplay && !(vblank & 1u))
                want = 1;                          /* menus: one per two vblanks */
            if (vblank != last_retire_vblank) {
                last_retire_vblank = vblank;
                retires_this_vblank = 0;
            }
            if (retires_this_vblank < want) {
                retires_this_vblank++;
                if (g_eax)
                    nv2a_pb_flip_retired(g_eax);
                else
                    nv2a_pb_flip_retired_one_if_empty();
            }
        }
    }
}

/* ── D3D's pushbuffer callback, logged (MC3_FLIP_TRACE=1) ──
 *
 * sub_0041B550(param) runs when the GPU reaches one of D3D's software
 * methods (the executor's NOP trap). param & 0x1F is the command; for a
 * flip, (param >> 5) & 7 is the presentation interval and the frame's
 * target vblank becomes max(previous target +0x1C8 + interval, vblank
 * count +0x1C0). It showed the trap storm (targets millions of vblanks
 * ahead) and the menus' interval 2 (2026-10-18). First 40 calls, then
 * every 120th. */
extern void sub_0041B550_gen(void);
void sub_0041B550(void)
{
    static int on = -1;
    static int trace_on = -1;
    static long n;
    static FILE *frame_trace;
    static long frame_trace_count;
    uint32_t dev = g_ecx, param = MEM32(g_esp + 4), t_before = 0, vb = 0;
#ifdef RECOMP_FRAME_RATE_TEST
    g_mc3_flip_callbacks++;      /* frame-test metrics only; absent in release */
#endif
    if (on < 0) {
        const char *e = getenv("MC3_FLIP_TRACE");
        on = e && e[0] == '1';
    }
    if (trace_on < 0) {
        const char *e = getenv("MC3_FRAME_RATE_TRACE");
        trace_on = e && e[0];
    }
    if (trace_on && !frame_trace)
        frame_trace = fopen("frame-flip-trace.log", "w");
    if (frame_trace && frame_trace_count < 240) {
        fprintf(frame_trace, "n=%ld param=%08X cmd=%u interval=%u dev=%08X\n",
                ++frame_trace_count, param, param & 0x1F, (param >> 5) & 7, dev);
        fflush(frame_trace);
    }
    if (on && dev) { t_before = MEM32(dev + 0x1C8); vb = MEM32(dev + 0x1C0); }
    sub_0041B550_gen();
    if (on && dev && (++n <= 40 || n % 120 == 0))
        fprintf(stderr, "[FLIP] #%ld param %08X cmd %u data %X | target %u -> %u, next %u, vblank %u (dev %08X)\n",
                n, param, param & 0x1F, param >> 5, t_before, MEM32(dev + 0x1C8),
                MEM32(dev + 0x1C4), vb, dev);
}

/* ── The loading screen starts: compile the remembered shaders now ──
 *
 * sub_0001BC10 starts the loading-screen thread (0x0001BBF0, priority 14):
 * first behind the legal screen, while the game loads for seconds. Without
 * program binaries (the Switch's Mesa 20.1) the renderer compiled its whole
 * program list before the first logo: 4.8 s of black (2026-10-18). Now it
 * waits for this and compiles a program a frame behind a static screen
 * (nv2a_gl_prewarm_start; RECOMP_PROG_PREWARM=boot for the old way). */
extern void nv2a_gl_prewarm_start(void);
extern void sub_0001BC10_gen(void);
void sub_0001BC10(void)
{
    nv2a_gl_prewarm_start();
    sub_0001BC10_gen();
}

/* ── The loading screen keeps its 4:3 shape ──
 *
 * sub_0001B9D0 is the loading-screen thread's loop (thread 0x0001BBF0,
 * started only from the boot sequence at 0x00020387 (PORT?)): the legal screen.
 * With "legal" in MC3_FILL_SKIP it is shown at its own 4:3 shape, the fill
 * held while the loop runs. Not the default: the loop also runs behind
 * other loading screens and ends before some of them do, so they started
 * at 4:3 and switched to 16:9 halfway (2026-10-20). */
extern void nv2a_gl_fill_hold(int delta) __attribute__((weak));
extern void nv2a_gl_fill_freeze(int delta) __attribute__((weak));   /* RECOMP_GL_FILL_STEADY */
extern void sub_0001B9D0_gen(void);
static RECOMP_TLS int t_ls_thread;          /* this is the loading-screen thread */
void sub_0001B9D0(void)
{
    const char *e = getenv("MC3_FILL_SKIP");
    int hold = nv2a_gl_fill_hold && e && strstr(e, "legal");   /* opt-in: see above */
    if (hold)
        nv2a_gl_fill_hold(1);
    t_ls_thread = 1;
    if (nv2a_gl_fill_freeze)
        nv2a_gl_fill_freeze(1);             /* the circle's in and out: no change of size */
    sub_0001B9D0_gen();
    if (nv2a_gl_fill_freeze)
        nv2a_gl_fill_freeze(-1);
    t_ls_thread = 0;
    if (hold)
        nv2a_gl_fill_hold(-1);
}

/* ── The loading screen's loop, timed (MC3_LS_TRACE=1) ──
 *
 * Each turn of sub_0001B9D0: the Flash movie advances a fixed 1/30 s
 * (sub_0013A0F0, 0x3D08882F), is drawn and presented (sub_002D2990), then
 * Sleep(24) (sub_0001735C). Logs every call on the loading thread with
 * its time, and presents from any other thread. It showed the loop turning
 * evenly every 33 ms while the circles stuttered (2026-10-24): the cause
 * was the renderer, which ignored the 2D blit that carries each frame into
 * the next back buffer (xboxrecomp, nv2a_pb_exec.c). */
static int ls_trace_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_LS_TRACE");
        on = e && e[0] == '1';
    }
    return on;
}
static double ls_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
const char *xbox_gil_my_label(void);
static void ls_log(const char *what, double t0)
{
    double t1 = ls_now();
    const char *l = xbox_gil_my_label();
    fprintf(stderr, "[LS] @%.3f %s %s %.1f ms\n", t0 / 1e3, t_ls_thread ? "ls" : l ? l : "?",
            what, t1 - t0);
}
static void ls_call(void (*fn)(void), const char *what, int any_thread)
{
    double t0;
    if (!ls_trace_on() || !(t_ls_thread || any_thread)) {
        fn();
        return;
    }
    t0 = ls_now();
    fn();
    ls_log(what, t0);
}
extern void sub_0013A0F0_gen(void);
void sub_0013A0F0(void) { ls_call(sub_0013A0F0_gen, "advance", 0); }
extern void sub_002D2990_gen(void);
void sub_002D2990(void) { ls_call(sub_002D2990_gen, "present", 1); }
/* ── The heartbeat thread sleeps instead of spinning (MC3_HEARTBEAT_MS) ──
 *
 * sub_001B15D0 (its own thread) is a one-second watchdog: enter a critical
 * section, compare the CPU clock with the last beat (ticks / 733 MHz >=
 * 1.0 s: sub_00047CE0), leave, Sleep(0) at 0x001B164D, again. On the
 * Switch each turn took the guest lock from the main thread: 4,200 turns
 * a second, the main thread waiting behind it 3,400 times a second at
 * ~50 us a hand-over -- most of its 30% waiting in the menus (19.5 fps,
 * 2026-10-21 log). A beat checked every few milliseconds is as good.
 * This turns that one Sleep(0) into Sleep(MC3_HEARTBEAT_MS), default 8;
 * 0 restores the game's own. */
static uint32_t heartbeat_ms(void)
{
    static int ms = -1;
    if (ms < 0) {
        const char *e = getenv("MC3_HEARTBEAT_MS");
        ms = e ? atoi(e) : 8;
        if (ms < 0) ms = 0;
    }
    return (uint32_t)ms;
}

extern void sub_0001735C_gen(void);
void sub_0001735C(void)
{
    if (MEM32(g_esp) == 0x001B1654u && MEM32(g_esp + 4) == 0) {   /* the heartbeat's Sleep(0) */
        static int said;
        MEM32(g_esp + 4) = heartbeat_ms();
        if (!said++) {
            const char *l = xbox_gil_my_label();
            fprintf(stderr, "[MC3] heartbeat thread (%s): Sleep(0) -> Sleep(%u) (MC3_HEARTBEAT_MS)\n",
                    l ? l : "?", heartbeat_ms());
        }
    }
    ls_call(sub_0001735C_gen, "sleep", 0);
}

/* The way out: once the game sets the stop flag (+0x2004 of the loop's
 * object), each turn sends the Flash movie to "StartOfTransitionOut"
 * (sub_00139520, this = movie, label, 1) and leaves when sub_00139690
 * says it has reached "EndOfTransitionOut" (returns 1). Logged with their
 * arguments and results on the loading thread. A load that ends while the
 * intro still plays (fast storage: 0.5 s) gets a transition-out with its
 * shapes still off stage, so the screen cuts away with the spinner up; at
 * a DVD's pace (RECOMP_IO_MBPS=8 RECOMP_IO_SEEK_MS=80) it plays out. */
static void ls_label(void (*fn)(void), const char *what)
{
    uint32_t movie = g_ecx, label = MEM32(g_esp + 4), arg2 = MEM32(g_esp + 8);
    char name[40];
    uint32_t i;
    fn();
    if (!ls_trace_on() || !t_ls_thread)
        return;
    for (i = 0; i + 1 < sizeof name && label && MEM8(label + i); i++)
        name[i] = (char)MEM8(label + i);
    name[i] = 0;
    fprintf(stderr, "[LS] @%.3f ls %s(movie %08X, \"%s\", %u) -> %u\n", ls_now() / 1e3,
            what, movie, name, arg2, g_eax);
}
extern void sub_00139520_gen(void);
void sub_00139520(void) { ls_label(sub_00139520_gen, "goto"); }
extern void sub_00139690_gen(void);
void sub_00139690(void) { ls_label(sub_00139690_gen, "reached"); }

/* ── Save-game deletion, traced (MC3_SAVE_TRACE=1) ──
 *
 * Deleting a profile says "deleted" but leaves it on disk, and no file call
 * reaches the kernel (2026-10-18). sub_0002A950(name) is the game's delete:
 * it calls XDeleteSaveGame (sub_00324C8E) with "<letter>:\\" only when its
 * device's drive letter ([this+8]) is set. This logs which way it goes and
 * what XDeleteSaveGame returned. */
static int save_trace_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_SAVE_TRACE");
        on = e && e[0] == '1';
    }
    return on;
}

static void save_trace_name(char *out, size_t cap, uint32_t va)
{
    size_t i = 0;
    for (; va && i + 1 < cap; i++) {        /* wide string, ASCII shown */
        uint16_t c = MEM16(va + 2 * i);
        if (!c) break;
        out[i] = c < 0x80 ? (char)c : '?';
    }
    out[i] = 0;
}

extern void sub_0002A950_gen(void);
void sub_0002A950(void)
{
    uint32_t self = g_ecx, name = MEM32(g_esp + 4), ret = MEM32(g_esp);
    char n[64];
    if (save_trace_on()) {
        save_trace_name(n, sizeof n, name);
        fprintf(stderr, "[SAVE] delete \"%s\" device %08X letter '%c' (0x%02X), from %08X\n",
                n, self, MEM8(self + 8) >= 0x20 ? MEM8(self + 8) : '?', MEM8(self + 8), ret);
    }
    sub_0002A950_gen();
    if (save_trace_on())
        fprintf(stderr, "[SAVE] delete -> %u\n", g_eax);
}

extern void sub_00324C8E_gen(void);
void sub_00324C8E(void)
{
    uint32_t root = MEM32(g_esp + 4), name = MEM32(g_esp + 8);
    char n[64], r[8] = {0};
    int i;
    if (save_trace_on()) {
        save_trace_name(n, sizeof n, name);
        for (i = 0; i < 7 && root && MEM8(root + i); i++)
            r[i] = (char)MEM8(root + i);
        fprintf(stderr, "[SAVE] XDeleteSaveGame(\"%s\", \"%s\")\n", r, n);
    }
    sub_00324C8E_gen();
    if (save_trace_on())
        fprintf(stderr, "[SAVE] XDeleteSaveGame -> %u\n", g_eax);
}

/* ── "Is pad n a steering wheel?", asked once per controller ──
 *
 * sub_00084BD0 (thiscall, the game's pad object) returns
 * XInputGetCapabilities(handle).SubType == 0x10 (XINPUT_DEVSUBTYPE_GC_WHEEL),
 * and the game asks every frame. XInputGetCapabilities is a synchronous USB
 * control transfer (XPP 0x00441DAF, waiting at 0x0044535B with a 50 ms
 * timeout): ~1-2 ms on the Xbox, ~19 ms on the emulated OHCI on the Switch,
 * where the transfer waits for the controller thread's 4 ms pass and the
 * completion interrupt for the guest lock. Two per frame took 450 ms of
 * every second of the main thread in a race (10-12 fps, 2026-10-20). The
 * answer cannot change while the same handle is open, so it is kept per
 * handle ([pad+0x34]; [pad+0x1C] == 4 is "open"). MC3_PAD_CAPS_CACHE=0
 * asks every time, as the game does. */
extern void sub_00084BD0_gen(void);
void sub_00084BD0(void)
{
    static uint32_t handle_of[8], answer_of[8];
    static unsigned next;
    static int on = -1;
    uint32_t pad = g_ecx, handle = MEM32(pad + 0x34);
    int open = MEM32(pad + 0x1C) == 4 && handle;
    unsigned i;
    if (on < 0) {
        const char *e = getenv("MC3_PAD_CAPS_CACHE");
        on = !(e && e[0] == '0');
    }
    if (on && open)
        for (i = 0; i < 8; i++)
            if (handle_of[i] == handle) {
                g_eax = answer_of[i];
                g_esp += 4;                      /* ret: thiscall, no arguments */
                return;
            }
    sub_00084BD0_gen();
    if (on && open) {
        handle_of[next % 8] = handle;
        answer_of[next % 8] = g_eax & 0xFFu;
        next++;
    }
}

/* ── Stick shaping (sub_000847B0), traced ──
 *
 * The pad object's read (sub_000848F0) turns each stick into two bytes
 * (0x80 = centred) and hands them, as esi/edi pointers, to sub_000847B0,
 * which shapes them in SSE + x87 (the DUB Edition's twin is x87 only).
 * MC3_PAD_TRACE=1 logs the bytes going in and out, once a second while
 * they hold still and at every change. */
extern void sub_000847B0_gen(void);
void sub_000847B0(void)
{
    static int on = -1;
    static uint32_t last_key[2];
    static unsigned n[2];
    uint32_t px = g_esi, py = g_edi;
    uint8_t x0, y0;
    if (on < 0) {
        const char *e = getenv("MC3_PAD_TRACE");
        const char *t = getenv("MC3_PAD_SELFTEST");
        on = e && e[0] == '1';
        if (t && t[0] == '1') {
            /* Once: the lifted shaping over every x byte (y centred) with
             * this call's weight, for tools/difftest-style comparison with
             * the original in Unicorn. Guest stack below esp as scratch. */
            uint32_t sx = g_esp - 0x200, sy = g_esp - 0x1FC, arg = MEM32(g_esp + 4);
            uint32_t esi0 = g_esi, edi0 = g_edi, esp0 = g_esp, b;
            char line[1100];
            size_t q = 0;
            for (b = 0; b < 256; b++) {
                MEM8(sx) = (uint8_t)b;
                MEM8(sy) = 0x80;
                g_esi = sx; g_edi = sy;
                g_esp = esp0 - 0x100;
                g_esp -= 4; MEM32(g_esp) = arg;
                g_esp -= 4; MEM32(g_esp) = 0;          /* return address */
                sub_000847B0_gen();                    /* ret: pops it */
                q += (size_t)snprintf(line + q, sizeof line - q, "%02X", MEM8(sx));
            }
            g_esi = esi0; g_edi = edi0; g_esp = esp0;
            fprintf(stderr, "[PAD] selftest weight %08X: x out for x in 00..FF: %s\n", arg, line);
        }
    }
    if (!on) {
        sub_000847B0_gen();
        return;
    }
    x0 = MEM8(px); y0 = MEM8(py);
    sub_000847B0_gen();
    {
        int which = (px & 3) == 2;         /* +0x28/0x29 left, +0x2A/0x2B right */
        uint32_t key = (uint32_t)x0 << 24 | (uint32_t)y0 << 16 | MEM8(px) << 8 | MEM8(py);
        if (key != last_key[which] || ++n[which] % 60 == 0) {
            fprintf(stderr, "[PAD] %s stick in %02X,%02X -> out %02X,%02X (pad 0x%08X)\n",
                    which ? "right" : "left", x0, y0, MEM8(px), MEM8(py), px & ~3u);
            last_key[which] = key;
        }
    }
}

/* ── Race cameras, traced and capturable ──
 *
 * The camera classes (camTrackCS, camPolarCS, camPovCS, camPointCS; their
 * vtables in .rdata at 0x00476500-0x00476E40 and 0x004A4200-0x004A4E60)
 * share a 15-slot layout; wrapped here are their own methods (slots 3, 4,
 * 8-10, 13, 14) and the camera base's (sub_001383F0 tune/camera,
 * sub_00138470 CameraFOV, sub_00138510). The DUB Edition's are x87, the
 * Remix's SSE.
 *
 * MC3_CAM_TRACE=1: once a second, each method called that second, how
 * often, and its object and vtable (which class is active).
 * MC3_DIFFTEST_CALL=key (src/difftest.c): F10 captures the next call of
 * each, for tools/difftest.py. */
extern void mc3_difftest_call(uint32_t func, void (*lifted)(void), long *calls);

#define CAM_N 54
static uint32_t cam_addr[CAM_N], cam_this[CAM_N];
static long cam_calls[CAM_N], cam_count[CAM_N];

/* ── The keyboard while driving ──
 *
 * The keyboard's up arrow is the gas (right trigger) and, for menus, left
 * stick up. In a race the Remix reads stick up/down for its camera (it
 * rises and moves in, 2026-10-25; the DUB Edition ignores it), so with
 * the gas held the camera stayed pulled in. While the player's race camera
 * control (sub_002C1520) runs every frame and the game is not paused, the
 * keyboard leaves the stick alone (xbox_keyboard_driving, sdl_keyboard.c).
 * Paused / menu: the state machine's flag at +9 (sub_002C48C0 turns the
 * look-around off on it) or state 3. MC3_KEYS_TRACE=1 logs each change. */
extern int (*xbox_keyboard_driving)(void);
static volatile uint32_t s_race_cam_ms;

static uint32_t host_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static int remix_driving(void)
{
    static int last = -1;
    uint32_t sm = MEM32(0x00557ED0u);
    int on = s_race_cam_ms && host_ms() - s_race_cam_ms < 250u
             && sm && MEM32(sm + 4) != 3 && MEM8(sm + 9) == 0;
    if (on != last) {
        const char *e = getenv("MC3_KEYS_TRACE");
        if (e && e[0] == '1')
            fprintf(stderr, "[KEYS] %s: up/down %s\n", on ? "driving" : "not driving",
                    on ? "are the triggers only" : "move the stick too");
        last = on;
    }
    return on;
}

static void race_camera_ran(void)
{
    s_race_cam_ms = host_ms();
    xbox_keyboard_driving = remix_driving;
}

static void cam_call(int slot, uint32_t addr, void (*lifted)(void))
{
    static int on = -1;
    static time_t last;
    time_t now;
    int i;
    if (on < 0) {
        const char *e = getenv("MC3_CAM_TRACE");
        on = e && e[0] == '1';
    }
    if (on) {
        cam_addr[slot] = addr;
        cam_this[slot] = g_ecx;
        cam_count[slot]++;
        now = time(NULL);
        if (now != last) {
            for (i = 0; i < CAM_N; i++)
                if (cam_count[i]) {
                    uint32_t t = cam_this[i];
                    uint32_t vt = (t >= 0x10000u && t < 0x08000000u) || (t >= 0x80000000u && t < 0x84000000u) ? MEM32(t) : 0;
                    fprintf(stderr, "[CAM] sub_%08X x%ld this=%08X vtable=%08X\n",
                            cam_addr[i], cam_count[i], cam_this[i], vt);
                    cam_count[i] = 0;
                }
            fprintf(stderr, "[CAM] --\n");
            last = now;
        }
    }
    if (addr == 0x002C1520u)
        race_camera_ran();
    mc3_difftest_call(addr, lifted, &cam_calls[slot]);
}

extern void sub_000AF930_gen(void);
void sub_000AF930(void) { cam_call(0, 0x000AF930u, sub_000AF930_gen); }
extern void sub_001383F0_gen(void);
void sub_001383F0(void) { cam_call(1, 0x001383F0u, sub_001383F0_gen); }
extern void sub_00138470_gen(void);
void sub_00138470(void) { cam_call(2, 0x00138470u, sub_00138470_gen); }
extern void sub_00138510_gen(void);
void sub_00138510(void) { cam_call(3, 0x00138510u, sub_00138510_gen); }
extern void sub_001E28A0_gen(void);
void sub_001E28A0(void) { cam_call(4, 0x001E28A0u, sub_001E28A0_gen); }
extern void sub_002CDDE0_gen(void);
void sub_002CDDE0(void) { cam_call(5, 0x002CDDE0u, sub_002CDDE0_gen); }
extern void sub_002CE2E0_gen(void);
void sub_002CE2E0(void) { cam_call(6, 0x002CE2E0u, sub_002CE2E0_gen); }
extern void sub_002CE300_gen(void);
void sub_002CE300(void) { cam_call(7, 0x002CE300u, sub_002CE300_gen); }
extern void sub_002CE650_gen(void);
void sub_002CE650(void) { cam_call(8, 0x002CE650u, sub_002CE650_gen); }
extern void sub_002CF080_gen(void);
void sub_002CF080(void) { cam_call(9, 0x002CF080u, sub_002CF080_gen); }
extern void sub_002CF4A0_gen(void);
void sub_002CF4A0(void) { cam_call(10, 0x002CF4A0u, sub_002CF4A0_gen); }
extern void sub_002CF6C0_gen(void);
void sub_002CF6C0(void) { cam_call(11, 0x002CF6C0u, sub_002CF6C0_gen); }
extern void sub_002CF710_gen(void);
void sub_002CF710(void) { cam_call(12, 0x002CF710u, sub_002CF710_gen); }
extern void sub_002CF760_gen(void);
void sub_002CF760(void) { cam_call(13, 0x002CF760u, sub_002CF760_gen); }
extern void sub_002CF970_gen(void);
void sub_002CF970(void) { cam_call(14, 0x002CF970u, sub_002CF970_gen); }
extern void sub_002CFED0_gen(void);
void sub_002CFED0(void) { cam_call(15, 0x002CFED0u, sub_002CFED0_gen); }
extern void sub_002CFF50_gen(void);
void sub_002CFF50(void) { cam_call(16, 0x002CFF50u, sub_002CFF50_gen); }
extern void sub_002CFF60_gen(void);
void sub_002CFF60(void) { cam_call(17, 0x002CFF60u, sub_002CFF60_gen); }
extern void sub_002D01C0_gen(void);
void sub_002D01C0(void) { cam_call(18, 0x002D01C0u, sub_002D01C0_gen); }
extern void sub_00354C90_gen(void);
void sub_00354C90(void) { cam_call(19, 0x00354C90u, sub_00354C90_gen); }
extern void sub_00355140_gen(void);
void sub_00355140(void) { cam_call(20, 0x00355140u, sub_00355140_gen); }
extern void sub_00355170_gen(void);
void sub_00355170(void) { cam_call(21, 0x00355170u, sub_00355170_gen); }
extern void sub_00357410_gen(void);
void sub_00357410(void) { cam_call(22, 0x00357410u, sub_00357410_gen); }
extern void sub_00357960_gen(void);
void sub_00357960(void) { cam_call(23, 0x00357960u, sub_00357960_gen); }
extern void sub_00357D10_gen(void);
void sub_00357D10(void) { cam_call(24, 0x00357D10u, sub_00357D10_gen); }
extern void sub_00357FF0_gen(void);
void sub_00357FF0(void) { cam_call(25, 0x00357FF0u, sub_00357FF0_gen); }
extern void sub_003580C0_gen(void);
void sub_003580C0(void) { cam_call(26, 0x003580C0u, sub_003580C0_gen); }
extern void sub_00358110_gen(void);
void sub_00358110(void) { cam_call(27, 0x00358110u, sub_00358110_gen); }
extern void sub_00362200_gen(void);
void sub_00362200(void) { cam_call(28, 0x00362200u, sub_00362200_gen); }
extern void sub_00363030_gen(void);
void sub_00363030(void) { cam_call(29, 0x00363030u, sub_00363030_gen); }
extern void sub_003635D0_gen(void);
void sub_003635D0(void) { cam_call(30, 0x003635D0u, sub_003635D0_gen); }
extern void sub_003636A0_gen(void);
void sub_003636A0(void) { cam_call(31, 0x003636A0u, sub_003636A0_gen); }

/* The player manager (sub_002BE2C0) built at each cruise load. Free roam's
 * remote slots go into the race setup before the players are made (roam_build),
 * and its standalone cars after the game's own (roam_pool_load). */
extern void sub_002BE2C0_gen(void);
void sub_002BE2C0(void)
{
    uint32_t r[8] = { g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi };
    if (roam_running()) {
        roam_build();
        g_eax = r[0]; g_ecx = r[1]; g_edx = r[2]; g_ebx = r[3];
        g_esp = r[4]; g_ebp = r[5]; g_esi = r[6]; g_edi = r[7];
    }
    sub_002BE2C0_gen();
    if (roam_running()) {
        roam_built();
        roam_pool_load();
    }
}

/* sub_002BDEC0 (player) -> sub_002C1520 (the player's camera control)
 * -> sub_002C5B30 -> the camera's update. Wrapped for the census and for
 * MC3_DIFFTEST_FUNC=198E90,2BDEC0,2C1520,2C5B30. */
extern void sub_00198E90_gen(void);
void sub_00198E90(void) { cam_call(32, 0x00198E90u, sub_00198E90_gen); }
extern void sub_002BDEC0_gen(void);
void sub_002BDEC0(void)
{
    cam_call(33, 0x002BDEC0u, sub_002BDEC0_gen);
    if (roam_running()) {               /* after the physics step (roam_pool.c) */
        roam_pool_frame();
        roam_tag_frame();               /* and the name tags over the remote cars */
        dri_ambient_pass();             /* MC3_ROAM_AMBIENT: the ambient pools, once */
    }
}
extern void sub_002C1520_gen(void);
void sub_002C1520(void)
{
    uint32_t p = g_ecx;
    if (roam_running())                 /* before each player's update (roam_game.c) */
        roam_player(p);
    g_ecx = p;
    cam_call(34, 0x002C1520u, sub_002C1520_gen);
}

/* The HUD map's player-blip walker (sub_000BF850, thiscall on the map): our
 * linked peers' blips after the game's own (roam_blips.c). */
extern void sub_000BF850_gen(void);
void sub_000BF850(void)
{
    uint32_t map = g_ecx, top = g_esp;
    sub_000BF850_gen();
    if (roam_running()) {
        uint32_t r[8] = { g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi };
        roam_blips(map, top);
        g_eax = r[0]; g_ecx = r[1]; g_edx = r[2]; g_ebx = r[3];
        g_esp = r[4]; g_ebp = r[5]; g_esi = r[6]; g_edi = r[7];
    }
}
extern void sub_002C5B30_gen(void);
void sub_002C5B30(void) { cam_call(35, 0x002C5B30u, sub_002C5B30_gen); }

/* The drawing side: the projection builder sub_002D4AE0 (thiscall on a
 * viewport; fov, aspect, near, far on the stack) and the code that calls
 * it while a frame is drawn. The chase camera computes its FOV right
 * (dolly zoom: 40 deg standing, up to 60 at speed, while it moves in to
 * 0.8 of its distance), yet on screen only the move-in shows. MC3_CAM_TRACE
 * logs what the builder is handed, once a second. */
extern void sub_002D4F10_gen(void);
void sub_002D4F10(void) { cam_call(36, 0x002D4F10u, sub_002D4F10_gen); }
extern void sub_00331840_gen(void);
void sub_00331840(void) { cam_call(37, 0x00331840u, sub_00331840_gen); }
extern void sub_00331A30_gen(void);
void sub_00331A30(void) { cam_call(38, 0x00331A30u, sub_00331A30_gen); }
extern void sub_00332BD0_gen(void);
void sub_00332BD0(void) { cam_call(39, 0x00332BD0u, sub_00332BD0_gen); }
extern void sub_00138640_gen(void);
void sub_00138640(void) { cam_call(40, 0x00138640u, sub_00138640_gen); }
extern void sub_0020ED50_gen(void);
void sub_0020ED50(void) { cam_call(41, 0x0020ED50u, sub_0020ED50_gen); }
extern void sub_002D4AE0_gen(void);
void sub_002D4AE0(void)
{
    static uint32_t vp[8];
    static time_t last[8];
    const char *e = getenv("MC3_CAM_TRACE");
    uint32_t this_vp = g_ecx;               /* thiscall: the viewport */
    int i;
    for (i = 0; i < 7 && vp[i] && vp[i] != this_vp; i++)
        ;                                  /* one line per viewport (main view, mirror, map) */
    vp[i] = this_vp;
    if (e && e[0] == '1' && time(NULL) != last[i]) {
        last[i] = time(NULL);
        fprintf(stderr, "[CAM] projection: viewport %08X fov %.3f aspect %.3f near %.3f far %.1f (from %08X)\n",
                this_vp, MEMF(g_esp + 4), MEMF(g_esp + 8), MEMF(g_esp + 12), MEMF(g_esp + 16), MEM32(g_esp));
    }
    cam_call(42, 0x002D4AE0u, sub_002D4AE0_gen);
    if (roam_running()) {               /* the main view's projection, for name tags */
        roam_tag_projection(this_vp);
        dri_ambient_draw();             /* MC3_ROAM_AMBIENT: ghost poses, last write before the draw */
    }
}

/* The rest of the race's frame update (the dispatcher at 0x0001EEDE calls
 * them in turn after sub_00198E90): wrapped so a capture can replay the
 * car's physics too. The camera and the drawing replay exactly at speed
 * (2026-10-25), so the zoom's wrong input comes from here. */
extern void sub_00034A10_gen(void);
void sub_00034A10(void) { cam_call(44, 0x00034A10u, sub_00034A10_gen); }
extern void sub_000398B0_gen(void);
void sub_000398B0(void) { cam_call(45, 0x000398B0u, sub_000398B0_gen); }
extern void sub_00036710_gen(void);
void sub_00036710(void) { cam_call(46, 0x00036710u, sub_00036710_gen); }
extern void sub_001C1FC0_gen(void);
void sub_001C1FC0(void) { cam_call(47, 0x001C1FC0u, sub_001C1FC0_gen); }
extern void sub_0009B270_gen(void);
void sub_0009B270(void) { cam_call(48, 0x0009B270u, sub_0009B270_gen); }
extern void sub_00034340_gen(void);
void sub_00034340(void) { cam_call(49, 0x00034340u, sub_00034340_gen); }
extern void sub_00032150_gen(void);
void sub_00032150(void) { cam_call(50, 0x00032150u, sub_00032150_gen); }
extern void sub_00049FC0_gen(void);
void sub_00049FC0(void) { cam_call(51, 0x00049FC0u, sub_00049FC0_gen); }
extern void sub_0006CC90_gen(void);
void sub_0006CC90(void) { cam_call(52, 0x0006CC90u, sub_0006CC90_gen); }
extern void sub_000EC910_gen(void);
void sub_000EC910(void) { cam_call(53, 0x000EC910u, sub_000EC910_gen); }

/* Ambient traffic (MC3_ROAM_AMBIENT): the wrappers that hand the probe the
 * live objects. The disassembly puts the city's traffic in a container with
 * the cars at +0x30/+0x34, built by sub_0006C840 from the tune nodes
 * (sub_000807A0 -> sub_0006BD20), spawned per frame by sub_0006A7D0. The
 * global manager at [0x5587E0] is captured too, so the probe can show which
 * of the two actually holds the cars. Nothing here runs unless the env var
 * is set (amb_note checks it), and none of it changes a register. */
extern void sub_0006C840_gen(void);
void sub_0006C840(void)                         /* the city traffic owner, once per load */
{
    uint32_t self = g_ecx;
    sub_0006C840_gen();
    amb_note(0, self, 0);
}

extern void sub_0006BD20_gen(void);
void sub_0006BD20(void)                         /* the cruise load's feed of it */
{
    uint32_t self = g_ecx;
    sub_0006BD20_gen();
    amb_note(1, self, 0);
}

extern void sub_0006A7D0_gen(void);
void sub_0006A7D0(void)                         /* its per-frame spawn pass */
{
    uint32_t r[8] = { g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi };
    uint32_t self = g_ecx, arg = MEM32(g_esp + 4u);
    amb_note(2, self, arg);
    g_eax = r[0]; g_ecx = r[1]; g_edx = r[2]; g_ebx = r[3];
    g_esp = r[4]; g_ebp = r[5]; g_esi = r[6]; g_edi = r[7];
    sub_0006A7D0_gen();
}

extern void sub_002B8B20_gen(void);
void sub_002B8B20(void)                         /* [0x5587E0], the global manager */
{
    uint32_t self = g_ecx;
    sub_002B8B20_gen();
    amb_note(3, self, 0);
}

/* Pedestrian types: sub_0022C110 (thiscall, 4 params) is the type's init -
 * it builds the model path from "$/ped" via sub_0022BC30 and stores the
 * tune/density at +0x40/+0x48. The type object's vtable is 0x46EBD8, and the
 * class has no live-agent array of its own, so capturing the type is the
 * first step to finding the manager that owns the walkers. */
extern void sub_0022C110_gen(void);
void sub_0022C110(void)
{
    uint32_t self = g_ecx;
    sub_0022C110_gen();
    amb_note(4, self, 0);
}

extern void sub_0022C170_gen(void);
void sub_0022C170(void)                         /* the type's second vtable pass */
{
    uint32_t self = g_ecx;
    sub_0022C170_gen();
    amb_note(5, self, 0);
}

/* The shared agent-record init: sub_00071AC0(record=ecx, type-or-object).
 * Traffic calls it with a 0x690 car object, and sub_0006D7D0 calls it with an
 * ambient *type* pointer. A pedestrian type (vtable 0x46EBD8) arriving here
 * means the record being filled is a live walker, which is what M5 needs. */
extern void sub_00071AC0_gen(void);
void sub_00071AC0(void)
{
    uint32_t r[8] = { g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi };
    uint32_t rec = g_ecx, who = MEM32(g_esp + 4u);
    amb_note(6, rec, who);
    g_eax = r[0]; g_ecx = r[1]; g_edx = r[2]; g_ebx = r[3];
    g_esp = r[4]; g_ebp = r[5]; g_esi = r[6]; g_edi = r[7];
    sub_00071AC0_gen();
}

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345 (PORT?)(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 (PORT?) entered, eax=0x%08X\n", g_eax);
 *       sub_00012345 (PORT?)();
 *       fprintf(stderr, "[TRACE] sub_00012345 (PORT?) returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345 (PORT?)) return traced_sub_00012345;
     * if (xbox_va == 0x0006D0D0) return stub_00067890;
     * if (xbox_va == 0x000ABCDE (PORT?)) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}

/* ── Career race results: listeners checked before they are called ──
 *
 * mcPgCareer::ReportRaceResult() (sub_00277EE0, named by its own messages)
 * hands the result to sub_002793C0, which calls every listener in a list
 * of 12-byte entries {u16 id, u32 key, object*} at this+0x78..+0x7C
 * (vtable slot 0x28; the entry at index this+0x94 first, -1 = none);
 * sub_00279430 does the same through slot 0x2C. Starting one career race
 * on the Switch, and after ~8 minutes on the Mac, the list held a stale
 * entry: first a null method (the runtime skipped the call), then an
 * object at garbage (0xCDCDCD08 on the Mac, 0x535F4D7C on the Switch), and
 * the game crashed (2026-10-21; same call chain on both). Which object
 * leaves its entry behind is not known yet. Until it is, entries whose
 * object, vtable or method do not point where they must (object in RAM,
 * vtable in .rdata, method in code) are taken out of the list before the
 * call and logged with what they held. MC3_LISTENER_CHECK=0 turns it off. */
static int listener_ram(uint32_t va)
{
    return !(va & 3) && ((va >= 0x00010000u && va < 0x04000000u)
                         || (va >= 0x80000000u && va < 0x84000000u));
}

static int listener_valid(uint32_t obj, unsigned slot)
{
    uint32_t vt, fn;
    if (!listener_ram(obj))
        return 0;
    vt = MEM32(obj);
    if (vt < 0x004476E0u || vt >= 0x004A8140u || (vt & 3))   /* .rdata */
        return 0;
    fn = MEM32(vt + slot);
    return fn >= 0x00012000u && fn < 0x004472A0u;               /* code */
}

static void listener_prune(uint32_t self, unsigned slot, const char *who)
{
    static int on = -1, logged;
    uint32_t begin, end, rd, wr, idx, new_idx = 0xFFFFFFFFu, i;
    if (on < 0) {
        const char *e = getenv("MC3_LISTENER_CHECK");
        on = !(e && e[0] == '0');
    }
    if (!on || !listener_ram(self))
        return;
    begin = MEM32(self + 0x78);
    end = MEM32(self + 0x7C);
    idx = MEM32(self + 0x94);
    if (!listener_ram(begin) || end < begin || end - begin > 12u * 4096 || (end - begin) % 12) {
        if (logged++ < 20)
            fprintf(stderr, "[LISTEN] %s: list of %08X is not a list (begin %08X end %08X): left alone\n",
                    who, self, begin, end);
        return;
    }
    for (rd = wr = begin, i = 0; rd < end; rd += 12, i++) {
        uint32_t obj = MEM32(rd + 8);
        if (!listener_valid(obj, slot)) {
            if (logged++ < 20)
                fprintf(stderr, "[LISTEN] %s: entry %u of %08X dropped: id %04X key %08X object %08X "
                                "vtable %08X%s\n", who, i, self, MEM16(rd), MEM32(rd + 4), obj,
                        listener_ram(obj) ? MEM32(obj) : 0, i == idx ? " (the current one)" : "");
            continue;
        }
        if (i == idx)
            new_idx = (wr - begin) / 12;
        if (wr != rd) {
            MEM32(wr) = MEM32(rd);
            MEM32(wr + 4) = MEM32(rd + 4);
            MEM32(wr + 8) = MEM32(rd + 8);
        }
        wr += 12;
    }
    if (wr != end) {
        MEM32(self + 0x7C) = wr;
        if (idx != 0xFFFFFFFFu)
            MEM32(self + 0x94) = new_idx;
    }
}

/* The broadcast itself, done safely. The crash came back with every entry
 * valid before the call (2026-10-21, no [LISTEN] line; the loop pointer
 * esi inside the list, a garbage object read): a listener's callback
 * changes the list while the original loop walks it, so the loop reads an
 * entry that was moved or freed. On the Xbox freed memory keeps its old
 * contents and the game gets away with it. So: the current entry first
 * (this+0x94), as the original, then every other entry from a copy taken
 * before the calls, each called only if it is still in the list and valid
 * at that moment. thiscall, one argument, callee pops it (ret 4). */
static void listener_call(uint32_t obj, unsigned slot, uint32_t arg)
{
    uint32_t fn_va = MEM32(MEM32(obj) + slot);
    recomp_func_t fn = recomp_lookup(fn_va);
    if (!fn)
        fn = recomp_lookup_manual(fn_va);
    if (!fn) {
        fprintf(stderr, "[LISTEN] method %08X of %08X not in dispatch: skipped\n", fn_va, obj);
        return;
    }
    {
        static int trace = -1;
        if (trace < 0) {
            const char *e = getenv("MC3_LISTEN_TRACE");
            trace = e && e[0] == '1';
        }
        if (trace)
            fprintf(stderr, "[LISTEN]   -> object %08X vtable %08X method %08X (arg %u)\n",
                    obj, MEM32(obj), fn_va, arg);
    }
    {
        /* The callee must pop its return address and argument (ret 4) and
         * keep ebx, esi, edi, ebp, as MSVC's thiscall does. A translated
         * method that does not is a lifter bug; the original loop kept its
         * position in esi and crashed on one. Say which method, and put the
         * stack back where the call left it. */
        uint32_t esp0 = g_esp, bx = g_ebx, si = g_esi, di = g_edi, bp = g_ebp;
        static int logged;
        g_esp -= 4; MEM32(g_esp) = arg;
        g_esp -= 4; MEM32(g_esp) = 0;          /* return address */
        g_ecx = obj;
        fn();                                  /* ret 4: pops both */
        if (g_esp != esp0 || g_ebx != bx || g_esi != si || g_edi != di || g_ebp != bp) {
            if (logged++ < 20)
                fprintf(stderr, "[LISTEN] method %08X of object %08X (vtable %08X +%02X) broke the "
                                "calling convention:%s%s%s%s%s esp %+d\n", fn_va, obj, MEM32(obj), slot,
                        g_ebx != bx ? " ebx" : "", g_esi != si ? " esi" : "", g_edi != di ? " edi" : "",
                        g_ebp != bp ? " ebp" : "", "", (int)(g_esp - esp0));
            g_esp = esp0;
        }
    }
}

static int listener_live(uint32_t self, uint32_t obj, uint16_t id, uint32_t key)
{
    uint32_t e, begin = MEM32(self + 0x78), end = MEM32(self + 0x7C);
    if (!listener_ram(begin) || end < begin || end - begin > 12u * 4096)
        return 0;
    for (e = begin; e + 12 <= end; e += 12)
        if (MEM32(e + 8) == obj && MEM16(e) == id && MEM32(e + 4) == key)
            return 1;
    return 0;
}

#define LISTEN_MAX 512
static void listener_broadcast(uint32_t self, unsigned slot, const char *who)
{
    static int changed_logged;
    uint32_t begin, end, idx, n, i, arg = MEM32(g_esp + 4);
    uint32_t cur_key = 0, saved_ebx = g_ebx, saved_esi = g_esi, saved_edi = g_edi, saved_ebp = g_ebp;
    uint16_t cur_id = 0;
    struct { uint16_t id; uint32_t key, obj; } snap[LISTEN_MAX];

    listener_prune(self, slot, who);           /* drop what is invalid already */
    begin = MEM32(self + 0x78);
    end = MEM32(self + 0x7C);
    idx = MEM32(self + 0x94);
    n = (listener_ram(begin) && end >= begin) ? (end - begin) / 12 : 0;
    if (n > LISTEN_MAX)
        n = LISTEN_MAX;
    for (i = 0; i < n; i++) {
        uint32_t e = begin + 12 * i;
        snap[i].id = MEM16(e); snap[i].key = MEM32(e + 4); snap[i].obj = MEM32(e + 8);
    }
    if (idx != 0xFFFFFFFFu) {
        if (idx < n) {
            cur_id = snap[idx].id; cur_key = snap[idx].key;
            if (listener_valid(snap[idx].obj, slot))
                listener_call(snap[idx].obj, slot, arg);
        } else if (changed_logged++ < 20) {
            fprintf(stderr, "[LISTEN] %s: current entry %u of %08X is past the list (%u entries): skipped\n",
                    who, idx, self, n);
        }
    }
    for (i = 0; i < n; i++) {
        if (snap[i].id == cur_id && snap[i].key == cur_key)
            continue;                          /* the current one: done above */
        if (!listener_live(self, snap[i].obj, snap[i].id, snap[i].key)) {
            if (changed_logged++ < 20)
                fprintf(stderr, "[LISTEN] %s: entry %u of %08X (object %08X) left the list during "
                                "the broadcast: not called\n", who, i, self, snap[i].obj);
            continue;
        }
        if (listener_valid(snap[i].obj, slot))
            listener_call(snap[i].obj, slot, arg);
    }
    /* Listeners added during the broadcast hear it too, as the original
     * loop reached them (it runs to the list's live end): starting a race
     * registers the race's own listeners from a callback, and without this
     * the race stopped at "PREPARE TO RACE" (2026-10-21). Rounds until no
     * new entry appears, each called once. */
    {
        int round;
        for (round = 0; round < 8 && n < LISTEN_MAX; round++) {
            uint32_t e, b = MEM32(self + 0x78), en = MEM32(self + 0x7C), added = 0;
            if (!listener_ram(b) || en < b || en - b > 12u * 4096)
                break;
            for (e = b; e + 12 <= en && n < LISTEN_MAX; e += 12) {
                uint16_t id = MEM16(e);
                uint32_t key = MEM32(e + 4), obj = MEM32(e + 8), k;
                int seen = id == cur_id && key == cur_key;
                for (k = 0; k < n && !seen; k++)
                    seen = snap[k].obj == obj && snap[k].id == id && snap[k].key == key;
                if (seen)
                    continue;
                snap[n].id = id; snap[n].key = key; snap[n].obj = obj;
                n++;
                added++;
                if (listener_valid(obj, slot))
                    listener_call(obj, slot, arg);
            }
            if (!added)
                break;
        }
    }
    {
        static int trace = -1;
        static long count;
        if (trace < 0) {
            const char *e = getenv("MC3_LISTEN_TRACE");
            trace = e && e[0] == '1';
        }
        if (trace && count++ < 200)
            fprintf(stderr, "[LISTEN] %s %08X(arg %u): %u listener(s), current %d, list now %u\n",
                    who, self, arg, n, (int)idx,
                    (MEM32(self + 0x7C) - MEM32(self + 0x78)) / 12);
    }
    g_ebx = saved_ebx; g_esi = saved_esi; g_edi = saved_edi; g_ebp = saved_ebp;
    g_esp += 8;                                /* ret 4: return address and the argument */
}

static int listener_fix_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_LISTENER_CHECK");
        on = !(e && e[0] == '0');
    }
    return on;
}

/* Written out, not by macro: tools/recomp/manual_scan.py reads the text. */
extern void sub_002793C0_gen(void);
void sub_002793C0(void)
{
    if (!listener_fix_on()) { sub_002793C0_gen(); return; }
    listener_broadcast(g_ecx, 0x28, "race result");
}

extern void sub_00279430_gen(void);
void sub_00279430(void)
{
    if (!listener_fix_on()) { sub_00279430_gen(); return; }
    listener_broadcast(g_ecx, 0x2C, "race event");
}

/* ── Remix bring-up: the table parser that faults (MC3_TABLE_TRACE=1) ──
 *
 * First boot (2026-10-24) faulted in sub_00293B50 -- the same code as the
 * DUB Edition's sub_00269CD0 -- on a row pointer 0xFA4F0322 while the
 * game read its assets. It looks a table up (sub_00330400) and walks its
 * rows as pairs of string pointers. Logs each call's this and arguments
 * and the bytes they point to, so the input can be compared with the DUB
 * Edition's. */
static void table_dump(const char *what, uint32_t p)
{
    char hex[3 * 32 + 1], txt[33];
    int i;
    if (!((p >= 0x00010000u && p < 0x04000000u) || (p >= 0x80000000u && p < 0x84000000u))) {
        fprintf(stderr, "        %s %08X (not a pointer)\n", what, p);
        return;
    }
    for (i = 0; i < 32; i++) {
        uint8_t b = MEM8(p + (uint32_t)i);
        snprintf(hex + 3 * i, 4, "%02X ", b);
        txt[i] = (b >= 32 && b < 127) ? (char)b : '.';
    }
    txt[32] = 0;
    fprintf(stderr, "        %s %08X: %s|%s|\n", what, p, hex, txt);
}
extern void sub_00293B50_gen(void);
void sub_00293B50(void)
{
    static int on = -1, n;
    if (on < 0) {
        const char *e = getenv("MC3_TABLE_TRACE");
        on = e && e[0] == '1';
    }
    if (on && n++ < 40) {
        uint32_t i;
        fprintf(stderr, "[TABLE] #%d this %08X, args %08X %08X %08X %08X, from %08X\n", n, g_ecx,
                MEM32(g_esp + 4), MEM32(g_esp + 8), MEM32(g_esp + 12), MEM32(g_esp + 16), MEM32(g_esp));
        table_dump("this", g_ecx);
        for (i = 1; i <= 4; i++) {
            char what[8];
            snprintf(what, sizeof what, "arg%u", i);
            table_dump(what, MEM32(g_esp + 4 * i));
        }
    }
    sub_00293B50_gen();
}

/* The table loader (sub_00330400, the DUB Edition's sub_003000A0): this =
 * the table object; afterwards +0x10 holds the cell pointers (row-major),
 * +0x0C (16 bits) the columns per row and +0x18 the row count, as
 * sub_00293B50 reads them. With MC3_TABLE_TRACE=1: the shape, the first
 * rows, and the first cell that is not a pointer into memory. */
static int table_ptr_ok(uint32_t p)
{
    return (p >= 0x00010000u && p < 0x04000000u) || (p >= 0x80000000u && p < 0x84000000u);
}
static void table_str(char *out, size_t cap, uint32_t p)
{
    size_t i;
    if (!table_ptr_ok(p)) { snprintf(out, cap, "<%08X>", p); return; }
    for (i = 0; i + 1 < cap && MEM8(p + (uint32_t)i); i++) {
        uint8_t b = MEM8(p + (uint32_t)i);
        out[i] = (b >= 32 && b < 127) ? (char)b : '?';
    }
    out[i] = 0;
}
extern void sub_00330400_gen(void);
void sub_00330400(void)
{
    static int on = -1, n;
    uint32_t obj = g_ecx;
    if (on < 0) {
        const char *e = getenv("MC3_TABLE_TRACE");
        on = e && e[0] == '1';
    }
    sub_00330400_gen();
    if (on && n++ < 40) {
        uint32_t cells = MEM32(obj + 0x10), cols = MEM16(obj + 0x0C), rows = MEM32(obj + 0x18), r, c;
        char a[40], b[40];
        fprintf(stderr, "[TABLE] load #%d -> %u: obj %08X cells %08X, %u columns x %u rows\n",
                n, g_eax & 0xFF, obj, cells, cols, rows);
        if (!table_ptr_ok(cells) || cols == 0 || rows > 100000)
            return;
        for (r = 0; r < rows && r < 3; r++) {
            table_str(a, sizeof a, MEM32(cells + 4 * (r * cols)));
            table_str(b, sizeof b, MEM32(cells + 4 * (r * cols + 1)));
            fprintf(stderr, "        row %u: \"%s\" \"%s\" ...\n", r, a, b);
        }
        for (r = 0; r < rows; r++)
            for (c = 0; c < cols; c++) {
                uint32_t p = MEM32(cells + 4 * (r * cols + c));
                if (p && !table_ptr_ok(p)) {                /* 0: an empty field */
                    table_str(a, sizeof a, r ? MEM32(cells + 4 * ((r - 1) * cols)) : 0);
                    fprintf(stderr, "        first bad cell: row %u column %u = %08X (row before starts \"%s\")\n",
                            r, c, p, a);
                    return;
                }
            }
        fprintf(stderr, "        every cell a pointer\n");
    }
}

/* ── The game's state machine, traced (MC3_STATE_TRACE=1) ──
 *
 * The object behind the main loop's dispatch (DUB 0x0051886C, Remix
 * 0x00557ED0) keeps the state at +4 (1..7, the main loop's switch); its
 * vtable +4 (sub_00020E40) requests a screen, +8 (sub_00020EC0) runs a pass and
 * applies requests. Logs every request with its caller and every change of
 * state -- the DUB Edition goes to state 4 for the boot movies, the Remix
 * stayed in 1 (2026-10-24). */
static int state_trace_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_STATE_TRACE");
        on = e && e[0] == '1';
    }
    return on;
}
extern void sub_00020E40_gen(void);
void sub_00020E40(void)
{
    if (state_trace_on())
        fprintf(stderr, "[STATE] request %u (state %u) from %08X\n", MEM32(g_esp + 4),
                MEM32(g_ecx + 4), MEM32(g_esp));
    sub_00020E40_gen();
}
extern void sub_00020EC0_gen(void);
static uint32_t s_state_obj;                    /* the front end's state object, for MC3_AUTOSTART */
void sub_00020EC0(void)
{
    uint32_t obj = g_ecx, before = MEM32(obj + 4);
    uint32_t r[8];
    s_state_obj = obj;
    if (roam_running()) {                   /* a queued request may end the cruise (roam_pool.c) */
        r[0] = g_eax; r[1] = g_ecx; r[2] = g_edx; r[3] = g_ebx;
        r[4] = g_esp; r[5] = g_ebp; r[6] = g_esi; r[7] = g_edi;
        roam_pool_pass(obj, 0);
        g_eax = r[0]; g_ecx = r[1]; g_edx = r[2]; g_ebx = r[3];
        g_esp = r[4]; g_ebp = r[5]; g_esi = r[6]; g_edi = r[7];
    }
    sub_00020EC0_gen();
    if (roam_running())
        roam_pool_pass(obj, 1);
    if (state_trace_on() && MEM32(obj + 4) != before)
        fprintf(stderr, "[STATE] %u -> %u\n", before, MEM32(obj + 4));
}

/* ── Remix bring-up: callee-saved registers on the movie path (MC3_ABI_CHECK=1) ──
 *
 * The state machine's "enter state 4" (sub_00021D30) stores the state
 * through esi after sub_00021390 returns, and the Remix never left state 1
 * (2026-10-24): something on the path to the first movie returns with a
 * callee-saved register changed. Each wrapper below compares ebx, esi,
 * edi and ebp before and after and says which function broke them, once. */
static void abi_call(void (*fn)(void), const char *name)
{
    static int on = -1;
    uint32_t b = g_ebx, s = g_esi, d = g_edi, p = g_ebp;
    if (on < 0) {
        const char *e = getenv("MC3_ABI_CHECK");
        on = e && e[0] == '1';
    }
    fn();
    (void)p;                     /* ebp: frameless lifted code does not write it back */
    if (on && (g_ebx != b || g_esi != s || g_edi != d)) {
        static int said;
        if (said++ < 20)
            fprintf(stderr, "[ABI] %s changed%s%s%s (esi %08X -> %08X, edi %08X -> %08X)\n", name,
                    g_ebx != b ? " ebx" : "", g_esi != s ? " esi" : "", g_edi != d ? " edi" : "",
                    s, g_esi, d, g_edi);
    }
}
extern void sub_00021390_gen(void);
void sub_00021390(void)
{
    uint32_t obj = g_ecx;
    if (state_trace_on())
        fprintf(stderr, "[STATE] sub_00021390 enter: this %08X state %u, from %08X\n",
                obj, MEM32(obj + 4), MEM32(g_esp));
    abi_call(sub_00021390_gen, "sub_00021390");
    if (state_trace_on())
        fprintf(stderr, "[STATE] sub_00021390 leave: esi %08X edi %08X, state %u\n",
                g_esi, g_edi, MEM32(obj + 4));
}
extern void sub_00029020_gen(void);
void sub_00029020(void) { abi_call(sub_00029020_gen, "sub_00029020"); }
extern void sub_0015AA30_gen(void);
void sub_0015AA30(void) { abi_call(sub_0015AA30_gen, "sub_0015AA30"); }
extern void sub_003639C0_gen(void);
void sub_003639C0(void) { abi_call(sub_003639C0_gen, "sub_003639C0"); }
extern void sub_003638A0_gen(void);
void sub_003638A0(void) { abi_call(sub_003638A0_gen, "sub_003638A0"); }
extern void sub_002D1610_gen(void);
void sub_002D1610(void) { abi_call(sub_002D1610_gen, "sub_002D1610"); }
extern void sub_002DA8A0_gen(void);
void sub_002DA8A0(void) { abi_call(sub_002DA8A0_gen, "sub_002DA8A0"); }
extern void sub_002D5F30_gen(void);
void sub_002D5F30(void) { abi_call(sub_002D5F30_gen, "sub_002D5F30"); }
extern void sub_000CE440_gen(void);
void sub_000CE440(void) { abi_call(sub_000CE440_gen, "sub_000CE440"); }
extern void sub_00417BC0_gen(void);
void sub_00417BC0(void) { abi_call(sub_00417BC0_gen, "sub_00417BC0"); }
extern void sub_003AD0F0_gen(void);
void sub_003AD0F0(void) { abi_call(sub_003AD0F0_gen, "sub_003AD0F0"); }
extern void sub_002DBBC0_gen(void);
void sub_002DBBC0(void) { abi_call(sub_002DBBC0_gen, "sub_002DBBC0"); }

/* ── Real-time steps below 30 fps ──
 *
 * The game measures each frame with the CPU's cycle counter (sub_0020B6F0:
 * rdtsc at 733 MHz) and clamps the step to [0x4B2D60, 0x4B2D5C]. The race
 * setup (sub_00020880) sets the maximum to 1 / its rate -- 1/30 s -- so on
 * the Xbox, which held 30 fps, nothing moved slower than real time. On the
 * Switch the Remix drops below 30 in rain, with nitrous and in dense parts of
 * the city, and every such frame advanced the world by only 1/30 s: slow
 * motion, the car "slowing down" (2026-10-30; the perf report's game frame
 * time stayed 0.0333 at 19 fps). The engine's own fallback without a rate is
 * 0.1 s, so steps up to MC3_MAX_DT (default 0.0667, 15 fps) keep the race at
 * its real speed with fewer frames. MC3_MAX_DT=0: the game's own cap. */
extern void sub_00020880_gen(void);
void sub_00020880(void)
{
    static float cap = -1.0f;
    static int logged;
    sub_00020880_gen();
    if (cap < 0.0f) {
        const char *e = getenv("MC3_MAX_DT");
        cap = e ? (float)atof(e) : 0.0667f;
        if (cap > 0.1f) cap = 0.1f;            /* the engine's own limit */
    }
    if (cap > 0.0f && MEMF(0x004B2D5Cu) < cap) {
        if (logged++ < 4)
            fprintf(stderr, "[TIME] frame step capped at %.4f s by the game; %.4f s here (MC3_MAX_DT)\n",
                    MEMF(0x004B2D5Cu), cap);
        MEMF(0x004B2D5Cu) = cap;
    }
}

/* MC3_TIME_TRACE=1: the game's frame step (sub_0020B6F0 -> 0x4B2D70), once
 * a second: steps taken, their shortest / mean / longest, the clamp
 * [0x4B2D60, 0x4B2D5C] and the mode flags that pick how the step is made
 * (0x534774-76 snapping and fixed steps, 0x534AC4 / 0x534A9C replay). */
/* MC3_REALTIME=1 (off by default): in a race the game runs fixed steps --
 * 0x534770 set, the step 0x4B2D58 = 1/30 s written whatever the frame took
 * (0x0020B942) -- so below 30 fps the race is slow motion (trace above,
 * 2026-11-02: 0.0167 s a step at 4 fps in the flythrough, 0.0333 in races
 * at 18 fps). With it, a race frame that took longer advances by the real
 * time, up to MC3_MAX_DT (0.0667 s), through the same variables the fixed
 * path writes: 0x4B2D70 (step), 0x4B2D4C (scaled), 0x53475C (game time),
 * 0x4B2D48 (clamped) and 0x4B2D50 (its reciprocal). Only steps of 1/30
 * (races); the title flythrough's 1/60 half-speed showcase is left alone.
 * Replays recorded this way may play back slightly differently. */
/* Is the player in gameplay (a race) right now? The desktop builds never run
 * port_gameplay_update() -- it is compiled out with #ifndef _WIN32 -- so
 * port_gameplay_active stays 0 there and every mode-specific catch-up gate
 * below would decline for ever. The desktop answer is the media context the
 * title already reports (the same one FXAA and the music HUD use). */
static int realtime_gameplay(void)
{
#if (defined(_WIN32) || defined(__linux__)) && !defined(__ANDROID__)
    extern int windows_media_gameplay_get(void);
    return windows_media_gameplay_get();
#else
    return atomic_load(&port_gameplay_active);
#endif
}

static void realtime_step(float real)
{
    static int on = -1;
    static float cap;
    float f = MEMF(0x004B2D58u), scale = MEMF(0x004B2D6Cu), d, cl;
    if (on < 0) {
        const char *e = getenv("MC3_REALTIME"), *c = getenv("MC3_MAX_DT");
        on = e && e[0] == '1';
        cap = c && atof(c) > 0.0 ? (float)atof(c) : 0.0667f;
        if (cap > 0.1f) cap = 0.1f;
        if (on)
            fprintf(stderr, "[TIME] races advance in real time below 30 fps, steps up to %.4f s"
                    " (MC3_REALTIME)\n", cap);
    }
    if (!on || MEM32(0x00534770u) == 0 || real <= f * 1.1f)
    {
        /* MC3_REALTIME_TRACE=1: why the catch-up declined (a rare print, so a
         * session can be read without a profiler). */
        static int dbg = -1;
        static unsigned dbg_n;
        if (dbg < 0) {
            const char *e = getenv("MC3_REALTIME_TRACE");
            dbg = e && e[0] == '1';
        }
        if (dbg && (dbg_n++ % 90u) == 0)
            fprintf(stderr, "[RT] skip f=%.4f real=%.4f mode=%d gameplay=%d flag=%u on=%d\n",
                    f, real, mc3_frame_rate_mode(),
                    atomic_load(&port_gameplay_active), MEM32(0x00534770u), on);
        return;
    }
    /* The 1/30 race step, as before, and the 1/60 step of the 60 fps mode:
     * without the second range a race that could not hold 60 advanced by
     * one 1/60 step per drawn frame and ran at 2/3 speed at 40 fps, which
     * read as the whole game slowing down (2026-11-18). The 120 fps mode
     * needs the same compensation for its 1/120 step: without it a build
     * that presents ~80 frames/s advanced 80/120 of real time, measured as
     * a race clock running at 72.6% (2026-11-18). */
    if (!(f >= 0.030f && f <= 0.037f)
        && !(mc3_frame_rate_mode() >= 60 && realtime_gameplay()
             && f > 0.010f && f < 0.023f)
        && !(mc3_frame_rate_mode() >= 120 && realtime_gameplay()
             && f > 0.0070f && f < 0.0112f))
        return;
    d = real < cap ? real : cap;
    MEMF(0x004B2D70u) = d;
    MEMF(0x0053475Cu) += (d - f) * scale;           /* game time: the real step, not f */
    MEMF(0x004B2D4Cu) = d * scale;
    cl = d * scale;
    if (cl < MEMF(0x004B2D60u)) cl = MEMF(0x004B2D60u);
    if (cl > MEMF(0x004B2D5Cu)) cl = MEMF(0x004B2D5Cu);
    MEMF(0x004B2D48u) = cl;
    MEMF(0x004B2D50u) = 1.0f / cl;
}

/* Local 60 Hz investigation: the pedestrian/creature manager has two
 * frame-driven entry points.  Count them before changing their cadence so a
 * rendering call is never mistaken for simulation.  This is opt-in and is
 * intentionally absent from release launchers. */
static int pedestrian_call_trace_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_PEDESTRIAN_CALL_TRACE");
        on = e && e[0] == '1';
    }
    return on;
}

static void pedestrian_call_note(unsigned slot, uint32_t object)
{
    static FILE *f;
    static uint64_t began;
    static unsigned calls[4];
    static uint32_t objects[4];
    static float update_arg;
    static float creature_arg;
    struct timespec ts;
    uint64_t now;
    if (!pedestrian_call_trace_on()) return;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    if (!f) {
        f = fopen("pedestrian-call-trace.log", "w");
        began = now;
    }
    if (slot < 4) {
        calls[slot]++;
        objects[slot] = object;
        if (slot == 2) update_arg = MEMF(g_esp + 4);
        if (slot == 3) creature_arg = MEMF(g_esp + 4);
    }
    if (f && now - began >= 1000000000ull) {
        fprintf(f, "real=%.3f creature_update=%u object=%08X arg=%.6f sub_00222900=%u object=%08X arg=%.6f sub_00222920=%u object=%08X sub_00222F00=%u object=%08X gameplay=%d\n",
                (double)(now - began) / 1e9, calls[3], objects[3], creature_arg,
                calls[2], objects[2],
                update_arg,
                calls[0], objects[0], calls[1], objects[1],
                atomic_load(&port_gameplay_active));
        fflush(f);
        calls[0] = calls[1] = calls[2] = calls[3] = 0;
        began = now;
    }
}

/* Creature simulation, separate from the visibility/pose virtual above. */
extern void sub_002257B0_gen(void);
void sub_002257B0(void)
{
    /* 30 FPS follows the original simulation body without diagnostics,
     * animation-field reads, cadence state, or timestep adjustment. */
    if (mc3_frame_rate_mode() < 60) {
        sub_002257B0_gen();
        return;
    }
    /* Optional A/B: retain the original creature cadence instead of the
     * corrected 60 Hz one. Off unless asked for. */
    static int cadence_test = -1;
    if (cadence_test < 0) {
        const char *e = getenv("MC3_PEDESTRIAN_30HZ_TEST");
        cadence_test = mc3_frame_rate_mode() == 60 && e && e[0] == '1';
    }
    if (cadence_test) {
        static struct { uint32_t object; double pending; } clocks[256];
        unsigned slot = (g_ecx >> 2) & 255u, probe;
        float dt = MEMF(g_esp + 4);
        for (probe = 0; probe < 256; probe++, slot = (slot + 1) & 255u)
            if (!clocks[slot].object || clocks[slot].object == g_ecx) break;
        if (probe < 256 && dt > 0 && dt < 0.1f) {
            clocks[slot].object = g_ecx;
            clocks[slot].pending += dt;
            if (clocks[slot].pending + 1e-7 < 1.0 / 30.0) {
                g_esp += 24; /* original callee returns with ret 20 */
                return;
            }
            MEMF(g_esp + 4) = (float)clocks[slot].pending;
            clocks[slot].pending = 0;
        }
    }
    /* Compare measured travel per simulated second at 30/60/120 Hz.
     * Preserve guest registers: the generated callee changes ECX/ESP. */
    const uint32_t trace_object = g_ecx;
    const float trace_dt = MEMF(g_esp + 4);
    float trace_x = 0, trace_y = 0, trace_z = 0;
    const int trace_motion = pedestrian_call_trace_on();
    if (trace_motion) {
        trace_x = MEMF(trace_object + 0x108);
        trace_y = MEMF(trace_object + 0x10C);
        trace_z = MEMF(trace_object + 0x110);
    }
    extern int windows_media_gameplay_get(void);
    if (pedestrian_call_trace_on()) {
        static FILE *f;
        static uint64_t began;
        static uint32_t sample;
        static unsigned total, sample_calls;
        static float sum_dt;
        struct timespec ts;
        uint64_t now;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
        if (!f) { f = fopen("pedestrian-simulation-trace.log", "w"); began = now; }
        if (!sample) sample = g_ecx;
        total++;
        if (sample == g_ecx) { sample_calls++; sum_dt += MEMF(g_esp + 4); }
        if (f && now - began >= 1000000000ull) {
            fprintf(f, "real=%.3f total=%u sample=%08X calls=%u sum_dt=%.6f args=%.6f,%.6f,%.6f,%08X gameplay=%d\n",
                    (double)(now - began) / 1e9, total, sample, sample_calls, sum_dt,
                    MEMF(g_esp + 4), MEMF(g_esp + 8), MEMF(g_esp + 12), MEM32(g_esp + 16),
                    windows_media_gameplay_get());
            fflush(f);
            total = sample_calls = 0; sum_dt = 0; sample = g_ecx; began = now;
        }
    }
    /* Narrow alternative to cadence gating: the clip sampler at 22B150
     * consumes a per-update increment at animation +0x10. */
    uint32_t trace_anim = 0;
    float saved_increment = 0, scaled_increment = 0;
    float clip_cursor_before = 0;
    {
        static int clip_step_test = -1;   /* runs per creature: resolve once */
        if (clip_step_test < 0)
            clip_step_test = mc3_pedestrian_switch("MC3_PEDESTRIAN_CLIP_STEP_TEST");
        if (clip_step_test &&
            trace_dt > 0 && trace_dt < 0.033f) {
            trace_anim = MEM32(trace_object + 0xD8);
            if (trace_anim >= 0x80000000u && trace_anim < 0x84000000u) {
                saved_increment = MEMF(trace_anim + 0x10);
                clip_cursor_before = MEMF(trace_anim + 0xC);
                scaled_increment = saved_increment * (trace_dt * 30.0f);
                MEMF(trace_anim + 0x10) = scaled_increment;
            } else trace_anim = 0;
        }
    }
    sub_002257B0_gen();
    if (trace_anim && trace_motion) {
        static FILE *clip_log;
        static uint32_t clip_sample;
        static unsigned clip_frames;
        if (!clip_log) clip_log = fopen("pedestrian-clip-trace.log", "w");
        if (!clip_sample) clip_sample = trace_object;
        if (clip_log && trace_object == clip_sample && clip_frames < 600) {
            fprintf(clip_log, "frame=%u object=%08X anim=%08X next=%08X dt=%.8f cursor=%.8f->%.8f step=%.8f scaled=%.8f after=%.8f\n",
                    clip_frames++, trace_object, trace_anim,
                    MEM32(trace_object + 0xD8), trace_dt, clip_cursor_before,
                    MEMF(trace_anim + 0xC), saved_increment, scaled_increment,
                    MEMF(trace_anim + 0x10));
            fflush(clip_log);
        }
    }
    if (trace_anim && MEM32(trace_object + 0xD8) == trace_anim &&
        MEMF(trace_anim + 0x10) == scaled_increment)
        MEMF(trace_anim + 0x10) = saved_increment;
    if (trace_motion) {
        static FILE *motion;
        static uint32_t object;
        static unsigned calls;
        static double elapsed, distance;
        if (!motion) motion = fopen("pedestrian-motion-trace.log", "w");
        if (!object) object = trace_object;
        if (object == trace_object && trace_dt > 0 && trace_dt < 0.2f) {
            double dx = MEMF(trace_object + 0x108) - trace_x;
            double dy = MEMF(trace_object + 0x10C) - trace_y;
            double dz = MEMF(trace_object + 0x110) - trace_z;
            distance += sqrt(dx*dx + dy*dy + dz*dz);
            elapsed += trace_dt;
            calls++;
            if (motion && elapsed >= 1.0) {
                fprintf(motion, "object=%08X calls=%u dt_sum=%.6f travel=%.6f speed=%.6f state=%u motion=%.6f,%.6f,%.6f\n",
                        object, calls, elapsed, distance, distance/elapsed,
                        MEM32(object + 0x168), MEMF(object + 0x114),
                        MEMF(object + 0x118), MEMF(object + 0x11C));
                fflush(motion);
                elapsed = distance = 0; calls = 0; object = 0;
            }
        }
    }
}

/* Every guest pointer this file dereferences on behalf of the title has to be
 * inside the 64 MB guest window before it is read, otherwise a bad object
 * field becomes a host crash instead of the title's own fault. */
static int mc3_guest_ptr_ok(uint32_t p, uint32_t bytes)
{
    return p >= 0x80000000u && p <= 0x84000000u && bytes <= 0x04000000u &&
           p <= 0x84000000u - bytes;
}

/* Local render-only pose experiment. Decode adjacent integer poses, blend
 * their local transforms, then orthonormalize rotation rows to avoid scale
 * collapse from linear matrix blending. The legacy mode uses one decode.
 *
 * Loop boundary: the sampler (sub_0022B150) clamps its cursor to frames-1
 * when the clip loops (byte +0x0D of the sampler's clip record) and to a
 * stop value when it does not, so this decoder never sees a fractional
 * cursor past the last frame. The frame+1 >= frames fallback below therefore
 * only triggers when the cursor sits exactly on the final frame, where there
 * is no next pose to blend. */
extern void sub_00373150_gen(void);
static int pose_blend_enabled(void)
{
    static int on = -1;
    if (on < 0)
        on = mc3_pedestrian_switch("MC3_PEDESTRIAN_POSE_TEST");
    return on;
}
void sub_00373150(void)
{
    /* Preserve the original decoder for 30 Hz and every other caller.
     * Test the call site before reading pedestrian-specific object fields. */
    if (!pose_blend_enabled() || MEM32(g_esp) != 0x0022419Cu) {
        sub_00373150_gen(); return;
    }
    uint32_t entry_sp = g_esp, clip = g_ecx;
    uint32_t dest = MEM32(entry_sp + 4), frame = MEM32(entry_sp + 8);
    uint32_t anim = MEM32(g_esi + 0xC4);
    if (!mc3_guest_ptr_ok(anim, 0x10) || !mc3_guest_ptr_ok(dest, 12) ||
        !mc3_guest_ptr_ok(clip, 0x24)) {
        sub_00373150_gen(); return;
    }
    uint32_t bone_ptr = MEM32(dest + 4);         /* -> uint16 bone count */
    uint32_t output = MEM32(dest + 8);
    unsigned bones, frames = MEM16(clip);
    float fraction;
    float first[128][12];
    unsigned i, j;
    uint32_t saved_arg_frame, r_eax, r_ebx, r_ecx, r_edx, r_esi, r_edi, r_ebp;

    if (!mc3_guest_ptr_ok(bone_ptr, 2) || !mc3_guest_ptr_ok(output, 60))
        { sub_00373150_gen(); return; }
    bones = MEM16(bone_ptr);
    fraction = MEMF(anim + 0xC) - (float)frame;
    /* Bound the frame index before it is used in pointer arithmetic: a
     * non-finite or out-of-range cursor must fall back, not wrap. */
    if (bones == 0 || bones > 128 || frames < 2 || frame > frames - 2 ||
        !(fraction > 0.001f && fraction < 1.0f) ||
        !mc3_guest_ptr_ok(output, bones * 60u)) {
        sub_00373150_gen(); return;
    }
    /* sub_00373150_gen indexes frame i as base + i*8, base taken from
     * clip + 8; validate the second pose's entry before the extra decode. */
    if (!mc3_guest_ptr_ok(MEM32(clip + 8), (frame + 2) * 8u)) {
        sub_00373150_gen(); return;
    }
    sub_00373150_gen();
    /* Everything the first decode left behind is the state the guest would
     * have seen; keep it and give it back after the extra decode. */
    saved_arg_frame = MEM32(entry_sp + 8);
    r_eax = g_eax; r_ebx = g_ebx; r_ecx = g_ecx; r_edx = g_edx;
    r_esi = g_esi; r_edi = g_edi; r_ebp = g_ebp;
    for (i = 0; i < bones; i++) for (j = 0; j < 12; j++)
        first[i][j] = MEMF(output + i*60 + j*4);
    g_esp = entry_sp; g_ecx = clip;
    MEM32(entry_sp + 4) = dest; MEM32(entry_sp + 8) = frame + 1;
    sub_00373150_gen();
    /* Undo the argument change and the second decode's volatile state.  The
     * decode itself ends with "ret 8", so g_esp is already where the guest
     * expects it; only the argument slot and the registers are restored. */
    MEM32(entry_sp + 8) = saved_arg_frame;
    g_eax = r_eax; g_ebx = r_ebx; g_ecx = r_ecx; g_edx = r_edx;
    g_esi = r_esi; g_edi = r_edi; g_ebp = r_ebp;
    for (i = 0; i < bones; i++) {
        float m[12], second[12]; double n, dot;
        int finite = 1;
        for (j = 0; j < 12; j++) {
            second[j] = MEMF(output + i*60 + j*4);
            m[j] = first[i][j] + fraction * (second[j] - first[i][j]);
            if (!(m[j] > -1e30f && m[j] < 1e30f)) finite = 0;
        }
        /* A non-finite or degenerate pair leaves the second pose in the
         * output (a real decode) instead of writing a collapsed matrix. */
        if (!finite) continue;
        n = sqrt(m[0]*m[0]+m[1]*m[1]+m[2]*m[2]);
        if (n < 1e-6) continue;
        for (j=0;j<3;j++) m[j] /= (float)n;
        dot = m[0]*m[3]+m[1]*m[4]+m[2]*m[5];
        for (j=0;j<3;j++) m[j+3] -= (float)dot*m[j];
        n = sqrt(m[3]*m[3]+m[4]*m[4]+m[5]*m[5]);
        if (n < 1e-6) continue;
        for (j=3;j<6;j++) m[j] /= (float)n;
        m[6]=m[1]*m[5]-m[2]*m[4];
        m[7]=m[2]*m[3]-m[0]*m[5];
        m[8]=m[0]*m[4]-m[1]*m[3];
        for(j=0;j<12;j++) MEMF(output+i*60+j*4)=m[j];
    }
}

extern void sub_00223ED0_gen(void);
void sub_00223ED0(void)
{
    pedestrian_call_note(3, g_ecx);
    sub_00223ED0_gen();
}

extern void sub_00222900_gen(void);
void sub_00222900(void)
{
    pedestrian_call_note(2, g_ecx);
    sub_00222900_gen();
}

extern void sub_00222920_gen(void);
void sub_00222920(void)
{
    pedestrian_call_note(0, g_ecx);
    sub_00222920_gen();
}

extern void sub_00222F00_gen(void);
void sub_00222F00(void)
{
    pedestrian_call_note(1, g_ecx);
    sub_00222F00_gen();
}

extern void sub_0020B6F0_gen(void);
/* How many Downs from the main menu's first slot the autostart should make.
 * MC3_MAIN_DOWNS overrides it: the menu order can move (Online is reordered to
 * slot 0 when an online session is wanted), and a wrong count silently opens
 * the item the cursor happens to be on. */
static int autostart_main_downs(void)
{
    static int n = -1;
    if (n < 0) {
        const char *e = getenv("MC3_MAIN_DOWNS");
        /* Arcade is the third item; Networking (now Online) is the fourth, and
         * that is still the one this walker skips to. */
        if (e && *e)
            n = atoi(e);
        else
            n = 2;
    }
    return n;
}
/* The Online session (online_session.c is included near the end of this file,
 * with the other free-roam sources): MC3_ONLINE_SESSION=1. */
static void online_session_pass(void);
static void online_leave(const char *why);
static void unlock_all_pass(void);      /* unlock_all.c, included at the end */

/* Leaving the session. Arcade, Career and Tokyo all end it: each of the three
 * main-menu callbacks restores the career's state before the game changes mode,
 * so a session can never leak its money or unlocks into a career save.
 *
 * These are added only when the override exists in the lift; LIFT_ONLY=1
 * tools/regen.sh keeps them. */
extern void sub_0018C530_gen(void);
void sub_0018C530(void) { online_leave("Arcade"); sub_0018C530_gen(); }
extern void sub_0018C560_gen(void);
void sub_0018C560(void) { online_leave("Career"); sub_0018C560_gen(); }
extern void sub_0018C580_gen(void);
void sub_0018C580(void) { online_leave("Tokyo Challenge"); sub_0018C580_gen(); }

/* ── Entering: the main menu's "Networking" item ──
 *
 * sub_0018C5A0 is CM_Networking: it opens the Xbox Live / System Link screen
 * (FE -> sub_00154B30(0x1E)). In a session we first take the career's money,
 * garage and unlock flags aside, then let the original screen open.
 *
 * Native builds deliberately keep the original screen: Free Roam is then picked
 * there, and the cruise that loads runs inside the session. The upstream's
 * "skip straight to the cruise" shortcut was browser-only because the browser
 * had no way through that screen; our autostart already walks menus, so we do
 * not need a second path that fakes the front end's state.
 *
 * With MC3_ONLINE_SESSION unset this is the untouched menu item. */
extern void sub_0018C5A0_gen(void);
void sub_0018C5A0(void)
{
    extern int online_session_wanted(void);
    extern void online_enter(const char *why);
    if (online_session_wanted())
        online_enter("main menu Networking");
    sub_0018C5A0_gen();
}

/* ── Free Roam: the networking screen's first item ──
 *
 * sub_00180160 is the callback behind "Xbox Live". On a console that is a live
 * service; here it is a dead end - pressing it opens "New Account", observed on
 * screen. Free Roam therefore cannot be reached through the item as shipped.
 *
 * In a session this makes that item mean Free Roam instead, by delegating to
 * Arcade's own menu callback (sub_0018C530_gen). That function already does the
 * whole sequence correctly - [0x50F14C] = 0, copy the picked car, sub_0013A590,
 * Go(0x19), the Arcade setup screen - so nothing here is rebuilt by hand.
 *
 * The earlier attempt hand-rolled that sequence with its own stack pushes and
 * killed the game: "[PB] NOP(...) trap still pending after 2 s", i.e. the
 * pushbuffer executor stalled, because calling a lifted function with a hand
 * built frame is not the same as RECOMP_ABI_CALL doing it. Calling the lifted
 * callback instead is both shorter and correct.
 *
 * Note it calls the _gen: sub_0018C530 (ours) ends the session, which is right
 * when the player picks Arcade from the menu but wrong here.
 *
 * Outside a session this is the untouched callback. */
extern void sub_00180160_gen(void);
extern void sub_0018C530_gen(void);
/* Online play is on unless MC3_ONLINE=0 says otherwise (ios_host.c applies
 * that default at boot; the file can carry the address on any platform). */
static int online_play_enabled(void)
{
    const char *off = getenv("MC3_ONLINE");
    const char *join, *roam;
    if (off && off[0] == '0')
        return 0;
    join = getenv("MC3_ROAM_JOIN");
    roam = getenv("MC3_ROAM");
    return (join && *join) || (roam && *roam && strcmp(roam, "0") != 0);
}
void sub_00180160(void)
{
    extern int online_session_wanted(void);
    extern void online_enter(const char *why);
    /* Without online play this stays the game's own callback (the live
     * service screen, dead here). With it, the item means Free Roam whenever
     * the player can actually play online -- a session is not required, the
     * session only adds the money/unlocks sandbox on top. */
    if (!online_session_wanted() && !online_play_enabled()) {
        sub_00180160_gen();
        return;
    }
    if (online_session_wanted())
        online_enter("networking screen, Free Roam");
    sub_0018C530_gen();
    fprintf(stderr, "[ONLINE] Free Roam: networking item -> Arcade's own cruise path"
            " (sub_0018C530)\n");
}
/** "Online" keeps the Networking item's place
 *
 * The main menu builds its own order and Networking is the fourth item; that is
 * where players already look for it, so the item stays there and only its name
 * changes. online_menu_patch() below rewrites the visible label to "ONLINE".
 *
 * The wrapper remains because the dispatch table calls sub_0018C770 for the
 * builder; it is a plain pass-through and touches nothing. */
extern void sub_0018C770_gen(void);
void sub_0018C770(void)
{
    sub_0018C770_gen();
}

static void roam_net_boot(void);        /* roam_net.c, included at the end */
static void roam_caps_pass(void);       /* roam_caps.c, included at the end */

/** The online menu's names
 *
 * The main menu's fourth item ships as "Networking" and the screen behind it
 * lists "Xbox Live" (free roam, which sub_00180160 turns into our cruise) and
 * "System Link" (the race lobby). None of those names belong in ReDUB, and the
 * strings are not in the item list: the records only hold display objects, and
 * the visible text is interned by the front end as UTF-16 buffers in the guest
 * heap.
 *
 * So the buffers themselves are the patch point. They are found once the menu
 * is built (the heap window is 0x80000000-0x84000000; Windows asks the OS
 * which pages are readable, the other platforms ask the view registry), and
 * the text is overwritten in place. A shorter text is space-padded because
 * the buffer carries no terminator -- it is filled with 0xCDCD -- and a longer
 * one is only used when the filler after the string proves there is room.
 *
 * The swaps run every frame: they are no-ops once applied, and they put the
 * names back if the front end re-interns a string. */
static const char *const online_label_from[3] = { "Networking",  "Xbox Live", "System Link" };
static const char *const online_label_to[3]   = { "Online",      "Online Cruise Mode", "Online Race Mode" };
static const char *const online_label_short[3] = { "Online",     "Online Cruise", "Online Races" };
static struct { uint32_t va; } online_label[3][8];
static int online_label_n[3];

static int online_guest_ok(uint32_t va, uint32_t n)
{
    return n <= 0x04000000u &&
           ((va >= 0x10000u && va <= 0x04000000u - n) ||
            (va >= 0x80000000u && va <= 0x84000000u - n));
}

#if defined(_WIN32)
static int online_scan_readable(uint32_t va, uint32_t *size);  /* after <windows.h> */
#else
/* The guest arena is fully reserved but handed out piecemeal, so a read has to
 * stay inside a live view. The platform keeps a registry of every view it
 * mapped (win32_compat.c: view_lookup), which is what VirtualQuery reports on
 * Windows. */
extern int view_lookup(const void *addr, void **base_out, size_t *len_out);
static int online_scan_readable(uint32_t va, uint32_t *size)
{
    void *base = NULL;
    size_t len = 0, left;
    uintptr_t host = (uintptr_t)XBOX_PTR(va);
    if (!view_lookup((const void *)host, &base, &len))
        return 0;
    left = (size_t)((uintptr_t)base + len - host);
    if (left < 2u)
        return 0;
    *size = left > 0x04000000u ? 0x04000000u : (uint32_t)left;
    return 1;
}
#endif

static void online_labels_discover(void)
{
    const uint32_t hi = 0x84000000u;
    unsigned k, n;
    for (k = 0; k < 3; k++) {
        const char *s = online_label_from[k];
        uint32_t len = (uint32_t)strlen(s), va = 0x80000000u;
        online_label_n[k] = 0;
        while (va < hi && online_label_n[k] < 8) {
            uint32_t size;
            if (!online_scan_readable(va, &size)) {
                va += 0x1000u;
                continue;
            }
            if (va + size > hi)
                size = hi - va;
            {
                const uint16_t *w = (const uint16_t *)(uintptr_t)XBOX_PTR(va);
                uint32_t words = size / 2u, i;
                for (i = 0; i + len <= words && online_label_n[k] < 8; i++) {
                    uint32_t j;
                    if (w[i] != (uint16_t)(uint8_t)s[0])
                        continue;
                    for (j = 1; j < len; j++)
                        if (w[i + j] != (uint16_t)(uint8_t)s[j])
                            break;
                    if (j == len)
                        online_label[k][online_label_n[k]++].va = va + 2u * i;
                }
            }
            va += size;
        }
    }
}

static void online_labels_apply(void)
{
    unsigned k, n;
    for (k = 0; k < 3; k++) {
        uint32_t len = (uint32_t)strlen(online_label_from[k]);
        for (n = 0; n < (unsigned)online_label_n[k]; n++) {
            uint32_t va = online_label[k][n].va, i, slack = 0, to_len;
            const char *to;
            uint16_t next;
            if (!online_guest_ok(va, 2u * len + 2u))
                continue;
            for (i = 0; i < len; i++) {
                uint16_t w = MEM16(va + 2u * i);
                uint16_t c = (uint16_t)(uint8_t)online_label_from[k][i];
                if (w >= 'a' && w <= 'z')
                    w = (uint16_t)(w - 32);
                if (c >= 'a' && c <= 'z')
                    c = (uint16_t)(c - 32);
                if (w != c)
                    break;
            }
            if (i != len)
                continue;              /* already renamed, or the space was reused */
            next = MEM16(va + 2u * len);
            if (next != 0 && next < 0xCD00u)
                continue;              /* part of a longer string: not ours */
            while (slack < 64u && MEM16(va + 2u * (len + slack)) == 0xCDCDu)
                slack++;
            to = online_label_to[k];
            if (strlen(to) > len + slack)
                to = online_label_short[k];
            to_len = (uint32_t)strlen(to);
            if (to_len > len + slack)
                continue;
            for (i = 0; i < len; i++)
                MEM16(va + 2u * i) = (uint16_t)(uint8_t)(i < to_len ? to[i] : ' ');
            fprintf(stderr, "[ONLINE] label %08X \"%s\" -> \"%s\"\n",
                    va, online_label_from[k], to);
        }
    }
}

void online_menu_patch(void)
{
    uint32_t fe, lst, col, rows, count;

    if (!online_guest_ok(MEM32(0x0050F148u), 16))
        return;
    fe = MEM32(MEM32(0x0050F148u) + 0xCu);
    if (!online_guest_ok(fe, 0x100))
        return;
    lst = MEM32(fe + 0xFCu);
    if (!online_guest_ok(lst, 0x300))
        return;
    col = MEM32(lst + 0x258u);
    rows = MEM32(lst + 0x230u);
    if (col > 15u || rows < 1u || rows > 16u)
        return;
    count = MEM32(lst + 0x238u + 4u * col);
    if (count < 1u || count > 16u)
        return;
    {   /* Search once the list is built; retry while it comes up empty. */
        static int discovered;
        if (!discovered) {
            online_labels_discover();
            discovered = online_label_n[0] > 0;
        }
        online_labels_apply();
    }
}

void sub_0020B6F0(void)
{
    extern void online_menu_patch(void);
    static int on = -1;
    static unsigned n;
    static float lo = 1e9f, hi = 0.0f, sum = 0.0f;
    static uint32_t last_ms;
    static uint64_t last_ns;
    static FILE *ped_trace;
    static uint64_t ped_trace_ns;
    static float ped_game_start;
    static unsigned ped_steps;
    float dt, real;
    int frame_rate_mode = mc3_frame_rate_mode();
    online_menu_patch();
    if (on < 0) {
        const char *e = getenv("MC3_TIME_TRACE");
        on = e && e[0] == '1';
    }
    {
        struct timespec ts;
        uint64_t now;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
        real = last_ns ? (float)((now - last_ns) / 1e9) : 0.0f;
        last_ns = now;
    }
    /* Races normally force a fixed 1/30 s simulation step. When the isolated
     * 60 Hz test lets the main loop advance every vblank, halve only that
     * known race step. Flythroughs already use 1/60 and remain unchanged.
     * The gameplay query is only needed in the 60 Hz test, so 30 Hz mode does
     * not call into the port layer at all. */
    int gameplay = 0;
    if (frame_rate_mode >= 60) {
#if (defined(_WIN32) || defined(__linux__)) && !defined(__ANDROID__)
        extern int windows_media_gameplay_get(void);
        gameplay = windows_media_gameplay_get();
#else
        gameplay = atomic_load(&port_gameplay_active);
#endif
    }
    if (frame_rate_mode >= 60 && gameplay && MEMF(0x004B2D58u) >= 0.030f &&
        MEMF(0x004B2D58u) <= 0.037f)
#ifdef MC3_ALLOW_120
        MEMF(0x004B2D58u) = 1.0f / (float)frame_rate_mode;
#else
        MEMF(0x004B2D58u) = 1.0f / 60.0f;
#endif
    unlock_all_pass();      /* MC3_UNLOCK_ALL: before the game reads its flags */
    online_session_pass();  /* MC3_ONLINE_SESSION: keep the session's money and unlocks */
    roam_net_boot();        /* MC3_ROAM=1: free-roam transport, once */
    roam_caps_pass();       /* MC3_ROAM=1: vehicle registry, once */
    sub_0020B6F0_gen();
    if (s_state_obj) {      /* states 2 and 3: a race or cruise (sub_0001EDB0) */
        extern void mc3_autostart_frame(int in_game, int downs_to_arcade);
        extern int online_session_wanted(void);
        /* MC3_ONLINE_SESSION: with the menu reorder active, Networking is slot
         * 0, so the autostart must not press Down at all. Without the reorder
         * it is the fourth item and needs three. Asking for the wrong count is
         * silent - the cursor lands on a different item and the press opens
         * that instead - so the two must agree, and they do because both read
         * online_session_wanted(). */
        mc3_autostart_frame(MEM32(s_state_obj + 4) == 2 || MEM32(s_state_obj + 4) == 3,
                            autostart_main_downs());
    }
    {
        static int time_trace = -1;
        if (time_trace < 0) {
            const char *trace = getenv("MC3_PEDESTRIAN_TIME_TRACE");
            time_trace = trace && trace[0] == '1';
        }
        if (time_trace) {
            if (!ped_trace) {
                ped_trace = fopen("pedestrian-time-trace.log", "w");
                ped_trace_ns = last_ns;
                ped_game_start = MEMF(0x0053475Cu);
            }
            ped_steps++;
            if (ped_trace && last_ns - ped_trace_ns >= 1000000000ull) {
                float game_now = MEMF(0x0053475Cu);
                fprintf(ped_trace,
                        "real=%.3f game=%.6f steps=%u dt=%.6f fixed=%.6f scale=%.6f gameplay=%d\n",
                        (double)(last_ns - ped_trace_ns) / 1e9,
                        game_now - ped_game_start, ped_steps,
                        MEMF(0x004B2D70u), MEMF(0x004B2D58u),
                        MEMF(0x004B2D6Cu), gameplay);
                fflush(ped_trace);
                ped_trace_ns = last_ns;
                ped_game_start = game_now;
                ped_steps = 0;
            }
        }
    }
    realtime_step(real);
    if (!on)
        return;
    dt = MEMF(0x004B2D70u);
    n++; sum += dt;
    if (dt < lo) lo = dt;
    if (dt > hi) hi = dt;
    {
        struct timespec ts;
        uint32_t ms;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ms = (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
        if (ms - last_ms >= 1000u) {
            fprintf(stderr, "[TIME] %u steps/s, step %.4f .. %.4f (mean %.4f) s; fixed step %.4f;"
                    " clamp %.4f .. %.4f; flags snap %u fixed %u/%u/%08X replay %08X/%u\n",
                    n, lo, hi, n ? sum / n : 0.0f, MEMF(0x004B2D58u), MEMF(0x004B2D60u), MEMF(0x004B2D5Cu),
                    MEM8(0x00534776u), MEM8(0x00534774u), MEM8(0x00534775u), MEM32(0x00534770u),
                    MEM32(0x00534AC4u), MEM8(0x00534A9Cu));
            n = 0; sum = 0.0f; lo = 1e9f; hi = 0.0f;
            last_ms = ms;
        }
    }
}

/* ── DirectSound: stopping a voice while the APU waits for us ──
 *
 * sub_003C77C0 waits for a voice to stop: while the voice's flags (+0x12)
 * have 0x8000, sub_003C77C0 calls sub_003C6B43, which reads the system time
 * and, 500 ms after the stop was asked for (+0x78), stops the voice itself
 * (sub_003C6A5A). The APU says "stopped" through its front end; when the
 * front end is trapped, it waits for DirectSound's interrupt handler, which
 * runs on the same emulated interrupt thread that is in this loop -- on the
 * Xbox the interrupt would pre-empt it. So the loop ran out its 500 ms
 * holding the guest lock: the main thread stood still for 499 ms
 * ([gil] ... held by thread 1007 (timer)), the hangs in the DUB Edition's
 * races (Switch, 2026-10-27); the Remix's DirectSound is the same code. Now the loop is the
 * same, except that once the front end has been trapped for
 * MC3_VOICE_STOP_MS (1) of it, the voice is stopped as DirectSound does
 * at its timeout. MC3_VOICE_STOP_MS=0: the original loop. */
extern int mcpx_apu_fe_trapped(void);
extern void sub_003C77C0_gen(void);
extern void sub_003C6B43(void);
extern void sub_003C6A5A(void);
static uint64_t voice_ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
static void voice_call(void (*fn)(void), uint32_t voice)
{
    g_esp -= 4; MEM32(g_esp) = 0;          /* return address; thiscall, no arguments */
    g_ecx = voice;
    fn();                                  /* ret: pops it */
}
void sub_003C77C0(void)
{
    static int ms = -1, logged;
    uint32_t v = g_ecx;
    uint64_t trapped_since = 0;
    int forced = 0;
    if (ms < 0) {
        const char *e = getenv("MC3_VOICE_STOP_MS");
        ms = e ? atoi(e) : 1;
    }
    if (ms <= 0) {
        sub_003C77C0_gen();
        return;
    }
    if (MEM8(v + 0x12) & 1) {
        while (MEM16(v + 0x12) & 0x8000) {
            if (!forced && (MEM16(v + 0x12) & 0x8001) == 0x8001 && mcpx_apu_fe_trapped()) {
                uint64_t now = voice_ms_now();
                if (!trapped_since)
                    trapped_since = now;
                if (now - trapped_since >= (uint64_t)ms) {
                    if (logged++ < 10)
                        fprintf(stderr, "[DSOUND] voice %08X: stop waited %u ms on a trapped APU"
                                " front end -- stopped as at DirectSound's timeout\n", v,
                                (unsigned)(now - trapped_since));
                    voice_call(sub_003C6A5A, v);
                    forced = 1;
                    continue;
                }
            } else {
                trapped_since = 0;
            }
            voice_call(sub_003C6B43, v);
        }
    }
    g_ecx = v;
    g_esp += 4;                            /* ret */
}

/* ── DirectSound's DSP command wait: its doorbell found at run time ──
 *
 * sub_003C3D23 (DSOUND) hands the audio DSP a command block and waits for the
 * DSP to clear the command word at block + 0x810. No DSP is emulated: the
 * runtime answers words registered with it (apu_dsp_ack_add). The block is
 * [[[this+8]+0x10]], the object's command buffer, and moves with the
 * title's allocations -- the Remix's sat at 0x83B10000 when told the TV
 * is widescreen and at 0x83A5C000 when not (2026-10-25), so a fixed
 * address hung one of the two and zeroed a stray word in the other. */
extern void apu_dsp_ack_add(uint32_t va);
extern void sub_003C3D23_gen(void);
void sub_003C3D23(void)
{
    uint32_t dev = MEM32(g_ecx + 8);
    uint32_t buf = dev ? MEM32(dev + 0x10) : 0;
    uint32_t block = buf ? MEM32(buf) : 0;
    if (block)
        apu_dsp_ack_add(block + 0x810);
    sub_003C3D23_gen();
}

/* Port Options uses the original Xbox list, text and controller navigation. */
#include "input/xinput_xbox.h"
extern void sub_0018D610_gen(void);
void sub_0018D610(void);
extern void sub_00163830_gen(void);
extern void sub_0018D590_gen(void);
extern void sub_00348BE0(void),sub_00348950(void),sub_001395E0(void);
#if defined(_WIN32)
#include <windows.h>
#endif

/* Windows: a heap page may be reserved without being committed, so the text
 * searches ask the OS which pages can be read. */
static int online_scan_readable(uint32_t va, uint32_t *size)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)XBOX_PTR(va), &mbi, sizeof mbi) || !mbi.RegionSize)
        return 0;
    *size = (uint32_t)mbi.RegionSize;
    if (mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                         PAGE_EXECUTE_WRITECOPY)) ||
        (mbi.Protect & PAGE_GUARD))
        return 0;
    return 1;
}


static uint32_t port_self,port_list,port_rows[32];
static uint32_t port_list_vtable;
static int port_open,port_back_down,port_back_release;
static int port_accept_down;
static int port_building;
static int port_page;                   /* Port Options page: 0 main, 1 advanced (iOS) */
static int port_row_id[32];             /* id behind each row of the current page */
static int port_row_n;
static char port_saved_title[120];
extern void windows_port_setting_change(int id);
extern void windows_port_setting_label(int id,char *out,size_t size);
#if defined(_WIN32) || defined(__linux__)
extern int windows_port_pc_music_get(void);
extern int windows_port_music_hud_get(void);
extern void windows_port_music_backup(float);
extern float windows_port_music_restore(void);
extern void sub_0026C1F0(void),sub_00062BA0(void);
extern void windows_media_context(int),windows_media_native_hud(int);
extern void windows_media_skip(int);
extern void windows_media_route_commit(const wchar_t *,int);
extern int windows_media_gameplay_get(void),windows_media_banner_get(wchar_t *,int);
#endif
#if defined(_WIN32) || defined(__linux__)
extern void sub_00139520(void),sub_001395E0(void);
#endif
typedef struct {uint32_t a,c,d,b,si,di,sp,bp,seh;} PortRegs;
static PortRegs port_save(void) {return (PortRegs){g_eax,g_ecx,g_edx,g_ebx,g_esi,g_edi,g_esp,g_ebp,g_seh_ebp};}
static void port_restore(PortRegs r) {g_eax=r.a;g_ecx=r.c;g_edx=r.d;g_ebx=r.b;g_esi=r.si;g_edi=r.di;g_esp=r.sp;g_ebp=r.bp;g_seh_ebp=r.seh;}
static void port_push(uint32_t x) {g_esp-=4;MEM32(g_esp)=x;}
static int port_ram(uint32_t p,uint32_t n)
{
    return n<=0x04000000u&&((p>=0x10000u&&p<=0x04000000u-n)||(p>=0x80000000u&&p<=0x84000000u-n));
}
#ifndef _WIN32
static void port_gameplay_update(void)
{
    uint32_t fsm=MEM32(0x557ED0u),front=MEM32(0x50F148u);
    uint32_t mode=port_ram(fsm,8)?MEM32(fsm+4):0;
    uint32_t owner=port_ram(front,16)?MEM32(front+12):0;
    uint32_t header=port_ram(owner,16)?MEM32(owner+12):0;
    uint32_t screen=port_ram(header,0xDC)?MEM32(header+0xD8):0;
    atomic_store(&port_gameplay_active,mode==2&&screen==1);
}
#else
static void port_gameplay_update(void) { }
#endif
/* Frontend row lists are destroyed/reused when gameplay or another menu takes
 * over. port_open alone is not evidence that the cached list still exists. */
static int port_context_valid(void)
{
    uint32_t front=MEM32(0x50F148u);
    if(!port_ram(front,16)||!port_ram(port_self,0x100)||MEM32(front+0xC)!=port_self||
       MEM32(port_self+0xFC)!=port_list||!port_ram(port_list,0x300)||MEM32(port_list)!=port_list_vtable)return 0;
    uint32_t page=MEM32(port_list+0x258),cap=MEM32(port_list+0x230);
    uint32_t columns=MEM32(port_list+0x234);
    return page<=2&&cap>=16&&cap<=64&&columns>=3&&columns<=32&&MEM32(port_list+0x238+page*4)<=cap;
}
static void port_invalidate(void)
{
    port_open=port_back_release=port_back_down=port_accept_down=0;
    port_page=0;port_row_n=0;
    port_self=port_list=port_list_vtable=0;memset(port_rows,0,sizeof port_rows);
}
#if defined(_WIN32) || defined(__linux__)
static uint64_t port_now_ms(void)
{
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;
#endif
}
/* Audio Options' music slider uses category 2, 0x53F308 + 0x2D4 + 8.
 * Use its own setter and refresh; never mute the master/effects categories.
 * The old music level is saved alongside the PC Music preference so it can
 * be restored even after restarting while external music is enabled. */
static void port_radio_sync(int menu_transition)
{
    static int applied,initialized;static uint64_t menu_restore_until;
    int pc=windows_port_pc_music_get(),on=pc&&windows_media_gameplay_get();
    if(!initialized){initialized=1;applied=pc&&!on;}
    /* Career/profile loading can overwrite the audio config with the zero
     * saved while PC Music was muting the radio. Repair that zero during the
     * menu transition only; do not continuously override the Audio slider. */
    if(menu_transition&&pc&&!on)menu_restore_until=port_now_ms()+500;
    if(on||!pc)menu_restore_until=0;
    if(pc&&!on&&port_now_ms()<menu_restore_until&&
       MEMF(0x53F5E4u)==0.0f&&windows_port_music_restore()>0.0f)applied=1;
    if(!MEM32(0x5366DCu))return;
    if((on&&(!applied||MEMF(0x53F5E4u)!=0.0f))||(!on&&applied)){
        PortRegs r=port_save();float volume=on?0.0f:windows_port_music_restore();uint32_t bits;
        memcpy(&bits,&volume,4);port_push(bits);port_push(2);port_push(0);g_ecx=0x53F308u;sub_0026C1F0();
        port_push(0);sub_00062BA0();port_restore(r);applied=on;
        fprintf(stderr,"[PORT] PC Music %s: game music volume %.2f (effects unchanged)\n",on?"on":"off",volume);
    }
}
/* Use the same live HUD and Flash properties as sub_00144220. No cached guest
 * object survives a menu transition. The old overlay stays the Unicode/absence
 * fallback, and all guest register/FPU state is preserved around HUD calls. */
static int port_media_classify(uint32_t mode,uint32_t screen)
{
    if(mode<1||mode>7)return 0;
    if(mode==2&&screen==1)return 1;
    if((screen>=2&&screen<=0x100)||(mode!=2&&screen==1))return 2;
    return 0;
}
/* The native message prompt's idle position is centred at (-53,93).
 * Its 128px texture therefore still reaches x=11 on a fully visible PC
 * framebuffer. The frame-2105 trace and hud_messageOn_en_x asset identify
 * this as the blue/white edge. State 0 is idle (no message and no animation);
 * retain the original draw for all entering, visible and exiting states. */
/* The native font covers printable ASCII. Decompose accented Latin letters
 * and omit unsupported glyphs; retain the Unicode overlay when no meaningful
 * native text remains (for example, a title written entirely in Japanese). */
/* ReDUB original cruising radio announcement slot (preferred). */
extern void sub_000B4980(void),sub_000B6330(void),sub_000B3550(void);
static uint32_t port_media_slot(uint32_t *manager,uint32_t *buffer)
{
    uint32_t pool=MEM32(0x558814u);
    if(!port_ram(pool,12))return 0;
    uint32_t player=MEM32(pool+8);
    if(!port_ram(player,0x5C))return 0;
    uint32_t owner=MEM32(player+0x58);
    if(!port_ram(owner,0x54))return 0;
    uint32_t m=MEM32(owner+0x50);
    if(!port_ram(m,0x300)||!MEM8(m+0x84))return 0;
    uint32_t slot=MEM32(m+0xC+24*4);
    if(!port_ram(slot,0x8C)||MEM32(slot)!=0x44F6D0u)return 0;
    uint32_t b=MEM32(slot+0x84),queue=MEM32(slot+0x10);
    /* This title slot is constructed with a 100-wchar allocation. Validate
     * the allocator's recorded 200-byte size before touching the text. */
    if(queue>=32||!port_ram(m,0x8C+(queue+1)*0xAC)||
       !port_ram(b,200)||!port_ram(b-16,16)||MEM32(b-12)!=200)return 0;
    *manager=m;*buffer=b;return slot;
}
static int port_media_native_text(const wchar_t *source,wchar_t *dest,size_t capacity)
{
    size_t n=0;int meaningful=0;
    if(!capacity)return 0;
#ifdef _WIN32
    wchar_t expanded[1024];
    if(!FoldStringW(MAP_COMPOSITE,source,-1,expanded,(int)(sizeof expanded/sizeof expanded[0])))return 0;
#else
    const wchar_t *expanded=source;
#endif
    for(const wchar_t *p=expanded;*p;p++){
        wchar_t c=*p;if(c<32||c>126)continue;
        if(n+1>=capacity)return 0;
        dest[n++]=c;if((c>=L'A'&&c<=L'Z')||(c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9'))meaningful=1;
    }
    dest[n]=0;
    if(meaningful){
        size_t first=0;while(first<n&&!((dest[first]>=L'A'&&dest[first]<=L'Z')||
            (dest[first]>=L'a'&&dest[first]<=L'z')||(dest[first]>=L'0'&&dest[first]<=L'9')))first++;
        if(first)memmove(dest,dest+first,(n-first+1)*sizeof *dest);
    }
    return meaningful;
}
static int port_media_cruising_title(const wchar_t *song,int visible)
{
    static uint32_t previous_slot;static wchar_t previous[100];
    uint32_t manager=0,buffer=0,slot=port_media_slot(&manager,&buffer);
    size_t n=wcslen(song);int supported=visible&&windows_port_music_hud_get()&&n>0&&n<90;
    for(size_t i=0;i<n;i++)if(song[i]<32||song[i]>126)supported=0;
    if(!slot||!supported){
        if(slot&&slot==previous_slot){
            MEM16(buffer)=0;
            port_push(24);port_push(0);g_ecx=manager;sub_000B4980();
            /* Queued text is copied by B39F0/B6330. Clearing the source
             * buffer alone cannot remove an already-running announcement. */
            port_push(0);g_ecx=slot;sub_000B3550();
        }
        previous_slot=0;previous[0]=0;return 0;
    }
    if(slot!=previous_slot||wcscmp(previous,song)){
        for(size_t i=0;i<=n;i++)MEM16(buffer+2*i)=(uint16_t)song[i];
        port_push(24);port_push(0);g_ecx=manager;sub_000B4980();
        port_push(0);port_push(0);port_push(24);port_push(0);g_ecx=manager;sub_000B6330();
        previous_slot=slot;wcscpy(previous,song);
        fprintf(stderr,"[MEDIA] song banner sent to cruising radio HUD (slot 24)\n");
    }
    return 1;
}
static void port_media_frame(void)
{
    static uint32_t previous_movie;static wchar_t previous_text[512];static int shown,last_context=-1;
    uint32_t hud=MEM32(0x50F12Cu),movie=0;
    if(port_ram(hud,0x1A24))movie=MEM32(hud+0x14);
    int hud_ready=port_ram(movie,0x40)&&port_ram(MEM32(movie+8),12);
    uint32_t fsm=MEM32(0x557ED0u),mode=port_ram(fsm,8)?MEM32(fsm+4):0;
    uint32_t front=MEM32(0x50F148u),owner=port_ram(front,16)?MEM32(front+12):0;
    uint32_t header=port_ram(owner,16)?MEM32(owner+12):0;
    uint32_t screen=port_ram(header,0xDC)?MEM32(header+0xD8):0;
    /* Measured on-road: mode 2, screen 1. Paused: mode 2, screen 2.
     * HUD availability is independent: this mode uses the non-Flash HUD. */
    int context=port_media_classify(mode,screen);
    int gameplay=context==1;
    windows_media_context(context);
    /* Edge-triggered: a held direction skips once, and holding through a menu
     * transition does not issue a fresh command. Never run WinRT on this thread. */
    static uint16_t media_dpad;
    XBOX_INPUT_STATE media_input={0};uint16_t dpad=0;
    if(xbox_InputGetState(0,&media_input)==0)dpad=media_input.Gamepad.wButtons&
        (XBOX_GAMEPAD_DPAD_LEFT|XBOX_GAMEPAD_DPAD_RIGHT);
    uint16_t pressed=dpad&~media_dpad;media_dpad=dpad;
    if(gameplay&&windows_port_pc_music_get()){
        if(pressed==XBOX_GAMEPAD_DPAD_LEFT)windows_media_skip(0);
        else if(pressed==XBOX_GAMEPAD_DPAD_RIGHT)windows_media_skip(1);
    }
    if(context!=last_context){fprintf(stderr,"[MEDIA] context %s mode=%u screen=%u HUD=%08X movie=%08X\n",context==1?"gameplay":context==2?"menu":"unknown",mode,screen,hud,movie);last_context=context;}
    PortRegs r=port_save();
    RecompXmm xs[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
    double fp[8];memcpy(fp,g_fp_stack,sizeof fp);int top=g_fp_top;
    static uint32_t last_music_screen;
    int menu_transition=context==2&&screen!=last_music_screen;
    last_music_screen=context==2?screen:0;
    port_radio_sync(menu_transition);
    /* Reserve the announcement while selecting/updating its native route.
     * The render thread must not sample the fallback between the native draw
     * being enabled and the final route publication below. Release it only
     * after obsolete native text has been cleared. */
    windows_media_native_hud(1);
    wchar_t text[512]={0};int visible=windows_media_banner_get(text,512),native=0;
    const wchar_t *track=!wcsncmp(text,L"NOW PLAYING: ",13)?text+13:text;
    wchar_t native_song[512]={0};int have_native=port_media_native_text(track,native_song,512);
    int cruising=port_media_cruising_title(native_song,gameplay&&visible&&have_native);
    native=cruising;
    if(gameplay&&visible&&hud_ready&&!cruising){
        const wchar_t *song=native_song;
        char utf8[150];size_t bytes=0;for(const wchar_t *p=song;*p&&bytes+1<sizeof utf8;p++){if(*p>126||*p<32){bytes=0;break;}utf8[bytes++]=(char)*p;}utf8[bytes]=0;
        int supported=have_native&&bytes>0&&windows_port_music_hud_get();
        /* Native fonts do not cover every Windows media title. */
        for(const wchar_t *p=song;*p;p++)if(*p>126||*p<32)supported=0;
        if(supported){
            if(!shown||movie!=previous_movie||wcscmp(previous_text,text)){
                memcpy((void *)XBOX_PTR(hud+0x198D),utf8,bytes);
                port_push(hud+0x198D);port_push(0x459D3Cu);port_push(0);g_ecx=movie;sub_001395E0();
                port_push(1);port_push(0x459D2Cu);port_push(0);g_ecx=movie;sub_00139520();
                wcscpy(previous_text,text);previous_movie=movie;shown=1;
                fprintf(stderr,"[MEDIA] song banner sent to native radio HUD\n");
            }
            native=1;
        }
    }
    if(shown&&(!native||!visible||cruising)){
        if(hud_ready&&movie==previous_movie){
            MEM8(hud+0x198D)=0;
            port_push(hud+0x198D);port_push(0x459D3Cu);port_push(0);g_ecx=movie;sub_001395E0();
            port_push(0);port_push(0x459D2Cu);port_push(0);g_ecx=movie;sub_00139520();
        }
        shown=0;previous_movie=0;previous_text[0]=0;
    }
    windows_media_route_commit(text,native);
    port_restore(r);g_xmm0=xs[0];g_xmm1=xs[1];g_xmm2=xs[2];g_xmm3=xs[3];g_xmm4=xs[4];g_xmm5=xs[5];g_xmm6=xs[6];g_xmm7=xs[7];memcpy(g_fp_stack,fp,sizeof fp);g_fp_top=top;
}
#else
static void port_media_frame(void) { }
#endif
static void port_text(uint32_t row,const char *text)
{
    if(!port_context_valid()||!port_ram(row,12))return;
    uint32_t column=MEM32(port_list+0x244),cell=MEM32(row+8);
    if(!port_ram(column,32)||!port_ram(cell,20))return;
    uint32_t cap=MEM32(column+28);
    if(cap<2 || cap>1024)return;
    uint32_t dst=MEM32(cell+16);
    if(!port_ram(dst,cap*2))return;
    size_t n=strlen(text);if(n>=cap)n=cap-1;
    for(size_t i=0;i<n;i++)MEM16(dst+i*2)=(unsigned char)text[i];
    MEM16(dst+n*2)=0;
}
static void port_labels(void)
{
    if(!port_context_valid())return;
    for(int k=0;k<port_row_n;k++){
        int i=port_row_id[k];char text[96];text[0]=0;
        windows_port_setting_label(i,text,sizeof text);port_text(port_rows[i],text);
    }
}
static uint32_t port_add(uint32_t event,const char *text)
{
    uint32_t page=MEM32(port_list+0x258),count=MEM32(port_list+0x238+page*4);
    uint32_t args[]={count,0,0,MEM32(port_self+0xC),0x00163830u,event,2};
    for(int k=6;k>=0;k--)port_push(args[k]);
    port_push(0);g_ecx=port_list;sub_00348BE0();
    uint32_t row=g_eax;if(text)port_text(row,text);return row;
}
static void port_title(int enter)
{
    if(!port_context_valid())return;
    uint32_t title=MEM32(port_self+0xC)+0x60;
    if(!port_ram(title,120))return;
    if(enter) {if(enter==1)memcpy(port_saved_title,(void *)XBOX_PTR(title),120);memset((void *)XBOX_PTR(title),0,120);memcpy((void *)XBOX_PTR(title),"PORT OPTIONS",12);}
    else memcpy((void *)XBOX_PTR(title),port_saved_title,120);
    uint32_t movie=MEM32(0x50F15Cu);
    if(port_ram(movie,0xFB0) && (movie=MEM32(movie+0xFAC)) && port_ram(movie,4)) {
        uint32_t sp=g_esp;
        port_push(title);port_push(0x45B9F8u);port_push(0);
        g_ecx=movie;sub_001395E0();g_esp=sp;
    }
}
static void port_build(void)
{
    if(!port_context_valid()||port_building)return;
    port_building=1;
    port_push(1);port_push(0);g_ecx=port_list;sub_00348950();
    uint32_t page=MEM32(port_list+0x258),state=port_list+page*16;
    MEM32(port_list+0x238+page*4)=0;
    MEM8(state+0x26C)=0;MEM32(state+0x268)=0;
    MEM8(state+0x29C)=0;MEM32(state+0x298)=0;
    /* The Options page holds 16 rows including BACK (4 columns x 4 rows,
     * page 2 is the last page), so the row count must stay below capacity:
     * 15 settings + BACK at capacity 16. PHONE MUSIC (id 9) and SONG DISPLAY
     * (id 11) are the media rows. Per-app volume does not exist on Android,
     * so id 10 (PC MUSIC VOLUME) is not offered here. */
#if defined(__ANDROID__)
    /* 16-slot page: 15 rows + BACK. The always-fullscreen status and the
     * graphics preset (both also in the Android launcher) give up their slots
     * to phone music and controller vibration; both of those were previously
     * labels with no handler. */
    const int order[]={1,2,3,4,6,7,8,9,10,11,12,13,14,15,16};
    int settings=15;
#elif defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    /* iOS: the launcher's touch controls on the main page, the fine-tuning on
     * an Advanced page. Ids 0..16 fit; 15 opens Advanced, and ids 17..21 get
     * their own event range below. Desktop keeps its own order. */
    const int main_order[]={0,1,8,9,10,11,2,12,3,13,14,16,15};
    const int adv_order[]={5,7,4,6,19,20,21,17,18,22,23};
    const int *order=port_page?adv_order:main_order;
    int settings=port_page?11:13;
#else
    const int order[]={0,5,2,12,3,4,6,13,14,1,16,7,9,11,15,8,10};
    int capacity=MEM32(port_list+0x230);
    int settings=capacity>=18?17:capacity>=17?16:15;
#endif
    port_row_n=0;
    for(int k=0;k<settings;k++){
        int i=order[k];
        /* FRAME RATE is id 16 (0x7F12); the mobile controls are ids 17+ in
         * their own event range, because 0x7F10 already opens Port Options. */
        uint32_t ev = i==16 ? 0x7F12u
                    : i>=17 ? 0x7F20u+(uint32_t)(i-17)
                    : 0x7F00u+(uint32_t)i;
        port_rows[i]=port_add(ev,NULL);
        port_row_id[port_row_n++]=i;
    }
    port_add(0x7F11u,port_page?"BACK TO OPTIONS":"BACK");port_labels();
    port_building=0;
}
static void port_enter(void)
{
    if(!port_context_valid()) {port_invalidate();return;}
    PortRegs r=port_save();port_open=1;port_page=0;
    XBOX_INPUT_STATE input={0};
    port_back_down=xbox_InputGetState(0,&input)==0 && input.Gamepad.bAnalogButtons[XBOX_BUTTON_B]>XBOX_ANALOG_BUTTON_THRESHOLD;
    port_accept_down=input.Gamepad.bAnalogButtons[XBOX_BUTTON_A]>XBOX_ANALOG_BUTTON_THRESHOLD;
    port_build();port_title(1);
    fprintf(stderr,"[PORT] opened Port Options\n");port_restore(r);
}
static void port_parent(void)
{
    if(!port_context_valid()) {port_invalidate();return;}
    PortRegs r=port_save();port_open=0;
    port_push(0);sub_0018D610();port_title(0);
    fprintf(stderr,"[PORT] returned to Options\n");port_restore(r);
}
/* Retain the desktop-specific presentation fix and shared soundtrack hook. */
extern void sub_000D1010_gen(void);
void sub_000D1010(void)
{
#if defined(_WIN32) || defined(__linux__)
    uint32_t self=g_ecx;
    if(port_ram(self,0xD4)&&MEM32(self)==0x451DE0u&&
       MEM32(self+0x54)==0&&MEM32(self+0x88)==0&&!MEM8(self+0xD0)){
        g_esp+=4; /* native thiscall ret, no arguments */
        return;
    }
#endif
    sub_000D1010_gen();
}
/* The soundtrack posts generic slot-24 announcements even when muted. Skip
 * them while PC Music is active so the game's radio stays silent. */
extern void sub_0001BDF0_gen(void);
void sub_0001BDF0(void)
{
#if defined(_WIN32) || defined(__linux__)
    if(windows_port_pc_music_get()&&windows_media_gameplay_get()){
        g_esp+=8; /* ret 4: notification's player-index argument */
        return;
    }
#endif
    sub_0001BDF0_gen();
}
void sub_0018D610(void)
{
    if(port_open&&!port_context_valid()) {fprintf(stderr,"[PORT] Options owner replaced; clearing cached page\n");port_invalidate();}
    if(port_open&&!port_building) {PortRegs r=port_save();port_build();port_restore(r);g_esp+=4;return;}
    sub_0018D610_gen();
    port_open=0;port_self=MEM32(MEM32(0x50F148u)+0xCu);port_list=MEM32(port_self+0xFC);
    port_list_vtable=port_ram(port_list,0x300)?MEM32(port_list):0;
    if(port_ram(port_list,0x300))fprintf(stderr,"[PORT] Options context self=%08X list=%08X vtable=%08X page=%u capacity=%u columns=%u\n",port_self,port_list,port_list_vtable,MEM32(port_list+0x258),MEM32(port_list+0x230),MEM32(port_list+0x234));
    if(!port_context_valid()) {port_invalidate();return;}
    uint32_t page=MEM32(port_list+0x258),count=MEM32(port_list+0x238+page*4),capacity=MEM32(port_list+0x230);
    if(page>2 || count+1>capacity || MEM32(port_list+0x234)<3)return;
    PortRegs r=port_save();port_add(0x7F10u,"Port Options");port_restore(r);
}
void sub_00163830(void)
{
    uint32_t event=MEM32(g_esp+4);
    if(port_open&&!port_context_valid())port_invalidate();
    if(event==0x7F10) {port_enter();g_eax=0;g_esp+=8;return;}
    if(event==0x7F11) {
#if defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
        /* On the Advanced page, Back returns to the main page rather than
         * leaving Port Options. */
        if(port_page>0){port_page=0;port_build();g_eax=0;g_esp+=8;return;}
#endif
        port_parent();g_eax=0;g_esp+=8;return;
    }
#if defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    if(port_open && event==0x7F0F) {
        /* The Advanced... row (id 15): switch page and rebuild the list. */
        if(port_accept_down){g_eax=0;g_esp+=8;return;}
        port_accept_down=1;
        port_page=port_page?0:1;
        port_build();
        g_eax=0;g_esp+=8;return;
    }
#endif
    if(port_open && event==0x7F12) {
        if(port_accept_down){g_eax=0;g_esp+=8;return;}
        port_accept_down=1;
        windows_port_setting_change(16);port_labels();g_eax=0;g_esp+=8;return;
    }
    if(port_open && event>=0x7F20 && event<0x7F30) {
        if(port_accept_down){g_eax=0;g_esp+=8;return;}
        port_accept_down=1;
        windows_port_setting_change(17+(int)(event-0x7F20));
        port_labels();g_eax=0;g_esp+=8;return;
    }
    if(port_open && event>=0x7F00 && event<0x7F10) {
        if(port_accept_down){g_eax=0;g_esp+=8;return;}
        port_accept_down=1;
#if defined(_WIN32) || defined(__linux__)
        if(event==0x7F09&&!windows_port_pc_music_get())windows_port_music_backup(MEMF(0x53F5E4u));
#endif
        windows_port_setting_change(event-0x7F00);port_labels();g_eax=0;g_esp+=8;return;
    }
    sub_00163830_gen();
}
void sub_0018D590(void)
{
    if(port_open&&!port_context_valid()) {fprintf(stderr,"[PORT] Options owner replaced; clearing cached page\n");port_invalidate();}
#if defined(_WIN32) || defined(__linux__)
    port_radio_sync(0);
#endif
    if(port_open || port_back_release) {
        XBOX_INPUT_STATE input={0};
        int down=xbox_InputGetState(0,&input)==0 && input.Gamepad.bAnalogButtons[XBOX_BUTTON_B]>XBOX_ANALOG_BUTTON_THRESHOLD;
        if(input.Gamepad.bAnalogButtons[XBOX_BUTTON_A]<=XBOX_ANALOG_BUTTON_THRESHOLD)port_accept_down=0;
        if(port_open && down && !port_back_down) {
            port_parent();port_back_release=1;port_back_down=down;g_esp+=4;return;
        }
        port_back_down=down;
        if(port_back_release) {
            /* +0x44 bit 0 enables the entire native menu, not the B button.
             * Swallow only this held press; never disable the parent page. */
            if(!down)port_back_release=0;
            g_esp+=4;return;
        }
    }
    sub_0018D590_gen();
    if(port_open) {PortRegs r=port_save();port_labels();port_restore(r);}
}

/* ── System Link: the XNet API answered by the runtime (MC3_NETPLAY=1) ──
 *
 * MC3 plays System Link over plain Winsock datagrams: UDP :1000 with
 * SO_BROADCAST (discovery to 255.255.255.255:1000) and VDP :1001, both
 * non-blocking; peers by XNADDR -> XNetXnAddrToInAddr. The statically linked
 * XNET library (CXn at [0x0055FDAC], its NIC driver, secure key exchange)
 * cannot run: no NIC interrupts. So its exported thunks (`mov ecx,
 * [0x55FDAC]; jmp CXn::method`, XNET section) are answered by
 * xboxrecomp/src/net/xnet_hle.c over host UDP sockets.
 *
 * XNetStartup / WSAStartup never create CXn, so the Live-only thunks left
 * lifted (XNetQosLookup, ...) keep returning WSANOTINITIALISED, and
 * RECOMP_NET_LINK=0 keeps the NIC idle (PhyGetLinkState: no cable), which is
 * what avoids the ~20 s XNET DHCP black screen. Addresses from the USA XBE
 * (the edition this tree lifts); the EUR disc is the same code.
 *
 * Off by default: each override runs the lifted thunk unless MC3_NETPLAY=1.
 * Transport: RECOMP_NET_PORT_BASE / _ADDR / _PEERS (xnet_hle.c).
 *
 * Adapted from mc3-web by Leandro Dettmer (MIT, commit 7a64713). */
#include "net/xnet_hle.h"

static int netplay_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_NETPLAY");
        on = e && *e == '1';
        if (on) {
            fprintf(stderr, "[NET] System Link HLE on (MC3_NETPLAY): XNet answered by the runtime\n");
            /* The guest's XNetStartup comes before the first frame that reaches
             * roam_net_boot(), and xnet_startup() latches the medium's address
             * once. The free-roam session (its id, and the relay) therefore has
             * to exist here, or System Link would come up on host UDP and the
             * relay could never carry a race. roam_net_boot is idempotent and
             * does nothing unless a roam setup was asked for. */
            roam_net_boot();
        }
    }
    return on;
}

#define NET_ARG(i) MEM32(g_esp + 4u * (i))
static uint8_t *net_ptr(uint32_t va) { return va ? (uint8_t *)XBOX_PTR(va) : NULL; }
/* stdcall return: result in eax, pop the return address and the arguments */
static void net_ret(uint32_t eax, uint32_t nargs) { g_eax = eax; g_esp += 4u + 4u * nargs; }

/* XNetGetEthernetLinkStatus (XAPI, .text): the System Link menu is gated on it */
extern void sub_002E9B40_gen(void);
void sub_002E9B40(void)
{
    if (!netplay_on()) { sub_002E9B40_gen(); return; }
    net_ret(xnet_link_status(), 0);
}

/* XNetStartup(params) */
extern void sub_003CC350_gen(void);
void sub_003CC350(void)
{
    if (!netplay_on()) { sub_003CC350_gen(); return; }
    net_ret((uint32_t)xnet_startup(), 1);
}

/* WSAStartup(wVersionRequested, lpWSAData). wVersion is the version asked
 * for, up to 2.2. MC3's network init (sub_001A2870) asks for 2.0 and quits
 * unless it gets exactly 2.0. */
extern void sub_003CC367_gen(void);
void sub_003CC367(void)
{
    uint8_t *wsa;
    uint32_t want = NET_ARG(1);
    uint8_t major = want & 0xFF, minor = (want >> 8) & 0xFF;
    int r;
    if (!netplay_on()) { sub_003CC367_gen(); return; }
    if (major > 2 || (major == 2 && minor > 2))
        major = 2, minor = 2;
    wsa = net_ptr(NET_ARG(2));
    r = xnet_startup();
    if (!r && wsa) {
        wsa[0] = major; wsa[1] = minor;   /* wVersion */
        wsa[2] = 2; wsa[3] = 2;           /* wHighVersion */
    }
    net_ret((uint32_t)(wsa ? r : XNET_WSAEFAULT), 2);
}

/* WSACleanup() */
extern void sub_003CC455_gen(void);
void sub_003CC455(void)
{
    if (!netplay_on()) { sub_003CC455_gen(); return; }
    net_ret(0, 0);
}

/* WSAGetLastError() */
extern void sub_003CC4E5_gen(void);
void sub_003CC4E5(void)
{
    if (!netplay_on()) { sub_003CC4E5_gen(); return; }
    net_ret((uint32_t)xnet_last_error(), 0);
}

/* XNetRandom(pb, cb) */
extern void sub_003CBF8F_gen(void);
void sub_003CBF8F(void)
{
    uint8_t *p;
    if (!netplay_on()) { sub_003CBF8F_gen(); return; }
    p = net_ptr(NET_ARG(1));
    if (p) xnet_random(p, NET_ARG(2));
    net_ret(0, 2);
}

/* XNetCreateKey(pxnkid, pxnkey) */
extern void sub_003CBF9A_gen(void);
void sub_003CBF9A(void)
{
    uint8_t *kid, *key;
    if (!netplay_on()) { sub_003CBF9A_gen(); return; }
    kid = net_ptr(NET_ARG(1));
    key = net_ptr(NET_ARG(2));
    if (kid && key) xnet_create_key(kid, key);
    net_ret(0, 2);
}

/* XNetRegisterKey(pxnkid, pxnkey): no security, nothing to register */
extern void sub_003CBFA5_gen(void);
void sub_003CBFA5(void)
{
    if (!netplay_on()) { sub_003CBFA5_gen(); return; }
    net_ret(0, 2);
}

/* XNetUnregisterKey(pxnkid) */
extern void sub_003CBFB0_gen(void);
void sub_003CBFB0(void)
{
    if (!netplay_on()) { sub_003CBFB0_gen(); return; }
    net_ret(0, 1);
}

/* XNetXnAddrToInAddr(pxna, pxnkid, pina): the peer's plain address, xna.ina */
extern void sub_003CBFBB_gen(void);
void sub_003CBFBB(void)
{
    uint32_t xna, ina;
    if (!netplay_on()) { sub_003CBFBB_gen(); return; }
    xna = NET_ARG(1);
    ina = NET_ARG(3);
    if (!xna || !ina) { net_ret(XNET_WSAEFAULT, 3); return; }
    MEM32(ina) = MEM32(xna);
    net_ret(0, 3);
}

/* XNetQosListen(...): Live matchmaking only */
extern void sub_003CBFF4_gen(void);
void sub_003CBFF4(void)
{
    if (!netplay_on()) { sub_003CBFF4_gen(); return; }
    net_ret(0, 5);
}

/* XNetQosRelease(pxnqos) */
extern void sub_003CC01F_gen(void);
void sub_003CC01F(void)
{
    if (!netplay_on()) { sub_003CC01F_gen(); return; }
    net_ret(0, 1);
}

/* XNetGetTitleXnAddr(pxna) */
extern void sub_003CC02A_gen(void);
void sub_003CC02A(void)
{
    uint8_t *xna;
    if (!netplay_on()) { sub_003CC02A_gen(); return; }
    xna = net_ptr(NET_ARG(1));
    net_ret(xna ? xnet_get_title_xnaddr(xna) : 0x0001u, 1);
}

/* XNetGetConnectStatus(ina): no key exchange, so always connected */
extern void sub_003CC13F_gen(void);
void sub_003CC13F(void)
{
    if (!netplay_on()) { sub_003CC13F_gen(); return; }
    net_ret(XNET_CONNECT_STATUS_CONNECTED, 1);
}

/* socket(af, type, protocol) */
extern void sub_003CC035_gen(void);
void sub_003CC035(void)
{
    if (!netplay_on()) { sub_003CC035_gen(); return; }
    net_ret(xnet_socket((int)NET_ARG(1), (int)NET_ARG(2), (int)NET_ARG(3)), 3);
}

/* closesocket(s) */
extern void sub_003CC040_gen(void);
void sub_003CC040(void)
{
    if (!netplay_on()) { sub_003CC040_gen(); return; }
    net_ret((uint32_t)xnet_closesocket(NET_ARG(1)), 1);
}

/* ioctlsocket(s, cmd, argp) */
extern void sub_003CC056_gen(void);
void sub_003CC056(void)
{
    if (!netplay_on()) { sub_003CC056_gen(); return; }
    net_ret((uint32_t)xnet_ioctlsocket(NET_ARG(1), NET_ARG(2), net_ptr(NET_ARG(3))), 3);
}

/* setsockopt(s, level, optname, optval, optlen) */
extern void sub_003CC061_gen(void);
void sub_003CC061(void)
{
    if (!netplay_on()) { sub_003CC061_gen(); return; }
    net_ret((uint32_t)xnet_setsockopt(NET_ARG(1), (int)NET_ARG(2), (int)NET_ARG(3),
                                      net_ptr(NET_ARG(4)), (int)NET_ARG(5)), 5);
}

/* bind(s, name, namelen) */
extern void sub_003CC070_gen(void);
void sub_003CC070(void)
{
    if (!netplay_on()) { sub_003CC070_gen(); return; }
    net_ret((uint32_t)xnet_bind(NET_ARG(1), net_ptr(NET_ARG(2)), (int)NET_ARG(3)), 3);
}

/* recvfrom(s, buf, len, flags, from, fromlen) */
extern void sub_003CC0BE_gen(void);
void sub_003CC0BE(void)
{
    if (!netplay_on()) { sub_003CC0BE_gen(); return; }
    net_ret((uint32_t)xnet_recvfrom(NET_ARG(1), net_ptr(NET_ARG(2)), (int)NET_ARG(3),
                                    (int)NET_ARG(4), net_ptr(NET_ARG(5)), net_ptr(NET_ARG(6))), 6);
}

/* sendto(s, buf, len, flags, to, tolen) */
extern void sub_003CC0DC_gen(void);
void sub_003CC0DC(void)
{
    if (!netplay_on()) { sub_003CC0DC_gen(); return; }
    net_ret((uint32_t)xnet_sendto(NET_ARG(1), net_ptr(NET_ARG(2)), (int)NET_ARG(3),
                                  (int)NET_ARG(4), net_ptr(NET_ARG(5)), (int)NET_ARG(6)), 6);
}

/* Mods: MC3_UNLOCK_ALL, the game's own unlock flags (unlock_all.c). */
#include "unlock_all.c"

/* sub_0028B4E0 is the game's generic "is this unlocked?" check; the reward
 * paints are asked for by name. Defined here (not in unlock_all.c) because
 * the lift scanner only reads this file. */
extern void sub_0028B4E0_gen(void);
void sub_0028B4E0(void)
{
    if (!unlock_all_paint())
        sub_0028B4E0_gen();
}

/* Free roam: the transport (roam_net.c); the car pool comes next. */
#include "roam_net.c"

/* The Online session: money, garage and unlocks kept aside and put back. */
#include "online_session.c"

/* Free roam: the engine capacity it needs (roam_caps.c). */
#include "roam_caps.c"

/* Free roam game layer: the upstream files compiled against roam_compat.h.
 * Inert unless MC3_ROAM=1 started the transport (roam_running). */
#include "roam_compat.h"
#include "roam_game.c"
#include "roam_cars.c"
#include "roam_pool.c"

/* Free roam: the name tags over the remote cars (roam_tagdraw.c). */
#include "roam_tagdraw.c"

/* Free roam: the map blips for linked peers (roam_blips.c). */
#include "roam_blips.c"

/* Free roam: the ambient traffic/police/pedestrian probe (roam_ambient.c). */
#include "roam_ambient.c"

#!/usr/bin/env python3
"""Exercise the real setup transitions with guest call/return probes.

This compiles the actual `free_roam_setup_enter`, `free_roam_start_cruise`,
`free_roam_setup_exit`, `port_fire_event` and `port_parent` bodies against a
byte-addressed guest memory and probes for the guest call/return convention.

Note: the direct Cruise load (`free_roam_start_cruise`) is checked here for the
setup-struct fields it writes and its guest stack use, with the recompiled
`sub_00184FD0` stubbed. Whether the game's own loader actually starts a cruise
is only provable on a device, so that remains a live test.
"""
import importlib.util
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("online_abi", ROOT / "tools/test_online_abi.py")
abi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(abi)


def main():
    cc = shutil.which("clang") or shutil.which("gcc")
    if os.name == "nt":
        msys = Path(os.environ.get("MSYS2_ROOT", os.environ["SystemDrive"] + os.sep))
        if "MSYS2_ROOT" not in os.environ:
            msys /= "msys64"
        compiler = msys / "clang64/bin/clang.exe"
        if compiler.exists():
            cc = str(compiler)
    assert cc, "C compiler required"
    regs = abi.SOURCE[abi.SOURCE.index("typedef struct {uint32_t a,c,d,b,si,di,sp,bp,seh;}"):]
    regs = regs[:regs.index("\n\n")]
    harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t g_eax,g_ecx,g_edx,g_ebx,g_esi,g_edi,g_esp,g_ebp,g_seh_ebp;
static uint8_t memory[0x600000];
#define XBOX_PTR(a) ((uintptr_t)(memory + (uint32_t)(a)))
#define MEM8(a)  (*(uint8_t  *)XBOX_PTR(a))
#define MEM32(a) (*(uint32_t *)XBOX_PTR(a))
static int free_roam_setup_requested,free_roam_setup_active,port_open,port_find_pending;
static int free_roam_nickname_edit,free_roam_city;
static int options,back,cruise,builds,title_restored,options_parent,garage_copies,ram_ok=1;
static uint32_t room_set,dispatched,cruise_context,cruise_return;
static int port_context_valid(void) { return 1; }
static void port_invalidate(void) {}
static int port_ram(uint32_t p,uint32_t n) { (void)p;(void)n;return ram_ok; }
static void port_build(void) { builds++;port_open=1; }
static void roam_set_room(uint32_t r) { room_set=r; }
static void free_roam_nickname_finish(int save) { (void)save;free_roam_nickname_edit=0; }
static void sub_0013AB20(void) { garage_copies++;g_esp+=4; }   /* generated RET */
static void sub_0018D610(void) { options_parent++;g_esp+=4; }
static void sub_00163830(void) { assert(MEM32(g_esp)==0);dispatched=MEM32(g_esp+4);g_esp+=8; }
static void port_push(uint32_t v) { g_esp-=4;MEM32(g_esp)=v; }
static void port_title(int enter) { assert(enter==0);title_restored++; }
static void sub_00163830_gen(void) {
    assert(g_ecx==384 && MEM32(g_esp)==0 && MEM32(g_esp+4)==7);
    assert(free_roam_setup_requested==1);options++;g_esp+=8;g_ecx=99;
}
static void sub_0018C5A0_gen(void) { back++;g_esp+=4;g_ecx=99; }
static void sub_00184FD0(void) {
    cruise++;cruise_context=g_ecx;cruise_return=MEM32(g_esp);
    assert(MEM8(g_ecx+0x1E0u)==1);            /* the UI-context byte */
    g_esp+=4;g_ecx=99;
}
'''
    harness += regs + "\n" + abi.function("static void online_frame_call(void (*action)(void))")
    harness += "\n" + abi.function("static void port_fire_event(uint32_t ev)")
    harness += "\n" + abi.function("static void free_roam_setup_enter(void)")
    harness += "\n" + abi.function("static int free_roam_start_cruise(void)")
    harness += "\n" + abi.function("static void free_roam_setup_exit(int start)")
    harness += "\n" + abi.function("static void port_parent(void)")
    harness += r'''
int main(void) {
    uint32_t ptr_slot=0x53F2E8u,setup=0x10000u;
    /* the front-buffer -> owner -> header chain the page reads for its list */
    MEM32(0x50F148u)=128;MEM32(128+12)=256;MEM32(256+12)=384;
    MEM32(ptr_slot)=setup;MEM32(setup)=2;MEM32(setup+0x120u)=0x20000u;
    g_esp=1024;g_ecx=42;

    /* the test event consumes the argument and the return slot, nothing else */
    port_fire_event(0x7F19u);
    assert(dispatched==0x7F19u && g_esp==1024 && g_ecx==42);

    /* Enter: copies the garage car, reads the city, opens the Options canvas
     * with the same guest context on every path. */
    free_roam_setup_enter();
    assert(options==1 && garage_copies==1 && free_roam_setup_requested==1 && free_roam_city==2);
    assert(g_esp==1024 && g_ecx==42);

    /* Start while a room search is pending is refused: nothing is consumed. */
    free_roam_setup_active=port_open=port_find_pending=1;
    free_roam_setup_exit(1);
    assert(cruise==0 && builds==0 && free_roam_setup_active==1 && port_open==1);
    assert(g_esp==1024 && g_ecx==42 && title_restored==0);

    /* Start with a valid car: the Cruise loader gets a clean frame and the
     * setup struct carries the city, type 0 (Cruise), and one local player. */
    port_find_pending=0;
    free_roam_setup_exit(1);
    assert(cruise==1 && builds==0 && title_restored==1);
    assert(!free_roam_setup_active && !port_open && !port_find_pending);
    assert(g_esp==1024 && g_ecx==42 && cruise_return==0);
    assert(room_set==2 && MEM32(setup)==2 && MEM32(setup+0x18)==0);
    assert(MEM32(setup+0x4Cu)==1 && MEM32(setup+0x50u)==1);
    assert(MEM8(setup+0x108u)==0 && MEM8(setup+0x13Au)==0);
    assert(cruise_context==540 && MEM8(cruise_context+0x1E0u)==1);

    /* No valid garage car: the load is refused and the page stays up. */
    ram_ok=0;free_roam_setup_active=port_open=1;
    free_roam_setup_exit(1);
    assert(cruise==1 && builds==1 && free_roam_setup_active==1 && port_open==1);
    assert(g_esp==1024 && g_ecx==42 && title_restored==2);
    ram_ok=1;

    /* Back from the page returns to Online without touching the game's stack. */
    free_roam_setup_active=port_open=port_find_pending=1;
    free_roam_setup_exit(0);
    assert(back==1 && !free_roam_setup_active && !port_find_pending && !free_roam_setup_requested);
    assert(g_esp==1024 && g_ecx==42 && title_restored==3);

    /* Back on the page routes through the page's own exit. */
    free_roam_setup_active=port_open=1;
    port_parent();
    assert(back==2 && options_parent==0 && !port_open && g_esp==1024);

    /* Back outside the page is the inherited Options route. */
    free_roam_setup_active=0;port_open=1;
    port_parent();
    assert(options_parent==1 && !port_open && g_esp==1024);

    puts("PASS: setup event, search gating, direct Cruise load, refused load, Start/Back and guest context");
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "menu.c"
        exe = Path(tmp) / ("menu.exe" if os.name == "nt" else "menu")
        source.write_text(harness, encoding="utf-8")
        subprocess.run([cc, str(source), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()

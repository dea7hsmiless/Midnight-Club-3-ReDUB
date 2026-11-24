#!/usr/bin/env python3
"""Compile the real Online callback and frame bridge against guest ABI probes."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/games/remix/recomp_manual.c").read_text(encoding="utf-8-sig")


def function(signature):
    start = SOURCE.index(signature + "\n{")
    end = SOURCE.index("\n}", start) + 2
    return SOURCE[start:end]


def main():
    cc = shutil.which("clang") or shutil.which("gcc")
    if os.name == "nt" and Path("C:/msys64/clang64/bin/clang.exe").exists():
        cc = "C:/msys64/clang64/bin/clang.exe"
    assert cc, "C compiler required"
    regs = SOURCE[SOURCE.index("typedef struct {uint32_t a,c,d,b,si,di,sp,bp,seh;}"):]
    regs = regs[:regs.index("\n\n")]
    harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static uint32_t g_eax,g_ecx,g_edx,g_ebx,g_esi,g_edi,g_esp,g_ebp,g_seh_ebp;
static uint32_t memory[1024];
#define MEM32(p) memory[(p)/4]
static int online_cruise_pending, wanted, enabled, fallback, arcade;
int online_session_wanted(void) { return wanted; }
static int online_play_enabled(void) { return enabled; }
void online_enter(const char *why) { (void)why; }
void sub_00180160_gen(void) { fallback++; g_esp += 4; }
static void free_roam_setup_enter(void) { arcade++; }
'''
    harness += regs + "\n" + function("static void online_frame_call(void (*action)(void))")
    harness += "\n" + function("void sub_00180160(void)")
    harness += r'''
static void action(void) {
    assert(g_esp == 508 && MEM32(g_esp) == 0);
    g_eax=11; g_ecx=12; g_edx=13; g_ebx=14;
    g_esi=15; g_edi=16; g_ebp=17; g_seh_ebp=18;
    g_esp += 4;
}
int main(void) {
    for (wanted=0; wanted<=1; wanted++) {
        for (enabled=0; enabled<=1; enabled++) {
            g_esp=512; online_cruise_pending=0; fallback=0; arcade=0; g_ecx=2;
            sub_00180160();
            assert(g_esp==516);
            /* 0.2.5: Free Roam is deferred, so the item does nothing; the
             * callback still returns its guest frame either way. */
            assert(arcade==0);
            assert(g_ecx==2);
            assert(fallback==!(wanted||enabled));
        }
    }
    g_eax=1; g_ecx=2; g_edx=3; g_ebx=4;
    g_esi=5; g_edi=6; g_esp=512; g_ebp=7; g_seh_ebp=8;
    online_frame_call(action);
    assert(g_eax==1 && g_ecx==2 && g_edx==3 && g_ebx==4);
    assert(g_esi==5 && g_edi==6 && g_esp==512 && g_ebp==7 && g_seh_ebp==8);
    puts("PASS: callback RET on all paths; frame call stack and register preservation");
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "abi.c"
        exe = Path(tmp) / ("abi.exe" if os.name == "nt" else "abi")
        source.write_text(harness, encoding="utf-8")
        subprocess.run([cc, str(source), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()

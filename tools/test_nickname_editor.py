#!/usr/bin/env python3
"""Compile the actual editor callbacks and save/cancel path with I/O probes."""
import importlib.util
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("abi", ROOT / "tools/test_online_abi.py")
abi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(abi)


def main():
    cc = shutil.which("clang") or shutil.which("cc")
    if os.name == "nt":
        msys = Path(os.environ.get("MSYS2_ROOT", os.environ["SystemDrive"] + os.sep))
        if "MSYS2_ROOT" not in os.environ:
            msys /= "msys64"
        compiler = msys / "clang64/bin/clang.exe"
        if compiler.exists():
            cc = str(compiler)
    assert cc, "C compiler required"
    code = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "roam_name.h"
static int free_roam_nickname_edit,free_roam_nickname_character,free_roam_nickname_delete_down;
static atomic_flag free_roam_nickname_lock=ATOMIC_FLAG_INIT;
static atomic_int free_roam_nickname_action;
static char free_roam_nickname_buffer[MC3_NICKNAME_MAX+1],free_roam_nickname_error[64];
static int text_on,save_ok=1,saves,applies,selected;
static char written[25],applied[25];
static void port_build(void) {}
static void port_labels(void) {}
static void free_roam_select(int row) { selected=row; }
static void xbox_text_input_set(int on,void (*text)(const char *),void (*key)(int)) {
    text_on=on;(void)text;(void)key;
}
static int windows_port_save_nickname(const char *name) {
    if(!save_ok)return 0;
    saves++;strcpy(written,name);return 1;
}
static int roam_set_name(const char *name) { applies++;strcpy(applied,name);return 0; }
'''
    a = abi.SOURCE.index("static void nickname_lock(void)")
    b = abi.SOURCE.index("static void port_labels(void)", a)
    code += abi.SOURCE[a:b]
    code += abi.function("static void free_roam_nickname_begin(void)")
    code += abi.function("static void free_roam_nickname_finish(int save)")
    code += r'''
int main(void) {
#ifdef _WIN32
    _putenv_s("MC3_ROAM_NAME","Original");
#else
    setenv("MC3_ROAM_NAME","Original",1);
#endif
    free_roam_nickname_begin();
    assert(free_roam_nickname_edit && text_on);
    assert(!strcmp(free_roam_nickname_buffer,"Original"));
    free_roam_nickname_text(" A#=2\xc3\xa9");
    assert(!strcmp(free_roam_nickname_buffer,"Original A2"));
    free_roam_nickname_key(1);
    assert(!strcmp(free_roam_nickname_buffer,"Original A"));
    free_roam_nickname_text("123456789012345678901234567890");
    assert(strlen(free_roam_nickname_buffer)==24);
    for(int k=0;k<30;k++)free_roam_nickname_key(1);
    free_roam_nickname_text("   ");
    free_roam_nickname_finish(1);
    assert(free_roam_nickname_edit && !saves && !applies && free_roam_nickname_error[0]);
    for(int k=0;k<3;k++)free_roam_nickname_key(1);
    free_roam_nickname_text(" New_2 ");
    save_ok=0;free_roam_nickname_finish(1);
    assert(free_roam_nickname_edit && text_on && !saves && !applies);
    save_ok=1;free_roam_nickname_finish(1);
    assert(!free_roam_nickname_edit && !text_on && selected==1);
    assert(saves==1 && applies==1 && !strcmp(written,"New_2") && !strcmp(applied,"New_2"));
    free_roam_nickname_begin();free_roam_nickname_text("Discard");
    free_roam_nickname_finish(0);
    assert(!free_roam_nickname_edit && !text_on && saves==1 && applies==1);
    puts("PASS: typing/filtering, delete, limit, blank rejection, failed-save retry, live apply and cancel");
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "editor.c"
        exe = Path(tmp) / ("editor.exe" if os.name == "nt" else "editor")
        source.write_text(code, encoding="utf-8")
        subprocess.run([cc, str(source), "-I", str(ROOT / "xboxrecomp/src/net/roam"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        # Use the actual SDL event bridge: typing must not leak into game buttons.
        bridge = Path(tmp) / "text_input.c"
        bridge_exe = Path(tmp) / ("text_input.exe" if os.name == "nt" else "text_input")
        bridge.write_text(r"""
#include <assert.h>
#include <string.h>
#include <SDL.h>
extern void xbox_text_input_set(int,void (*)(const char *),void (*)(int));
extern int xbox_text_input_event(const SDL_Event *);
void xbox_host_main_call(void (*fn)(void *),void *arg) {fn(arg);}
static char typed[32];static int key;
static void text(const char *s) {strcpy(typed,s);}
static void command(int k) {key=k;}
int main(void) {
    SDL_Event e;memset(&e,0,sizeof e);
    xbox_text_input_set(1,text,command);
    e.type=SDL_TEXTINPUT;strcpy(e.text.text,"Typed_2");
    assert(xbox_text_input_event(&e)==1 && !strcmp(typed,"Typed_2"));
    e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_a;
    assert(xbox_text_input_event(&e)==1 && key==0);
    e.key.keysym.sym=SDLK_BACKSPACE;assert(xbox_text_input_event(&e)==1 && key==1);
    e.key.keysym.sym=SDLK_RETURN;assert(xbox_text_input_event(&e)==1 && key==2);
    e.key.keysym.sym=SDLK_ESCAPE;assert(xbox_text_input_event(&e)==1 && key==3);
    e.type=SDL_CONTROLLERBUTTONDOWN;assert(xbox_text_input_event(&e)==0);
    xbox_text_input_set(0,0,0);e.type=SDL_KEYDOWN;
    assert(xbox_text_input_event(&e)==0);
    return 0;
}
""", encoding="utf-8")
        if os.name == "nt":
            prefix=Path(cc).parent.parent
            flags=["-I",str(prefix/"include/SDL2"),"-L",str(prefix/"lib"),"-lSDL2"]
        else:
            flags=shlex.split(subprocess.check_output(["pkg-config","--cflags","--libs","sdl2"]).decode("utf-8"))
        subprocess.run([cc,str(bridge),str(ROOT/"xboxrecomp/src/input/sdl_keyboard.c"),
                        "-DXBOXRECOMP_SDL_HOST=1","-DSDL_MAIN_HANDLED=1","-I",str(ROOT/"xboxrecomp/src"),"-o",str(bridge_exe),*flags],check=True)
        env=dict(os.environ);env["PATH"]=str(Path(cc).parent)+os.pathsep+env.get("PATH","")
        subprocess.run([str(bridge_exe)],env=env,check=True)
        print("PASS: SDL typing, backspace, save/cancel, keyboard isolation and controller passthrough")


if __name__ == "__main__":
    main()

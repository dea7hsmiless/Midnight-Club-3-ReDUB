#!/usr/bin/env python3
"""Rebuild changed iOS host sources and link the existing generated game objects.
The full recomp and SDL static archives must already exist from the baseline build.
"""
import os, re, subprocess
from pathlib import Path
PROJ=Path(__file__).resolve().parents[1]
BUILD=Path(os.environ['TEMP'])/'iosbuild'
SDK=Path(os.environ.get('IOS_SDK',r'E:\AI\iosc\xcode\Platforms\iPhoneOS.platform\Developer\SDKs\iPhoneOS.sdk'))
CLANG=Path(os.environ.get('IOS_CLANG',r'C:\Users\Donte\AppData\Local\Programs\Swift\Toolchains\6.4.1+Asserts\usr\bin\clang.exe'))
LINK=Path(os.environ.get('IOS_LINK_TOOLS',r'E:\AI\iosc\tools'))
SDL=Path(os.environ.get('IOS_SDL_SOURCE',r'C:\Users\Donte\Documents\Codex\ios-toolchain-references\SDL2'))
# One version for every package: config/remix/app.env APP_VERSION (the same key
# Windows/Linux use). The build id carries it so the log banner identifies the
# exact binary without a separate number to bump.
_appenv=(PROJ/'config/remix/app.env').read_text(encoding='utf8',errors='replace')
_match=re.search(r'^APP_VERSION="([^"]+)"',_appenv,re.M)
VERSION=_match.group(1) if _match else 'dev'
base=[str(CLANG),'-target','arm64-apple-ios16.0','-isysroot',str(SDK),'-O2','-D_GNU_SOURCE','-DMC3_APP="mcdub3remix"',f'-DMC3_VERSION="{VERSION}"',f'-DMC3_BUILD_ID="ios-v{VERSION}"']
includes=['src','config/remix','work-remix/gen','xboxrecomp/include','xboxrecomp/src']+['xboxrecomp/src/'+s for s in ['platform','kernel','d3d','nv2a','audio','input','video','apu','usb','nv2a_gl']]
for p in [PROJ/s for s in includes]+[SDL/'include']:base+=['-I',str(p)]
subprocess.run([os.sys.executable,str(PROJ/'ios/prepare_sdl_scenes.py')],check=True)
subprocess.run(['cmake','--build',str(BUILD.parent/'iossdl'),'--target','SDL2-static'],check=True)
for name in ['main.c','ios_host.c','ios_xdvdfs.c','ios_launcher.m','ios_scene.m']:
    args=base+(['-fobjc-arc','-fblocks'] if name.endswith('.m') else [])
    subprocess.run(args+['-c',str(PROJ/'src'/name),'-o',str(BUILD/(Path(name).stem+'.o'))],check=True)
# The iOS backing-store implementation is part of the launcher/runtime fix and
# must not silently reuse the older object from the baseline full recomp.
subprocess.run(base+['-c',str(PROJ/'xboxrecomp/src/platform/win32_compat.c'),
                     '-o',str(BUILD/'xr'/'win32_compat.o')],check=True)
# The fixed guest arena is implemented here. Reusing the baseline object made
# device builds silently omit the source-level iOS mapping changes.
subprocess.run(base+['-c',str(PROJ/'xboxrecomp/src/kernel/xbox_memory_layout.c'),
                     '-o',str(BUILD/'xr'/'k_xbox_memory_layout.o')],check=True)
# Background presentation is gated inside the renderer on iOS. Force this
# object to rebuild so the packaged app cannot reuse the baseline copy.
subprocess.run(base+['-c',str(PROJ/'xboxrecomp/src/nv2a_gl/nv2a_gl.c'),
                     '-o',str(BUILD/'xr'/'gl_nv2a_gl.o')],check=True)
# The entry-point table is generated from gl_api.h. A new entry point (the
# feedback snapshot's glCopyTexSubImage2D) must not link against the baseline
# object, which does not define it.
subprocess.run(base+['-c',str(PROJ/'xboxrecomp/src/nv2a_gl/gl_api.c'),
                     '-o',str(BUILD/'xr'/'gl_gl_api.o')],check=True)
# Call tracing wraps GL entry points and must not drain the errors the DXT
# upload probes read; keep the two in step.
subprocess.run(base+['-c',str(PROJ/'xboxrecomp/src/nv2a_gl/gl_check.c'),
                     '-o',str(BUILD/'xr'/'gl_gl_check.o')],check=True)
# HD-pack startup diagnostics (missing folder, loaded count) live here.
subprocess.run(base+['-c',str(PROJ/'xboxrecomp/src/nv2a_gl/hd_pack.c'),
                     '-o',str(BUILD/'xr'/'gl_hd_pack.o')],check=True)
# The on-screen pad defaults to auto on iOS and now reports its mode. The
# cached object from before that change would silently ship a pad that never
# appears, the same stale-object trap as the v7/v8 memory layout.
subprocess.run(base+['-c',str(PROJ/'xboxrecomp/src/input/touch_pad.c'),
                     '-o',str(BUILD/'xr'/'input_touch_pad.o')],check=True)
# The vibration strength is cached in the input layer; the in-game Port
# Options can change it, so this object must not come from the baseline.
subprocess.run(base+['-c',str(PROJ/'xboxrecomp/src/input/xinput_device.c'),
                     '-o',str(BUILD/'xr'/'input_xinput_device.o')],check=True)
# Port Options' row order and labels are overridden in the manual file; the
# baseline object still carries the desktop row list.
subprocess.run(base+['-c',str(PROJ/'src/games/remix/recomp_manual.c'),
                     '-o',str(BUILD/'manual.o')],check=True)
nm=CLANG.parent/'llvm-nm.exe'
symbols=subprocess.check_output([str(nm),str(BUILD/'main.o')],text=True)
assert ' U _ios_launcher_run' in symbols and ' T _SDL_main' in symbols, 'Missing SDL launcher entry point'
layout_strings=subprocess.check_output([str(CLANG.parent/'llvm-strings.exe'),str(BUILD/'xr'/'k_xbox_memory_layout.o')],text=True)
assert 'guest arena reservation' in layout_strings, 'Stale iOS memory-layout object'
renderer_symbols=subprocess.check_output([str(nm),str(BUILD/'xr'/'gl_nv2a_gl.o')],text=True)
assert ' U _ios_scene_is_active' in renderer_symbols, 'Missing iOS background rendering gate'
pad_strings=subprocess.check_output([str(CLANG.parent/'llvm-strings.exe'),str(BUILD/'xr'/'input_touch_pad.o')],text=True)
assert 'on-screen pad:' in pad_strings, 'Stale iOS on-screen pad object'
assert 'pedals %s' in pad_strings, 'Stale iOS pedal options object'
pad_symbols=subprocess.check_output([str(nm),str(BUILD/'xr'/'input_touch_pad.o')],text=True)
assert ' T _xbox_touchpad_image' in pad_symbols, 'On-screen pad object missing its overlay entry point'
objects=list(BUILD.glob('*.o'))+list((BUILD/'gen').glob('*.o'))+list((BUILD/'xr').glob('*.o'))
objects=[p for p in objects if p.name not in ['nv2a_nv2a_pb_replay.o','nv2a_nv2a_pb_test.o']]
frameworks=['CoreAudio','AudioToolbox','AVFoundation','CoreMotion','GameController','CoreHaptics','Metal','Foundation','UIKit','OpenGLES','CoreBluetooth','QuartzCore','CoreGraphics','UniformTypeIdentifiers']
cmd=base+[f'-B{LINK}'+os.sep,'-fuse-ld=lld','-Wl,--error-limit=0','-o',str(BUILD/'mcdub3-launcher')]+[str(p) for p in objects]+[str(BUILD.parent/'iossdl'/'libSDL2main.a'),str(BUILD.parent/'iossdl'/'libSDL2.a')]
for f in frameworks:cmd+=['-framework',f]
subprocess.run(cmd+['-lobjc','-lz','-lm'],check=True)
print('Built',BUILD/'mcdub3-launcher')

import shutil
(PROJ/'ios'/'bin').mkdir(exist_ok=True)
shutil.copy2(BUILD/'mcdub3-launcher',PROJ/'ios'/'bin'/'mcdub3-launcher')

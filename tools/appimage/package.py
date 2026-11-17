#!/usr/bin/env python3
"""Stage a self-contained AppDir with a recursive ELF dependency closure.
Host-owned glibc, GL/EGL/Vulkan, GBM and DRM remain host dependencies.
Run inside the release build container after build-release.sh.
"""
import hashlib,json,os,re,shutil,subprocess
from pathlib import Path
W=Path('/work'); A=W/'AppDir'; P=W/'packaging'
assert str(A)=='/work/AppDir'
if A.exists():shutil.rmtree(A)
for d in ['usr/bin','usr/lib','usr/share/applications','usr/share/metainfo','usr/share/doc/redub','usr/share/licenses/redub']:(A/d).mkdir(parents=True,exist_ok=True)
def run(*args,**kwargs):return subprocess.check_output(args,text=True,**kwargs).strip()
def copy(src,dst):
 dst=Path(dst);dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
copy(W/'linux-build/mcdub3remix',A/'usr/bin/mcdub3remix')
subprocess.run(['cc',str(P/'redub-message.c'),'-o',str(A/'usr/bin/redub-message')]+run('pkg-config','--cflags','--libs','sdl2').split(),check=True)
for f in ['AppRun','redub.png']:copy(P/f,A/f)
copy(P/'io.github.dea7hsmiless.ReDUB.desktop',A/'usr/share/applications/io.github.dea7hsmiless.ReDUB.desktop')
copy(P/'io.github.dea7hsmiless.ReDUB.metainfo.xml',A/'usr/share/metainfo/io.github.dea7hsmiless.ReDUB.appdata.xml')
copy(P/'io.github.dea7hsmiless.ReDUB.desktop',A/'io.github.dea7hsmiless.ReDUB.desktop')
# Preserve all original icon sizes from the project's existing icon.
# PNGs are generated on the host from the original ICO, with no artwork changes.
for f in (P/'icons').glob('*.png'):
 size=f.stem;copy(f,A/f'usr/share/icons/hicolor/{size}x{size}/apps/redub.png')
icon=A/'.DirIcon'
if icon.exists() or icon.is_symlink():icon.unlink()
icon.symlink_to('redub.png')
logo_names=['22762f0e2f14cca4.png','7680ba077867b3e3.png','851e07656bc1c45f.png']
for name in logo_names:copy(W/'release-source/hd/textures'/name,A/'hd/textures'/name)
copy(W/'release-source/LICENSE',A/'usr/share/licenses/redub/LICENSE')
copy(W/'release-source/xboxrecomp/LICENSE',A/'usr/share/licenses/redub/xboxrecomp-LICENSE')
copy(W/'release-source/xboxrecomp/NOTICE',A/'usr/share/licenses/redub/xboxrecomp-NOTICE')
shutil.copytree(W/'release-source/xboxrecomp/LICENSES',A/'usr/share/licenses/redub/xboxrecomp',dirs_exist_ok=True)
copy(W/'ffmpeg-6.1.4/COPYING.LGPLv2.1',A/'usr/share/licenses/redub/FFmpeg-LGPL-2.1.txt')
copy(W/'ffmpeg-build/ffbuild/config.mak',A/'usr/share/doc/redub/ffmpeg-config.mak')
env=dict(os.environ,LD_LIBRARY_PATH='/work/ffmpeg-install/lib')
exclude=re.compile(r'^(ld-linux.*|lib(c|m|dl|rt|pthread|resolv|util|anl)\.so\..*|lib(GL|EGL|GLX|GLdispatch|OpenGL|vulkan|gbm|drm).*\.so.*)$')
queue=[A/'usr/bin/mcdub3remix',A/'usr/bin/redub-message']; seen=set(); bundled={}; host={}
while queue:
 f=queue.pop()
 for line in run('ldd',str(f),env=env).splitlines():
  if 'not found' in line:raise RuntimeError(f'{f}: {line}')
  match=re.match(r'\s*(\S+)\s+=>\s+(/\S+)',line)
  if not match:continue
  soname,path=match.groups()
  if exclude.match(soname):host[soname]=path;continue
  if soname in seen:continue
  seen.add(soname); dest=A/'usr/lib'/soname;copy(path,dest);queue.append(Path(path))
  real=Path(path).resolve(); bundled[soname]={'origin':str(real),'sha256_original':hashlib.sha256(real.read_bytes()).hexdigest()}
  if str(real).startswith('/work/ffmpeg-install'):
   bundled[soname]['source']='FFmpeg 6.1.4, Bink-only LGPL build';continue
  owner=None
  for candidate in [str(real),str(real).replace('/usr/lib/','/lib/',1),path]:
   q=subprocess.run(['dpkg-query','-S',candidate],capture_output=True,text=True)
   if q.returncode==0:owner=q.stdout.split(': ')[0].splitlines()[0];break
  if owner:
   info=run('dpkg-query','-W','-f=${source:Package}\t${source:Version}\t${Version}',owner).split('\t')
   bundled[soname].update(package=owner,source_package=info[0],source_version=info[1],binary_version=info[2])
   notice=Path('/usr/share/doc')/owner.split(':')[0]/'copyright'
   if notice.exists():copy(notice,A/'usr/share/licenses/redub'/f'{owner.replace(":","_")}.copyright')
  else:raise RuntimeError(f'No source package owner for {real}')
for f in list((A/'usr/bin').iterdir())+list((A/'usr/lib').iterdir()):
 if f.is_dir():continue
 subprocess.run(['patchelf','--set-rpath','$ORIGIN/../lib' if f.parent.name=='bin' else '$ORIGIN',str(f)],check=True)
 f.chmod(0o755)
(A/'AppRun').chmod(0o755)
shutil.copytree('/usr/share/common-licenses',A/'usr/share/licenses/redub/common',dirs_exist_ok=True)
source_commit=os.environ.get('REDUB_SOURCE_COMMIT') or ((W/'source-commit.txt').read_text().strip() if (W/'source-commit.txt').exists() else 'unknown')
manifest={'source_commit':source_commit,'base_commit':'741a35facc54530b73f45ec87208015b756b3e84','version':'0.2.2','baseline':'Ubuntu 24.04 x86_64 / glibc 2.39','bundled_libraries':bundled,'host_libraries':host,'display_backend':'X11 / XWayland (native Wayland unsupported)','patches':['src/games/remix/recomp_manual.c: Port Options mixed-case entry and title','src/linux_settings.c: MC3_SETTINGS_FILE override','xboxrecomp/src/kernel/kernel_path.c: Linux Partition1 title metadata uses user save profile'],'bundled_logo_textures':{name:hashlib.sha256((A/'hd/textures'/name).read_bytes()).hexdigest() for name in logo_names},'renderer':'OpenGL; experimental 60/120 FPS changes excluded'}
for dest in [A/'usr/share/doc/redub/build-manifest.json',W/'build-manifest.json']:dest.write_text(json.dumps(manifest,indent=2)+'\n')
subprocess.run(['desktop-file-validate',str(A/'io.github.dea7hsmiless.ReDUB.desktop')],check=True)
# appstream may emit non-blocking recommendations. A validation failure remains fatal.
subprocess.run(['appstreamcli','validate','--no-net',str(A/'usr/share/metainfo/io.github.dea7hsmiless.ReDUB.appdata.xml')],check=True)
print(f'Staged {len(bundled)} shared libraries; {len(host)} host libraries excluded.')

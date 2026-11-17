import json,os,subprocess,hashlib
from pathlib import Path
w=Path('/work');d=w/'dependency-sources';d.mkdir(exist_ok=True)
p=Path('/etc/apt/sources.list.d/ubuntu.sources');p.write_text(p.read_text().replace('Types: deb\n','Types: deb deb-src\n'))
subprocess.run(['apt-get','update','-qq'],check=True)
m=json.loads((w/'build-manifest.json').read_text())
sources=sorted({(v['source_package'],v['source_version']) for v in m['bundled_libraries'].values() if 'source_package' in v})
for name,version in sources:
 print('Source',name,version,flush=True)
 subprocess.run(['apt-get','source','--download-only','--only-source',f'{name}={version}'],cwd=d,check=True)
lock=[]
for p in sorted(d.iterdir()):
 if p.is_file():lock.append({'file':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(d/'SHA256SUMS.txt').write_text(''.join(f"{v['sha256']}  {v['file']}\n" for v in lock))
(w/'source-packages.json').write_text(json.dumps({'ubuntu_sources':sources,'archives':lock},indent=2)+'\n')
print('SOURCES_OK',flush=True)

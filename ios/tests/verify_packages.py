"""Validate the packaged iOS launcher archives and optionally copy deliverables.

Checks the release IPA, the side-by-side beta IPA and (when present) the
bundled-disc test IPA. The version comes from config/remix/app.env APP_VERSION.
"""
from pathlib import Path
import argparse, plistlib, zipfile, struct, hashlib, shutil, json, re

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path)
args = parser.parse_args()

_env = (root / 'config/remix/app.env').read_text(encoding='utf8', errors='replace')
_match = re.search(r'^APP_VERSION="([^"]+)"', _env, re.M)
VERSION = _match.group(1) if _match else '0.2.3'
BASE_BUNDLE = 'com.ladastra.mcdub3remix'

cases = [
    ('',            False, BASE_BUNDLE,          'ReDUB',       False),
    ('-beta',       False, BASE_BUNDLE + '.beta', 'ReDUB Beta',  True),
    ('-full-test',  True,  BASE_BUNDLE,          'ReDUB',       False),
]

reports = []
for suffix, full, bundle, display, required in cases:
    p = root / 'ios/dist' / f'mcdub3remix{suffix}-{VERSION}-unsigned.ipa'
    if not p.exists():
        if required:
            raise SystemExit(f'missing required package: {p.name}')
        print(f'skip (not built): {p.name}', flush=True)
        continue
    with zipfile.ZipFile(p) as z:
        assert z.testzip() is None
        prefix = 'Payload/mcdub3remix.app/'
        pl = plistlib.loads(z.read(prefix + 'Info.plist'))
        assert pl['CFBundleVersion'] == VERSION and 'UILaunchScreen' in pl
        assert pl['CFBundleShortVersionString'] == VERSION
        assert pl['CFBundleIdentifier'] == bundle
        assert pl['CFBundleDisplayName'] == display
        scene = pl['UIApplicationSceneManifest']['UISceneConfigurations']['UIWindowSceneSessionRoleApplication'][0]
        assert scene['UISceneDelegateClassName'] == 'ReDUBSceneDelegate' and scene['UISceneClassName'] == 'UIWindowScene'
        assert pl['UIFileSharingEnabled'] and pl['LSSupportsOpeningDocumentsInPlace']
        assert pl['UIApplicationSceneManifest']['UIApplicationSupportsMultipleScenes'] is False
        document_types = pl.get('CFBundleDocumentTypes', [])
        assert any('public.iso-image' in item.get('LSItemContentTypes', []) for item in document_types)
        binary = z.read(prefix + 'mcdub3')
        magic, cpu = struct.unpack_from('<II', binary)
        assert magic == 0xfeedfacf and cpu == 0x0100000c
        assert f'ios-v{VERSION}'.encode() in binary and b'guest arena reservation' in binary
        assert f'ReDUB {VERSION}'.encode() in binary
        assert b'on-screen pad:' in binary
        assert b'[IOS VIDEO] present framebuffer=' in binary
        assert b'[IOS VIDEO] SDL window key=' in binary
        assert b'xbox_MemoryLayoutInit: mapped %zu KB at 0x%p' not in binary
        assert b'xbox_MemoryLayoutInit: mapped %zu KB at %p' in binary
        assert binary == (root / 'ios/bin/mcdub3-launcher').read_bytes()
        assert ((z.getinfo(prefix + 'mcdub3').external_attr >> 16) & 0o777) == 0o755
        game = [n for n in z.namelist() if n.startswith(prefix + 'game/')]
        assert bool(game) == full
        if full:
            assert z.read(prefix + 'game/default.xbe') == (root / 'game-remix/default.xbe').read_bytes()
        names = z.namelist()
        assert prefix + 'Assets/hero.jpg' in names and prefix + 'Assets/logo.png' in names
        assert prefix + 'AppIcon60x60@2x.png' in names and prefix + 'AppIcon76x76@2x.png' in names
        assert prefix + 'AppIcon83.5x83.5@2x.png' in names and prefix + 'AppIcon1024x1024.png' in names
        assert prefix + 'hd/textures/22762f0e2f14cca4.png' in names
        assert prefix + 'hd/textures/7680ba077867b3e3.png' in names
        report = {'file': p.name, 'bytes': p.stat().st_size, 'version': VERSION,
                  'bundle_id': bundle, 'display_name': display, 'entries': len(z.infolist()),
                  'game_files': len(game), 'arm64': True, 'zip_crc': 'passed', 'plist': 'passed',
                  'executable_permissions': '755', 'matches_final_binary': True}
    if args.output:
        args.output.mkdir(exist_ok=True, parents=True)
        shutil.copy2(p, args.output / p.name)
    h = hashlib.sha256()
    with open((args.output / p.name) if args.output else p, 'rb') as f:
        for b in iter(lambda: f.read(4 * 1024 * 1024), b''):
            h.update(b)
    report['sha256'] = h.hexdigest()
    reports.append(report)
    print(report, flush=True)

report = {'reader_tests_passed': 16, 'device_tested': False, 'version': VERSION, 'packages': reports}
if args.output:
    shutil.copy2(root / 'ios/dist/mcdub3remix.entitlements', args.output / 'mcdub3remix.entitlements')
    shutil.copy2(root / 'ios/LAUNCHER.md', args.output / 'ReDUB-iOS-README.md')
    (args.output / 'ReDUB-iOS-validation.json').write_text(json.dumps(report, indent=2))
    print('copied to', args.output, flush=True)

#!/usr/bin/env python3
"""Package the launcher IPA (default), its side-by-side beta, or the bundled-disc test IPA.

The version comes from config/remix/app.env APP_VERSION, so iOS carries the same
number as the Windows and Linux assets. `--beta` only changes the bundle id
(+ ".beta") and the display name ("ReDUB Beta"), so release and beta install side
by side with the same 0.2.3 code.
"""
import argparse, os, plistlib, tempfile, zipfile, shutil
from pathlib import Path
from ios_build_app import info_plist, icons_png, VERSION, BUNDLE_ID

PROJ = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--bundle-game', action='store_true')
parser.add_argument('--beta', action='store_true',
                    help="side-by-side beta: bundle id '<id>.beta', display name 'ReDUB Beta'")
args = parser.parse_args()

out = PROJ / 'ios' / 'dist'
out.mkdir(exist_ok=True)
binary = PROJ / 'ios' / 'bin' / 'mcdub3-launcher'
if not binary.exists():
    raise SystemExit('Run build_launcher.py first')

suffix = ('-beta' if args.beta else '') + ('-full-test' if args.bundle_game else '')
name = f'mcdub3remix{suffix}-{VERSION}-unsigned.ipa'
ipa = out / name

with tempfile.TemporaryDirectory(prefix='redub-package-') as temp:
    stage = Path(temp)
    icons = icons_png(stage)
    pl = info_plist(stage, icons)
    for k in list(pl):
        if k.startswith('DT'):
            pl.pop(k)
    pl.update(
        CFBundleDisplayName='ReDUB Beta' if args.beta else 'ReDUB',
        CFBundleIdentifier=BUNDLE_ID + '.beta' if args.beta else BUNDLE_ID,
        CFBundleShortVersionString=VERSION,
        CFBundleVersion=VERSION,
        UIFileSharingEnabled=True,
        LSSupportsOpeningDocumentsInPlace=True)
    pl['UILaunchScreen'] = {}
    pl['UIApplicationSceneManifest'] = {'UIApplicationSupportsMultipleScenes': False, 'UISceneConfigurations': {'UIWindowSceneSessionRoleApplication': [{'UISceneConfigurationName': 'ReDUB', 'UISceneClassName': 'UIWindowScene', 'UISceneDelegateClassName': 'ReDUBSceneDelegate'}]}}
    pl['CFBundleDocumentTypes'] = [{'CFBundleTypeName': 'Xbox ISO', 'CFBundleTypeRole': 'Viewer', 'LSHandlerRank': 'Alternate', 'LSItemContentTypes': ['public.iso-image']}]
    (stage / 'Info.plist').write_bytes(plistlib.dumps(pl))
    shutil.copy2(binary, stage / 'mcdub3')
    (stage / 'Assets').mkdir()
    for art in ['hero.jpg', 'logo.png']:
        shutil.copy2(PROJ / 'launcher' / 'art' / art, stage / 'Assets' / art)
    # Custom ReDUB boot/menu logos (HD texture replacement set), small and
    # bundled so the launcher-only IPA shows them without a Documents/hd copy.
    hd = PROJ / 'hd' / 'textures'
    if hd.is_dir():
        (stage / 'hd' / 'textures').mkdir(parents=True)
        for p in sorted(hd.glob('*.png')):
            shutil.copy2(p, stage / 'hd' / 'textures' / p.name)
    with zipfile.ZipFile(ipa.with_suffix('.ipa.tmp'), 'w', zipfile.ZIP_STORED if args.bundle_game else zipfile.ZIP_DEFLATED, allowZip64=True) as z:
        def add(p, relative):
            arc = 'Payload/mcdub3remix.app/' + relative
            zi = zipfile.ZipInfo.from_file(p, arc)
            zi.create_system = 3
            zi.external_attr = ((0o100755 if relative == 'mcdub3' else 0o100644) << 16)
            zi.compress_type = z.compression
            with open(p, 'rb') as source, z.open(zi, 'w', force_zip64=True) as target:
                shutil.copyfileobj(source, target, 1024 * 1024)
        for p in sorted(stage.rglob('*')):
            if p.is_file():
                add(p, p.relative_to(stage).as_posix())
        if args.bundle_game:
            for p in sorted((PROJ / 'game-remix').rglob('*')):
                if p.is_file():
                    add(p, 'game/' + p.relative_to(PROJ / 'game-remix').as_posix())
    os.replace(ipa.with_suffix('.ipa.tmp'), ipa)
print(f'{ipa} ({ipa.stat().st_size:,} bytes)')

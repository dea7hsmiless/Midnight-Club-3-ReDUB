#!/usr/bin/env python3
"""Release-package guard: never ship a player's settings file.

The 0.2.4a Windows zip was built from a dist that still held this machine's
``mcdub3remix_env.txt`` (with ``MC3_PC_MUSIC=1``), so every fresh extract
started with PC music on.  This check fails a package that carries a settings
file, that has active values in the shipped example (the game seeds the real
file from it), or that turns PC music on in a defaults file or a launch script.

Usage:
    python tools/check-release-package.py <dir|zip|tar.gz> [...]
    python tools/check-release-package.py --self-test

Exit 0: every package is clean.  Exit 1: at least one FAIL line above.
"""
from __future__ import annotations

import argparse
import io
import re
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

LIMIT = 4 << 20                      # settings/scripts are tiny; skip big binaries
PC_MUSIC_ON = re.compile(
    rb"""(?mi)^\s*(?:set\s+|export\s+)?["']?MC3_PC_MUSIC\s*=\s*["']?1["']?\s*$"""
)


def _base(name: str) -> str:
    return name.replace("\\", "/").rsplit("/", 1)[-1].lower()


def _needs_content(name: str) -> bool:
    base = _base(name)
    return (
        base.endswith("_env.example.txt")
        or base.endswith(("_defaults.txt", "defaults_env.txt"))
        or base == "apprun"
        or base.endswith((".bat", ".cmd", ".sh"))
    )


def _active_lines(data: bytes) -> list[str]:
    """Uncommented KEY=VALUE lines: what the game would actually read."""
    out: list[str] = []
    for raw in data.decode("latin-1").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith(";"):
            continue
        if "=" in line:
            out.append(line)
    return out


def _scan(rel: str, data: bytes | None, findings: list[str]) -> None:
    base = _base(rel)

    # A real settings file must never be in a package: an update unzips over
    # the install, replacing what the player set.  The game creates it from
    # the example on first run.
    if base.endswith("_env.txt"):
        findings.append(f"player settings file shipped: {rel}")
        return

    # The launcher's own backup of the settings file it is rewriting. Same
    # player state as above, and it is not covered by the _env.txt test because
    # the name ends with .launcher-backup.
    if ".launcher-backup" in base:
        findings.append(f"player settings backup shipped: {rel}")
        return

    if data is None:
        return

    if base.endswith("_env.example.txt"):
        active = _active_lines(data)
        if active:
            findings.append(
                f"{rel} has active defaults ({active[0]!r}); keep the example commented"
            )
    if base.endswith(("_defaults.txt", "defaults_env.txt")) and PC_MUSIC_ON.search(data):
        findings.append(f"{rel} turns PC music on (MC3_PC_MUSIC=1)")
    if (base == "apprun" or base.endswith((".bat", ".cmd", ".sh"))) and PC_MUSIC_ON.search(data):
        findings.append(f"{rel} sets MC3_PC_MUSIC=1")


def _scan_dir(root: Path, findings: list[str]) -> None:
    for path in sorted(root.rglob("*")):
        if path.is_dir() or path.is_symlink():
            continue
        rel = str(path.relative_to(root))
        data = None
        if _needs_content(rel) and path.stat().st_size <= LIMIT:
            data = path.read_bytes()
        _scan(rel, data, findings)


def _scan_zip(path: Path, findings: list[str]) -> None:
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            if info.is_dir():
                continue
            data = None
            if _needs_content(info.filename) and info.file_size <= LIMIT:
                with archive.open(info) as handle:
                    data = handle.read()
            _scan(info.filename, data, findings)


def _scan_tar(path: Path, findings: list[str]) -> None:
    with tarfile.open(path, "r:*") as archive:
        for member in archive.getmembers():
            if not member.isfile():
                continue
            data = None
            if _needs_content(member.name) and member.size <= LIMIT:
                extracted = archive.extractfile(member)
                if extracted is not None:
                    data = extracted.read()
            _scan(member.name, data, findings)


def scan(path: Path) -> list[str]:
    findings: list[str] = []
    if path.is_dir():
        _scan_dir(path, findings)
    elif zipfile.is_zipfile(path):
        _scan_zip(path, findings)
    elif tarfile.is_tarfile(path):
        _scan_tar(path, findings)
    else:
        raise SystemExit(f"{path}: not a directory, zip or tar archive")
    return findings


def self_test() -> int:
    """Exercise the failing and passing shapes; used by CI."""
    import zipfile as zf

    cases: list[tuple[str, dict[str, bytes], bool]] = [
        (
            "clean dist",
            {
                "mcdub3remix_defaults.txt": b"RECOMP_GL_SCALE=2\r\n",
                "mcdub3remix_env.example.txt": b"#MC3_PC_MUSIC=0\r\n# a comment\r\n",
                "run_mcdub3remix.bat": b"set MC3_RESOLUTION=auto\r\n",
            },
            False,
        ),
        (
            "settings file",
            {"mcdub3remix_env.txt": b"MC3_PC_MUSIC=0\r\n"},
            True,
        ),
        (
            "active example",
            {"mcdub3remix_env.example.txt": b"MC3_PC_MUSIC=0\r\n"},
            True,
        ),
        (
            "defaults turn music on",
            {"mcdub3remix_defaults.txt": b"MC3_PC_MUSIC=1\r\n"},
            True,
        ),
        (
            "linux defaults turn music on",
            {"defaults_env.txt": b"MC3_PC_MUSIC=1\n"},
            True,
        ),
        (
            "script turns music on",
            {"run_mcdub3remix.bat": b'set "MC3_PC_MUSIC=1"\r\n'},
            True,
        ),
    ]
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)
        for name, files, want_fail in cases:
            root = base / name.replace(" ", "-")
            for rel, data in files.items():
                target = root / rel
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            got = bool(scan(root))
            status = "ok" if got == want_fail else "FAIL"
            print(f"{status:4} self-test: {name} ({'fails' if got else 'passes'})")
            failed += got != want_fail

            archive = base / (root.name + ".zip")
            with zf.ZipFile(archive, "w") as z:
                for rel, data in files.items():
                    z.writestr(rel, data)
            got = bool(scan(archive))
            status = "ok" if got == want_fail else "FAIL"
            print(f"{status:4} self-test: {name} as zip ({'fails' if got else 'passes'})")
            failed += got != want_fail
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("packages", nargs="*", help="directory, .zip or .tar.gz to audit")
    parser.add_argument("--self-test", action="store_true", help="test the checker itself")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if not args.packages:
        parser.error("give at least one package (or --self-test)")

    failed = False
    for raw in args.packages:
        path = Path(raw)
        findings = scan(path)
        if findings:
            failed = True
            print(f"FAIL  {path}")
            for finding in findings:
                print(f"      {finding}")
        else:
            print(f"ok    {path}: no player settings, no PC-music default")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

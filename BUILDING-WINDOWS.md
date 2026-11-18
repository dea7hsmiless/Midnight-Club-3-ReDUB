# ReDUB Windows setup

Open **ReDUB Launcher.exe**, or **Launch ReDUB.cmd** when building the launcher from source.

## Normal setup

1. Open **Add game files**.
2. Choose your own original Xbox Remix ISO/XISO or the extracted folder containing `default.xbe`.
3. Select **Add game files**. The folder is copied, or the ISO is unpacked by the built-in reader, into `game-remix/`. The `default.xbe` is checked against the supported Remix editions.
4. Select **Play**. The release zip already contains the game executable and runtime; nothing is compiled, and no MSYS2, CMake or internet download is used.

An existing installation can play immediately. Replacing game files keeps saves and settings. If the release folder is missing `work-remix/build-windows-x86_64/dist/mcdub3remix.exe`, use the full ReDUB Windows zip or **Settings → Advanced → Rebuild from source**.

## Advanced: rebuild from source

**Settings → Advanced → Rebuild from source** compiles the game port locally. It needs MSYS2, about 10 GB free and 20+ minutes; the first run downloads missing tools. Use a writable folder. For paths containing spaces, the build helper creates a junction alias for upstream tools; source and build output remain in the installation folder. Save and close the game first. Game files, saves, and settings are preserved. **Repair build** rebuilds the runtime; **Check prerequisites** reports what is missing. The launcher also includes **Check setup**, **Create shortcuts**, logs and the installed folder. No automatic GitHub pull is performed.

Command line:

```
powershell -File .portable\Build.ps1 -Mode Import -Disc <extracted-folder>
powershell -File .portable\Build.ps1 -Mode Build|Rebuild|Doctor
ReDUB Launcher.exe --import <folder-or-iso>
ReDUB Launcher.exe --check
ReDUB Launcher.exe --play
ReDUB Launcher.exe --build
```

`-Mode Import` copies an extracted folder only; ISO import lives in the launcher's built-in reader. `-NoInstall` stages a build. Build log: `work-remix/logs/easy-build.log`. Launcher CLI log: `work-remix/logs/launcher-cli.log`. Backups: `work-remix/build-backups/`.

Move the complete project together. Location-specific caches avoid stale CMake paths; recreate shortcuts after a move.

Rebuild the launcher itself with `.portable/Build-Launcher.ps1`. This uses the Windows .NET Framework compiler (including `.portable/Xdvdfs.cs`) and does not need Python, Node, or a separate launcher runtime download. The release build uses a standard `asInvoker` Windows application manifest and deterministic compiler settings; it does not request administrator access or install a background updater. See [README.md](README.md) for included features and limitations.

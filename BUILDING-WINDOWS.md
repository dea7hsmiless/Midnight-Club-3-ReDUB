# ReDUB Windows setup

Open **ReDUB Launcher.exe**, or **Launch ReDUB.cmd** when building the launcher from source. For a new installation, choose your own Xbox Remix ISO or extracted folder and select **Build / Repair**. For an existing installation, select **Play**.

The first build downloads missing tools. Later builds reuse conversion and compiler caches. Use a writable folder. For a path containing spaces, the build helper creates a junction alias for upstream tools; source and build output remain in the installation folder. Save and close the game before installing a build; game files, saves, and existing settings are preserved.

The launcher also includes **Check setup**, **Rebuild game code**, **Create shortcuts**, and access to logs and the installed folder. No automatic GitHub pull is performed.

The legacy command files still work:

```powershell
& '.\Build - Midnight Club 3 Remix.cmd' -Disc 'D:\MyGame\Remix.iso'
& '.\Build - Midnight Club 3 Remix.cmd' -Disc 'D:\MyGame\ExtractedRemix'
```

Use `-Mode Rebuild` for a full conversion, `-Mode Doctor` to check setup, or `-NoInstall` to stage a game build. Build log: `work-remix/logs/easy-build.log`. Backups: `work-remix/build-backups/`.

Move the complete project together. Location-specific caches avoid stale CMake paths; recreate shortcuts after a move.

Rebuild the launcher itself with `.portable/Build-Launcher.ps1`. This uses the Windows .NET Framework compiler and does not need Python, Node, or a separate launcher runtime download. See [README.md](README.md) for included features and limitations.

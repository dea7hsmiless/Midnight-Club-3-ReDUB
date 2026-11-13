# Launcher artwork

The project owner supplied `hero.jpg` and the ReDUB `logo.png` and confirmed permission to publish both with this source package. This does not transfer third-party trademarks or artwork ownership. No SafeHouse artwork is included.

The logo preserves its enclosed white badge and lettering, with the outer canvas transparent. The launcher fits it without stretching and crops the background to fill its artwork area.

Artwork is embedded in the launcher so moving the EXE preserves it. Local `hero.jpg`/`hero.png` and `logo.png` files can override embedded artwork. Restart the launcher after changing them. If artwork cannot be read, the launcher uses a gradient/text fallback.

`redub.ico` contains seven sizes from 16–256 pixels. After replacing the logo, run `.portable/Build-Icon.ps1` and `.portable/Build-Launcher.ps1` to refresh the embedded icon and shortcuts.
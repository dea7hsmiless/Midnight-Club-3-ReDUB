# Vulkan and DLSS status

Updated 2026-11-13. Short successful checks do not establish release-wide stability.

Settings → Graphics selects OpenGL or Vulkan, with DLSS Off, Quality, Balanced and Performance. The launcher builds missing dependencies when Play is selected. OpenGL and Vulkan currently use separate save profiles.

DLSS jitter is OFF by default at the user's request. Jitter-on causes stationary detail shaking; reversing projection direction added ghosting and was reverted. Jitter-off was stable in the user comparison but can reconstruct less detail. See [the deferred investigation](DLSS-JITTER-FOLLOWUP.md).

## Local verification

- All three modes passed controlled GPU evaluations on an RTX 3070 and short user driving checks with jitter off.
- Balanced sampled approximately 29.6–29.9 FPS; Performance 29.4–29.9 FPS. These are local log samples, not comparative benchmarks. Simulation remains 30 FPS.
- Small effects-buffer allocation, render-texture padding sampling and fixed-function texture matrices now follow OpenGL.
- User reports particles/markers animate correctly and earlier white borders and the blurred right-side strip are absent. No final-image crop or edge-masking workaround is applied.

Long career/race sessions, all minimap/HUD states, controller reconnects, fullscreen/resolution transitions and other GPUs/drivers remain to verify. Physical wheel testing remains outstanding; full steering force feedback is not implemented. Frame generation and 60/120 FPS simulation are deferred.

## Developer launch

Build with `.portable/Build-Vulkan-Experiment.ps1 -WithDLSS`.

```powershell
.portable/Start-Vulkan-Experiment.ps1 -DlssMode Off
.portable/Start-Vulkan-Experiment.ps1 -DlssMode Quality -PresentScene
.portable/Start-Vulkan-Experiment.ps1 -DlssMode Balanced -PresentScene
.portable/Start-Vulkan-Experiment.ps1 -DlssMode Performance -PresentScene
# Diagnostic jitter opt-in only:
.portable/Start-Vulkan-Experiment.ps1 -DlssMode Quality -PresentScene -JitterScene
```

Runtime files: `work-remix/build-windows-vulkan-experimental/dist`. OpenGL and Vulkan share Career profiles under the game directory and emulated system data under `work-remix/save`. These runtime folders are excluded from source packaging, along with NVIDIA SDK/runtime binaries, disc data, personal preferences and texture packs. On first launch after upgrading, an older `work-remix/dlss-experimental-save` folder is moved to the shared location only when that location does not yet exist, avoiding duplicate multi-gigabyte partition images.

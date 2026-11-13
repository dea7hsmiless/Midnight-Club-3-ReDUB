# Windows release checklist

Updated 2026-11-13. This records evidence rather than assuming every requested feature is validated.

## Completed checks

- OpenGL and Vulkan game builds compile.
- Shader generation/validation and temporal-history regression checks pass.
- Portable launcher artwork, extraction into its own moved folder, source completeness, settings preservation, bypass recovery and renderer/mode routing checks pass.
- Embedded source excludes disc data, SDK/runtime binaries, build output, saves and personal preferences.
- User reports Vulkan Career loading is corrected after adding GPU 2D image copies.
- User reports native Vulkan effects/markers animate correctly and right/bottom borders and right-side blur are absent after allocation/sampling parity corrections.
- User short driving checks passed Quality, Balanced and Performance with jitter off.
- Xbox controller left/right vibration commands succeeded and user felt both pulses.
- User verified fullscreen/windowed transitions, Xbox controller reconnect and repeated Port Options entry/back navigation in the Vulkan Quality build.
- User confirmed fullscreen persists after restart and FXAA-enabled menus remain sharp.
- Native Vulkan runtime tracing confirmed FXAA High selects presentation mode 3 during driving, mode 0 in menus, and returns to mode 3 on the road. DLSS remains explicitly excluded from the FXAA pass.

## Pending hands-on verification

No additional hands-on checks are required for this release scope.

Extended race/Career stability testing is not a release gate at the user's request. The completed short driving, menu-transition and renderer checks remain the current validation evidence.

## Deferred by the user

- DLSS jitter correction; default remains off. See `DLSS-JITTER-FOLLOWUP.md`.
- Texture-pack conversion and full wheel force feedback.
- 60/120 FPS simulation and frame generation.
- MSAA renderer support. FXAA remains the supported anti-aliasing path for this release.

## Publishing

Main documentation is `README.md`, with Windows build instructions in `BUILDING-WINDOWS.md`. Player guides live in `docs/`; implementation history lives in `docs/development/`. Artwork usage and provenance are consolidated in `launcher/art/ARTWORK.md`. A source-only review ZIP is staged under ignored `work-remix/release/`. No GitHub repository, release or upload has been created by this workflow.

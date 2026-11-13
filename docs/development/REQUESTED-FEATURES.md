# Requested feature checklist

This is an implementation inventory, not a claim that all runtime defects are resolved.

Release scope update: texture-pack conversion and full wheel force feedback are deferred to a later release by the user. Controller vibration, menu/launcher polish, Vulkan Career-loading parity and GitHub cleanup remain current work. Launcher Controls now includes a separate left/right Xbox motor test; physical confirmation and gameplay vibration testing remain pending. Vulkan's previously missing 2D GPU copy handler is implemented and builds; the Career-loading transition still needs visual verification.

Current decision (2026-11-13): user deferred the DLSS jitter investigation and requested jitter OFF for normal DLSS launches. Jitter-off Quality was visually stable in the stationary-scene comparison. Jitter-on still causes fine-detail shaking; reversing the projection sign added ghosting and was reverted. This remains unresolved future work, not a completed DLSS quality fix. Native OpenGL/Vulkan are unaffected. Explicit `-JitterScene` is retained for diagnostics only. See `DLSS-JITTER-FOLLOWUP.md` before resuming this investigation.

Implemented: portable launcher with embedded artwork/source, local-folder setup/build, setup checks, repair, desktop logo icon, launcher bypass/recovery, legal original-Xbox Remix requirements, OpenGL/Vulkan selection, fullscreen/resolution persistence, native Port Options, graphics presets/defaults, gamma/brightness/sharpening, HUD texture quality, filtering, VSync, FPS overlay, controller rumble strength, optional raw SDL wheel mapping, PC Music with Windows media controls, player volume, six-second track announcements, D-pad track skipping, menu pause/resume, native radio titles with exclusive backup routing and supported-character normalization.

Newly implemented: launcher previous/next arrow controls, FXAA Low/standard/High profiles, Vulkan presentation effects and overlays, Vulkan filtering/HUD scale/one-frame queue, DLSS Balanced/Performance using SDK-recommended input sizes. New modes require gameplay validation.

Remaining release validation: Balanced/Performance driving, minimap/HUD edge checks, fullscreen/resolution transitions, longer stability and controller reconnection. The white border/right-side blur was resolved in user visual checks of native Vulkan and Quality. Jitter remains deferred as described above.

Latest renderer verification (2026-11-13): small-effects-buffer scaling, render-texture padding sampling, and fixed-function texture matrices now follow OpenGL. User reports the line/blurred right-side strip is absent in native Vulkan and effects animate correctly. DLSS verification of these corrections and the remaining HUD/minimap/stability checks are still pending; the earlier defect list above records the pre-correction state.

Follow-up: user also confirmed DLSS Quality looks correct. Balanced/Performance and longer stability tests remain pending. Renderer corrections are implemented in allocation/sampling; presentation cropping and the temporary edge-masking workaround are removed.

Physical wheel compatibility remains unverified. Full force-feedback steering, clutch and shifter mapping were not implemented; the requested feasibility pass identified these limits. The Desktop ULUS10021 pack is a PSP/PPSSPP pack and cannot be loaded directly as an Xbox texture pack; see TEXTURE-PACK-COMPATIBILITY.md. Automatic mapping/conversion is not implemented.

Deferred by the user: 60/120 FPS simulation, frame generation, and MSAA renderer support. FXAA remains the supported anti-aliasing path for this release. Spotify account login was replaced with the requested Windows media-session integration. Existing music is played by its PC app, not captured/rebroadcast by the game.

Source packaging excludes disc files, saves, credentials/preferences, build output, NVIDIA SDK/runtime binaries and third-party texture packs. Final GitHub cleanup and release review remain pending; nothing has been uploaded.

Career transition follow-up: user reports the Vulkan GPU-copy implementation resolves the loading glitch. Physical controller vibration and longer stability checks remain pending.

Vibration follow-up: the Xbox/XInput test returned success for both motors, and the user confirmed feeling both pulses. This verifies the connected controller's motors; it does not establish every gameplay effect or other controller model. SDL rumble failures now propagate failure and log the reason instead of silently reporting success.

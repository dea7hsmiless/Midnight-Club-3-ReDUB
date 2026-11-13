# Windows renderer settings audit

Source audit: 2026-11-13. A connected code path is not a claim of exhaustive live validation.

| Setting | OpenGL | Vulkan / DLSS |
| --- | --- | --- |
| Fullscreen / resolution | Shared window settings | Shared window settings; fullscreen persistence still needs visual validation |
| VSync | Live swap interval | Swapchain recreation reads live setting |
| Render scale | Startup; restart required | Startup; restart required |
| HUD texture quality | Startup texture upscale | Cached premultiplied-alpha Catmull-Rom upscale of eligible UI textures; restart |
| FXAA | Low/standard/high search profiles | Low/standard/high directional filtering profiles; final display pass |
| Texture filtering | Startup anisotropy | Startup anisotropy, clamped to the device limit |
| Sharpening | Live post processing | Live final display pass; user reports settings respond |
| Gamma / brightness | Live post processing | Live final display pass; user reports settings respond |
| FPS overlay | OpenGL overlay | Cached GDI text uploaded to presentation overlay; user reports settings respond |
| PC Music enable / volume | Shared media worker | Shared media worker; missing helper packaging fixed |
| Radio song title | Shared guest HUD hooks | Shared guest HUD hooks; music works after packaging fix in user test |
| Backup song overlay | OpenGL overlay | Shared display routing; GDI text composited after scene effects |
| Presets / restore defaults | Writes shared graphics settings | Writes connected settings; scale/HUD/filter/queue require restart |
| Frame queue | OpenGL worker queue setting | One or two fence-backed Vulkan frame slots; restart |
| Controller / wheel rumble | Shared SDL backend | Shared SDL backend; physical wheel testing outstanding |

Vulkan staging now includes `now_playing.ps1`, matching the OpenGL runtime and source helper SHA256. Previously the worker started PowerShell with a nonexistent script, explaining the missing music functionality.

The formerly missing Vulkan controls now have renderer implementations. The user confirmed the first settings/music build responds, but the white right/bottom border and minimap mask defects persist. Updated HUD/filter/queue and AA profiles still require live validation. FXAA filtering implementations differ between the APIs; parity means functional controls, not identical pixel output.

DLSS Quality, Balanced, and Performance each passed sixteen real GPU evaluations with controlled-scene output readback on the RTX 3070. Game integration queries NVIDIA's optimal input dimensions at a fixed output size; Balanced and Performance still need live driving validation.

## Renderer parity investigation — 2026-11-13

The user confirmed OpenGL does not show the Vulkan border. Internal GPU readbacks traced the white line to a smaller effects texture, sampled by an additive full-screen pass. Vulkan ignored `RECOMP_GL_SMALL_SCALE` and lacked OpenGL's `RECOMP_GL_RT_EDGE` handling, so effects buffers were enlarged and blur samples could access unused allocation padding. Both behaviors now follow OpenGL. Vulkan fixed-function texture matrices were also missing and are now uploaded/applied.

After these corrections, the user reports native Vulkan (DLSS Off) looks clean, the right-side line/blurred strip is no longer visible, and particles/markers appear correct. This is a user visual check, not exhaustive parity or stability validation. DLSS Quality must be rechecked with the corrected renderer; Balanced/Performance remain pending. The attempted final-image crop was removed, and final-presentation/input edge-masking clamps are disabled; the current correction operates in effects allocation and texture sampling.

Follow-up user verification: DLSS Quality also looks correct after the same corrections. Native Vulkan and Quality therefore pass the reported scene/effects visual check. Balanced, Performance, extended stability, and fullscreen/resolution transitions remain to verify.

Balanced follow-up: with jitter disabled, user reports it "looks decent." Sampled runtime logs show approximately 29.6–29.9 FPS, jitter=0 and reset=0 during continued DLSS evaluation. This is a short visual/performance check; Performance and longer stability remain pending. Quality's later fine-detail shaking is tracked in `DLSS-JITTER-FOLLOWUP.md`, and jitter is now off by default.

Performance follow-up: user reports it looks fine with jitter off. Local logs sampled 29.4–29.9 FPS with continuing DLSS evaluation and reset=0. All modes now have short user checks; extended validation remains outstanding.

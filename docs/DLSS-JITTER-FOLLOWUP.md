# Deferred DLSS jitter investigation

User decision, 2026-11-13: use DLSS without jitter for now and continue remaining features, launcher polish, validation, and source-package cleanup. Do not silently re-enable jitter in normal launcher or script launches.

Observed: Quality shakes fine building, railing and post details while moving and especially while stopped. With DLSS still active and jitter disabled, user confirmed the shakiness disappears. This temporary mode may reconstruct less detail and retain aliasing; it is not a complete temporal-integration fix.

Changes investigated:

- Replaced the evaluation gate requiring moving geometry with a gate accepting valid stationary history. Shakiness persisted.
- Reversed projection-jitter direction as a diagnostic. User reported unchanged shaking plus ghosting. Reverted that change; positive projection offset is restored in source.
- Native Vulkan and Quality passed the border/effects check after adding OpenGL parity for small effects-buffer scaling, render-texture padding, and fixed-function texture matrices. No final-image border crop is applied.

Next investigation should measure static per-pixel motion, history rejection coverage, exact projection/sample offsets, jitter reported to NGX, and reset state before trying another sign or scale change. Current renderer logs `DLSS temporal inputs` with jitter, reset, elapsed time and history counts. Optional GPU input readback uses `MC3_DLSS_CAPTURE`. The script's `-JitterScene` is diagnostic opt-in; ordinary `-PresentScene` leaves jitter disabled.

Audit note: `s_temporal_moving_matches` accumulates while draw/match totals reset periodically in the flip log. Do not interpret that value as per-frame motion coverage or assume the previous moving-match gate alone explained stationary jitter. Measure actual per-frame/per-pixel values when this deferred investigation resumes.

Screen inspection starts a Windows capture helper and can show a yellow outline. The user prefers no screen capture except when explicitly requested; stop the helper after any authorized inspection.

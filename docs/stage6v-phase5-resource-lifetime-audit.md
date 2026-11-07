# Stage 6V Phase 5 — RSX resource-lifetime source audit

**Status:** source review only. The flicker root cause remains unconfirmed.

## Findings in source/main.cpp

- `present()` flushes Tiny3D UI batches, calls `tiny3d_Flip()` and then records `Video_currentBuffer`. Alternating indices do not prove safe GPU memory reuse.
- `rsx_preview_tex_init()` allocates a single `g_rsx_icn_tex` and a single `g_rsx_model_tex` (both 128×128×4 bytes) using `tiny3d_AllocTexture()`.
- The ICN upload path copies pixels with `memcpy(g_rsx_icn_tex,tex,128u*128u*4u)` and submits a quad sampling the same allocation. A GPU-completion guarantee before reuse is not evident from this call site. Audit the model upload path as well.
- The font atlas (128×64) and background textures are initialized once; they are less likely to be repeatedly overwritten after startup.
- Browser thumbnail textures are per-slot and copied on cache invalidation; verify outstanding GPU reads cannot overlap slot reuse.
- `details_base_capture()` copies from the previous display framebuffer into a persistent texture; its source/destination completion semantics need inspection.
- UI rectangle, line and text batches generate Tiny3D vertex commands. Review command-buffer and vertex-buffer ownership and the linked Tiny3D callback before changing FIFO allocations.

## Real-hardware evidence

- Both recorded scenes maintained approximately 30 FPS; flicker persisted in both.
- Buffer indices alternated, but no GPU completion fence was established by this observation.
- FIFO GET/PUT sampled at flip boundaries were constant during ICN preview; this does not prove GPU idle.

## Controlled next steps

1. Add RAM-only per-frame texture-upload counters and timestamps; preserve all flip and memory behavior.
2. Compare a diagnostic static-background/minimal-UI scene with the normal menu, retaining identical Tiny3D flip sequencing.
3. If static scene is clean, isolate mutable ICN/model texture uploads with a single-variable A/B test, using verified GPU completion or alternate allocations.
4. Audit Tiny3D command/vertex buffer lifetimes and callbacks before touching FIFO size.
5. Keep the stable main application's source unchanged. Do not pursue 60 FPS until corruption is resolved.

No PS3 binary is required for this source-only audit.
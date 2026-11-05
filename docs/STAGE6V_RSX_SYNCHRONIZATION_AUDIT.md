# Stage 6V-A — RSX synchronization audit (Build 220.1)

Status: experimental source instrumentation only; not compiled or tested on PS3.

## Supplied upstream reference archives

- BitEdits/xf86-video-ps3: src/ps3_dma.c, PS3Sync() waits for FIFO GET to reach submitted PUT with a 2000 ms deadline. On timeout it reports a lockup. This is Linux X.org driver code, **not** a safe recovery API for PSL1GHT.
- BitEdits/ps3rsx: src/library/egl.c, swap path submits gcmSetFlip, flushes RSX commands, issues gcmSetWaitFlip, and polls gcmGetFlipStatus subject to its configured iteration cap. A timed-out poll is **not** evidence of flip completion.
- BitEdits/ps3spe: SPU task scheduling is separate from RSX flip completion and is not part of this patch.

## Stage 6V-A instrumentation

- Preserve the original Tiny3D and GCM command order.
- Existing Phase 5G sampler observes the RSX control register in a second PPU thread only during the probed callback.
- New g_6v_fifo_max_stagnant_us records the maximum observed interval with queued work (GET != PUT) and no GET progress.
- New g_6v_fifo_stagnant_samples counts samples at or beyond 100 ms without observed GET progress while work remains queued.
- Reset these counters at the start of each instrumented callback probe.
- Neither counter currently appears in the on-screen diagnostics; they must be exposed or logged in a subsequent change before hardware validation.
- These counters do not establish a GPU hang or framebuffer safety. A stationary GET may be transient; the sampling thread itself can be delayed.

## Required before enabling recovery

1. Compile with the project's exact PS3DEV/PSL1GHT/Tiny3D toolchain and check for build regressions.
2. Expose the sampled counters in the existing diagnostics, preferably with bounded formatting and without additional RSX commands.
3. Compare the last hardware-validated Stage 5P build against the modified build on real PS3, testing Homebrew Store, category navigation, stream downscale controls and GPU Bilinear selection.
4. Capture FIFO GET/PUT, flip status, submitted/displayed buffer IDs and elapsed time during a suspected stall.
5. Never assume that a timeout releases a GPU-owned framebuffer. Do not reset the FIFO or recycle a render target based on elapsed time alone.

This audit does not claim to fix the Stage 5O full-console freeze.

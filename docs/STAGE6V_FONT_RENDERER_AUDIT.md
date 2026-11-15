# Stage 6V font renderer audit and unattended benchmark plan

Source inspected: source/main.cpp, branch stage6v-phase2-observation-only (2026-11-15).

## Verified existing implementation
- glyph(char) returns legacy 5x7 bitmap rows.
- rsx_font_init() builds one persistent 128x64 RGBA8888 atlas (32,768 bytes), for printable ASCII 32..126, on first use via tiny3d_AllocTexture.
- rsx_flush_text_batch() binds that atlas with nearest filtering and submits Tiny3D quads.
- RSX_TEXT_BATCH_MAX=256 text commands; g_perf_text_flushes already counts flushes.
- text_raw() has a separate legacy path that draws each lit glyph pixel as a rectangle when RSX UI overlay is not active.
- text() avoids a shadow pass for normal-sized RSX text, but large display text can still use shadow.

## Critical diagnosis
The legacy per-pixel rectangle path may cost far more than the atlas path. Merely benchmarking bitmap versus atlas as if the atlas were absent would be misleading. Font-file type (TTF/OTF) is independent of GPU texture format; adding font decoders is not justified before profiling.

## Unattended A/B/C matrix
A: RSX atlas with text batches (current).
B: legacy 5x7 per-pixel rectangle renderer (current fallback).
C: atlas with controlled single flush versus forced small batches to expose command submission overhead.

For each case, use identical text strings, glyph counts, font sizes, positions and visual contents. Exercise menu, file browser (dense rows), virtual keyboard, ICON.SYS pages and 3D ICN hidden/visible. Run alongside background on/off and GPU format workload. Preserve all other state.

## Capture
font_render_workload.csv columns:
scene,mode,frame,glyph_count,text_commands,text_flushes,rect_submits,atlas_binds,atlas_bytes,cpu_text_us,submit_us,present_us,frame_interval_us,over_16667us,fifo_get,fifo_put,rsx_status,icn_active

font_render_summary.csv columns:
scene,mode,frames,mean_cpu_text_us,p95_frame_interval_us,max_frame_interval_us,over_budget_count,flicker_events,notes

Capture first-frame atlas initialization separately from steady-state; do not count a zero-duration unsupported test as a pass. Verify that the visual outputs are equivalent and no GPU buffer is overwritten before RSX completion.

## Acceptance
Strict 60fps: frame interval <= 16.67 ms for every sampled frame, no flicker, no RSX errors. Retain existing atlas unless a verified on-console alternative is faster and stable.

Status: audit/specification only; benchmark is NOT yet integrated or compiled.

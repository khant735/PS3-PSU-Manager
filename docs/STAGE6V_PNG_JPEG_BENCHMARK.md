# Stage 6V: unattended PNG vs JPEG benchmark specification

Status: test specification only; not yet wired into the executable.

## Fair paired inputs
Use the same opaque background source at the same dimensions. Encode one lossless PNG and JPEG at quality 95, 85 and 75. Keep original PNG for a quality reference. Include a sharp-edged UI/background composite as a second image class. Record source SHA-256 and dimensions. Do not compare unrelated images or silently resize.

## Automated test matrix
For every image/quality and repeat, measure:
1. Compressed file bytes and filesystem read microseconds, both cold-first and warmed.
2. CPU decode microseconds, including allocations; log peak resident buffer bytes.
3. Convert to the **same** RSX texture format (RGBA8888 then RGB565), with conversion microseconds.
4. Upload/bind/draw/present timings, separately, plus GPU allocation bytes.
5. Frame interval and max, p50, p95, p99; number above 16,667 microseconds.
6. PSNR / pixel error against original PNG after both images are decoded to identical RGBA.
7. Any flicker, FIFO stalls, RSX errors, or incomplete GPU texture synchronization.

## Safety and validity
Use libpng and libjpeg from the uploaded PS3DEV portlibs; they are available locally. Decode to separate buffers and do not overwrite RSX texture storage still in use by the GPU. Reuse the identical rendering path, dimensions, filtering and blend mode. Do not conflate JPEG compressed file size with RSX VRAM consumption. PNG and JPEG are **disk encodings**; RGB565 and RGBA8888 are **GPU formats**.

Run fully unattended after Stage 6V's existing scene suite; never require a controller action. Log CSV under /dev_hdd0/tmp/ps3psu_stage6v/. A missing paired image or decoder failure is an explicit test failure, not a zero-millisecond pass.

## Intended output files
- image_codec_workload.csv: codec,quality,scene,repeat,width,height,file_bytes,read_us,decode_us,convert_us,upload_us,submit_us,present_us,frame_interval_us,over_16667us,rsx_format,status
- image_codec_summary.csv: codec,quality,scene,rsx_format,count,mean_decode_us,p95_frame_us,max_frame_us,over_budget_count,psnr_db,quality_notes

## Acceptance
The user requires a rock-solid 60 FPS: no measured frame interval greater than 16.67 ms, no visible flicker, no RSX faults. Faster decoding or smaller JPEG files alone are not sufficient to select JPEG for production. Do not alter the app's PNG assets until on-console results show an actual benefit.

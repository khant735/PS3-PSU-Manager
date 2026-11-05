# Full dependency and integration audit — PS3 PSU Manager
Date: 2026-11-05. Scope: experimental Stage6V source, Makefile and CI, plus reference homebrew. **Source review only; no console execution. Stable main unchanged.**

## Actual build dependency inventory

- Compiler: PS3DEV PSL1GHT PPU C/C++ (C++ compiled with `-fno-exceptions -fno-rtti`); C/C++ flags `-O1 -ffast-math -fno-math-errno -mcpu=cell`.
- Static link: `-lfont3d -ltiny3d -lfont -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lnet -lnetctl -lsysmodule -lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm`.
- Runtime system modules: NET, NETCTL; controller via `ioPadInit`; system callback via `sysUtilRegisterCallback`.
- Graphics: Tiny3D + RSX/GCM + custom CPU framebuffer compositor + custom Tiny3D font/ICN/background textures; native font library linked.
- Network: libcurl easy and multi APIs, HTTPS certificate bundle, PS3 net/netctl, Filen decryption with Mbed TLS.
- Compression/archive: zlib raw-deflate ZIP, PSU/CBS/MAX/XPO and other save format inspection/conversion, image/icon parsers.
- Data: `PS2_GAME_TITLES.tsv`, `PS2_GAME_TITLES.releases.tsv`, `cacert.pem`, SFO and ICON0; build-time generated database downloads.
- Build: `hldtux/ps3dev:latest` container is **mutable**, therefore dependency versions and library ABI cannot be reproduced from the workflow alone. Exact image digest, archive hashes, header hashes, link map and build metadata should be recorded.

## Findings, ranked

### F1 — HIGH — shared graphics ownership and initialization
`video_init` calls `tiny3d_Init`, `tiny3d_UserViewport`, `tiny3d_Project2D`, `rsx_font_init`, then manually clears `Video_buffer[0/1]`, and replaces Tiny3D's GCM flip/vblank handlers with application callbacks. All must be compared with a plain Tiny3D-only control. This does not establish a defect, but is an actual integration difference from upstream examples. Source: `source/main.cpp` ~2606–2637.

### F2 — HIGH — geometry failure boundary and allocation checks
`tiny3d_VertexPos` flushes the *previous* vertex via `put_vertex`; J14 without J15 means the second vertex flush is a likely failure point. Wargio Tiny3D writes through `rsx_vertex` without a null check in `put_vertex`. Verify matched **linked** Tiny3D implementation, its memory allocation and address mapping. Source: `wargio/tiny3D/lib/source/tiny3d.c`.

### F3 — HIGH — nonminimal isolation
The current diagnostic path calls settings, catalogue, filesystem and custom font-atlas startup before testing geometry; therefore the test cannot distinguish independent library failure from prior application corruption. Build standalone reference ELF with same exact SDK first. Source: `source/main.cpp` ~5996–6057.

### F4 — HIGH — nonreproducible SDK and static library set
GitHub Actions uses `hldtux/ps3dev:latest` rather than an immutable digest; link options depend on the image's headers/archives. CI and uploaded SDK snapshots already have different Tiny3D archive hashes. Capture `sha256sum` for `libtiny3d.a`, `libfont.a`, `libfont3d.a`, `libcurl.a`, `libmbedtls.a`, `libmbedx509.a`, `libmbedcrypto.a`, `libz.a`, `librsx.a`, `libgcm_sys.a`, and relevant headers. Source: workflow and Makefile.

### F5 — MEDIUM — network initialization has no rollback and is not synchronized
`downloader_net_init` loads NET, NETCTL, then calls `netInitialize`, `netCtlInit`, `curl_global_init`, with early returns but **no cleanup/rollback** on intermediate failure. `downloader_net_ready` is a plain integer and may be reached from multiple background worker paths, so concurrent first-time initialization is a possible race. Review all call sites and thread lifecycle before fixing. Source: `source/main.cpp` ~4508–4516, ~5255, ~5979–5994. Network initialization happens on demand and is **not a demonstrated cause of the initial vertex stall**.

### F6 — MEDIUM — segmented downloader callback return-value bug
`segment_write_cb` returns `fwrite(ptr,size,nmemb,...)`'s **item count**, but libcurl's write callback contract requires returning **bytes** consumed (`size*nmemb`). If `size != 1`, this causes a false short-write abort; bytes-accounting uses `n*size`, but the callback returns `n`. Fix by computing `bytes = n*size`, checking overflow and returning `bytes`. Source: `source/main.cpp` ~4519–4531. This is a concrete contract violation, albeit libcurl often calls with size=1.

### F7 — MEDIUM — HTTPS trust and backend consistency
Filen downloader sets `CURLOPT_CAINFO` from a helper and enforces TLSv1.2; GameFAQs transport hardcodes `/dev_hdd0/game/PSU000001/USRDIR/cacert.pem`. Check all curl call paths, redirects, SSL backend compatibility with the linked Mbed TLS archives, and CA file existence/refresh. Source: `source/filen_download.cpp`, `source/gamefaqs_curl_transport.c`, `source/ca_bundle_path.cpp`.

### F8 — MEDIUM — package resource consistency
Makefile `pkg` checks presence and identity of two TSVs and CA bundle in staged PKG. Verify the final installed package path, generated `PARAM.SFO`, app ID, source release archive and ELF/SELF/PKG against CI manifest. Source: Makefile `pkg` and workflows. Packaging problems do not by themselves explain the second-vertex CPU-side call.

### F9 — MEDIUM — decompression/format parser coexistence
`zip_extract.cpp` uses `inflateInit2(-MAX_WBITS)` for raw ZIP deflate; PS2 format parsers and icon parsers allocate buffers based on untrusted archive metadata. Audit per-entry and aggregate size limits, integer multiplication, path traversal, symlinks, destination writes, error unwinding and read-only opening. Need dynamic tests and fuzz corpus; no specific exploit established here.

### F10 — MEDIUM — thread lifecycle, callback and shutdown
`sysThreadCreate` is used for database and GameFAQs workers. Several completion flags are `volatile` with selected memory barriers. Verify whether shared fields are accessed under synchronization, whether workers can overlap during shutdown, and whether app shutdown waits for active networking/filesystem work. GPU flip/vblank handlers are globally replaced and reset to null before Tiny3D exit. Source: `source/main.cpp`.

### F11 — LOW/MEDIUM — optimization and ABI differences
Compare `-O1 -ffast-math` to upstream apps' `-O2` using same code and archive, plus symbol signatures, C/C++ ABI, linker order and duplicate archive symbol providers. A source build linking successfully is not proof of runtime ABI compatibility.

## Interaction matrix

| Integration | Failure mode to test | Distinguishing test |
|---|---|---|
| Tiny3D × RSX/GCM | dual ownership of display, flip, vblank, memory | minimal Tiny3D vs custom handlers |
| Tiny3D × font3d/libfont | incompatible header/archive, texture allocator state | no-font vs font atlas vs libfont |
| Tiny3D × CPU framebuffer writes | frame ownership and asynchronous GPU writes | disable manual buffer clear/compositing |
| Tiny3D × callbacks | callback reentrancy or shutdown order | register one at a time |
| curl × NET/NETCTL | repeated init, race, no rollback | serialized initialization and failure injection |
| curl × Mbed TLS × CA bundle | TLS backend, cert path, redirect/protocol | independent HTTPS probes per backend |
| curl multi × filesystem | 16 segmented writes, partial files, callbacks | short-write and concurrent segment tests |
| zlib × ZIP × save parsers | memory bounds and decompression bombs | malformed input/fuzz corpus |
| catalogue workers × UI | concurrent state and file operations | thread sanitizer on host-compatible logic |
| PKG × data files | missing or mismatched resources | inspect staged and installed paths |
| toolchain × all archives | headers, ABI, stale library snapshots | pin container digest + hash manifest |

## Recommended order

1. Pin and inventory full toolchain; produce link map, symbol table, ELF dependency manifest and package file list.
2. Standalone Tiny3D triangle **with exact same CI toolchain**, then add graphics subsystems one by one.
3. Fix `segment_write_cb` contract and add unit tests on a separate branch, without changing main.
4. Make networking initialization idempotent and serialized with failure rollback, after confirming NET/NETCTL lifecycle contract.
5. Verify all HTTPS endpoints, trust stores, package resources and read-only archive semantics.
6. Audit all memory bounds and thread interactions; run host-side unit tests/fuzzers where possible.
7. Real PS3 tests for integration changes; do not claim resolved based only on CI success.

This is an initial integration audit, **not yet a line-by-line validation of every function or a completed runtime dependency certification**.

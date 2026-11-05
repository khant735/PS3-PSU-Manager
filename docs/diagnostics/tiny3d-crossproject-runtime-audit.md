# Tiny3D / libfont cross-project runtime audit (5 November 2026)

**Scope:** Experimental Stage6V isolation Q compared with original Tiny3D, its later PSL1GHT port, and other PS3 homebrew applications. Not a validated root-cause fix. The stable v1.64 main branch is not modified.

## Confirmed facts

- Dual log Isolation O records harmless-call controls and stops at `J14 BEFORE VERTEX 3 POSITION` in both files; no J15.
- Current test calls `tiny3d_VertexPos(300,330,0)`; Wargio Tiny3D's `tiny3d_VertexPos` calls `put_vertex()` before saving the NEW position. Thus the call flushes vertex 2; it does not yet write vertex 3 to its vertex buffer.
- `put_vertex` writes position (16 bytes) and RGBA integer color (4 bytes) to the pointer `rsx_vertex + pos_rsx_vertex`, incrementing the offset. The out-of-space check compares the offset with `size_rsx_vertex - 1024`; **it does not check `rsx_vertex != NULL`**.
- Both our `video_init` and the Wargio sprites2D example use `tiny3d_Init(1024*1024)`. A 1 MiB buffer is an established reference size.
- Current isolation calls `video_init` (which also runs `tiny3d_UserViewport`, `tiny3d_Project2D`, and custom `rsx_font_init`), then `load_settings`, `catalogue_load_cache`, `refresh_fs`, before the geometry test. The test is **not minimal**.
- Our Makefile uses `-O1 -ffast-math -fno-math-errno` and links `-lfont3d -ltiny3d -lfont`. Apollo, PS3LoadX and PKGi use `-O2` and do not include `-ffast-math` in their inspected Makefiles. The presence of `-lfont3d` is also normal in PS3LoadX and PKGi.
- The user-provided full PS3DEV toolchain archive has `ps3dev/portlibs/ppu/lib/libtiny3d.a` SHA256 `f048395e10e4cb60632d4430d99bf376d9ccf60efe8ad6a24c6c9c8c19434194`. Earlier CI audit recorded SHA256 `c08b5ad6e02bd1321362c0d58fce10924f4efb7550d05defe54f96364db174cd`. These *two available toolchain snapshots* are not byte-identical; do not infer which is correct or whether this is the cause.
- Tiny3D and libfont are statically linked archives; missing application assets cannot explain a fault precisely at a compiled `tiny3d_VertexPos` call, unless some earlier program action corrupts memory. Verify the ELF linked code and package separately.

## Upstream differences

- `granberro/tiny3d` documents its 2011 PSL1GHT updates and labels itself obsolete; uses older RSX APIs such as `rsxMemAlign`/reality.
- `wargio/tiny3D` includes newer PSL1GHT/GCC updates and uses `rsxtiny_MemAlign` / `rsxtiny_` APIs.
- `wargio/tiny3D/lib/source/rsxutil.c` allocates a 1 MiB-aligned host buffer and initializes the Tiny3D command context. It then allocates video and depth buffers through `rsxtiny_MemAlign`.
- `wargio/tiny3D/lib/source/tiny3d.c` allocates the vertex buffer with `rsxtiny_MemAlign(64, vertex_buff_size)`, but does not reject a null return before reporting successful `tiny3d_Init`.
- In `put_vertex`, the **second vertex** is flushed upon the call that supplies vertex 3. Missing J15 narrows the failure boundary to this call, but does not by itself distinguish CPU memory access, command-buffer callback, or error in persistence/termination.
- PowerPC64 ELFv1 `.opd` function descriptors are not the function instructions; disassemble their `.text` targets, with exact build-linked code offsets.

## Reference projects

1. **wargio/tiny3D samples** — e.g. sprites2D initializes Tiny3D with 1 MiB and renders successive quads.
2. **bucanero/apollo-ps3** — uses Tiny3D 1 MiB and `tiny3d_UserViewport` with equivalent dimensions, loads its font textures after init, and draws through `Clear` -> `Project2D` -> `Flip`.
3. **bucanero/ps3loadx** — linked with `-lfont3d -ltiny3d`, uses 1 MiB initialization and `Clear/Flip`.
4. **bucanero/pkgi-ps3** — also links `-lfont3d -ltiny3d`.

These show a functioning architectural pattern, **not proof that a binary built from our exact library archive is valid on the test PS3**.

## Priority-ordered differential experiments

1. **Reference control (A)**: compile a *standalone small C source* with the exact CI headers and archive, `tiny3d_Init(1MiB)`, a plain color triangle, `tiny3d_End`, and normal `tiny3d_Flip`. Avoid PSU Manager source, file operations, custom font atlas, wrappers, callbacks, and texture loads. Log each phase. This is the most decisive missing control.
2. **Same ELF with font/resource initialization (B)**: add PSU's `rsx_font_init()` (and only that). Compare with A.
3. **Same ELF with viewport and callback overrides (C)**: add `tiny3d_UserViewport`, then separately app VBlank/flip handlers and slot0 callback. Change one factor at a time.
4. **Same ELF with startup operations (D)**: add `load_settings`, `catalogue_load_cache`, and `refresh_fs` individually. Retest triangle after each.
5. **Toolchain/flags matrix (E)**: rebuild A with `-O2` vs `-O1 -ffast-math`, and then with the other provided library archive + *matching headers and dependent RSX libs*. Record archive/header SHAs, linker map and `objdump -dr`. Do NOT swap one archive in isolation across incompatible portlib snapshots.
6. **If A still fails**: instrument `put_vertex` in a source-built matched Tiny3D, checking vertex-buffer nullness, aligned address, offset, size, and first/second write. Prefer independent serial/UDP diagnostics where possible; do not assume duplicate filesystem streams are independent.
7. **If A works**: binary-search our startup and state changes using B–D. A working A would strongly argue against missing runtime shaders/assets or a fundamentally unusable SDK.
8. Recompare against a **known working PS3 PSU Manager binary** built with same SDK when possible; compare source, compiler flags, link map and libraries; a stable binary built with a different toolchain isn't a controlled test.

## Specific source concerns

- `video_init()` invokes GPU font atlas creation *before* we test geometry, and clears both framebuffer memory regions manually.
- Our application replaces Tiny3D's GCM VBlank/flip handlers and contains numerous linker wrapping functions (even if the isolation's linker flags disable wrapper redirection). These are differences from the simplest reference code and must be isolated.
- Existing isolation runs include filesystem/catalogue startup; a crash in the test may be caused by prior corruption, not the same Tiny3D call in isolation.
- Missing PNG/JPG modules, app assets or a freetype font are **not currently demonstrated** as a cause of the recorded third-vertex stall. The diagnostic triangle uses only position + integer color, not loaded textures or libfont.
- No conclusion yet that any of `libfont`, `libfont3d`, or Tiny3D is obsolete **in the actual CI build**.

## Source links

- https://github.com/granberro/tiny3d
- https://github.com/wargio/tiny3D/blob/master/lib/source/tiny3d.c
- https://github.com/wargio/tiny3D/blob/master/lib/source/rsxutil.c
- https://github.com/wargio/tiny3D/blob/master/samples/sprites2D/source/main.c
- https://github.com/bucanero/apollo-ps3/blob/master/source/main.c
- https://github.com/bucanero/apollo-ps3/blob/master/Makefile
- https://github.com/bucanero/ps3loadx/blob/master/source/main.c
- https://github.com/bucanero/ps3loadx/blob/master/Makefile
- https://github.com/bucanero/pkgi-ps3/blob/master/Makefile
- https://github.com/ps3dev/PSL1GHT

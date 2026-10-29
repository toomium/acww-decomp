# Nintendo 3DS port (plan)

Goal: the game as a native 3DS homebrew program (`.3dsx`, later `.cia`), built from this tree like the PC builds,
with the 3D scenes drawn by the 3DS's GPU (PICA200) sharper than the DS's 256x192, smoothed edges, and stereoscopic
3D. Not an emulator: the same sources and the same runtime (`modern/host/`) as the PC port, with a 3DS platform
layer, frontend and 3D backend added beside the PC ones.

This file is the plan and, as work goes on, its log (as docs/portability.md is for the PC platforms). Each task
says what to read first, what to write, and how to know it is done; tasks of a phase are done in order unless they
say otherwise. Write down what you find under the task, with the date.

Everything in section 1 was checked on 2026-10-28 on this machine (commands and results there). Anything not
checked is marked **unverified** and has a task that checks it. Don't add facts to this file without checking them
the same way.

## 0. Rules for whoever works on this

- Branch `3ds` of the fork (`origin`); `upstream` is the PC port. Merge `upstream/main` regularly. The 3DS work
  adds files and touches shared ones as little as it can, so that these merges stay easy.
- Changes to shared code (game sources in `src/` and `include/`, shared runtime files, `tools/`) go in **their
  own commits**, separate from 3DS-only files, each one useful on its own: they are candidates for pull requests to
  upstream. A shared change must not change the PC builds' behaviour (checked with `tools/host_oracle.sh`, task
  T0.2).
- The game sources are not changed for the 3DS. If the game needs a change to work on ARM32, it is a portability
  bug and is fixed for every build.
- Read docs/host.md first (how the runtime works, its checks and options), then the files a task names.
- Write code like the code around it: the same comment style (what and why, in plain sentences), naming, C11 for
  the runtime.
- Options stay environment variables (`ACWW_*`, docs/host.md); the 3DS gets them from a file (T2.2), no second
  options system.
- The 3DS API is whatever the installed headers say (`/opt/devkitpro/libctru/include`: `3ds/`, `c3d/`); the
  examples in `/opt/devkitpro/examples/3ds` show working use. Don't write calls from memory.

## 1. Facts (checked 2026-10-28)

### 1.1 This machine

| What | Result |
|---|---|
| devkitPro | `/opt/devkitpro`: devkitARM (`arm-none-eabi-gcc` 16.1.0), libctru with citro3d and citro2d (`libctru/lib/libcitro3d.a`, headers in `libctru/include`), tools `3dsxtool`, `smdhtool`, `picasso`, `tex3ds`, `3dslink`; examples in `examples/3ds` |
| Emulator | Azahar 2126.2 (flatpak `org.azahar_emu.Azahar`) |
| armhf Linux cross compiler, qemu | not installed; in Ubuntu 24.04's archive: `gcc-arm-linux-gnueabihf`, `g++-arm-linux-gnueabihf`, `qemu-user` (8.2.2) |
| libclang for Python (`tools/host_portcheck.py`) | not installed (`pip install --user clang==21.*`, docs/host.md) |
| ROM | `~/Dokumente/Animal Crossing - Wild World (USA) (Rev 1).nds`: SHA-1 `77fde3e3...c351`, the supported one (`acww_usa.sha1`); **32 MB** (header: 32 MB card, 33,131,516 bytes used). `modern/host/card.c`'s comment "ACWW's is 16 MB" is wrong |
| Oracle captures (`~/acww/ref/oracle`) | none; `~/acww` doesn't exist |
| Host compiler | GCC 13.3.0 (Ubuntu 24.04) |
| PC build `build/host64_usa/acww` | the attract demo and title run (headless to frame 3000; in a window to frame 900 with `ACWW_RENDERER3D=own` and `wgpu`, RX 5700 XT); `tools/host_oracle.sh ... quick` passes against captures recorded from it. **It crashes in the oracle's play run** (A every second from frame 2400): SIGSEGV at frame 2619, a call through a null vtable in `EffectModel::~EffectModel()` (`src/main/unk_0208f268.cpp:1451`), from `EffectModelGroup::~EffectModelGroup()` ← `EffectSplProc::~EffectSplProc()` ← `ProcBase::postDelete` ← `TaskList::run` ← `Main_Loop`: probably the user's crash (T0.1) |

### 1.2 The game and runtime under devkitARM (trial build, not committed)

Done with a throwaway script that took every game unit's command from `build_host64.ninja`, replaced `-m64 -fPIE`
with `-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft` (the flags of devkitPro's 3DS example Makefiles) and
added `-fpcc-struct-return` (as the i386 build), and again without it (as D2 decides):

- **All 872 game units compile**, both ways, with `arm-none-eabi-gcc` without errors, including the `static_assert`s on the
  save data's layout.
- **The runtime does not compile unchanged**, for one reason: on arm-none-eabi `uint32_t` is `unsigned long` and
  `int32_t` is `long` (on every PC target they are `unsigned int` / `int`). The game's `u32`/`s32`
  (`include/types.h`) are `unsigned int` / `signed int`; the runtime's are `uint32_t` / `int32_t` (`host.h`,
  `gpu2d/gpu2d_internal.h`, `gpu3d/gpu3d_internal.h`, `sdk.c`, `wgpu_display.c`), and the two meet in the same
  files (`include/sys/OSThread.h` in `os.c`, `pxi7.c`, `wm7.c`). Changing those five typedefs to `unsigned int` /
  `signed int` leaves errors only where the runtime's public headers say `uint32_t` and the code passes `u32 *`:
  `render.c`, `render_selftest.c`, `render_selftest3d.c`, `gpu2d/gpu2d.c`, `gpu3d/gpu3d_geom.c` (about 20 sites),
  plus `-Wformat` warnings. Changing the game's types instead (to `__UINT32_TYPE__`) breaks 171 game units (it
  mixes `s32` and `int`/`BOOL` freely): not an option. Fix: T1.1.
- Missing for the 3DS, as expected: the ARM32 variant of `coro_switch.S` and of `host_coro_prepare` (`coro.c`:
  `#error`), `plat_posix.c` (needs `sys/mman.h`), `debug_posix.c` (needs `link.h`; with `HOST_DEBUG_POSIX` 0 it
  is empty, as on Windows), the melonDS files.
- **Trial link** (game objects + runtime objects with the type fix + libctru, `-specs=3dsx.specs`; the game objects
  with `-fpcc-struct-return`, the runtime's without: a mismatch the linker can't see, so the link proves nothing
  about it - D2 removes it): the only
  undefined symbols are the ones still to be written or excluded: `host_plat_*` (11 functions), `host_coro_switch`,
  `host_coro_prepare`, `render_selftest*` (their files were left out), `host_guards_*` / `host_debug_*` (their files
  were left out; with `HOST_DEBUG_POSIX` 0 and no `HOST_GUARDS` they are stubs), and `timespec_get`
  (`gpu3d/gpu3d_raster.c:573,588`, a timing aid; newlib doesn't have it). No game symbol is missing, no C++ name
  differs.
- Sizes (`arm-none-eabi-size`, all objects): code 3.8 MB, data 0.13 MB, **bss 19.9 MB**. The largest: the 3D
  engine's polygon and vertex RAM (`sPolyRam` 2.6 MB, `sVertexRam` 1.5 MB), the main RAM arena (`gHostMainRam`
  2 MB), the 2D renderer's state (`sEngine` 2.0 MB, `sOverlap` 1.2 MB, `sDeferred` 0.75 MB), `render.c`'s `gpu`
  buffer 0.75 MB, the VRAM windows and banks (`vram.c`, about 2.4 MB).
- The game and the runtime contain no `__arm__` / `__thumb__` conditionals; the runtime's per-CPU code is
  `coro_switch.S`, `coro.c` (`host_coro_prepare`), `os.c:166` (`host_dump_threads`, debugging only: its `#else`
  assumes arm64's frame layout), `main.c:46` (`host_panic`: `*(va_list *)&args`; ARM32's `va_list` is a struct
  holding one pointer, so the cast has the same meaning as on i386), `debug_posix.c`, `hw.c` (x86-64 Linux only).
- The runtime's threads share data through 32-bit `__atomic_*` only (`snd7/output.c`, `frontend_sdl.c`, `hw.c`,
  `frontend.c`); ARMv6K has the instructions for these (ldrex/strex).
- A test program using `setenv`, `localtime_r`, `time`, `memalign`, `socket`, `fopen("sdmc:/...")` links with
  libctru and `3dsxtool` turns it into a `.3dsx`.

### 1.3 The 3DS (from the installed headers and examples)

| | DS (original) | 3DS |
|---|---|---|
| CPU | ARM946E-S, 67 MHz | ARM11 MPCore, ARMv6K (devkitPro's flags: `-march=armv6k -mtune=mpcore -mfloat-abi=hard`: hardware floating point). `SYSCLOCK_ARM11` = 268,111,856 Hz (also `svcGetSystemTick`'s rate); New 3DS: `SYSCLOCK_ARM11_NEW` = `SYSCLOCK_ARM11_LGR2` = 3x, `osSetSpeedupEnable` (`3ds/os.h`) |
| Cores for the program | – | core 0 always; core 1 one thread, after `APT_SetAppCpuTimeLimit`; core 2 (New 3DS) only with an exheader flag (not for a `.3dsx` without one); core 3 never (`3ds/thread.h`) |
| Screens | 2 x 256x192 | top 400x240, bottom 320x240 (touch). Render targets are made transposed (`C3D_RenderTargetCreate(240, 400, ...)`, `examples/3ds/graphics/gpu/*`) with the `Mtx_*Tilt` projections. Wide mode (`gfxSetWide`): top 800x240 with non-square pixels (twice the horizontal resolution), not with stereoscopic 3D, not on the Old 2DS (`3ds/gfx.h`) |
| 3D | geometry engine + rasteriser | PICA200 via citro3d: vertex shaders (`picasso`), fixed-function fragment stage (TEV combiners); alpha test, blending with a separate alpha equation (`C3D_AlphaBlend`, `GPU_BLEND_MAX` exists), stencil, Z- or W-buffer (`C3D_DepthMap(bIsZBuffer, ...)`), fog LUT (`FogLut_FromArray`, 256 entries), texture formats RGBA8, RGB8, RGBA5551, RGB565, RGBA4, LA8, L8, A8, ... ETC1 (no palette formats), `GPU_MIRRORED_REPEAT`. `C3D_TexLoadImage` / `C3D_TexUpload` is a plain `memcpy` (disassembled): the data must already be in the PICA's tiled layout |
| Display transfer | – | `GX_TRANSFER_SCALE_X` / `_XY` (2x1 / 2x2 averaging) and `GX_TRANSFER_OUT_TILED` (a linear picture into the tiled layout textures need, on the GPU) (`3ds/gpu/gx.h`) |
| Sound | – | ndsp (libctru); needs the DSP firmware at `sdmc:/3ds/dspfirm.cdc` (the path is in `libctru.a`), dumped by the user (DSP1) |
| Memory | 4 MB | **unverified** how much a `.3dsx` gets on each model: measure with `osGetMemRegionSize(MEMREGION_APPLICATION)` and `envGetHeapSize()` / `envGetLinearHeapSize()` (T2.2) |

## 2. Decisions

- **D1: same tree, same build script.** `tools/configure_host.py --os 3ds` writes `build_host_3ds.ninja`, building
  `build/host3ds_<version>/acww.elf` from the same source lists and per-source flags as the other builds, and from
  it `acww.smdh` and `acww.3dsx`. No separate Makefile.
- **D2: flags.** CPU: `-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft`, plus `-mword-relocations` (as
  devkitPro's example Makefiles; T2.1 checks whether `3dsxtool` needs it for every unit). Link: `-specs=3dsx.specs
  -L$DEVKITPRO/libctru/lib -lcitro3d -lctru -lm`. The game keeps its `GAME_FLAGS` as on i386 (32-bit, `HOST64`
  off) **without** `-fpcc-struct-return`: the x86-64 build dropped it with identical results (docs/portability.md
  2.2), so the game doesn't need it, and on ARM it would make game and runtime disagree on how a small struct is
  returned.
- **D3: the runtime's integer types are the game's.** `u32`/`s32` stay `unsigned int`/`signed int` everywhere
  (T1.1). It changes nothing on the PC targets (there `uint32_t` is `unsigned int`).
- **D4: the ROM is read from the SD card when needed, not held in memory.** It is 32 MB, and the program's static
  data alone is 20 MB. Only `card.c` reads it (`host_card_read`, `card_read_data`) and `main.c` its header
  (`host_rom`); a file read at an offset with a small cache is enough (T2.3). On the PC nothing changes.
- **D5: the ROM and the save on the SD card.** `sdmc:/3ds/acww/game.nds` by default (`argv[1]` when the launcher
  passes one); the save beside it (`game.sav`), which `pxi7.c` already does for runs with a window. The ROM is the
  user's own; nothing of the game is in the program.
- **D6: 2D on the CPU, 3D on the PICA200.** `gpu2d/` is reused as it is, with `defer3d`. `gpu2d.wgsl` (the 2D
  layers on the GPU) needs fragment shaders the PICA doesn't have and is not ported; revisit only if profiling says
  the 2D layers are the bottleneck. The 3D rasteriser gets a third backend, `gpu3d_pica.c`, modelled on
  `gpu3d_wgpu.c`; the composite gets `pica_display.c`, modelled on `wgpu_display.c`.
- **D7: the New 3DS first.** The Old 3DS is best effort (T4.4). The game's own code is cheap (it ran at 67 MHz);
  the cost is the runtime around it (register calls, virtual time, VRAM windows, the 2D layers, the geometry
  engine). How fast it runs is **unverified** until T2.8.
- **D8: checking.** The 3DS build's headless runs in Azahar (screenshots and sound to the SD card) are compared with
  the x86-64 reference's (`tools/host_oracle.sh`'s frames); real hardware for speed and anything Azahar gets wrong.
  An armhf Linux build under qemu (T1.3-T1.5) only if a difference needs the PC's tools to find.

## 3. New files

```
modern/host/
  coro_switch.S          + the ARM32 variant (#elif defined(__arm__))
  coro.c                 + host_coro_prepare for ARM32
  plat_3ds.c             plat.h for the 3DS (libctru)
  frontend_3ds.c         frontend.h's backend: screens, hid (keys, touch), ndsp (sound), the home menu and sleep
  pica_display.c/.h      the screens composited on the PICA (2D pictures + 3D picture), scaled to the 3DS screens
  gpu3d/gpu3d_pica.c     the 3D engine's polygons on the PICA (citro3d), the texture cache
  gpu3d/gpu3d_pica.v.pica  its vertex shader (picasso)
  tests/coro_a32_test.c
tools/configure_host.py  --arch arm (armhf Linux, qemu) and --os 3ds
meta/3ds/icon.png        the program's 48x48 icon (own artwork, not the game's)
```

## 4. Phases and tasks

### Phase 0: the reference

**T0.1 The PC build's crash. Done (2026-10-28): build with GCC 14 or newer.** The crash at "New Game" (1.1) is
the compiler's: `EffectSplProc::onCreate` zeroes its model groups byte by byte (`MI_CpuFill8(modelGroups, 0,
0x14c0)`, `src/main/unk_0208f268.cpp`, as the DS did), their animations' vtable pointers too. GCC 13.3 destroys the
arrays of `ModelAnim` through those pointers (a call through 0 when the process ends); GCC 14.2 and devkitARM's GCC
16.1 call `ModelAnim::~ModelAnim()` directly, as mwcc did. The game code stays as it is (the user's decision):
`sudo apt install gcc-14 g++-14`, then `python3 tools/configure_host.py --cc gcc-14` (the game and the runtime's C;
the runtime's C++ and the link stay `g++`). With it the play run reaches frame 25000 and the save and continue
scripts work (the player in town in front of the house).

**T0.2 Reference captures.** There are none on this machine. Record them from the x86-64 build with the CPU
renderers (what `tools/host_oracle.sh` compares; it sets that up itself):
```
ACWW_ROM="$HOME/Dokumente/Animal Crossing - Wild World (USA) (Rev 1).nds" ORACLE_DIR=$HOME/acww-ref \
    tools/host_oracle.sh build/host64_usa/acww record
```
(The script sets `ACWW_FIRMWARE` to `~/acww/ref/firmware_generated.bin`; that file doesn't exist here, and a
missing firmware image falls back to the built-in one, `pxi7.c:475`: the captures are made with that.) Checked
2026-10-28: `record` and then `quick` (frames 2500, 8000 and the sound identical) work; the play captures are
incomplete with GCC 13 (T0.1): record them with the GCC 14 build. Then run `quick` and `play` against them with the same
variables: both must pass. Also write a RAM hash file of the play run
(`ACWW_RAMHASH`, docs/host.md) to compare the ARM32 builds with frame by frame. Done when both checks pass and the
files are written; note the directory here.

### Phase 1: shared ARM32 fixes

T1.1 and T1.2 are needed for the 3DS build and are done first. T1.3-T1.5 (an armhf Linux build under qemu) are
**optional**, decided 2026-10-28: the 3DS build is compared with the PC build directly (headless runs in Azahar
with `ACWW_SHOT` / `ACWW_WAV` to the SD card, the same frames as `tools/host_oracle.sh`). Do T1.3-T1.5 only if the
3DS build behaves differently and it is unclear whether the cause is ARM32 code or the 3DS platform layer: under
qemu the ARM32 game runs on Linux, with the PC's tools (RAM hashes and dumps) and no 3DS code involved.

**T1.1 One set of integer types (shared, upstream PR). Done 2026-10-28.** Change the runtime's typedefs of `u8`/`u16`/`u32`/`s32` in
`host.h`, `gpu2d/gpu2d_internal.h`, `gpu3d/gpu3d_internal.h`, `sdk.c` and `wgpu_display.c` to the game's
(`unsigned char`, `unsigned short`, `unsigned int`, `signed int`), and the runtime's own headers that pass pixel and
register buffers (`render.h`, `gpu2d/gpu2d.h`, `gpu3d/...`, `frontend.h` where its users mix them) consistently to
one of `u32` / `uint32_t` so that no `u32 *` meets a `uint32_t *`. Fix the `-Wformat` warnings that remain (`%u`
for `u32`; `PRIu32` only for real `uint32_t`). Replace `timespec_get` in `gpu3d_raster.c` with
`host_plat_time_ns()`. Check: every runtime C file compiles with `arm-none-eabi-gcc -Wall` (the 1.2 flags plus
`-DHOST -Iinclude -DHOST_DEBUG_POSIX=0`) without errors or warnings, and the x86-64 build passes the T0.2 checks
unchanged. Done when both hold.

**T1.2 ARM32 coroutine switch (shared, upstream PR). Done 2026-10-28** (no separate test: the 3DS build's
game threads run). In `coro_switch.S`, a `#elif defined(__arm__)` variant
like the arm64 one: push r4-r11, lr and d8-d15 (AAPCS's callee-saved registers), store sp in `*save`, load sp from
`to`, pop, return; `host_coro_boot` calls `fn(arg)` from two of the saved registers. In `coro.c`'s
`host_coro_prepare`, the matching initial frame (sp 8-byte aligned at the call, the AAPCS rule). In `os.c`'s
`host_dump_threads`, the frame pointer's slot for ARM32 (`r11` in the saved frame). A test like
`tests/coro_a64_test.c` (callee-saved registers survive a switch, arguments arrive, the stack is aligned), built for
ARM32 and run under `qemu-arm` if T1.3's toolchain is there; otherwise the 3DS build's game threads are the test
(they switch from the first frame). Done when the test passes or the 3DS build reaches the title.

**T1.3 (optional) `--arch arm` (armhf Linux, shared, upstream PR).** Install `gcc-arm-linux-gnueabihf
g++-arm-linux-gnueabihf qemu-user` (apt; the user does it, it needs sudo). In `tools/configure_host.py`, an `arm`
arch like `aarch64` (read its branch: `A64`, `--cross`, `--qemu`, `ACWW_RUNNER`): `arm-linux-gnueabihf-gcc`,
`-march=armv6k -mfpu=vfp -mfloat-abi=hard`, the 32-bit game flags of D2, `frontend_none.c`, no melonDS, no
wgpu, `build_host_arm.ninja`, `build/hostarm_<version>/acww`, and the `coro-a32-test` target. Done when it builds
and `qemu-arm -L /usr/arm-linux-gnueabihf build/hostarm_usa/acww <rom>` reaches the title headless
(`ACWW_HEADLESS=1 ACWW_SHOT=...`).

**T1.4 (optional) Portability check for ARM32.** Install libclang for Python (1.1). Add an `arm32` target to
`tools/host_portcheck.py`'s list (`--target=arm-none-eabi -mfloat-abi=hard -march=armv6k` plus the game flags) and
run it. Fix what it reports beyond the baseline (in the game: portability fixes, own commits). Done when it reports
0 for `arm32`. A place to look in T1.5: the runtime's own C files are compiled without `-fsigned-char` (only the
game's are), and plain `char` is unsigned on ARM; the aarch64 build has the same property and plays like the
reference, so it is not a known bug.

**T1.5 (optional) The armhf build plays like the reference.** Under qemu with `ACWW_RUNNER="qemu-arm -L
/usr/arm-linux-gnueabihf"`: the T0.2 `quick` and `play` checks, and the town, save and continue scripts
(docs/host.md). Where it differs: `ACWW_RAMHASH` against T0.2's file to find the first frame, `ACWW_RAMDUMP` to
find the address (docs/host.md "Finding where two builds part"). Every fix is a portability fix in its own commit.
Done when screenshots, sound and save files are the same as the reference's. (If they differ only by a few
pixels from floating-point rounding, write down where and why instead of forcing them equal.)

### Phase 2: on the 3DS, CPU renderers

**T2.1 `--os 3ds` in configure_host.py. Done 2026-10-28.** The compilers from `$DEVKITARM/bin` (`DEVKITPRO`/`DEVKITARM` from the
environment, as devkitPro sets them; fail with a clear message without them), the flags of D2, include path
`$DEVKITPRO/libctru/include`, the link of D2, then `smdhtool --create "<title>" "<description>" "<author>"
meta/3ds/icon.png acww.smdh` and `3dsxtool acww.elf acww.3dsx --smdh=acww.smdh` (the commands of
`$DEVKITARM/3ds_rules`). Runtime files: the common ones plus `plat_3ds.c` and `frontend_3ds.c`, without
`plat_posix.c`, `plat_win32.c`, `frontend_sdl.c`, `frontend_ui.c`, `frontend_viewer.c`, the melonDS files
(`melon_none.c` stays), `wgpu_display.c`/`gpu3d_wgpu.c` compile as their stubs (no `HOST_WGPU`); `debug_posix.c`
and `guards.c` with `HOST_DEBUG_POSIX` 0 and no `HOST_GUARDS` (as on Windows: configure_host.py's `WIN` branch
shows how). Define `HOST_FRONTEND_3DS`. Done when `acww.3dsx` is built.

**T2.2 `plat_3ds.c`. Done 2026-10-28.** Every function of `plat.h`, with `plat_posix.c` as the model; for each libctru call, look
it up in the header first:
- time: `svcGetSystemTick()` to nanoseconds (`SYSCLOCK_ARM11` ticks a second); sleeping: `svcSleepThread`
  (nanoseconds);
- local time: `time()` + `localtime_r`;
- background thread: `threadCreate` (`3ds/thread.h`: priority 0x18-0x3f, the main thread's usually 0x30; core 1
  needs `APT_SetAppCpuTimeLimit` first and allows one thread). **Unverified**: how the 3DS kernel shares a core
  between threads (whether threads of the same priority are time-sliced). Until checked, keep the watchdog
  sleeping between its checks (it does: `hw.c`) and run it at a lower priority than the game (a higher number);
- stacks and pages: `memalign` + `memset` (no guard page: no `mprotect` for a 3DS application; say so in a comment);
- `host_plat_restart`: the simplest thing that works (back to the home menu), with a comment;
- image bounds, backtraces: none (return 0), as the platform can't;
- options: at start, read `sdmc:/3ds/acww/acww.cfg` (`NAME=value` lines, `#` comments) into `setenv`, so that every
  `ACWW_*` option of docs/host.md works unchanged; then log the memory numbers (1.3: `osGetMemRegionSize`,
  `envGetHeapSize`, `envGetLinearHeapSize`) to `sdmc:/3ds/acww/log.txt` (stderr there too).
Done when the program starts in Azahar and runs headless (`ACWW_HEADLESS=1` in acww.cfg, `ACWW_SHOT` to the SD
card, `ACWW_EXIT_FRAME`) to the title, and the memory numbers for Azahar's Old and New 3DS are written in 1.3.

**T2.3 The ROM from the SD card (D4). Done 2026-10-28.** `card.c`: open the file once; `host_card_read` and `card_read_data` read
through a small cache of 512-byte pages (`fseek` + `fread`; the SDK reads 0x200-byte pages, `card.c:1-3`).
`host_rom` keeps its meaning for `main.c`'s header copy (only the first 0x170 bytes). On the PC builds the
behaviour is the same (also through the cache, or keep `host_plat_read_file` there: whichever is simpler, the same
results either way). Fix the comment's 16 MB. Done when the T0.2 checks pass on x86-64 and the 3DS build reaches
the title with the ROM not in memory.

**T2.4 `frontend_3ds.c`: picture. Written 2026-10-28, not yet tried.** `frontend_backend_open` returns a plain `AcwwShm` (as `frontend_sdl.c` does);
`frontend_backend_frame` shows `pixels` (top screen, then bottom, `0x00RRGGBB`). First version, the simplest that
works: the two 256x192 pictures converted into the framebuffers (`gfxInitDefault`, `gfxGetFramebuffer`, top
centred in 400x240, bottom in 320x240; the framebuffers are column-major, rotated, see
`examples/3ds/graphics/bitmap`), `gfxFlushBuffers`, `gfxSwapBuffers`, `gspWaitForVBlank`. Done when the attract
demo is seen in Azahar.

**T2.5 `frontend_3ds.c`: input. Written 2026-10-28, not yet tried.** `hidScanInput` / `hidKeysHeld` to `keys` (`frontend.h`'s bit order: A, B,
Select, Start, Right, Left, Up, Down, R, L, X, Y; the circle pad as the D-pad), `hidTouchRead` to
`touch_x/y/down`, mapped from the bottom screen's 320x240 to the centred 256x192 (outside it: no touch).
`aptMainLoop()` returning false sets `quit`. Done when the intro can be played to the town in Azahar.

**T2.6 `frontend_3ds.c`: sound. Written 2026-10-28 (four 1024-frame wave buffers refilled
once a frame, no rate adjustment yet), not yet tried.** `ndspInit` (fails without `sdmc:/3ds/dspfirm.cdc`: tell the user then, and play
on silently), one stereo PCM16 channel at `ACWW_AUDIO_RATE` (32728 Hz), a few `ndspWaveBuf`s in `linearAlloc`'d
memory refilled from the `audio` ring (`audio_read` / `audio_write`, as `frontend_sdl.c`'s `audio_callback` reads
it, including holding the queue level). `examples/3ds/audio/streaming` shows the wave buffer cycle. Done when music
and effects play without gaps in Azahar, and the README says how to get `dspfirm.cdc`.

**T2.7 Save, sleep, the home menu.** The save is written through as on the PC (`pxi7.c`): check that a write
reaches the SD card before the home menu or power-off can end the program (close / `fflush` + `fsync`). On
`APTHOOK_ONSUSPEND` / `ONSLEEP` pause the game with `host_set_paused(1)` as the frontend's key binding
screen does (`hw.c:109`), on `ONRESTORE` / `ONWAKEUP` resume it with `host_set_paused(0)` (`aptHook`, `3ds/services/apt.h`). Done when
saving in game, leaving by the home menu and starting again continues the town.

**T2.8 On hardware: measure.** On a New 3DS with `osSetSpeedupEnable(true)`, and on an Old 3DS: frames a second in
the title, the town, a house, with `ACWW_FPS`. `ACWW_PROFILE` needs POSIX signals and is not there: measure with
`svcGetSystemTick` around the game's frame, `render_frame` (2D and the 3D geometry), the 3D rasteriser, the
frontend; print once a second. Write the numbers here. They decide the order of phases 3 and 4. Done when written.

### Phase 3: 3D on the PICA200

**T3.1 Map the features.** Read `gpu3d_wgpu.c` (its header comment lists what the game uses and what it doesn't),
`gpu3d_internal.h` (`G3RenderState`, the polygon list), `gpu3d_tex.h`, `gpu3d_raster.c` (the reference
behaviour), and the citro3d headers and examples (`examples/3ds/graphics/gpu`: `textured_cube`, `mipmap_fog`,
`both_screens`, `stereoscopic_2d`, `wide_mode_3d`). Write a table here, one row per feature `gpu3d_wgpu.c` does:
how the PICA does it, or that it can't and what instead. The facts of 1.3 decide most rows:
- textures: the DS's 7 formats decoded on the CPU into a PICA format (RGBA8 or RGBA5551 / RGBA4 to save memory),
  straight into the tiled layout `C3D_TexLoadImage` expects, cached by image address, format, size, palette
  address and a change counter from `vram.c`; the DS's sizes are 8-1024 in powers of two (GBATEK, TEXIMAGE_PARAM);
  check what `C3D_TexInit` accepts (its return value);
- repeat / flip / clamp: `GPU_REPEAT`, `GPU_MIRRORED_REPEAT`, `GPU_CLAMP_TO_EDGE` (`GPU_TEXTURE_WRAP_PARAM`);
- modulation / decal / vertex colours: TEV stages (`C3D_TexEnv*`);
- alpha test (`C3D_AlphaTest`), translucency (`C3D_AlphaBlend`; the DS's "larger alpha kept": alpha equation
  `GPU_BLEND_MAX`);
- the translucent polygon ID rule: stencil (`C3D_StencilTest`, `C3D_StencilOp`), as `gpu3d_wgpu.c` does it;
- Z-buffer / W-buffer: `C3D_DepthMap`;
- fog: the fog LUT from the DS's 32-entry table, offset and shift; whether the PICA's fog index (depth) can be
  made to follow the DS's: check and write down;
- clear colour, clear depth, the rear plane: as `gpu3d_wgpu.c`.
Done when every row is filled.

**T3.2 `gpu3d_pica.c`: polygons. First version 2026-10-28 (see the log).** The interface of `gpu3d_wgpu.c` (`g3w_init` / `g3w_render` / `g3w_picture`
become `g3p_*`), chosen in `render.c` with `ACWW_RENDERER3D=pica` (the default in the 3DS build). Per frame: the
geometry engine's polygons (screen positions with 4 fractional bits, depth, w, colour, texture coordinates) into a
`linearAlloc`'d vertex buffer; a vertex shader that passes them on; one draw call per run of polygons with the same
state; a `C3D_RenderTarget` at 256x192 first (transposed: 1.3). Done when the town's 3D looks like the CPU
rasteriser's picture of the same frame (render both: `ACWW_RENDER_COMPARE`'s idea, docs/host.md).

**T3.3 `pica_display.c`: composite. Done 2026-10-28 (see the log; differs from this first design).** The 3D picture and the 2D renderer's deferred words put together, as
`wgpu_display.c` does; its shader and the comment above `gpu2d_deferred` (`gpu2d/gpu2d.h:55-67`) are the rules.
Without fragment shaders, as textured quads: the CPU turns each screen's deferred words into two 256x192 textures,
UNDER (the colour below the 3D layer, opaque) and OVER (colour and alpha in front of it), written linearly and tiled
by the display transfer (`GX_TRANSFER_OUT_TILED`); the PICA draws UNDER, the
3D picture blended with its own alpha, OVER blended with its alpha. The kinds (gpu2d.h):
- 0, `A` final: OVER = A, opaque;
- 1, C where P is opaque, else A: whether P is opaque is known only on the GPU. UNDER = A; OVER = C, drawn with
  the 3D picture bound as a second texture (`GPU_TEXTURE1`) whose alpha a TEV stage passes on, and an alpha test
  that keeps only the pixels where it is opaque. The same "where P is opaque" mask is needed by kinds 2 and 3.
  **Unverified** that the TEV and alpha test do this exactly: try it first in T3.3, write the result here;
- 2, `min(63, (C*p1 + P*p2 + 8) >> 4)` where P is opaque: a sum, not a mix. One idea: the 3D picture drawn scaled
  by p2/16, then C*p1/16 added (additive blending saturates like the `min`). **Unverified**: the alpha test works
  on the TEV's output alpha, so one pass can't carry both the p2/16 factor and the "P opaque" mask; it may need two
  passes or the stencil buffer. Work it out in T3.3 and write it here;
- 3, P over C: UNDER = C; e = 1 the 3D picture's alpha; e = 2 / 3 (brightness up / down by p1) a white / black
  OVER with alpha p1/16 where P is opaque (`P + ((63 - P) * p1 + 8) >> 4` is a mix towards white, exact up to
  rounding);
- the master brightness (`gpu2d_deferred_lines`, per line): white / black quads per run of lines with the same
  factor.
The 2D pictures and the 3D picture then go to the screens: the top one scaled by 1.25 to 320x240 and centred, the
bottom one 1:1 centred (T2.5's touch mapping stays right). Check at 256x192: read the result back and compare with
the CPU renderers' `render_screen` for the same frames; write the differences that remain here. Done when the
title, the town and a house look like the CPU renderers' pictures.

**T3.4 Resolution.** The 3D target at 2x (512x384) and reduced by the display transfer's `GX_TRANSFER_SCALE_XY`
(2x2 averaging: the edge smoothing, like the PC's `ACWW_SCALE3D` + MSAA), then scaled to 320x240 like the 2D
layers; an option `ACWW_3DS_FILTER` for the 2D layers' filter (nearest / linear). Wide mode (1.3) as an option:
the top screen at 800x240 (640 for the picture), not on the Old 2DS, not together with T4.1. Done when the options
work and their cost in frames a second is written here.

### Phase 4: beyond a DS

**T4.1 Stereoscopic 3D.** When the 3D slider is up (`osGet3DSliderState`, 0.0-1.0): the 3D picture drawn twice,
left and right eye, each polygon's x moved by a parallax from its w (larger for far points, 0 at the depth chosen as
the screen plane); `gfxSet3D(true)` and a render target per eye (`examples/3ds/graphics/gpu/stereoscopic_2d`).
The 2D layers stay at screen depth. Nothing in the geometry engine changes: the shift is in the vertex shader as a
uniform. Tune the strength and the screen plane in the town. Done when the town has depth that is comfortable to
look at; menus and text unchanged.

**T4.2 Local play between consoles.** `wm7.c` sends the radio's traffic as UDP datagrams through its POSIX branch.
The 3DS has BSD sockets after `socInit(buffer, size)` with a 0x1000-aligned buffer (`3ds/services/soc.h`). Check
which calls of `wm7.c` libctru's sockets support (broadcast, non-blocking, `getaddrinfo`), adapt, and test two
3DSes, then a 3DS and a PC, with the gate (`tools/host_wireless_test.sh` is the PC's test). Done when a visit
works.

**T4.3 Wider view (experiment).** Whether the top screen can show 400 pixels wide instead of 256 by widening the
game's projection. The game may cull objects outside the DS's view, so things could pop in at the sides: test,
write the result here either way.

**T4.4 Old 3DS.** With T2.8's numbers: what has to get faster for 30 or 60 frames a second at 268 MHz.
Candidates: the 2D layers only where they changed; the geometry engine's hot loops; `-O2` for the runtime; the
rendering on core 1 (`APT_SetAppCpuTimeLimit`) while the game runs on core 0.

**T4.5 A `.cia`** (optional): with `makerom` and a banner, for installing to the home menu. Check then whether
the exheader can enable core 2 on the New 3DS (1.3).

## 5. Risks

- **Speed** (D7): the largest unknown; measured in T2.8 so that the plan can change early.
- **Memory**: 20 MB of static data before any heap, texture cache, render targets and stacks (512 KB a game thread,
  `os.c`); how much an application gets is unverified (T2.2). If it is too little: the 2D renderer's and the
  geometry engine's buffers are sized for the worst case and can shrink.
- **ARM32 behaviour**: checked by comparing the 3DS build's headless runs with the PC's; qemu (T1.3-T1.5) if
  a difference needs it.
- **Composite without fragment shaders** (T3.3): kind 1 needs a per-pixel decision on the 3D picture's coverage;
  if the PICA can't do it cleanly, the fallback is compositing on the CPU at 256x192 with the PICA's 3D read back
  (lower resolution, still the PICA's speed).

## 6. Log

- 2026-10-28: plan written; the checks of section 1 done (trial build in a scratch directory, nothing committed);
  the PC build's crash reproduced (T0.1).
- 2026-10-28: T0.1 done: GCC 14. The armhf/qemu step made optional (phase 1).
- 2026-10-28: the 3DS build (`python3 tools/configure_host.py --os 3ds && ninja -f build_host_3ds.ninja`) runs in
  Azahar: headless (`acww.cfg`: `ACWW_HEADLESS=1`, `ACWW_FAST=1`, `ACWW_SHOT=300:sdmc:/3ds/acww/a-300.ppm`) its
  frame 300 (the attract demo's 3D town) is byte for byte the x86-64 build's. Found on the way:
  - The runtime takes the game's 32-bit enums (`-fno-short-enums`, arm-none-eabi's default is the smallest size);
    the files that use libctru's structures keep the library's (configure_host.py `N3DS_NATIVE`).
  - GCC folds `page + (addr - 0x04000000)` into the constant `page - 0x04000000`, an address outside the program
    that a .3dsx can't relocate: Azahar refused the program ("A generic load error", `Failed to load ROM (Error
    1)`). Fixed with an empty asm in `hw.c` and `gpu2d.c` (`opaque`); `tools/host_3dsx_check.py` checks every
    build's relocations (the 3dsx rule fails on one).
  - Azahar's New 3DS: application memory 96 MB, heap 64 MB, linear heap 32 MB (`osGetMemRegionFree` reports
    nonsense there). The 32 MB ROM didn't fit beside the program: T2.3.
  - Speed in Azahar: about 0.6 frames a second (unpaced). Samples with Azahar's GDB stub (`-g <port>`,
    `arm-none-eabi-gdb`, interrupted every 4 s): most in the CPU renderers - the 2D layers (`render_line`) and the
    3D rasteriser (`draw_polygon`, `interp` with a 64-bit division per pixel: the ARM11 has no divide instruction);
    a few in SD card reads; the game's own code hardly. Azahar's speed isn't the hardware's (T2.8 measures that),
    but the 3D rasteriser on the CPU is out of the question: phase 3 next.
- 2026-10-28: T3.2, first version: `gpu3d/gpu3d_pica.c` draws the geometry engine's polygons on the PICA200 at
  256x192 and reads colour, depth and the fog flag (stencil bit 7) back into the software rasteriser's buffers; its
  fog pass and the CPU composite follow (`ACWW_RENDERER3D=pica`, the 3DS's default; `own` for the CPU). In Azahar
  3-4.5 frames a second (was 0.6). Frame 300 of the attract demo against the PC's CPU picture: the 2D screen
  identical, the 3D one close (mean difference 14 of 255, 13684 pixels differ by more than 8). Found:
  - the GPU's texture v runs from the bottom: v = 1 is the first row in memory (`o->tc[1] = 1 - t / h`); the
    framebuffer read back by the display transfer comes top row first;
  - the depth is not reversed (citro3d's own set-up is): the GPU computes depth in 24-bit floats, fine near 0 and
    coarse near 1, and the DS's depths are small; depth test "equal" gets the DS's margin of 0x200 (moved nearer,
    then less-or-equal); the W-buffer uses the GPU's W-buffer mode (`C3D_DepthMap(false, ...)`).
  - Open: the window left of the house in that frame is missing (a hole to the clear colour: some polygons not
    drawn - which and why is to be found); no anti-aliasing; the composite is still the CPU's at 256x192 (T3.3).
- 2026-10-28: speed, measured in Azahar (headless, the attract demo, `ACWW_FPS=1`: frames a second and the
  renderers' milliseconds a frame) and with samples through Azahar's GDB stub (`-g <port>`, `arm-none-eabi-gdb`,
  interrupted every second or so). Frames a second, step by step: 0.6 (all on the CPU) - 3-4.5 (the 3D on the
  PICA, T3.2) - 4.7-8.7 (texture slots gathered only after a VRAMCNT change: `vram.c host_vram_generation`; a 1 MB
  ROM cache; `-O2`) - 5.5 (no divisions in the read-back) - 8.2 (frameskip: `ACWW_FRAMESKIP`, the 3DS's default 1;
  the game runs every frame) - 13.2 (the compositing on the PICA: `pica_display.c`) - 15.7 (the 3D picture
  straight to it, not read back; the game's "alpha only" fog in the vertices' alpha) - 16.1 (texture lookups) -
  19.5 (the BGs drawn by the PICA from VRAM: textures of whole BGs, updated tile by tile; the windows as regions in
  the stencil buffer; the passes batched over groups of lines: the top screen's per-line affine effect had made
  1350 draws a frame). Frame 300 stays within rounding of the PC's picture throughout.
  Where the time goes now (150 samples): the 3D geometry engine 31 %, the game's code 21 %, the 2D renderer on the
  CPU (OBJs, the windows' regions) 14 %, the 3D renderer's CPU side 12 %, DMA 6 %, registers and events 5 %, sound
  5 %, the compositing's CPU side 4 %. Azahar's speed isn't the hardware's: T2.8 is still to be measured.
  Not yet seen on a screen: the GPU's pictures on the 3DS's screens (`Mtx_OrthoTilt`); the screenshots are read
  back from the composited pictures, not from the screens.
- 2026-10-28: first measurement on hardware (T2.8), a New 3DS, the build of commit 48b781ca (`ACWW_FPS=1`):
  about 29 game frames a second (frameskip 1: half of them drawn); the renderers 2D 3.5 ms, 3D 2.4 ms, compositing
  4.0 ms a frame on average - so about 24 of the 34 ms a frame are the game, the geometry engine and the runtime.
  Hardware is about 1.5 times Azahar's speed. In Azahar the window left of the house is black and the shadow under
  the player missing; on the hardware both are right (an Azahar inaccuracy). A watchdog report at the end of the
  user's run: probably the home menu (T2.7, pausing there, isn't done yet).
- 2026-10-28: the time outside the renderers, measured on the PC without drawing (`ACWW_FRAMESKIP=100000000
  ACWW_FAST=1`, samples with gdb: `perf` and attaching are not allowed on that machine, the program runs as gdb's
  child): the geometry engine 61 %, register accesses 16 %, DMA 9 %, the game's own code 4 %. Exact speed-ups (the
  save script's screenshots, save file and sound byte for byte the same): 64-bit divisions in 32 bits where they
  fit, two divisions a vertex for the screen position instead of four, the feature census inline, no diagnostic
  vertex copies, FIFO entries run at once when nothing is queued, two getenv calls per polygon / light command
  gone: 20000 frames without drawing take 13.4 s instead of 14.8 on the PC. `lighting` (once a normal) is the
  largest single function now; `vram.c remap` (every VRAMCNT change refills all windows) about 3 %.
  Next, the largest lever: the GPU work (gpu3d_pica.c, pica_display.c) on a second core while core 0 runs the
  next frame (core 1 needs `APT_SetAppCpuTimeLimit`; core 2 of the New 3DS only with an exheader flag - try, fall
  back). The frame's inputs must stay stable meanwhile: Gpu2dLayers double-buffered and the BG palette copied, the
  polygon list (its RAM bank is already double), the texture slots (rewritten at the next raster frame). Then the
  smaller exact items (remap, the DMA path through the register layer), and T2.7 (home menu, sleep).
- 2026-10-29: `ACWW_FPS` has a sampled profile now (`profile:` lines, milliseconds a frame per part; commit
  cc2a137e): the game's thread marks the part it is in, a sampler thread counts every millisecond (on the 3DS on
  another core: `3ds: the profile's sampler on core N` in the log; Azahar's New 3DS gives core 2). In Azahar
  (headless, the attract demo, 21.5 frames a second = 46.5 ms): geometry 14.7, registers 7.3-8.5, 2D 6.7, 3D 6.3,
  compositing 3.6, DMA 2.9, game 2.2, sound 2.2, frontend and pacing 0. The `render:` line's 3D leaves out the 3D
  picture drawn in the skipped frames (frameskip 1 draws it in the frame before the screens'): the profile's 3D is
  about twice that line's. Next: the user's run on hardware, then the order of the speed-ups.
- 2026-10-29: the profile on hardware (a New 3DS, the build of commit 2eb3892d, `ACWW_FPS=1`): the sampler got
  **core 2** (`APT_SetAppCpuTimeLimit(30)` returned 0), so a .3dsx has a second core there. About 30 frames a
  second (34 ms); milliseconds a frame from frame 360 on: geometry 7.9-8.7, 3D 6.0-7.8, registers 3.7-6.2, 2D
  3.0-3.9, game 3.5-4.1, compositing 3.1-3.7, sound 1.4-2.0, DMA 1.1-1.4, frontend 0.1-0.2. Frames 120-240 (the
  start, ROM reads from the SD card through the card registers) registers 29.7. The 3D part includes waits for the
  GPU (`C3D_FrameBegin(0)` waits for the work before it) and the texture decoding: not yet split. The watchdog
  report at the end again (the home menu, T2.7).
- 2026-10-29: the game swaps the 3D buffers every third frame (the save script on the PC: nearly every gap 3), so
  its 3D runs at 20 frames a second and every polygon list is drawn sometime: the geometry engine has no frames
  whose work could be skipped. What was double instead: frameskip 1 drew some lists twice. Now a list is drawn
  once (`g3_render`: no new picture without a swap or a VRAMCNT change; byte for byte the same at frameskip 0 and
  1). In Azahar the profile's 3D 6.3 -> 3.4 ms, 21.5 -> 22.7 frames a second. The profile shows the waits for the
  GPU (`GPU wait`: frame begin, display transfers; 0 in Azahar) and the geometry engine's lighting and vertices
  apart (Azahar: lighting 2.7, vertices 5.7, the rest of it - FIFO, matrices, timing - 6.2 ms). Azahar's
  function-level samples aren't the hardware's (`lighting` is ~250 instructions without calls, ~900 a frame).
- 2026-10-29: on hardware with a list drawn once (commit b45a8d9e): about 33 frames a second (30 ms, was 34).
  Milliseconds a frame from frame 360 on: registers 4.4-5.6, geometry 4.1-5.1 + vertices 3.0-3.4 + lighting
  1.2-1.5, game 3.8-4.0, 2D 3.0-3.8, 3D 2.3-2.5 (was 6.0-7.8), compositing 2.2-2.5, sound 1.6-1.9, DMA 1.0-1.4,
  GPU wait 0.5-0.7, frontend and pacing 0.1-0.3. So the waits for the GPU are small; what has to stay on the
  game's core (game, registers, DMA, the geometry engine, sound) is about 21 ms, the renderers about 9.
- 2026-10-29: the registers: 53 % of the game's register accesses are words for the 3D command FIFO from
  `MIi_CpuSend32` (`ACWW_IOHIST`, the save script); in the samples the register layer's time is its dispatch per
  access, and every access worked out the GX FIFO interrupt's level although the game never enables it (IE
  0x4000b). Exact: that level only when enabled, the FIFO's blocks through `HostIo_Send32`, DMA memory to memory as
  one copy. Azahar: registers 7.8-8.6 -> 5.9-6.9 ms, DMA 3.0 -> 2.7, 22.7 -> 22.8-24.3 frames a second.
- 2026-10-29: on hardware with the register layer's speed-ups (commit 280b8768): about 37 frames a second (27 ms,
  was 30); registers 3.0-4.4 ms (was 4.4-5.6). Next: the renderers on core 2.
- 2026-10-29: the GPU thread (commit c63dd6a7): the 3D pictures and the compositing are jobs on core 2
  (`pica_display.c` `pdisp_submit` / `pdisp_idle`, one at a time); the game's thread waits for it before it changes
  what a job reads (the 3D engine's list and textures, the 2D renderer's layers) and copies what the game changes
  at any time (VRAM and the BG palettes: the maps and tile data of the BGs the GPU draws, about 29 KB a frame in the
  attract demo). The 2D renderer (OBJs, the windows, the CPU's BGs) stays on core 0: it runs between the game's
  H-blank handlers. Checked in Azahar: the hashes of all 756 composited pictures of frames 0-1510 are the same with
  the thread, with it and 5 ms delays per job (`ACWW_PICA_DELAY`), and without it (`ACWW_PICA_THREAD=0`); the
  screenshots are the build before's. Azahar runs the cores one after another, so its frames a second (23.7 ->
  25.8) say little: the hardware decides. The `render:` line's 3D and compositing are now only the submitting.
- 2026-10-29: exactness of the GPU thread with the game played (the save script to frame 12000: new game, the town,
  menus, a house; `acww.cfg` lines can be 16 KB now for its `ACWW_KEYS`): the 6001 composited pictures' hashes are
  the same with the thread and 5 ms delays per job as without it, for the first version and for the queue.
- 2026-10-29: on hardware the first version of the GPU thread gave nothing (37 frames a second): the game's thread
  waited about 4 ms a frame for it while it was idle three quarters of the time. The game works every third frame
  and the frames between are short, so waiting for the last job each frame waited for work just submitted. With a
  queue and waits only for the job whose inputs change (commit after 36943403): about 43 frames a second (38.5-46,
  23 ms), waiting 0.1 ms. The game's core: the geometry engine 8.0 (4.0 + vertices 2.7 + lighting 1.3), registers
  4.7 (3-10), game 4.2, 2D 3.2, sound 1.1, DMA 0.9, the jobs' submitting 0.7. Core 2: 3D 2.4, compositing 2.5,
  waiting for the GPU 2.8, idle about 15. Also fixed: a data abort on core 2 when the game was closed from the home
  menu (the sampler woke while the program's end had freed its stack; the background threads stop first now).
  Next: what the registers' time is (ROM reads through the card registers?), the geometry engine, then the 2D
  renderer on core 2 (record each line's state on core 0, draw on core 2).
- 2026-10-29: the 2D renderer's layers on core 2 too: on the game's core the 2D renderer only records each line's
  state (the line loop with the H-blanks, as for the PC's GPU display: `gpu_line`, the palettes' and OAM's
  versions) and copies VRAM's 2D banks; the GPU thread draws the layers (`gpu2d_draw_layers`) before compositing.
  Exact in Azahar (attract demo and save script, with and without the thread and delays). Also the registers:
  ROM reads have their own part in the profile, a 3D command block after its first word goes at once when nothing
  between can make an interrupt due (Azahar registers 5.9-6.6 -> 5.1-5.6 ms); the geometry engine copies a
  polygon's vertices only when a plane cuts it (Azahar vertices 5.7-6.0 -> 4.7-5.3 ms). Without drawing on the PC
  20000 frames take 12.5-12.8 s (was 13.6-13.9 this morning).
- 2026-10-29: on hardware with the 2D layers on core 2 (commit 20436c6e): about 43-45 frames a second, 45-49 in
  quiet stretches; the game's core still had 2D 2.3 ms (the record: the sky's H-blank writes a palette colour every
  line, and a new palette version was kept and 4 KB compared a line) and waited 0.9 ms for the GPU thread (its
  compositing job, now with the 2D layers, was sometimes not done after short frames). Then: only the changed words
  kept; two records in turn, the game's thread waits only for the compositing before the last. Exact in Azahar.
- 2026-10-29: on hardware with the 2D record's fixes: 39-45 frames a second, the register time 3-10 ms and the slow
  stretches exactly those with much of it; the ARM7's side (now its own part) 0. Samples in Azahar in the town (the
  save script; its ACWW_KEYS was read again every frame, about 45 % of the samples: read once now, the PC's 20000
  frames 12.5 -> 10.4 s): the geometry engine about 47 % with its copies, VRAM's remapping about 10 % (inside
  register writes: every VRAMCNT change moved megabytes, several changes a frame while textures load). Now only the
  granules whose bank changes move; a new polygon is zeroed without its diagnostics copy. Exact.
- 2026-10-29: on hardware with VRAM's remapping fixed (commit 90660839): about 52-55 frames a second, the register
  time 1.4-2.2 ms (was 3-10), now and then pacing (the game ahead of 60). The game's core about 18.3 ms: the
  geometry engine 7.3 (3.7 + vertices 2.4 + lighting 1.2), game 4.1, registers 2.0, the 2D record 1.8, sound 1.1,
  DMA 0.9; core 2 idle about 7 of them.
- 2026-10-29: on hardware with the game at -O2 (commit d3eab214): 45-51 frames a second, the game's code about the
  same (3.8-4.2 ms), the geometry engine more (about 9 ms): another scene, so runs played by hand don't compare.
  A benchmark on the hardware: the save script plays itself, unpaced, with a fixed clock and no save file, the
  same game frame for frame each run; its `acww.cfg` (copy it to `sdmc:/3ds/acww/acww.cfg`, the usual one aside):

      { printf 'ACWW_FPS=1\nACWW_FAST=1\nACWW_WATCHDOG=0\nACWW_RTC=2026-04-01 12:00:00\nACWW_SAVE=\nACWW_EXIT_FRAME=12000\n'
        python3 tools/host_play_script.py save | sed "s/^export //; s/='/=/; s/'\$//"; } > build/host3ds_usa/acww-bench.cfg

  (`ACWW_SAVE=` empty: a blank save each run, nothing written.) Runs in Azahar with a window (to frame 3000).
- 2026-10-29: the benchmark on hardware, -O1 against -O2: -O2 makes the game's code about 5-8 % faster (about 2 %
  overall); kept. The benchmark's second half (the name entry, frames 4300-7700, about 56 frames a second; the
  town, 62-72) is bound by core 2: its 2D (OBJs, windows, the layers' conversion) up to 8.6 ms and its waits for
  the GPU about 4.5 ms a frame. Tried and reverted: drawing the layers straight into their textures in the GPU's
  tile order instead of display transfers made it slower (48 and 56-64): the GPU thread then has to wait for the
  GPU before drawing the layers instead of drawing them while the GPU works, and the tiled writes cost the CPU more.
- 2026-10-29: where core 2's waits come from: the layers' uploads cost nothing (the profile's new `uploads` part),
  the GPU itself takes 11-15 ms to composite a frame and 2-4 ms to draw a 3D picture (C3D_GetDrawingTime: the
  `pica: the GPU's drawing` lines). The compositing reads its textures many times over; they were in linear (main)
  memory. With the layers' textures in VRAM (commit 770d88be; 989 KB of VRAM left): compositing 9-10 ms, and in the
  benchmark the name entry about 60 frames a second (was 56), the intro 69-80 (63-71), the town 69-81 (62-72). The
  benchmark's start (frames 360-2640, 47-52) is bound by core 0: the geometry engine about 8.5 ms a frame there.
  Next: the geometry engine; the BG textures (also in linear memory) to VRAM; the OBJs on the GPU.
- 2026-10-29: the cursor behind the "New game" menu (the user saw it): the compositing drew a priority's layers in
  the wrong order (the OBJs first, BG0-3 over them), so an OBJ was behind a BG of its own priority; fixed (the CPU
  renderer's picture at frame 2480 of the save script). The geometry engine in the title's 3D (samples in Azahar):
  about half of it is the command path, a word at a time; the runtime at -O3 without frame pointers on the 3DS
  (Azahar: the geometry engine 13.9 -> 11.6 ms, the game's core 24.6 -> 23.4), a strip's shared vertices turned
  instead of copied (vertices 4.5 -> 3.9). All exact.
- 2026-10-29: the benchmark on hardware with the runtime at -O3 and the strips turned (commit da8532aa): the start
  (frames 360-2640) 47-54 frames a second (was 47-52), the geometry engine 7.5 ms a frame there (4.1 + vertices 2.3
  + lighting 1.2; was about 8.5); the name entry about 60 (core 2: its 2D 8.4 ms, the GPU 3.1), the intro and the
  town 69-81 as before. -O3 kept. Only the start is below 60 now; core 0 there: the geometry engine 7.5, game 4.2,
  registers 2.0, the 2D record 1.7, sound 1.2, DMA 1.0 (about 19 ms).
- 2026-10-29: the title's 3D in Azahar, function by function (180 samples): most of the geometry engine's commands
  come through the GX FIFO's DMA, and each word went through the register layer (io_write, forward_display,
  render_write32) before the engine. The DMA to a 3D command port now hands its words to the engine as one block, as
  `HostIo_Send32` does (commit 88ac3b5e): Azahar's attract demo, core 0 22.7 -> 20.2 ms a frame (the `DMA` part
  2.6 -> 0). Exact (the PC's save script byte for byte, Azahar's 6001 pictures). Still there: the DS divider's
  64-bit divisions (`__udivmoddi4`, FX_Div's 64/32) about 3 % of the samples, the clipping's vertex copies (124
  bytes, a third of them diagnostics) about 5 %, the 2D record's comparisons about 8 %.
- 2026-10-29: the benchmark on hardware with the GX FIFO's DMA as one block (commit 88ac3b5e): the start (frames
  360-2640) 52-59 frames a second (was 47-54), the geometry engine 6.7 ms a frame there (3.3 + vertices 2.2 +
  lighting 1.2; was 7.5), DMA 1.0 -> 0.1; the rest as before (the name entry about 60, bound by core 2). The sound
  in the benchmark (`ACWW_FAST=1`, unpaced) can't be right: slower than real time the DSP's queue runs dry
  (crackles), faster the ring overflows and `feed_audio` skips ahead (jumps).
- 2026-10-29: on hardware, paced (the user): the hitch every 2 seconds was `ACWW_FPS`'s report, written to the SD
  card unbuffered, piece by piece (stderr is line-buffered now: commit 0f239ede). Still: frames of 50-90 ms (the
  `fps:` line's slowest frame now) and odd sound, worse in the bus. Two causes found: (1) the sound fed a 1024-frame
  buffer as soon as one was waiting, so the DSP had at most about 30 ms queued and every hitch ran it dry; now a
  level near 2.5 buffers held by playing up to 0.5 % faster or slower, three buffers waiting after it ran dry
  (`sound:` line with `ACWW_FPS`: how often). (2) The PICA's texture cache decoded every 4x4 texture again when any
  palette byte changed (the game animates palettes): about 20 textures, 100,000 texels, every few 3D pictures, 40-60
  ms of the GPU thread in Azahar; now each 4x4 texture keeps the palette bytes its blocks use. Azahar's title, paced:
  47-50 -> 50-54 frames a second, the slowest frames 85-110 -> 50-60 ms (the 3D frames' geometry). Exact (6001
  pictures). The `fps:` line is followed by the slowest frame's parts (milliseconds per part, both threads).
- 2026-10-29: the odd sound (the user: still with the queue full) was the sound itself: the 3DS build's mixed sound
  wasn't the PC's from 0.63 s on (`ACWW_WAV` in Azahar against the x86-64 and i386 builds' identical files; never
  compared before, only pictures). Channel 4 started with a wave's format, timer and length wrong: a sound file's
  32-bit slots hold file offsets below 0x02000000 and pointers above (include/host_ptr32.h), and on the 3DS the
  program and its data are at 0x00100000 on, so the address of a wave loaded on its own was read as an offset. Such
  pointers are handles now (commit ea87cbf6: `HostPtr32_Of`). The save script's 12000 frames in Azahar: the
  sound (6.5 million frames) byte for byte the PC's, the 6001 pictures exact. Check the sound too from now on
  (`ACWW_WAV=sdmc:/3ds/acww/s.wav`, compared with the PC's from 44 bytes on). Elsewhere the runtime may also take
  an address below 0x02000000 or in DS ranges for something else: the 3DS's program reaches about 0x0250_0000.
- 2026-10-29: on hardware with the sound fixed (the user: right now, a little scratching on the title screen):
  paced, about 60 frames a second everywhere. What's left in the log: (1) with `ACWW_FPS` the frame after each
  report takes 45-70 ms (the report's six lines, about 10 ms a write to the SD card, under `game`): only in the
  measuring runs, but it can run the sound's queue dry where the title has 2-3 ms to spare. (2) The loads: frames
  of 0.3-1.5 s (ROM 200-860 ms, registers 120-430 ms) - the DSP's queue runs dry each time (the DS's ARM7 keeps
  playing through its loads). (3) The save: frames of 120-170 ms in `ARM7` for about 4 s (the save memory's
  writes to the SD card).
- 2026-10-29: the user's three points. (1) The scratching with deep notes, in time with the music: ndsp's default
  clipping is soft (it squeezes loud peaks); the mixed sound comes final, so normal now (commit 264647a9; the PC's
  SDL plays the samples as they are). Not heard here: Azahar has no DSP firmware. (2) The ROM image is held in
  memory when it fits with 12 MB to spare (card.c: each 64 KB piece read at its first access; the Old 3DS keeps the
  1 MB cache): on the New 3DS it does - at the save script's frame 12000 the heap's arena is 37.5 MB of 55 MB
  with the image. Azahar's first big load (frame 183): ROM 1046 -> 558 ms; `registers` stays at about 500 ms there
  (not looked into). (3) The save memory goes to the file once the game has written nothing for 30 frames, and
  before the program ends, the home menu and sleep (pxi7.c `pxi7_save_flush`): one write instead of hundreds of
  page writes, each flushed. The PC's save script byte for byte (its save file too), Azahar's 6001 pictures and the
  sound exact.
- 2026-10-29: open, left for later (the user): on hardware the sound still scratches with loud deep notes in the
  title's music, also with the console's volume very low (so not the speakers' level) and with the DSP's clipping
  normal (commit 264647a9). The mixed sound is the PC's byte for byte and never clips (peaks about 22000 of 32767
  in the title). Not yet tried: headphones; `ACWW_VOLUME=50` (commit 55c2295a: the channel's mix, which acts before
  the DSP's output stage); the PC build or a DS at the same place; the DSP's interpolation (`NDSP_INTERP_NONE` /
  `POLYPHASE`) and a fixed rate (the queue level's rate changes every frame).
- 2026-10-29, for later (the user): ZR as a second B (running) on the New 3DS - tried in commit 6fac6f91 and taken
  out again for now. Also wanted later: the map on ZR, the pockets on X, pulling weeds and shaking trees on Y.
- 2026-10-29: the wide view, step 1 (the user: "more of the world at the same pixel size", as emerald-3ds; no
  change to the game): `ACWW_WIDE=1`. In the town the game uses no display capture and no screen swap (POWCNT
  0x020f, DISPCAPCNT not written there): the 3D world is on the bottom screen only, the top one is 2D (the sky).
  The geometry engine clips the x and y planes at +-25/16 w and +-5/4 w (a 400x240 picture, the DS's 256x192 in
  its middle; `gpu3d_set_wide`, the screen positions not wrapped), gpu3d_pica.c draws into 512x256 pictures,
  pica_display.c samples the DS's part for the compositing and fills the margins: the backdrop, the wide 3D
  picture, the master brightness of the middle line. The layers' textures then live in linear memory (the 3D
  pictures take the VRAM: 2013 KB left), so the compositing's GPU time goes up again. In Azahar the attract
  demo's town fills the bottom screen (320x240) without a visible seam. Normal mode unchanged: the PC's save
  script byte for byte, Azahar's 6001 pictures and the sound exact. Next: on hardware, whether the game draws
  the objects near the edges early enough (it may cull what the DS wouldn't show), the speed; then step 2 if
  needed (the game's culling, behind an option). Not done: more polygon RAM (the DS's 2048 / 6144 limits now
  count the margins' polygons too: watch for missing polygons), the margins' fog colour and per-line effects.
- 2026-10-29: the wide view on hardware (the user: "looks great"): the town about 60 frames a second, the
  compositing's GPU time 13-15 ms a composited frame (was 9-11: the layers' textures in linear memory). The text
  box was cut off: it is at the DS's bottom edge, and the 3D world went on below it; now the DS's picture sits at
  the screen's bottom edge (`GPU3D_WIDE_TOP` = 48 lines of world above, none below; the touch screen follows,
  `pdisp_touch_y0`). For later: the screen transitions (black shapes closing in from the edges, an iris) cover only
  the DS's picture, so the margins show through them.
- 2026-10-29: the circle pad walks at any angle and as fast as it is tilted (commit dfdf6c71, `ACWW_ANALOG=0`
  off): `Pad_Update` takes the pad's angle (include/host_input.h) instead of the D-pad's eight, and the player's
  walking speed follows the tilt in the D-pad branch (as the stylus's distance does; a light tilt walks at a
  quarter, B still runs). Under HOST and only while the pad holds a direction: the PC's save script byte for byte.
  The wide view's DS picture is centred again (the user: "the middle stays the middle"; c8c749b0 reverted).
  The wish behind it: the text boxes at the screen's bottom edge. They are 2D (in the bus: the main engine on the
  top screen, BG0 3D, BG2 and the OBJs); moving the 2D layers down separates them from the 3D world (24 lines),
  and on the bottom screen the touch screen can then match only one of the two.
- 2026-10-29, for later (the user): the wide view's text boxes end at the DS's old bottom edge (24 lines of world
  below them); to be solved later (options: the 2D layers moved down on the top screen only, where there is no
  touch; or on both screens, the touch then following the 2D layers).
- 2026-10-29: the touch screen gave every touch at 0,0 (the user: "recognised, but only binary"), on the PC too:
  nothing wrote the firmware's user settings to HW_NVRAM_USER_INFO (0x027ffc80), where the DS's boot firmware
  leaves them, so `TP_GetUserInfo` read a calibration of 0 and `TP_GetCalibratedPoint` made every point 0,0 (the
  runtime's own inverse, `tp_raw`, read them from the flash). Now pxi7.c copies the newer valid copy there (0x70
  bytes, before wm7.c's MAC address) and `tp_raw` reads the same place: touches at 18,14 / 238,174 / 0,0 / 255,191
  / 128,96 reach the game as 18,14 / 237,173 / 0,0 / 254,190 / 127,95 (the integer calibration's rounding, as on
  a DS). The PC's save script (screenshots, sound, save file) and the oracle's quick run are byte for byte the
  build before's. Seen with Azahar on a virtual X server (Xvfb) and xdotool's clicks: the 3DS's mapping
  (frontend_3ds.c) was right all along.
- 2026-10-29: the wide view's 2D screen (the user picked "the 2D layers drawn on beyond the DS's screen"). What
  the game shows there (the PC build's registers and VRAM): in town the top screen is the sub engine's BG3 alone, an
  affine BG with wrap on (the clouds, 256x256, each line with its own reference point) over a gradient that comes
  from the per-line state; on the title the same BG3 and the logo on BG2 (a text BG) and OBJs (the stars); indoors
  only OBJs (black); in the bus's name entry the 2D screen is the bottom one (the keyboard: text BGs and OBJs).
  pica_display.c `margins2d`: on the screen without the wide 3D picture the engine's line groups are drawn again
  around the DS's picture, with only the backdrop, the affine BGs that wrap, the colour effects and the master
  brightness (the first and last line's state for the 24 lines above and below); OBJs, text BGs and the CPU's BGs
  stay in the middle. The screens' targets get a stencil buffer in the wide view for that (VRAM free 2013 -> 1338 KB).
  In Azahar the title's night sky fills the top screen without a seam (the gradient's bands and a cloud across the
  old edge continuous); the keyboard stays 1:1 in its place, its margins black (its backdrop). The composited
  pictures' hashes (`ACWW_PICA_HASH`, attract demo to frame 1500) are the build before's, with and without
  `ACWW_WIDE`. Not seen yet: the town by day, on hardware. Seen in the bus with and without this change: a grey
  slab in the 3D picture's lower left on some frames (Azahar's wrong window, or the wide 3D's own margins: open).

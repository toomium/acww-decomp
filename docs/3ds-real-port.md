# A real 3DS port (experiment, plan)

Branch `3ds-real-port`, from `3ds` (2026-10-29). The `3ds` branch runs the game on a rebuilt DS: the game and the
NitroSDK write the DS's registers and VRAM, and the runtime (`modern/host/`) plays the hardware's part (geometry
engine, 2D engines, SPU, DMA, timers, interrupts) before the 3DS draws the result. This branch tries the other way:
the game's code and its libraries talk to the 3DS (citro3d, ndsp, hid, the SD card) themselves, and the hardware
model goes away piece by piece.

Why try it: on hardware the game's core spends more on the DS's hardware than on the game (docs/3ds.md log,
2026-10-29: geometry engine 8.0 ms, registers 4.7, game 4.2, 2D 3.2, sound 1.1, DMA 0.9 a frame). A port also gets
what a rebuilt DS can't give cleanly: float precision in 3D, the 3DS's resolution for everything, stereoscopic 3D
from the real scene, the wide view without the DS's culling.

What it costs: byte-for-byte checks against the PC's reference stop working for whatever is replaced (the pictures
and the sound are the 3DS's own then), and the branch's game sources part from upstream's. It is an experiment: if
a step doesn't pay, write down why here and stop.

## 0. Rules for this branch

- docs/3ds.md's rules hold, except one: the game sources (`src/`, `include/`) may change for the 3DS here. Such
  changes are under `#ifdef N3DS_DIRECT` (set by `configure_host.py --os 3ds --direct`), so that the same tree still
  builds the rebuilt-DS 3DS and PC programs, and the two can be compared.
- One subsystem at a time, and the program runs after each step: a replaced subsystem's hardware model is left out
  of the direct build, the others stay as they are.
- What stays checkable byte for byte: the game's own state. The save script (`tools/host_play_script.py save`)
  gives the same save file, and `ACWW_RAMHASH` over the game's memory (not VRAM, not the registers' shadows) stays
  the same frame by frame, as long as the replaced part doesn't feed back into the game (see R0.2).
- Pictures and sound are checked by eye and ear against the `3ds` build, with screenshots of the same frames side
  by side. Differences are written down (what and why), not hidden.
- `3ds` is merged in regularly; this branch is never merged back unless the user decides so.

## 1. The boundary

Where the game meets the DS today (rough counts, 2026-10-29, grep over `src/`: lines in the SDK that start with the
name and a bracket, mostly definitions; the game's files that call a name, per prefix, so a file can count twice):

| What | In the SDK (`autoload_2`, `itcm`, `dtcm`) | In the game (`main`, `ov*`) |
|------|------------------------------------------|----------------------------------|
| Register accesses (`IO_R`/`IO_W`) | 691 | 615 |
| `NNS_G3d*` (NitroSystem 3D: models, textures, animations) | 275 | 48 files |
| `G3_*` / `G3X_*` (geometry commands) | 104 | 6 files |
| `GX_*` / `G2_*` (2D set-up, VRAM banks) | 225 | 58 files |
| `NNS_Snd*` / `SND_*` (sound) | 822 | 1 file |
| `FS_*` / `CARD_*` (files, ROM) | 439 | 25 files |
| `OS_*` (threads, interrupts, timers) | 839 | 92 files |
| `MI_*` (memory copies, DMA) | 961 | 168 files |

So the natural boundary is the SDK's functions, not the game: replace an SDK function's body for the 3DS and every
caller follows. The game's own 615 register accesses are what has to change in the game itself (R0.1 sorts them).

## 2. Decisions (proposed, for the user to confirm)

- **P1: replace at the SDK, not in the game.** Each replaced SDK unit gets a 3DS body in a new directory
  `modern/n3ds/`, and `configure_host.py --direct` links it instead of the original. The game's sources change only
  where they touch the hardware without the SDK.
- **P2: the cooperative threads and the game's frame stay.** The game's OS_Thread switching (`coro_switch.S`) and
  its 60 Hz frame loop are its logic, not hardware; they stay. What goes is the virtual time beneath them where
  nothing needs it.
- **P3: 3D first** (the largest cost, and the largest gain in looks). Then sound (simple: the DS's 16 channels fit
  ndsp's 24), input and files (small), 2D last (the game builds its screens from tiles and OAM in VRAM; the
  rebuilt 2D engine is cheap enough on core 2).

## 3. Tasks

### R0: inventory

**R0.1 The game's direct hardware use.** List the 615 register accesses in `main`/`ov*` by register and purpose
(3D, 2D, DMA, interrupts, input, ...), with the files. Also the game's direct VRAM writes (`HW_*VRAM*`, `0x06......`).
Done when the list is in this file and each entry names the task that removes it.

**R0.2 What feeds back.** Which hardware results the game reads: the geometry engine's results (`G3X_GetClipMtx`,
`G3X_GetVectorMtx`, box/position tests, polygon/vertex counts), RAM busy flags, the sound's channel states, the
VCOUNT. Each one is a place where a replacement can change the game's behaviour. Done when listed with the game's
callers.

**R0.3 The build switch.** `configure_host.py --os 3ds --direct`: defines `N3DS_DIRECT`, builds into
`build/host3ds_direct_<version>/`, swaps the units P1 names (none yet). Done when it builds the same program as
without `--direct`.

### R1: 3D straight to citro3d

**R1.1 The way in.** Read how NNS_G3d draws (`NNS_G3dDraw`, the SBC commands: matrix stack, materials, shapes as
display lists sent with `G3_*`/`MI_SendGXCommand`) and what the game sends itself (R0.1). Choose the level: either
(a) the display lists stay and a citro3d command processor replaces the geometry engine (`gpu3d_geom.c`) without
the DS's fixed point, clipping and register FIFO, or (b) NNS_G3d's models are converted once at load into PICA
vertex buffers and drawn with citro3d's matrices. (a) is smaller and keeps every effect; (b) is faster. Done when
the choice and its reasons are written here.

**R1.2 Matrices and lighting on the PICA.** The DS's matrix stack and its 4 lights in the vertex shader (float),
the projection the game sets taken as it is (the wide view's widening then a uniform). Done when the attract demo's
town looks as on `3ds`, and the geometry engine's time on core 0 is gone from `ACWW_FPS`'s profile.

**R1.3 What the DS does differently.** Toon/highlight shading, edge marking, fog, translucent polygon sorting,
polygon IDs, 1-dot polygons, the W-buffer: each from gpu3d_pica.c's existing solutions where they apply. Done when
the save script's frames are compared side by side with `3ds` and each difference is listed.

**R1.4 The results the game reads** (R0.2): computed on the CPU in float from the same matrices. Done when the save
script's save file and RAM hashes are the `3ds` build's.

### R2: sound to ndsp

**R2.1** The ARM7 sequencer (`snd7/seq.c`, driver) stays; its channel commands go to ndsp's channels instead of the
SPU's software mixer (`snd7/spu.c`, `output.c`). Check from `3ds/ndsp/` what ndsp offers: PCM8/16 yes; the DS's
IMA-ADPCM is not ndsp's DSP-ADPCM (decoded on the CPU at load, or the mixer kept for those channels); PSG square
waves and noise as short looped buffers. Done when the save script sounds as on `3ds` and the mixer's time is gone.

### R3: input, files, time

**R3.1** Keys and touch from hid straight into the SDK's `PAD`/`TP` results, without the PXI round trip.
**R3.2** `FS_*`/`CARD_*` reading the ROM's file system from the SD card by name (`game.nds` stays the source).
**R3.3** The V-blank and timers from the 3DS's (`gspWaitForVBlank`, `svcGetSystemTick`) where nothing needs the
DS's virtual time. Each done when the save script's save file is the `3ds` build's.

### R4: 2D (only if R1-R3 leave it the bottleneck)

**R4.1** BG and OBJ set-up (`GX_*`, `G2_*`, OAM) drawn as PICA quads from the tiles, at the 3DS's resolution, the
2D engine model gone. Decide after R3 with the profile.

## 4. Open questions (unverified)

- How much of NNS_G3d's time is the SDK's own CPU work (animations, matrix building), which a port keeps.
- Whether the game culls with the DS's results (R0.2) in ways a float pipeline answers differently.
- Whether the gain over `3ds` (which already draws on core 2 and the PICA) is worth the branch's divergence:
  R1.2's profile answers it for 3D.

## 5. Log

- 2026-10-29: plan written (the user: "a real port: not abstracting the DS hardware, but adapting the game code to
  the 3DS directly", on its own branch). Nothing built yet.

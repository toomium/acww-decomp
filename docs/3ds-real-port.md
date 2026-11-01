# An enhanced 3DS port (plan)

Branch `3ds-real-port`, from `3ds`. The `3ds` branch runs the game on a rebuilt DS: the game and the NitroSDK write
the DS's registers and VRAM, and the runtime (`modern/host/`) plays the hardware's part (geometry engine, 2D
engines, SPU, DMA, timers, interrupts) before the 3DS draws the result.

Goal (the user, 2026-11-01): not a faithful DS any more but an enhanced 3DS port. It runs better on a 3DS than the
game does on a DS, and adds what a DS can't: a true wide screen, stereoscopic 3D, quality-of-life features, new
features, more languages. Matching the DS or the PC build bit for bit is no longer a goal.

## 0. Rules for this branch

- docs/3ds.md's rules hold except these:
  - Pictures and sound need not match the `3ds` or PC builds. Differences are written down (what and why).
  - The game sources (`src/`, `include/`) may change. A small change goes under `#ifdef N3DS_DIRECT` (set by
    `configure_host.py --os 3ds --direct`), so the same tree still builds the `3ds` and PC programs. A large one
    goes in a new file under `modern/n3ds/` that the direct build links instead of the original.
  - A bug found in the game code that every build has (as Resetti's in a0f61d66) is fixed on `3ds` in its own
    commit and merged in here, so that upstream gets it too.
- One subsystem at a time, and the program runs after each step. A subsystem that is not replaced yet keeps its
  hardware model, which goes away when nothing uses it any more.
- Every step is measured on hardware (`ACWW_FPS=1`) against `3ds`. A step that doesn't pay is written down here and
  undone.
- A tool, not a rule: for a swap that should not change the gameplay (3D, sound, files), the save script's save file
  and `ACWW_RAMHASH` over the game's memory show whether the game logic still runs the same. Drop it for steps that
  change the gameplay.
- `3ds` is merged in regularly.

## 1. Where to cut between the game and the 3DS

A port is a choice of the layer at which the game stops and the 3DS begins. LLE, HLE and "adapting the game code"
are three points on that scale, not the only three. Each subsystem can pick its own point, and most ports mix them.

What it costs today (hardware, docs/3ds.md log 2026-10-29, the start, frames 360-2640): core 0 about 19 ms a frame:
the geometry engine 6.7, the game 4.2, registers 2.0, the 2D record 1.7, sound 1.2. Core 2: the 2D layers 8.4, the
GPU 3.1. Also loads of 0.3-1.5 s (ROM and registers) and about 4 s of 120-170 ms frames when saving.

| Cut at | The game and SDK talk to | Done elsewhere | Removes | Costs |
|---|---|---|---|---|
| A. Registers (today: LLE) | the DS's registers and VRAM, rebuilt | emulators, the `3ds` branch | - | the 19 ms above |
| B. The hardware unit's commands (HLE of a unit) | a 3DS implementation of the unit's command stream: the G3 display lists on the PICA (float, matrices and lighting in the vertex shader), the SPU's channels on ndsp | SM64's PC port (the N64's display lists drawn with OpenGL, "Fast3D"), Dolphin's HLE audio | geometry engine, most register traffic, the sound mixer | small: the game and the SDK stay; effects that depend on the DS's fixed point need care |
| C. The SDK's functions | 3DS bodies for `NNS_G3d*`, `NNS_Snd*`/`SND_*`, `FS_*`/`CARD_*`, `OS_*`, `GX_*`/`G2_*` | libultraship (Ship of Harkinian, 2Ship), libntr (NitroSDK at API level) | the same, plus the loads (files by name), the PXI round trips, the virtual time | medium: the SDK is mostly named (in `autoload_2`: 51 `NNS_G3d*`, 92 `NNS_Snd*`, 55 `SND_*`, 33 `FS_*`, 96 `OS_*` functions; 67 still `func_*`), but each function's behaviour has to be understood |
| D. The game's engine layer | the game's render, sound and UI code rewritten for the 3DS, its logic kept | devilutionX, the RSDK decompilation's ports | everything below the game | large: 872 units, and the logic calls the SDK everywhere (`OS_*` in 92 files, `MI_*` in 168), so it ends up rebuilding level C anyway |
| E. A remake | a new engine with the game's assets and rewritten logic | fan remakes | everything | the whole game, without the decompilation's exactness |
| F. Static recompilation | machine code translated to C, then A-C beneath | N64Recomp, XenonRecomp | - | only for games without source; this one has it |

Conclusion: nothing is thrown away. The decompilation, the build, the ARM32 fixes, the coroutines, `plat_3ds.c`,
`frontend_3ds.c`, `pica_display.c`, `gpu3d_pica.c`'s TEV set-ups and texture cache, `card.c` and the save handling
stay. The hardware model is replaced subsystem by subsystem at level B or C, and the game itself (level D edits) is
changed only for features.

What no level changes on its own: the game makes a new 3D picture every third frame in town (20 a second, log
2026-10-29). Smoother than a DS needs a change in the game (draw every frame, or interpolate between pictures as
Ship of Harkinian and Zelda64Recomp do), and that needs 3D at level B or C, where each draw's matrices are known.

## 2. Decisions (confirmed by the user, 2026-11-01)

- **P1: 3D at level B first.** A command processor on the PICA replaces `gpu3d_geom.c` (no fixed point, clipping or
  FIFO on the CPU); the game and NNS_G3d stay. It removes the largest cost and gives float depth for stereoscopic
  3D. Level C for NNS_G3d (models converted once into PICA vertex buffers) only if B's profile says so.
- **P2: sound at level B.** The ARM7 sequencer stays; its channels go to ndsp instead of the software mixer.
- **P3: files, save and input at level C.** `FS_*`/`CARD_*` read the ROM's files by name, the save is written
  directly, keys and touch come from hid without the PXI round trip.
- **P4: the coroutines and the game's frame stay; the virtual time goes** where nothing needs it (the V-blank from
  GSP).
- **P5: 2D at level B, after 3D.** BGs and OBJs drawn as PICA quads from the VRAM's tiles at the 3DS's resolution.
  Needed for the wide screen's UI (each layer placed on its own, e.g. the text boxes at the screen's bottom edge).

## 3. Tasks

### R0: before anything is replaced

**R0.0 What "too slow" is.** Answered (the user, 2026-11-01): the pictures a second on the New 3DS; and the Old 3DS
should run the game too, which it can't now. Open: whether the 20 3D pictures a second are the game's own (the PC
build, the same scene; then only R6.3 helps).

**R0.1 The game's direct hardware use.** List the 615 register accesses in `main`/`ov*` by register and purpose,
and the direct VRAM writes. Done when each entry names the task that removes it.

**R0.2 What feeds back.** The hardware results the game reads: `G3X_GetClipMtx`, `G3X_GetVectorMtx`, box and
position tests, polygon and vertex counts, busy flags, the sound's channel states, VCOUNT. Each is a place where a
replacement changes the game's behaviour. Done when listed with the game's callers.

**R0.3 The build switch.** `configure_host.py --os 3ds --direct`: defines `N3DS_DIRECT`, builds into
`build/host3ds_direct_<version>/`. Done when it builds the same program as without `--direct`.

### R1: 3D on the PICA (P1)

**R1.1** The G3 command processor: matrix stack, the 4 lights and the material in the vertex shader (float), the
projection the game sets taken as it is. Done when the attract demo's town looks as on `3ds` and the geometry
engine's time is gone from `ACWW_FPS`'s profile.

**R1.2** What the DS does differently: toon/highlight shading, edge marking, fog, translucent sorting, polygon IDs,
1-dot polygons, the W-buffer, from gpu3d_pica.c's solutions. Done when the save script's frames are compared side by
side with `3ds` and each difference is listed.

**R1.3** The results the game reads (R0.2), computed on the CPU from the same matrices. Done when the save script's
save file is the `3ds` build's.

### R2: sound on ndsp (P2)

Check in `3ds/ndsp/` what ndsp offers: PCM8/16 yes; the DS's IMA-ADPCM is not ndsp's DSP-ADPCM (decoded at load, or
the mixer kept for those channels); PSG square waves and noise as short looped buffers. Done when the save script
sounds as on `3ds` and the mixer's time is gone.

### R3: files, save, input, time (P3, P4)

Each done when the save script's save file is the `3ds` build's and the loads' times are in the log.

### R4: 2D on the PICA (P5)

Decide after R1-R3 with the profile.

### R5: wide screen and stereoscopic 3D

**R5.1** The wide view (`ACWW_WIDE`) with the game's culling widened (the game culls to the DS's view), behind an
option. **R5.2** Stereoscopic 3D from R1's float depth (per-eye projection as a uniform), the 2D layers at screen
depth. **R5.3** The UI laid out for the wide screens (R4).

### R6: features

**R6.1** Controls (the user's list, docs/3ds.md 2026-10-29): ZR as a second B (running), the map on ZR, the pockets
on X, pulling weeds and shaking trees on Y. **R6.2** Languages: no ROM at build time. The user's EUR ROM lies on the SD card beside `game.nds`
(an `ACWW_*` option names it and the language), and the SDK's `FS_OpenFile` (R3.2) opens a language file from it
instead: `/script/ENG/...` as `/script/GER/...`, `/item_info/always.bin` as `/item_info/GER/always.bin`, and so on
(the log, 2026-11-01, has the layouts). Then the game code where the EUR data differs: the furniture names' record
size, the fonts, the 2D pictures with text, the text boxes' widths. Done when a German game can be started, played and
saved, and every difference found is written here. **R6.3** More 3D pictures a second: the game draws
every frame, or pictures interpolated between the game's (needs R1).

## 4. Open questions (unverified)

- How much of NNS_G3d's time is its own CPU work (animations, matrix building), which level B keeps.
- Whether the game culls with the DS's results (R0.2) in ways a float pipeline answers differently.
- How much memory the Old 3DS leaves once the geometry engine's and 2D renderer's buffers (about 9 MB of bss,
  docs/3ds.md 1.2) are gone.

## 5. Log

- 2026-10-29: first plan written (the user: "a real port: not abstracting the DS hardware, but adapting the game
  code to the 3DS directly", on its own branch). Nothing built yet.
- 2026-11-01: the goal changed (the user): an enhanced port, no bit-exactness, faster on a 3DS than on a DS, wide
  screen, stereoscopic 3D, QoL, new features, languages. The branch rebased onto `3ds` (097f0bc7); the plan
  rewritten around the choice of layer (section 1). Nothing built yet.
- 2026-11-01: the user confirms P1-P5 and answers R0.0 (the pictures a second on the New 3DS; the Old 3DS should run
  too). Old 3DS, for the expectations: a third of the CPU (268 MHz) and no core 2, so the GPU thread's work (2D,
  compositing, the 3D's set-up) comes back onto cores 0 and 1. The geometry engine alone won't get it there; R2-R4
  and fewer CPU-side 2D layers will be needed.
- 2026-11-01: what the game asks of the 3D hardware (R0.2 for 3D). The PC build (x86-64, GCC 14), the save script's
  12000 frames, `ACWW_HEADLESS=1 ACWW_FAST=1 ACWW_EXIT_FRAME=12000 ACWW_GPU3D_FEATURES=1` (the features' first
  frames printed at exit): W-buffering from frame 1 and never a Z-buffered polygon; manual translucent sort (the
  translucent polygons in the order sent, after the opaque ones); fog and "fog alpha only" both from frame 184;
  far-plane polygons hidden, clipped and culled polygons, depth test equal, translucent depth update, the shininess
  table not; textures A3I5, 16-colour, 4x4, A5I3; decal (from frame 2632) and modulation; texture coordinates from
  texcoord and normal. The game reads the clip matrix (frame 190) and the vector matrix (2632); no box, position or
  vector tests, no RAM counts. No RAM overflow, no full FIFO; commands do queue behind a swap (frame 0).
- 2026-11-01: languages, from the user's EUR ROM (`ANIMAL_CROSS_ADMP01_00.nds` in the working directory, git-ignored;
  game code ADMP, 16976 files, 42.1 MB of files, against USA Rev 1's 9504 files, 30.8 MB). Each language has its own
  directories or names: `script/{ENG,FRN,GER,ITA,SPN}/` (the messages, `.bmg`), `item_info/<LANG>/`,
  `ftr_info/<LANG>/`, `myOrg/<LANG>/myD.bin`, `font/fontC_*_{eng,frn,ger,ita,spn}.bin`,
  `a_mes/a_mes_ttl_*_{eng,...}`, `sound_dataEUR.sdat` (USA: `sound_dataENG.sdat`). USA has the same names without the
  language (`item_info/always.bin`) and `script/ENG/`. 9207 of the 9487 files both have are the same bytes; 280
  differ (`bg/` 120, `menu/` 19, `script/` 54, `str/` 20, `PGls/` 27, `ftr/` 27, fonts 6, ...). The game's code opens
  these files by path (`FS_OpenFile`, `"/script/ENG/message"`, `"/item_info/always.bin"` and so on in `src/main`),
  so a runtime redirect by name works without the ROMs' file numbers matching. Known differences in format:
  `ftr_info/<LANG>/dma.bin` is 65536 bytes, USA's `ftr_info/dma.bin` 57344 (the game reads 0x800 records of 0x1c
  bytes: EUR's are 0x20 by size, **unverified**); `item_info` is the same size in both; `fontA`, `fontB`, `fontD`
  and `fontC` differ (more glyphs: **unverified**). Not looked at yet: the EUR code's own differences (text lengths,
  the save layout).
- 2026-11-01: R1, the first two steps, in the `--direct` build (Azahar, the save script, unpaced: `ACWW_FPS=1`,
  `ACWW_FAST=1`, a fixed clock, no save; means over the reports of frames 480-2640, the 3D start). Baseline (the
  plain code): 46.5 frames a second; core 0's geometry engine 11.9 ms a frame (commands 5.2, vertices 4.7, lighting
  2.1). (1) The GX FIFO's DMA blocks run in place, without the timing (commit 05d112e5): 51.1, commands 3.2; the 1501
  composited pictures to frame 3000 (`ACWW_PICA_HASH`) the baseline's. (2) The polygons straight into the GPU's
  vertex list (`g3p_polygon`): no clipping, screen positions, polygon RAM or sort on the CPU, the GPU clips; three
  lists in turn; the fog alpha at the vertices in the GPU thread's job. In the run with screenshots (frames 1200,
  2400, 2600, 9000, 11000; the baseline's the same way: 46.0): 58.0, vertices 2.0. The pictures against the
  baseline's: 7-16 % of the pixels differ, all at 3D edges (leaves, the horizon), nothing missing; the 2D the same.
  Differences by design: polygons crossing the near plane are drawn to w > 0 (the W-buffer's depth isn't the clip
  z), polygons crossing the far plane with attr bit 12 not clipped there, no 1-dot polygon hiding. Not checked yet:
  the plain 3DS build's pictures with the shader's new `target` (identity there; the run was stopped), hardware.
  Next: lighting (2.0 ms) and the vertex transform in the vertex shader, then R3's register and ROM time.

# An enhanced 3DS port (plan)

Branch `3ds-real-port`, from `3ds`. The `3ds` branch runs the game on a rebuilt DS: the game and the NitroSDK write
the DS's registers and VRAM, and the runtime (`modern/host/`) plays the hardware's part (geometry engine, 2D
engines, SPU, DMA, timers, interrupts) before the 3DS draws the result.

Goal (the user, 2026-11-01): not a faithful DS any more but an enhanced 3DS port. It runs better on a 3DS than the
game does on a DS, and adds what a DS can't: a true wide screen, stereoscopic 3D, quality-of-life features, new
features, more languages. Matching the DS or the PC build bit for bit is no longer a goal.

## Status: where each subsystem stands

Kept up to date with every step (the log, section 5, has the details and the numbers' sources). "LLE": the DS's
hardware rebuilt beneath the game; "HLE (commands)": the hardware unit's input (commands, state) carried out by the
3DS directly (section 1, level B); "HLE (SDK)": the SDK's functions replaced (level C). Costs: a New 3DS, frameskip
0, ms a game frame (the log, 2026-11-01).

| Subsystem | Now | What is still rebuilt | Cost | Next |
|---|---|---|---|---|
| 3D | HLE (commands), on the PICA | matrices and lighting in the DS's fixed point on the CPU (the game reads the matrices) | core 0 about 4.5 (geometry 1.8, lighting 0.9, vertices 1.0); with the pictures between (D) core 2 about 5.9 (3D 1.9, vertices 1.6, lighting 1.0, VRAM 0.7) and the GPU 3.6-4.6 a frame | interpolation done (R6.3, D below); the textures in VRAM; the transform stays the CPU's (C1, the GPU's, not taken: the log 2026-11-02) |
| 2D | HLE (commands) for the OBJs (sprites from OAM, decoded once a frame) and the windows (shapes into the stencil); the BGs from VRAM as tile textures; all drawn on the PICA, in the wide view straight onto the screens | the per-line record on core 0 (the registers each line, the sky's backdrop colours; the 2D banks F-I, about 80 KB, copied each frame) | core 0 1.3, core 2 1.1-1.5; the GPU's compositing 5.5 in town | B2 done (anchors); other screens' UI as found |
| Sound | LLE, on a thread of its own (option A): the driver, sequencer, mixer and capture clocked by the sound it mixes, feeding the DSP itself | the SPU (16 channels mixed on the CPU), one stream to ndsp | core 2 (New 3DS), not core 0 | the user's listening test; ndsp channels (option B) later for the Old 3DS |
| Registers, interrupts, DMA, virtual time | LLE | every register access of the SDK through the hardware model | core 0 about 2 | R3 |
| Files, ROM, save | HLE (SDK): the save memory's stream commands (`CARDi_RequestStreamCommand`) and the ROM reads (`CARDi_ReadRom`, all of FS's) done at once in the host; the ROM image read ahead in the background | the save file written to the SD card on the game's thread (one write, 30 frames after the last change); the identify and the other card requests through the PXI | loads in Azahar still about 1.3 s at the first big one: ROM about 0.5 s (the read-ahead only runs while the game waits), "registers" about 0.55 s (**unverified**: the VRAM banks' remapping) | the load's register time; the save file's write in the background |
| VRAM | HLE (SDK) for the texture uploads (`GX_LoadTex`, `GX_LoadTexPltt` straight into the banks, no remapping); otherwise LLE | the banks and the CPU's windows (vram.c): remapped at a VRAMCNT change, the mapped granules flushed each frame | Azahar: 0.1 a frame, 0 at the first big load (was 410 ms) | - |
| Input, clock, NVRAM | LLE (cheap) | the PXI round trip | negligible | R3 |
| Threads, the game's frame | kept (game logic) | - | - | - |

## Next steps (2026-11-02; details in the log, section 5)

Where it stands (a New 3DS, the wide view, town, 2026-11-02): the 3D at 60 pictures a second by interpolation
(D), 42-67 game frames a second, median 58.4, long stretches at 59-61, the fuller parts of town (more villagers)
at 52-57. A frame: core 2 (the GPU thread) about 10.6 ms of its own work (2D 1.1, 3D 1.9, vertices 1.6, lighting
1.0, VRAM 0.7, compositing 3.2, BG tiles 0.7, top hash 0.2, frame end 0.2) and 4.6 waiting for the GPU; the
GPU's 3D 3.6-4.6 ms a picture, its compositing about 8-9 (less where the top screen is skipped); core 0 waits for
core 2 about 6.4. Both core 2 and the GPU are near the limit.

Open, in the order proposed to the user (the user picks):
1. **D: R6.3, the 3D's pictures between the game's: done** (on by default, `ACWW_PICA_INTERP=0` off; the log,
   2026-11-02). Not looked at yet: a picture between across a cut (D4), the objects' shadows while walking
   (Azahar draws none), the input latency (a game frame more). The 2D (the UI) is up to two frames ahead of the
   3D. What could come next (speed, the 4 s stand, the rest): the list (a)-(g) in the log's entry "the user's run
   with the cheaper top screen check"; proposed: the 3D's textures in VRAM, then the 4 s stand.
2. **B2: R5.3, the UI at the wide screens' edges: done** (the log, 2026-11-02, "anchors"): the HUD's panels
   (clock, countdown, camera button, wallet) and HudProc's and MsgUiProc's icons (the green arrow, the input mode's
   pencil) towards the edges they are near, the text box with its OBJs (arrow, choices) flush with the bottom, the
   touch mapped back. Not looked at: the other screens' UI (menus, the map, the
   shops), the top screen's. **Open (the user, 2026-11-02, hardware):** when saving, "Save" is only reached with
   the touch put a bit lower than it shows - the save menu's choices and their touch boxes disagree (not fixed yet;
   to look at: whether the choices are drawn moved while the touch goes by another box, or the other way round).
3. Smaller, optional: texture VRAM's tracking from the written ranges (GX_LoadTex) instead of copying and comparing
   all 640 KB (g3_tex_setup on core 0, `track` on core 2) whenever a texture is uploaded; the sky's cloud BG as
   one-line quads (about 1.5-2 ms of the GPU): try the same quads ordered in 8-line blocks (the target's tile cache).

Still open questions: why C1's clip coordinates in the DS's own units (w about 10^5) made objects vanish on the GPU,
and why C1 flickers on hardware (24-bit shader floats suspected, **unverified**).

Checking (Azahar, hidden and silent, docs/3ds.md T0 and the memory notes): the continue script on `wide.sav` with
`ACWW_RTC` fixed (the time of day changes the 3D's light), `ACWW_PICA_SHOT` for whole screens, `ACWW_PICA_HASH`
("screens"), `ACWW_PICA_STATS` (pixels, draws, quads per pass); on hardware `ACWW_FPS=1`, `ACWW_PICA_PROBE=1` for the
GPU's time per part. `ACWW_RAMHASH` can't compare two direct-build runs: the sound's thread runs in real time.

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
option (done). **R5.2** Stereoscopic 3D from R1's float depth (per-eye projection as a uniform), the 2D layers at screen
depth: the 3DS shows it on the top screen only, the 3D world is on the bottom one. **R5.3** The UI laid out for the
wide screens (R4): B1, the screens composed straight onto their targets, done; B2, the anchors: Next steps.

### R6: features

**R6.1** Controls (the user's list, docs/3ds.md 2026-10-29): ZR as a second B (running), the map on ZR, the pockets
on X, pulling weeds and shaking trees on Y. **R6.2** Languages: no ROM at build time. The user's EUR ROM lies on the SD card beside `game.nds`
(an `ACWW_*` option names it and the language), and the SDK's `FS_OpenFile` (R3.2) opens a language file from it
instead: `/script/ENG/...` as `/script/GER/...`, `/item_info/always.bin` as `/item_info/GER/always.bin`, and so on
(the log, 2026-11-01, has the layouts). Then the game code where the EUR data differs: the furniture names' record
size, the fonts, the 2D pictures with text, the text boxes' widths. Done when a German game can be started, played and
saved, and every difference found is written here. **R6.3** More 3D pictures a second: pictures interpolated
between the game's (the game's loop is 20 Hz itself); plan D in Next steps.

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
- 2026-11-01: the user's two runs on a New 3DS with the direct build (de7e8ffb; `logs/`, not committed), played by
  hand. `ACWW_FRAMESKIP=1` (the default): 60 game frames a second (paced; core 0 waits 3.9 ms a frame), 30 pictures;
  core 2 idle 5.7 ms. `ACWW_FRAMESKIP=0`: about 44 game frames a second, the game slower than a DS; core 0 waits 8.2
  ms a frame for the GPU thread; core 2: 2D 6.9, compositing 5.3, waiting for the GPU 7.9, idle 0.8; the GPU's
  drawing 13-14 ms a composited frame for the compositing, 1.2 for the 3D; the sound's queue ran dry 83 times (8 with
  frameskip 1). So without frameskip the compositing (the GPU's time and core 2's) is the limit, not the geometry
  engine. (Azahar's GPU times say 1 ms: it can't show this.)
- 2026-11-01: what the compositing fills (`ACWW_PICA_STATS=1`, new: the pixels and draws a composited frame per kind
  of pass, the same in Azahar as on hardware; the save script to frame 3000, frameskip 0): about 1,000,000 pixels a
  frame, of which the OBJ passes 393,000 (the OBJ layer drawn over both pictures for each of the 4 priorities, for a
  few hundred OBJ pixels), the 3D layer's three passes (blended, as it is, marks) 240,000-330,000, the backdrop
  98,000, the window regions 0-170,000, the screens 98,000. Two changes, each exact (the 1501 composited pictures'
  hashes the build before's): the OBJs of a priority only on the lines that have some (commit 94b1617f: 393,000 ->
  1,000), the GPU's premultiplied 3D picture in one pass (commit 1540f52e). Now about 510,000-540,000 pixels a frame.
  To be measured on hardware (the GPU's compositing time, frameskip 0). Next candidates: the window regions' four
  passes only on their lines; core 2's 2D (6.9 ms: the OBJs, each line looking at all 128 OAM entries, the CPU's
  BGs); then R2 (sound on ndsp: notes keep playing through a slow frame, as on a DS).
- 2026-11-01: the user's run with frameskip 0 after the compositing changes (1540f52e): about 55 game frames a second
  (was 44), the GPU's compositing 8.6 ms a composited frame (was 13-14), core 2 waiting for the GPU 2.3 ms (was 7.9),
  core 0 waiting for core 2 3-5 ms (was 8.2). Core 2 then the limit with its CPU work (2D 6.3, compositing 4.1, the
  3D job 2.0-2.7). The status table (top of this file) added at the user's request.
- 2026-11-01: R4, the 2D as HLE. What the game uses of the 2D engines (the PC build, the save script's 12000 frames,
  `ACWW_RENDERER3D=own ACWW_GPU2D_FEATURES=1`): BG modes 0 and 1, text BGs in 16 colours, affine BGs (engine B), OBJs
  with 2D tile mapping, windows 0 and 1, the OBJ window (engine B, from frame 4750: the name entry), alpha blending,
  brightness up and down. No extended BGs, bitmaps, 256-colour or extended-palette OBJs, mosaic, semi-transparent
  OBJs. So every BG was the GPU's already; the CPU's 2D work was the OBJs and the windows, line by line. Three steps,
  each pixel for pixel the build before's at frames 1200, 2400 and 5000 (Azahar, the save script, frameskip 0):
  (1) the OBJs as sprites (commit b6311624): each shown OBJ's box decoded once a frame into an atlas and drawn as a
  quad; core 2's 2D at the start (frames 480-2640) 14.4 -> 2.3 ms a frame, the name entry 34.5 -> 22.2. (2) The
  windows' regions from their shapes (commit 78d70a79): rectangles per run of lines and the OBJ-window OBJs' sprites
  into the stencil, no line's OBJs drawn at all: 2.3 -> 1.3, the name entry 22.2 -> 16.7. (3) A plain OBJ decoded a
  tile row at a time: the name entry 16.7 -> 5.6. Azahar's frames a second (unpaced, core 0
  bound now): the start 39.9 -> 47.3, the name entry 26.9 -> 86.4. Not measured on hardware yet. Next: core 0's 2D
  record (about 5 ms in Azahar: the 2D VRAM banks copied every frame, the registers every line), then R2.
- 2026-11-01: checks after R4's three steps (Azahar): the screen transition's iris (window 0 changing every line;
  frames 2560 and 2600) pixel for pixel the build of de7e8ffb's; the plain 3DS build at HEAD (the shader's `target`
  the identity, the gpu2d changes off there) gives the baseline's 1501 composited pictures' hashes to frame 3000.
  Not tried with the direct build: `ACWW_WIDE` (its sprite atlas then in linear memory).
- 2026-11-01: the user's run with the 2D as HLE (e56383af), frameskip 0: about 56.5 game frames a second on average,
  long stretches at 60 (frames 1680-2880), the town and the name entry 50-60; core 2 idle 6.4 ms, the GPU's
  compositing 8-9 ms: core 0 is the limit now. Two frames of 9.6 s (frames 6001 and 12121; the user: once while Mr.
  Resetti talked, once while saving, the second text box late), all of it in `game`, the watchdog saying virtual
  time didn't move (no register access or wait). Not reproduced: the plain PC build through the save script's save
  (frame 33000) reports no spin. Both are the frame right after an `ACWW_FPS` report (every 120 frames: 6001 =
  50 x 120 + 1, 12121 = 101 x 120 + 1), whose lines go to the SD card on the game's thread, after a save, whose file
  is written on the game's thread too (a host file write moves no virtual time: the watchdog's message). Suspected,
  **unverified**: an SD card write that stalls after the save's. The user's choice: the files and the save next.
- 2026-11-01: R3, files and the save at the SDK level (commit 464d1cda): the save memory's stream commands and the
  ROM reads done at once in the host, the ROM image read ahead in the background. The game's whole save path is
  Backup_Read/Write(Async) (src/main/unk_0204fe7c.cpp) over one SDK function, CARDi_RequestStreamCommand; an async
  request that is done when it returns makes the game repeat it forever (it counts it as failed), so an async one
  finishes on the card thread a millisecond later. Checked on the PC with only the SDK changes (configure_host.py
  `--cc gcc-14 --suffix _cardtest --extra-cflags=-DN3DS_DIRECT`): the save script to frame 34000 runs through (17
  reads, 3 writes), the save file the plain build's but 4 bytes at the end of both slots (time-dependent, and the
  checksum). Azahar: frames 1200, 2400 and 5000 pixel for pixel the build before's. The first big load (frame 183)
  in Azahar 1330 -> 1270 ms; its "registers" part (about 550 ms) is not the card: next to look at. Whether the 9.6 s
  frames are gone needs the user's hardware run (the same places: Mr. Resetti, a save).
- 2026-11-01: the save file's write in the background (the direct build) and a report of slow SD writes (every
  build: a save write over 50 ms, an `ACWW_FPS` report over 100 ms). Checked on the PC: the continue script's
  writes in the background give the file of writing at once. Continuing from a save through the SDK-level reads
  (the PC, the continue script on the save script's file): the same town and house, the text a few frames earlier.
  For the user's next run: the same places (Mr. Resetti, a save); the log then says which write was slow, if one is.
- 2026-11-01: the user's run of 9cb3b4e9 on a New 3DS (frameskip 0, about 3400 frames): 58.3 game frames a second on
  average, mostly 56-60 outside the loads; no 9.6 s frame (whether the user saved or met Mr. Resetti in it: not
  known); three `ACWW_FPS` reports took 118-134 ms to write (the SD card). The first big load (frame 183) 1063 ->
  881 ms (ROM 433 -> 253: the read-ahead), a later one (frame 1683) 620 ms with ROM 16 ms: what is left of a load is
  about 430 ms of "registers" (and 165 ms of game). The sound's queue ran dry 36 times. Where the loads' register
  time goes, **unverified**: the game uploads textures with GX_BeginLoadTex / GX_LoadTex / GX_EndLoadTex (the SDK),
  which map the texture banks to the LCDC window and back: vram.c copies each bank's granules between the window and
  the bank at every VRAMCNT change. Also on core 0 every frame: host_vram_flush copies every CPU-mapped granule back
  to its bank (render.c), and the 2D record copies the 2D banks again (gpu2d.c record_end).
- 2026-11-01: VRAM (the user: before the sound). `ACWW_FPS` has a VRAM part now (vram.c's copies). Azahar, the save
  script to frame 3000: at the first big load (frame 183) VRAM 410 ms of about 1260; 0.59 ms a frame otherwise. The
  game maps only the small banks F-I for 2D (VRAMCNT A-I 83 8b 93 82 / 83 81 82 ...: A-C textures, E texture
  palettes), so the 2D record's copy is about 80 KB a frame. (1) The texture uploads at the SDK level (commit
  8bcfebd7): GX_BeginLoadTex leaves the banks mapped, GX_LoadTex(Pltt) copies straight into the banks' memory;
  the load 1260 -> about 850 ms in Azahar (VRAM 410 -> 0), VRAM 0.59 -> 0.12 ms a frame; pixel for pixel, and on
  the PC the save script's pictures and save file the same. (2) Core 0's 2D record, timed (an instrumented build, not
  committed): most of it compared the 4 KB of palettes and OAM after every line, for the sky's H-blank (the game's
  only one: the backdrop colours and registers). Only the two backdrop words now:
  core 0's 2D 2.44 -> 0.70 ms a frame, Azahar 61 -> 68 game frames a second; pixel for pixel. Next: the sound (R2).
- 2026-11-01: the user's run of 48102ac2 on a New 3DS (frameskip 0, about 5650 frames): 60.0 game frames a second
  on average, frames 2000-4800 at 59-61 throughout. The first big load (frame 183) 881 -> 562 ms (ROM 306, game 161,
  registers 75, VRAM 2), a later one (frame 1662) 620 -> 281 ms. Single slow frames of 195-200 ms at frames 241 and
  361, nearly all `game` (the game's own work, or runtime parts without a profile mark: not looked into). The sound's
  queue ran dry 22 times (was 36). No 9.6 s frame; the watchdog line at the end is the home menu's. Core 0 a frame:
  geometry with lighting and vertices 3.7, game 1.7, sound 1.5, 2D 1.4, registers 1.2, VRAM 0.55, waiting for core 2
  2.9, pacing 3.0 (time to spare); core 2: compositing 3.6, 3D 2.0, 2D 1.3, waiting for the GPU 5.2, idle 4.7.
- 2026-11-01: the user: frameskip 0 the default and the log off the game's thread, then the sound. `ACWW_FRAMESKIP`
  is 0 by default in the direct build now (commit 6bcdce6e; 1 brings back every other picture). The log (stderr,
  log.txt) is buffered (64 KB) and a thread of its own writes it out four times a second (commit db973422; it stops
  with the other background threads before the program ends): a crash now loses up to 250 ms of the log (Luma's
  dumps are unaffected). Azahar, paced to frame 1500: every report and the exit line in the log.
- 2026-11-01: R2, the sound: what the game asks of the SPU, before choosing (the PC build, the save script to frame
  34000, gdb on `spu_capcnt_written`): both capture units are switched on 20 times (SNDCAPCNT 0x80: the mixer,
  16 bits, looped) with SOUNDCNT 0xb97f (the outputs from channels 1 and 3, which the mixer leaves out): NitroSystem's
  output effect (Snd_StartOutputEffect, NNS_SndCaptureStartOutputEffect): the whole mix captured, processed on the
  ARM9 (OutputEffectCallback: normal, surround, headphone or mono) and played back through channels 1 and 3. The
  game's sound option picks it: Stereo -> 0 (normal: unchanged), Surround -> 1 (the side signal, and
  SND_SetSurroundDecay(0x3000) in the ARM7's panning); `sSndOutputModeTable[PlayerOptions_IsStereo()]`
  (src/main/unk_0209dc94.cpp). So the channels on ndsp one by one would play nothing in those scenes unless the
  output effect is recognised and replaced (surround then lost or redone), and Azahar here can't play ndsp (no DSP
  firmware). Where the queue runs dry (the user's last run): at the loads and at the slow frames (70-95 ms) of the
  title; with the channels on ndsp the notes playing would go on through them but the sequencer would stop, the
  music standing still. The alternative: the ARM7's sound (driver, sequencer, mixer, capture) on a thread of its own
  in real time, as the DS's ARM7 runs beside the ARM9. Choice put to the user.
- 2026-11-01: the user's run of db973422 (frameskip 0): 59.6 game frames a second, 58-62 throughout; the sound's
  queue ran dry 9 times (was 22); the first big load 547 ms. New: frame 2 (the start) took 10.4 s, nearly all `ROM`
  (the run before: 0.46 s). New in that build: the log's thread writing to the SD card beside the ROM's read-ahead
  and the game's first reads; how that would take 10 s is not known (**unverified**; maybe a one-off: the first
  start after copying). To watch at the next start.
- 2026-11-01: R2, option A (the user's choice): the ARM7's sound on a thread of its own (snd7/driver.c
  `snd7_thread`), on the other core with the high priority of the profile's sampler, clocked
  by the sound it mixes (about 3000 frames ahead of the DSP), feeding the DSP itself. Azahar (no DSP firmware, the
  ring played away in real time), paced, to frame 3000: the pictures the build before's, core 0 without the sound,
  the sound (ACWW_WAV) 52.8 s for 51.7 s of run, its first second the PC build's (correlation 0.997). Not known
  without listening: (1) the output effect (the game's "Surround" sound option, the default): the ARM9 processes the
  captured mix when the driver's alarm reaches it, which now waits until the game's thread next runs its events (up
  to about a frame, 16.7 ms; the capture buffer's half is about 16 ms), so the effect may lag and crackle where
  "Stereo" doesn't; (2) whether anything in the game waits on the sound in a way the real-time clock changes.
- 2026-11-01: with the user's DSP firmware in Azahar's SD card (sdmc:/3ds/dspfirm.cdc): the sound's thread through
  ndsp, paced, to frame 3000: the DSP's queue never ran dry, the first big load (frame 183) included.
- 2026-11-02: the user: on the New 3DS the title's music sounded too slow, as in slow motion (the DSP's queue never
  ran dry). The title's music is a stream (NNS_SndArcStrmStart from SndMgr_PlayBgm; strm 0: IMA-ADPCM, 2 channels,
  47605 Hz), whose buffer is the DS's 2 KB a channel: 4 blocks of 256 samples (5.4 ms each), each refilled by the
  ARM9 from the ROM when the ARM7's alarm for it comes; with two loads still pending StrmDataCallback drops the older
  one as silence, the stream's position not moving on. With the sound on its own thread the alarms reach the ARM9 at
  its next event pass and the load waits on the game's virtual time: Azahar (with the DSP firmware), to frame 2400,
  4840 of 8094 blocks dropped (the PC build: none). Changed in the direct build (heard on hardware: still to come): the stream buffers 16 times as large
  (blocks of 86 ms, refilled within about 260 ms; SetupStrmPlayers, a static 768 KB, the sound heap's block kept for
  the buffer's lifetime). The ADPCM decode buffer (0x200) is unchanged: each read stops at the file's block (512
  bytes). Azahar: no block dropped; the title music (ACWW_WAV) against the PC build's: envelope correlation 0.998 at
  speed ratio 1.00 (was 0.29 at 0.92), waveform 0.985. The ROM image is still held in memory (Azahar). Left: the load position (curSample, which the title's fade
  reads: func_0210e648) is now about 340 ms ahead of what plays instead of 21 ms, and a stream that doesn't loop
  ends about 350 ms after its last block instead of 20 ms; a hold-up of the game's thread longer than about 260 ms
  still drops blocks. ACWW_FPS's sound line now gives the frames played a second (Azahar: 32500-33100, the DS's
  32728). The log's thread starts at frame 600 (before: at the first frame), as the 10 s start may come from its SD
  writes beside the start's ROM reads (**unverified**; Azahar doesn't show it); a crash before frame 600 loses the
  start's lines.
- 2026-11-02: the user's run of deb06634: the title's music sounds right now. The start's frame 0.48 s (was 10.4 s).
  New: frame 719 took 8.3 s, all of it `frontend` on the game's thread: the frame 720 report's fprintf (in
  frontend_backend_frame) waiting for stderr's lock, which the log's thread held through its first write to the SD
  card (from frame 600 on). So the 9.6 s and 10.4 s frames before were most likely SD card writes too, now and then
  that slow on the user's card; the cause inside the 3DS is not known. "pxi7: receive FIFO overflow" just before is
  its consequence: the sound's thread ran on and its alarm messages for the ARM9 piled up (the outbox keeps 64, the
  rest are dropped). Changed (plat_3ds.c, the direct build): stdout and stderr go through a device of their own
  (devoptab_list[STD_OUT/STD_ERR]) that only copies into a 64 KB buffer (what doesn't fit is dropped and counted);
  the log's thread writes it to log.txt through a FILE of its own, and once more at the program's end (atexit).
  Azahar, to frame 1200: the log whole, the lines after the exit included; with a write held up for 8 s on purpose
  (a test build): no frame over 70 ms (the first load's 709 ms aside), against 8 s before.
- 2026-11-02: the user's run of 14cb7fc1 (about 5200 frames): everything fine (the user). 59.8 game frames a second
  throughout but at the loads; the start 0.49 s; no frame of seconds; the sound's queue never ran dry, 32670-33190
  frames played a second. The slowest: the first big load (frame 184) 581 ms, two later ones (2946, 4887) 280-285
  ms; otherwise at most 40-75 ms. Core 0 a frame: pacing 6.9 ms to spare. The watchdog line at the end is the home
  menu's.
- 2026-11-02: R5.1, the wide view in the direct build (the user: "a real wide screen", the town on the bottom screen
  as on a DS, 1:1 with more world around it: the `ACWW_WIDE` of docs/3ds.md, finished). Azahar, the continue script
  on a save from the PC's save script: the town fills the bottom screen (320x240), the sky the top one (400x240).
  (1) The game's own culling: `ViewFrustum::calcPlanes` (gViewFrustum, set by Camera::setFovy) builds the side
  planes from the DS's aspect; Actor::preExecute tests each actor's sphere against them, and an actor outside is
  neither drawn nor (actorFlags bit 0) run. Under N3DS_DIRECT with the wide view the planes go through the wide 3D
  picture's edges (x 200/128, y 120/96): an actor in the margins is drawn, and runs, as one on the DS's screen
  (a change to the game's logic, on purpose). Seen: the white object beside the player's house at the margin
  (frame 4800) was missing before. The box test (gpu3d_geom.c) had the wide planes already; the field and the trees
  were drawn there before. (2) The screen transitions: the iris is window 0 with WINOUT 0x24 (outside: BG2, a text
  BG of one black tile, and the effects; no 3D), and between its closing and opening the windows are off and BG2
  covers the screen (the PC build's registers, frames 4115-4228 of the continue script). The margins showed the
  world through both. Now (pica_display.c) the wide 3D picture goes in the bottom screen's margins only where they
  show the 3D layer outside the windows and no BG of one opaque tile everywhere is in front of it (`margins_3d`);
  else, as on the top screen, the 2D layers (margins2d), which now also draw the text BGs of one tile everywhere
  (BgTex.uniform; "opaque": no transparent pixel in it; with any text BG the windows show outside instead, a menu's
  BG would repeat into the margins; the iris's BG2 is of one tile, the narrower rule gives the same pictures). Azahar: the house's exit and the
  iris in town black to the screens' edges, the town and the sky as before. (3) `ACWW_WIDE` is on by default in
  the direct build (`ACWW_WIDE=0` off). Not done: the 2D UI (text boxes, the clock) stays in the DS's 256x192 in
  the middle (24 lines of world below the text box); OBJs and other text BGs aren't drawn in the margins; the speed
  on hardware (Azahar in town 46-47 game frames a second with the wide view; the old build's hardware numbers:
  docs/3ds.md 2026-10-29). Azahar, the same run in town (frames 4440-5040), `ACWW_WIDE=0` against on: 47-50
  against 43-48 game frames a second, core 2's compositing 3.7 against 5.3 ms a frame, VRAM free 477 against 1338 KB
  (the wide view keeps the layers' textures in linear memory); on hardware to be measured.
- 2026-11-02: the user's run of 738ceaa5 (the wide view on, about 5870 frames): the title 63-66 game frames a second,
  the town 55-59 (without the wide view about 60); the GPU's compositing 9.5-10.6 ms a composited frame where the
  3D is drawn (5.9 elsewhere), core 2 now waits on the GPU (9.3 ms a frame) with 0.3 ms idle: the wide view's cost
  is the GPU's. VRAM free 1338 KB. One frame of 6.0 s at frame 5738, all `game`, the watchdog saying virtual time
  didn't move (frame 5737), then the home menu's at the end (5874): whether the user was in the home menu at 5738
  too is to ask. "pxi7: receive FIFO overflow" once (the sound's thread runs on while the game's stands). Next
  question (the user): the game made to think it has the larger screen, the touch elements at the edges (R5.3).
- 2026-11-02: R5.3, what the UI in town is (the PC build, the save script, `ACWW_VRAMDUMP` at frames 22390 and
  26500): the bottom screen is engine A; its BG0 is the 3D, every other 2D layer there is UI. The clock: about 26
  OBJs (palette 9, x 167-253, y 154-189) from `HudClock::draw` (src/main/unk_02089fbc.cpp) through `Oam_DrawCell`;
  the green arrow at the top right: the HudController's camera button (OBJs); a text box: BG2 (map 6) shown only in
  window 0 (lines 60-192), with OBJs (the arrow). The game hides unused OBJs at y 192 (72 in the text box's frame),
  so OAM positions past 256x192 can't mean "in the margin": each element moved to the wide screen's edges needs an
  anchor from the game (under N3DS_DIRECT), and the bottom screen then composed at its own size (the 3D under it
  where the text box was). Not found: the code that sets the text box's BG2 and window 0 (`ACWW_IOHIST`'s callers
  didn't resolve to the right functions).
- 2026-11-02: why the 3D world shows 20 pictures a second (R0.0's open question), from the code: the whole main
  loop (Main_Loop, src/main/unk_0206d5b8.cpp: the tasks, actors, drawing) runs once every `gVBlanksPerFrame`
  V-blanks (Main_VBlankCallback, src/itcm/unk_01ffcc60.cpp), which BootLogoScene::onCreate and
  FieldScene::stepFinish (src/main/unk_020b4828.cpp) set to 3. So the game logic itself runs at 20 Hz; 1 would run
  the game three times as fast. More 3D pictures need interpolation between the game's frames (R6.3). Also seen in
  pica_display.c: in the direct build the CPU's OBJ and window-region textures (`sObj`, `sRegion`, with their
  linear staging buffers) are allocated but never drawn (the OBJs are sprites, the windows shapes), and the CPU's BG
  textures (`sBg`) only for a BG the GPU doesn't draw: **unverified** whether that ever happens there.
- 2026-11-02: measured (Azahar, the direct build of b4d5bd05, the continue script on wide.sav, unpaced,
  `ACWW_PICA_HASH=1 ACWW_PICA_STATS=1`, to frame 5400): of every three composited pictures two differ and one
  repeats the one before (runs of 1 and 2 alternating, from the title to the town; 67 % of the pictures new). So
  composing only changed pictures would save a third of the compositing, not two thirds; why the third frame of a
  game frame shows two different pictures (the 2D's change and the new 3D picture a frame apart?) is **unverified**.
  In town a composited frame fills about 713,000 pixels: backdrop 194,000 (37 draws), the screens and their margins
  290,000, BG layers 109,000, the 3D layer 105,000 (blended 85,000, as it is 10,000, marks 10,000), OBJs 6,000. No
  pixel came from the CPU's BG or OBJ textures (`sBg`, `sObj` unused in this run); in the wide view they are in linear
  memory, so dropping them frees linear memory, not VRAM. The sprite atlas in linear memory draws only the OBJs'
  6,000 pixels: not where the wide view's GPU time goes. Core 2 (Azahar's numbers): compositing 10.6-10.7 ms a frame.
- 2026-11-02: R5.3 B1, the wide view composed straight onto the screens (the user chose B, then C, D; A, skipping
  unchanged pictures, left out: after D every picture changes). In the direct build with the wide view each engine is
  now drawn onto its screen's target (pica_display.c `screen_compose`): no 256x192 picture of the engine first, no
  copy of it, no margins drawn apart. Beyond the DS's picture go the backdrop, the wide 3D picture, the BGs the margins
  drew before (`margin_bg`), the window outside region and the effects; the other BGs and the sprites are clipped to
  the DS's 256x192. For the first layer over a backdrop that is a 2nd target and the last one before no brightness
  effect, the passes that would draw nothing are left out (without windows; the margins did that before). Checks
  (Azahar, the continue script on wide.sav, `ACWW_RTC` fixed - the time of day changes the 3D's light): every
  composited picture's DS part the old path's (`ACWW_PICA_HASH`'s new "screens" hash over the screens' targets, read
  back once the GPU is done: a read right after C3D_FrameEnd raced the GPU in town; the pictures in the same order,
  the composited count one apart in one run); the whole screens at 12 frames (the title, the house's text boxes, the
  iris, black, the town; `ACWW_PICA_SHOT`, new: both screens as drawn to `sdmc:/3ds/acww/screen<0|1>-<frame>.ppm`)
  pixel for pixel the old path's. In town a composited frame fills 351,000 pixels (was 713,000: no "screens" copy,
  backdrop 194,000 -> 173,000, BG layers 109,000 -> 70,000, the 3D layer 105,000 -> 102,000 in one pass); core 2's
  compositing (Azahar, without the checks) 6.0 -> 4.0 ms a frame, 43.0 -> 44.3 game frames a second in town (Azahar
  runs the cores one after another: the hardware decides). The plain 3DS build: its 1501 composited pictures to frame
  3000 the build before's. `ACWW_PICA_SCREEN=0` brings the old path back, for comparing on hardware.
- 2026-11-02: the user's run of B1 (New 3DS, town): the GPU's compositing 8.0-9.8 ms a composited frame (was
  8.6-10.8): half the pixels, 7 % less time, so the fill rate isn't what costs. `ACWW_PICA_PROBE=1` (new, with
  `ACWW_FPS`: every 300 composited frames the next part left out, the report saying which) in the user's next run,
  town, three rounds alike: everything 9.4-9.7 ms; without the sprites' upload 6.6 (the atlas, 512 KB, copied in
  full every frame, into linear memory in the wide view); without the backdrop 6.6 (2.8 ms for 173,000 pixels of
  one colour); without the BGs and OBJs 7.2; without the blended passes (the 3D layer) 8.2; without any draw 3.8;
  without draws, clears and upload 1.3. The backdrop's cost: the sky's colour changes every line (its H-blank), so
  every layer of the top screen was drawn as one-line quads (`ACWW_PICA_STATS` now counts quads: backdrop 182, BG
  layers 141 a frame); a thin quad reads and writes each 8x8 tile of the target once per line. Changed (direct
  build): the backdrop from a texture of a colour per line (`sBackdrop`), one quad per group, and the backdrop no
  longer cuts the runs of lines (backdrop quads 182 -> 19); the sprites' upload only the atlas's rows in use. Azahar:
  every picture and the 6 screenshots the build before's. The user's run: the compositing 9.4-9.7 -> 5.1 ms (the
  upload 0.2, the backdrop 0.6, the BGs and OBJs 1.1-1.8, the 3D layer 0.5, no draws 1.9, nothing 1.4). What still
  cuts the top screen into lines: the sky's BG3 changes its PA every line (128 at the top to 253 at the bottom, 1/PA
  linear in the line: a perspective; its reference point with it) and BLDALPHA changes on 34 lines.
- 2026-11-02: the clouds beyond the DS's picture (the user: above the DS's old edge they weren't in perspective any
  more): the margins above and below took the first and last line's parameters. Now (screen_compose) an affine BG
  whose PA-PD change over the 32 lines at an edge goes on line by line into the margins, its parameters and reference
  point from a quadratic fitted to those lines (`edge_fit`). The DS's part unchanged (Azahar: the pictures the build
  before's); the title's sky shows larger clouds above the old edge, as the perspective goes on. 48 one-line quads
  more a pass for that BG. The plain 3DS build: its 1501 pictures to frame 3000 still HEAD's.
- 2026-11-02: the user's run of the fitted margins: compositing 5.6 ms (5.1 before: the 48 one-line quads about 0.5
  ms); the clouds above the old edge didn't look right (the user). Replaced by the game's own formulas: the sky's line
  tables come from Sky_UpdateLineTables (src/main/unk_020b8d9c.cpp): table line j has PA = 0x10000 / (0x200 -
  256 j / 192), X = the scroll X - (PA - 0x100) * 0x80, Y = the scroll Y + (u * u >> 8) * 0xc0 with u = (256 j +
  0x3000) / 0xc0; the screen's line L shows table line L - 1 + screenOffsetY (Sky_HBlankMain / Sub: the H-blank after
  line L-1 writes it; line 0 table line 0, from the V-blank). Now (N3DS_DIRECT) Sky_VBlankMain / Sub give the 24 lines
  above (table lines L + offset) and below (L - 1 + offset, not clamped at 191 as the DS's lines are) to the runtime
  (`gpu2d_margin_affine`, recorded at the frame's start: Gpu2dLayers.margin), and screen_compose draws the cloud BG's
  margin lines with them (the fit is gone). The other per-line state there stays the edge line's: the blend (the
  game's lineBlend is the same above table line 0x77; from 0xa8 on it is 0x1000, the clouds not shown, so below the
  DS's last line too), the backdrop's gradient (constant beyond its key lines), the layers below the horizon. The DS's
  part unchanged (Azahar: the pictures the build before's); the clouds go on across the old top edge.
- 2026-11-02: the user: the clouds look right now. (What looked like clouds only at the bottom was a probe round of
  `ACWW_PICA_PROBE`, which leaves parts out on purpose.)
- 2026-11-02: what is left for more (the user's run of f5cfc55f, New 3DS, town, frames 2400-8760, 54 reports): 59-60
  game frames a second throughout. Core 0 a frame: game 1.1, registers 1.3, geometry 2.1 + lighting 1.0 + vertices
  1.0 (about 12 ms a 3D picture: the game draws one every third frame), 2D 1.4, VRAM 0.4, 3D 0.3; waiting for core 2
  2.0, pacing 6.0 (to spare). Core 2: 2D 1.4, 3D 2.4 (about 7 ms a picture), compositing 2.4, waiting for the GPU
  2.8, idle 7.8. The GPU: compositing 5.5-5.6 ms a composited frame, 3D 1.8 (about 5.4 ms a picture). In nearly
  every 120 frames the slowest frame is 45 ms: core 2's 3D job then 29-34 ms (not looked into; texture decoding,
  **unverified**). Estimates for 60 3D pictures a second (interpolated): the GPU about 5.6 + 3 x 1.8 = 11 ms of 16.7;
  core 0 would need the geometry three times (+8 ms a frame against about 6-8 to spare, all in the game's frames):
  not without R1's transform and lighting in the vertex shader (C); core 2's 3D job three times (+4.7 of 7.8 idle)
  only if the in-between pictures reuse the batches. A higher 3D resolution: the 3D is already drawn 1:1 in the
  screen's own pixels (the wide picture, 400x240 in 512x256), so only supersampling (smoother edges) is left: 2x2 needs
  a 1024x512 picture (2 MB, double-buffered, plus depth) against 1338 KB of VRAM free, and four times the 3D's pixels.
- 2026-11-02: the 45 ms frames (core 2's 3D job 29-34 ms on hardware; Azahar shows them too, 55-68 ms, in the title
  and in town). Timed in the job (an instrumented build, not committed): the decoding of textures again, 8 textures
  (63,744 texels) each time, after 1-2 changed 4 KB pieces of texture VRAM; the decoded textures were the same as
  before (hashes). What changes: five colours of a palette the game animates (palette bytes 0x6e-0x78), which shares
  its 4 KB piece with the palettes of six large 4x4 textures (palette base 0xa0); and an animated 64x32 16-colour
  texture (0x2dc00) whose piece holds two more. Changed (gpu3d_pica.c, every 3DS build): the versions per 64 bytes
  instead of 4 KB (compared 4 KB at a time first). Azahar: the longest 3D job 65-68 -> 16-19 ms outside the loads; the
  pictures the build before's (both the direct and the plain 3DS build). Left: `track` compares all 640 KB of texture
  VRAM whenever a texture was uploaded (nearly every 3D picture; about 3-4 ms a picture in Azahar), after g3_tex_setup
  copied them all on core 0: the written ranges (GX_LoadTex) would do.
- 2026-11-02: the user's run with the fine texture versions (New 3DS, about 7100 frames; town 2040-6600, 39
  reports): 59.7-59.9 game frames a second; the slowest frame of each 120 now 37-39 ms (was about 45), and none of it
  core 2's 3D job any more: the game's own frame (game, registers, geometry, lighting, vertices: 20-25 ms, the game's
  work every third frame) and pacing. Core 0 a frame: geometry 2.4 + lighting 1.1 + vertices 1.0, registers 1.3, 2D
  1.3, game 1.2, VRAM 0.4, 3D 0.4; waiting for core 2 1.6, pacing 6.0. Core 2: 3D 1.9 (was 2.4), 2D 1.5, compositing
  2.5, waiting for the GPU 4.3, idle 6.7. The GPU: compositing 5.5-5.6 ms a composited frame (4.4-4.8 in part of the
  town), 3D 1.6-2.0.
- 2026-11-02: C1, the vertices transformed by the GPU (the user: "mach C"; direct build, `ACWW_PICA_TRANSFORM=0` the
  CPU's transform as before). Measured first (the PC build, the save script): 2200-5500 vertices a 3D picture, the
  clip matrix changing 150-470 times, about half of them within a polygon or strip (skinned characters). So the
  geometry engine now gives each vertex in object coordinates with the id of its clip matrix (g3p_matrix: the
  matrices in the order made, a list's own), and gpu3d_pica.c batches them with a palette of 12 matrices (vertex
  shader uniforms, matched by content: a skinned strip goes A, B, A, B; a full palette starts a batch with an empty
  one, a state change keeps it, slots uploaded only when their matrix changes). The new vertex shader
  (gpu3d_pica_direct.v.pica) does the transform, the viewport and the picture's clip space, the DS's depth (Z- and
  W-buffer), the texture coordinates and the fog's alpha (the density table in uniforms, as fog_density); the GPU
  culls by attr bits 6-7 (GPU_CULL_BACK_CCW for front faces only: the doors' backs show without culling, not with
  it). Vertices 16 bytes instead of 40. Found on the way: (1) without the CPU's culling a list has about twice the
  polygons (2770 against the DS's 2048 after culling, which the game keeps to): the lists hold 4096 now; (2) clip
  coordinates in the DS's own units (w about 10^5) made objects vanish on the GPU (Azahar): scaled by 1/4096 as the
  CPU's vertices were, it draws them (why: not looked into). Dropped, as differences by design: the far-plane rule
  (crossing it hidden unless attr bit 12) and the facing tested before clipping (a bow-tie quad may lose one
  triangle). Checks (Azahar, the continue script, `ACWW_RTC` fixed): the 3D screen against the CPU transform at 8
  frames (the title, the house with the player close, the town): 0.07-0.9 % of the pixels differ, 0.02-0.21 % by
  more than 8 of 255; the top screen the same but at two frames (54-164 pixels). Every list's polygons all there (the
  CPU's tests would keep 1398 of 2770 sent, the CPU path had 1396). Not checkable: `ACWW_RAMHASH` (two runs of the
  old path already differ: the sound's thread runs in real time; host pointers in game memory differ from frame 0);
  the game reads no vertex results (box and position tests stay the CPU's, with the same matrices; RAM_COUNT unread).
  Z-buffered polygons: none in the game, that branch **unverified**. Azahar's core 0 in town: vertices 2.95 -> 2.55
  ms a frame (geometry, lighting the same), core 2's 3D job 3.5 -> 4.0 ms (more batches and vertices), game frames a
  second 47.1 -> 46.0 (Azahar runs the cores one after another). What C1 is for: the lists can be drawn again with
  other matrices (R6.3, interpolation) without the geometry engine; its saving on core 0 is small (at most the
  "vertices" part). On hardware: to be measured.
- 2026-11-02: the user's runs of C1 on a New 3DS (town, from frame 2040), the GPU's transform against
  `ACWW_PICA_TRANSFORM=0`: core 0 vertices 1.00 against 0.97 ms a frame, geometry 1.80 against 1.73, core 2's 3D job
  1.53 against 1.47, the GPU's 3D 1.8-1.9 against 1.5-1.7 ms a composited frame; 59.9 against 60.0 game frames a
  second. So no saving on hardware (the "vertices" part is only about 1 ms a frame there). And it flickers on
  hardware, not in Azahar (the user): the trees' leaves, the trunks' feet on the ground, the museum's front.
  Suspected, **unverified**: the PICA's vertex shader computes in 24-bit floats, so surfaces meant to lie exactly on
  each other (decals, "depth equal") don't any more; the CPU's transform is exact (fixed point) and only its result
  rounded; Azahar computes more precisely. Consequence for R6.3: the in-between pictures can be transformed on the
  CPU from recorded object coordinates and interpolated matrices (about 3 ms a 3D picture on core 0, which has about
  7 ms a frame to spare); C1's GPU transform isn't needed for it. The user's choice: C1 not taken, kept on the branch
  `c1-gpu-transform` (1c7f7422); here the CPU's transform as before.
- 2026-11-02: D, the 3D pictures between the game's (the user: "dann leg bitte wie geplant los"), direct build only.
  D1: the geometry engine gives each vertex its object coordinates and the id of its clip matrix (G3Vertex.obj, seg;
  g3p_matrix once per clip matrix made, 150-470 a picture); gpu3d_pica.c keeps them beside the list's vertices (Rec,
  normal memory) and can draw the list again from other matrices (`replay`: the geometry engine's s64 arithmetic,
  then the same conversion to the GPU's clip space, `clip_pos`, as g3p_polygon; the viewport kept per batch).
  Check (Azahar, the continue script on wide.sav, `ACWW_RTC` fixed, `ACWW_PICA_HASH=1`): `ACWW_PICA_REPLAY=1` (new:
  the list drawn from the replay with its own matrices, the positions compared) 0 vertices differ in 1800+ pictures;
  the recording alone (`ACWW_PICA_INTERP=0`) gives the build before's screens in the same order, 2729 pictures to
  frame 4800 (two runs of the build before: the same as each other). With the replay check on, 24 pictures right
  after the title's touch (game frames 2212-2230) differ and the rest is the same: **unverified** why; suspected the
  check's work on core 2 shifting what depends on the sound's real time (Azahar's emulated time).
  D3 and D4: the pairs. Each clip matrix gets a signature, a hash of the object coordinates of the first 4 vertices
  submitted under it (before culling, so a turning model keeps it); at a new list's first picture (core 2) each
  matrix is paired with the one of the list before of the same signature whose first vertex lands nearest on the
  screen (at most 32 tried), none beyond a quarter of the screen; fewer than half paired: a cut, nothing
  interpolated. The phase on the game's thread (g3p_between, every frame from g3_render): the frames between the
  last two lists (2-4: interpolated, else not); the k-th frame after a new list shows t = (k + 1) / period, so the
  newest list is shown as made on its last frame (one game frame later than before). Matrices interpolated element
  by element in 20.12 (t in 16 bits), the vertices transformed as the replay into one of two vertex buffers (linear
  memory, 2 x 480 KB; Rec and the matrices 3 x about 650 KB of the heap). Measured (Azahar, `ACWW_PICA_STATS=1`, to
  frame 5900): 99.4-100 % of the matrices paired (36,000-72,000 per 300 lists), 0-2 cuts per 300 lists. Walking
  left in town (frames 5450-5455): with it the bottom screen changes every frame (32,000-38,000 pixels), without it
  every third (51,000-56,000); the house, the mailbox and the player move a step each frame, the player's skinned
  model whole. Not looked at: a picture between across a cut (D4), the shadows (Azahar draws none). The period is
  counted in g3_render's frames, so it holds with frameskip 0 only (the direct build's default; `ACWW_FRAMESKIP=1`
  rasterises every other frame: the period then 1-2, wrong). Found on the way: the direct build hangs in Azahar with `ACWW_HEADLESS=1` (the build before too;
  nothing logged, the log's thread starts at frame 600): the checks ran without it.
- 2026-11-02: the user's runs of D on a New 3DS (logs/normal.txt: on, frames to 6960; logs/interp0.txt:
  `ACWW_PICA_INTERP=0`, to 3000; reports from frame 2400): game frames a second 43-70, median 55 (off: 59.8-60.0).
  On: core 0 waits for the GPU thread 7.7 ms a frame (off 1.4), pacing 1.0 (off 8.7); core 2 GPU wait 8.1 (off
  3.7), idle 1.3 (off 8.5), its 3D job about 5.4 ms a frame (was 1.9: a picture every frame now); the GPU's 3D 5.0
  ms a composited frame (off 1.2), compositing 5.9. So the GPU thread is the limit, and it waited for the GPU before
  making the picture between (pair, lerp_matrices, replay ran after C3D_FrameBegin, which waits for the compositing
  before): now done before it, overlapping the GPU's work (the replay buffer written then was last read by the 3D
  job before the one before, which the last job's C3D_FrameBegin waited for). To be measured. Both runs end with
  the watchdog's report (virtual time stopped: the program left, **unverified**).
- 2026-11-02: the user's run with the replay before C3D_FrameBegin (logs/log.txt, New 3DS, town with more villagers
  about, frames 2040-6480): 46-58 game frames a second while the 3D is on (59.8 indoors without 3D, frames
  5520-6120). Per frame, core 2 (the GPU thread): 2D 1.3, 3D 5.1-7.7 (more with more villagers: 7.4-7.7 at 46-48
  frames a second), compositing 2.5-2.9, GPU wait 6.4-9.3, idle 0.1-3.9; the GPU: 3D 5.2-6.2 and compositing 5.7-5.9
  ms a composited frame (about 12 of 16.7: room left); core 0 waits for core 2 6.2-8.3 ms and has no pacing left.
  So the limit is core 2: its own work (about 10 ms) and its waits for the GPU (7-8 ms) add up, because citro3d
  works a frame at a time (C3D_FrameBegin waits until the GPU has done the work before) and the GPU thread does
  every job's CPU part itself; moving the replay before the wait saved little (GPU wait 8.1 -> 7-8 ms). The 3D job
  cost about 5.7 ms a picture before D too (1.9 ms a frame for a picture every third frame), so most of it is
  drawing the list (the batches' states, textures, citro3d's calls), not the replay: **unverified** until the split.
  For that, the GPU thread's profile now also gives the 3D job's parts: "vertices" (pairs, matrices, replay),
  "VRAM" (`track`: the texture VRAM compared after uploads), "lighting" (the fog worked into the alpha); "3D" the
  rest. Azahar (not the hardware's proportions): 3D 5.4, vertices 3.2, VRAM 1.3, lighting 0.9 ms a frame.
- 2026-11-02: the user's run with the 3D job's parts (logs/log.txt, New 3DS, town, frames 1800-3960): 51-57 game
  frames a second. Core 2 a frame: 2D 1.3, 3D (drawing the list) 2.0-2.5, vertices (pairs, replay) 1.6-2.3, VRAM
  (`track`) 0.2-1.2, lighting (the fog in the alpha) 0.8-1.1, compositing 2.5-2.9, GPU wait 6.3-8.1 (9.3), idle
  0.1-2.3; the GPU: 3D 5.0-5.6 and compositing 5.9-6.0 ms a composited frame. So the GPU wait is the largest part,
  and it is the GPU's own time not overlapped: the 3D job waits for the compositing's GPU work (6 ms) less the work
  done before its C3D_FrameBegin (vertices, VRAM: about 2.7), the compositing job for the 3D's (5.3) less its 2D
  (1.3); a frame on core 2 is then about both GPU times plus what core 2 does inside a frame (the 3D's drawing, the
  fog, the compositing: about 6 ms), about 17 ms. Changed: the fog worked into the alpha before C3D_FrameBegin too
  (Azahar: the walk's pictures the build before's). Left: what core 2 does inside a frame (the list's batches drawn
  again for each picture between: the same states and textures; the compositing's CPU part), and the GPU's 3D time
  per picture (5.3 ms a frame now, the pictures in the wide view; `ACWW_PICA_PROBE` splits it); a command list
  recorded once per list and sent again for the pictures between (GPUCMD_SetBuffer, GX_ProcessCommandList in
  libctru) would remove the drawing's CPU part, a larger change.
- 2026-11-02: the split between core 0, core 2 and the GPU looked at again (the user: is there a better
  architecture?). Facts: the PICA runs command lists only, so everything that prepares its work is the CPU's (the
  DS's texture formats decoded, the 2D layers, the vertex and command lists); citro3d's C3D_FrameBegin waits for the
  whole GPU queue (libcitro3d.a disassembled: gxCmdQueueWait(-1)), so at most one citro3d frame is in flight, and
  C3D_SyncDisplayTransfer inside a frame only splits it and queues the transfer (no wait). Threads: the GPU thread on
  core 2 at the game's priority (0x30); the sound and the profile's sampler also on core 2 at 0x18, above it, so
  their time shows in the GPU thread's parts (**unverified** how much: the sound was 1.1-2.0 ms a frame on core 0);
  core 1 unused (the fallback only). The split dates from 2026-10-29, when core 0 was the limit (about 21 ms); with
  the pictures between, core 2 is (the log above) and core 0 waits 6-8 ms. The largest loss: two citro3d frames a
  display frame (the 3D job's and the compositing's), each waiting for the other's GPU work, so core 2 and the GPU
  take turns. Proposed, by gain: (1) the 3D picture and the compositing in one citro3d frame, C3D_FrameSplit after
  the 3D (or in parts of it) so the GPU draws the 3D while core 2 builds the compositing: estimated about 2.3 (the
  3D's commands) + 11.3 (the GPU) = 13.6 ms a frame, **unverified**; (2) the sound on core 1
  (APT_SetAppCpuTimeLimit; about 10 % of it); (3) the 3D's command list recorded once per list and sent again for
  the pictures between; (4) the GPU's compositing (6 ms a frame), then the GPU's limit.
- 2026-11-02: (1) and (2) of the proposal above done (the user: "ja mach"). Found first (libcitro3d.a and libctru.a
  disassembled): C3D_FrameSplit only queues a frame's commands, the GPU starts on them at C3D_FrameEnd
  (gxCmdQueueRun); citro3d's GX queue is the first member of `__C3D_Context`; gxCmdQueueRun returns at once on a
  running queue, and on an idle one marks it so that gxCmdQueueAdd starts it; C3D_FrameEnd(0) flushes the data cache
  over the whole linear heap before it runs the queue, C3D_FrameEnd(GX_CMDLIST_FLUSH) only the last commands;
  C3D_FrameBufClear queues its memory fill without a split, C3D_FrameDrawOn doesn't split, C3D_SyncDisplayTransfer
  in a frame splits it without a flush and queues the transfer.
  (1) pica_display.c `pdisp_frame_open` / `pdisp_frame_run` / `frame_close`, direct build only (`ACWW_PICA_MERGE=0`:
  a frame each, as before): the 3D job begins the citro3d frame, queues its commands flushed
  (C3D_FrameSplit(GX_CMDLIST_FLUSH)) and starts the queue; the compositing goes on in the same frame (no
  C3D_FrameBegin, so no wait for the 3D's GPU work) and ends it with GX_CMDLIST_FLUSH; before each transfer in a
  running frame the commands are queued flushed first; every CPU write the GPU reads was already flushed where it is
  made (textures, vertex lists, the backdrop, the transfers' sources). pdisp_idle ends a frame left open, a 3D job
  after a 3D job ends the one before. The ACWW_FPS line of the GPU's time has "both" now (frames with the 3D and the
  compositing; C3D_GetDrawingTime counts from the frame's end, so it leaves out what the GPU drew meanwhile).
  (2) the sound's thread on the system core 1 first (host_plat_sampler_start's new `core1`; `ACWW_SOUND_CORE=2`: core
  2 first, as before): Azahar's log "a thread on core 1", the DSP's queue never ran dry.
  Checks (Azahar, the continue script with the walk, `ACWW_RTC` fixed): the plain 3DS build against HEAD's, 1278
  composited pictures the same; the direct build with (1), `ACWW_PICA_INTERP=0` and `ACWW_SOUND_CORE=2`: every
  picture to frame 3524 the build before's frame by frame, after it the same pictures a frame later (2729 to frame
  4800 in the same order: the game's time shifted once, as with the replay check); with everything on, the walk's
  frames 5450-5455 the build before's pictures of a frame earlier. Azahar's speed says nothing here (it runs the
  cores one after another): to be measured on hardware.
- 2026-11-02: one citro3d frame for the 3D and the compositing puts both's GX queue entries (transfers, fills,
  command lists) in a queue cleared only at C3D_FrameBegin, which breaks when full (libctru's gxCmdQueueAdd:
  svcBreak). Measured (Azahar, `ACWW_PICA_STATS=1`: the most entries a frame, read after C3D_FrameEnd): the continue
  script with the walk to frame 5600 and the new game's script (intro, name entry) to 9000: at most 14 of 32. Guard:
  before a transfer in a running frame with fewer than 12 entries left, the frame is ended and the compositing goes
  on in one of its own (checked with a build where it always does: the walk's pictures the build before's).
- 2026-11-02: on hardware (the user) the direct build with one citro3d frame stopped after the boot logo: its sound
  played to the end, then nothing, the screens black but for the frame counter at 0; with `ACWW_PICA_MERGE=0` the
  title came, with `ACWW_SOUND_CORE=2` (the frame still one) not. So the one frame stalls on hardware (Azahar: not);
  why is **unverified** (looked through, libctru.a and libcitro3d.a disassembled: the queue run, drained and
  restarted mid-frame, gxCmdQueueWait waiting on isRunning, the command lists' flushes; nothing found). Now off by
  default (`ACWW_PICA_MERGE=1` on). For the diagnosis: when the GPU's work isn't done after a second at a
  C3D_FrameBegin, the GX queue's entries (their types: 1 command list, 2 memory fill, 3 display transfer), how many
  were submitted and how many done go to the log, written out at once (gxCmdQueueWait's timeout is in system ticks,
  not nanoseconds as gx.h says: libctru.a disassembled). Azahar, the default: "in a frame each", the sound on core 1.
- 2026-11-02: the user's runs. (a) `ACWW_PICA_MERGE=1` (logs/log.txt): the stall's report at frame 7, after 5 frames
  with one citro3d frame: 10 entries, 3 submitted, 2 done, types 2 2 1 4 2 1 4 1 3 3: the 3D target's two memory
  fills done, the 3D's command list submitted and never done. So the GPU stopped on that list: it was queued with
  GX_CMDLIST_FLUSH, the flush left to GSP, and run at once, where C3D_FrameEnd(0) flushes the data cache itself
  (GSPGPU_FlushDataCache over the linear heap) before it runs the queue (**unverified** that GSP's flush misses it;
  Azahar has no cache). Also found: C3D_TexLoadImage of a texture in VRAM (the staged BG tiles, drawn while the
  compositing is made) is C3D_SyncTextureCopy, which in a frame queues the commands before it without a flush: with
  a running queue those would start unflushed too. Changed: the 3D's list queued with the queue stopped, its range
  flushed by GSPGPU_FlushDataCache (GPUCMD_Split ends a list and moves gpuCmdBuf past it: libctru.a disassembled),
  then run; before a transfer or a staged tile's copy with commands pending, and at the frame's end, the queue is
  stopped once the GPU has done what it holds (gxCmdQueueWait, gxCmdQueueStop), and the frame ends with
  C3D_FrameEnd(0) as citro3d's own (so only transfers and fills, whose sources are flushed where written, start
  while the frame is open). Azahar, merged: no stall report, the walk's pictures the build before's (a frame later),
  at most 14 GX entries. To be run on hardware with `ACWW_PICA_MERGE=1`.
  (b) the default with the sound on core 1 (logs/normal.txt): at frame 968 the game stood 4 s (the watchdog; the
  slowest frame 4029 ms, game 3997) and then "pxi7: receive FIFO overflow": **unverified**, suspected the sound's
  thread held back on the system core (the 30 % limit) while the game waited for it; town 47-53 game frames a second,
  core 2 GPU wait 7-10 ms, no better. The sound is back on core 2 by default (`ACWW_SOUND_CORE=1`: core 1 first).
- 2026-11-02: the user's runs with the fixed one frame (logs/merge1.txt, `ACWW_PICA_MERGE=1`, to frame 13217) and
  without (logs/normal.txt, to 3308), the sound on core 2 in both; medians over the reports with pictures between
  (102 and 18; the routes not the same): game frames a second 54.3 (38-59) against 50.0 (39-52); core 2 GPU wait 5.5
  against 9.0 ms, compositing 4.2 against 3.5, 3D 2.1, vertices 1.7-1.8, lighting 1.0-1.1, VRAM 0.9-1.0, 2D 1.3-1.5,
  idle 1.2 against 0.6; core 0 waits for core 2 7.0 against 8.4. No stall report in 12,000 frames: one frame is on by
  default now. The 4 s stand with "pxi7: receive FIFO overflow" came again in merge1.txt (frame 1067) with the sound
  on core 2, so it isn't the sound on core 1 (the entry before is wrong there); cause **unverified** (twice near
  frame 1000; not in normal.txt).
- 2026-11-02: the 3D job's preparation on core 0 (the user: "ja mach das"): gpu3d_pica.c `prepare`, called by
  g3p_render on the game's thread once the last 3D job is done: the texture VRAM's versions (`track`), the pairs, the
  picture between's vertices (replay) and the fog in their alpha (on hardware about 0.9 + 1.8 + 1.0 ms a frame on
  core 2, while core 0 waited for core 2 about 7). The job only draws what it is handed. The replay buffer written
  then was last drawn two jobs ago, which the last job's C3D_FrameBegin waited for. The profile: all of it core 0's
  "3D" now (the GPU thread's "vertices", "lighting", "VRAM" stay 0). Checks (Azahar): the plain 3DS build's 1278
  pictures HEAD's; `ACWW_PICA_REPLAY=1 ACWW_PICA_INTERP=0` 0 vertices differ in 900 pictures; the walk's frames
  5450-5455 the build before's pixel for pixel. To be measured on hardware.
- 2026-11-02: the user's run with `prepare` on core 0 (logs/log.txt, frames 240-4440, 32 reports with the 3D on):
  game frames a second 37.5-53.8, median 50.8 (merge1.txt: 54.3, another route). Core 0: "3D" 3.8 (the moved
  work), waiting for core 2 5.3 (was 7.0); core 2: 3D 2.0, compositing 5.1, GPU wait 8.4, idle 2.7: about 11 ms a
  frame of nothing to do. So the GPU is the limit now (normal.txt: 3D 5.3 + compositing 8.2 ms of its time a frame,
  besides the gaps while it waits for commands), and moving CPU work can't pay: undone, `prepare` runs in the job
  again, before the frame begins. Next: where the GPU's time goes. ACWW_PICA_STATS's "pica interp" line gives a
  list's draws (batches) and vertices too now.
- 2026-11-02: the user's probe run (logs/log.txt, `ACWW_PICA_MERGE=0 ACWW_PICA_PROBE=1 ACWW_PICA_STATS=1`, town):
  the GPU's 3D 5-7 ms a picture for a list of 98-114 draws (batches) and 3100-4300 vertices; the compositing 9.2 ms
  (probe 0), 8.7 without the sprites' upload, 7.3 without the blended passes (the 3D layer, 141,000 pixels), 7.9
  without the backdrop (173,000 pixels, 31 draws), 7.5 without the BGs and OBJs (70,000 pixels, 164 quads), 2.6
  without any draw, 1.8 without the screens' clears too. So the draws are about 6.6 ms; the backdrop (a small
  texture) about 133 million pixels a second, the BG layers (in the main memory: the wide view keeps them out of
  VRAM) about 41: textures read from the main memory are slow, and the 3D's are all there (RGBA8, C3D_TexInit).
  Changed (direct build): the 3D's textures of the formats with a 1-bit alpha at most (all but A3I5 and A5I3) as
  RGBA5551 (`ACWW_PICA_TEX16=0`: RGBA8 as before): half the bytes a texel. Azahar: the walk's pictures differ in
  about 2,300 of 76,800 pixels, every one by 1 of 255 in a channel (the 5 bits' expansion to 8: not the decoder's
  bit repetition there); `ACWW_PICA_STATS` gives the 3D's textures in use now: in town 53-68 of them, 330-500 KB
  (RGBA5551 and RGBA8), which would fit the 1338 KB of VRAM left (next: in VRAM, through a linear staging copy:
  the CPU's writes to VRAM fault on hardware, docs/3ds.md). To be measured on hardware.
- 2026-11-02: the user's run with the RGBA5551 textures (logs/merge0.txt, `ACWW_PICA_MERGE=0 ACWW_FPS=1`, town): the
  GPU's 3D 3.6-4.6 ms a picture (the probe run before: 5-7), the compositing 9.3; game frames a second 39.8-55.4,
  median 49.8 (50.0 before, the same mode); core 2 GPU wait 8.8, idle 0.2. So the textures' reads from the main
  memory were a large part of the 3D's GPU time; the compositing is the GPU's largest part now.
- 2026-11-02: the compositing's GPU time, step (2) (the user: "ok dann bitte 2"). The backdrop under the bottom
  screen's 3D can't go: the sky's BG shows behind the 3D there. Done instead (pica_display.c `top_unchanged`, direct
  build, `ACWW_PICA_TOPSKIP=0` off): the top screen drawn again only when a hash of its engine's inputs changed (the
  lines' states, the affine reference points, the OBJ priorities, the margins' affine BG, the BG palette, the BG
  VRAM's granules, the sprites and their atlas pixels, the fps text, split off show_fps into fps_text so that it
  counts every frame); never skipped with the 3D or a layer the CPU drew on it, or while probing. Its target keeps the
  picture, which C3D_FrameEnd shows again. Checks (Azahar, the continue script with the walk to frame 5500,
  `ACWW_PICA_HASH=1`): the screens' hash at every frame the same with and without it (5272 pictures), whole-screen
  shots at 9 frames (title, house, town, the walk) pixel for pixel. It skips about 1 of 3 frames in town
  (`ACWW_PICA_STATS`: drawn in 200 of 300): a temporary build named what changed: in each game frame's first frame
  the affine reference points, in its second the reference points and the margins' affine BG again (the sky's
  clouds), in its third nothing; so the top screen does change in 2 frames of 3. To be measured on hardware.
- 2026-11-02: the user's run with the top screen's skip (logs/log.txt, `ACWW_FPS=1 ACWW_PICA_STATS=1`, frames
  240-4560): game frames a second 41-64, median 53.5, with frames 1560-3240 at 56-64 and the fuller parts at 49-54;
  the top screen drawn in about 200 of 300 frames, as in Azahar; core 2 a frame: 2D 1.1, 3D 2.0, vertices 1.9,
  lighting 1.0, VRAM 0.6, compositing 5.1 (2.5-2.9 with a frame each before the one frame, 4.2 in merge1.txt), GPU
  wait 4.5 (5.5), idle 2.2; core 0 waits for core 2 7.0. The 4 s stand with the PXI FIFO's overflow again (about
  frame 1100). Next (the user: "ja mach das so"): (1) the compositing's CPU time split in the profile, the GPU
  thread's line: "BG tiles" (bg_texture: the BGs' tile textures made and copied), "top hash" (top_unchanged),
  "frame end" (C3D_FrameEnd: with GX_CMDLIST_FLUSH unset it flushes the data cache over the linear heap); the waits
  in frame_stop stay "GPU wait", the transfers "uploads". (2) the replay makes a vertex's position once for the
  polygon's vertices it shares (the last three made in a batch, the same object coordinates and matrix: the same
  position). Checks (Azahar): `ACWW_PICA_REPLAY=1 ACWW_PICA_INTERP=0` 0 vertices differ in 900 pictures; the walk's
  frames 5450-5454 the build before's frames 5451-5455 pixel for pixel (the game's time shifted again). Azahar's
  profile (not the hardware's proportions): BG tiles 0.3, top hash 1.7 (!), frame end 0.3 ms. To be run on hardware.
- 2026-11-02: the user's run with the split profile (logs/log.txt, frames 240-4440): game frames a second 41-63,
  median 55.3, long stretches at 59-61. Core 2 a frame: 2D 1.1, 3D 2.0, vertices 1.6 (was 1.9: the shared
  vertices' reuse saved 0.3), lighting 1.0, VRAM 0.7, compositing 3.0, BG tiles 0.6, top hash 1.1, frame end 0.2,
  GPU wait 4.5, idle 1.5; core 0 waits for core 2 6.8. So the compositing's 5.1 was 3.0 of commands, 0.6 of tiles,
  0.2 of the frame's end and 1.1 of the top screen's hash, mostly of the engine's BG VRAM (128 KB a frame, read on
  core 2 just after core 0 copied it). Changed: the hash leaves the BG VRAM and palette out; instead the top
  engine's BG textures are brought up to date before the decision (bg_texture, which compares the palette and only
  the map and tiles a BG uses, once a frame, and which the drawing needs anyway), and a texture made again means a
  change. Checks (Azahar, the walk to 5500, `ACWW_PICA_HASH=1`, with and without `ACWW_PICA_TOPSKIP=0`): the 5273
  pictures the same in the same order (some a frame apart: the game's time); skipped as often as before (100 of
  300 in town); Azahar's top hash 1.7 -> 0.2 ms. To be run on hardware.
- 2026-11-02: the user's run with the cheaper top screen check (logs/log.txt, frames 240-7440, 57 reports with
  pictures between): game frames a second 41.8-67.0, median 58.4 (55.3 before), long stretches at 59-61, the fuller
  parts at 52-57; no 4 s stand this time. Core 2: top hash 0.2 ms (1.1), compositing 3.2, BG tiles 0.7, frame end
  0.2, vertices 1.6, 3D 1.9, lighting 1.0, VRAM 0.7, 2D 1.1, GPU wait 4.6, idle 2.1; core 0 waits for core 2 6.4.
  The changes since cd9fd9a0 committed one by one (the user's wish): the profile's parts, the system core for the
  sound (option), the interpolation, the one citro3d frame, the top screen's skip, the RGBA5551 textures.
  What could come next, by the GPU's and core 2's time it would save:
  (a) the 3D's textures in VRAM (310-480 KB of 1338 KB free; the GPU reads its own memory faster: the 3D's GPU time
      halved before with half the bytes), through a staging copy (the CPU's writes to VRAM fault on hardware) in
      the one frame's rules (the copies' commands flushed, the queue stopped first);
  (b) the compositing's commands (3.2 ms of core 2): fewer state changes and draws (backdrop 31, BG layers 164
      quads, blended 205), or a command list kept for the top screen;
  (c) the replay (1.6 ms): positions only into a buffer of their own (16 of 40 bytes a vertex), the fog (1.0 ms)
      only where it is on;
  (d) the 3D's command list recorded once per list and sent again for the pictures between (about 2 ms of core
      2's 3D in 2 frames of 3; own command buffers beside citro3d's);
  (e) the GPU's compositing: the BG layers in VRAM where they fit (the wide view keeps them in the main memory);
  (f) the 4 s stand near frame 1000 with "pxi7: receive FIFO overflow" (seen in 4 of 6 runs, a bug of its own);
  (g) the D items not looked at: a cut, the shadows, the latency; B2 (the UI at the wide screens' edges); R6.1
      (controls), R6.2 (languages), R5.2 (stereoscopic 3D).
  Proposed to the user: (a) first (the largest gain expected where town is full), then (f).

- 2026-11-02: the iris drawn anew over the whole wide screens (the user: the iris slow, and its edges beyond the DS's
  screen plain black). The game's iris (IrisWipe_*, src/main/unk_020411dc.cpp) is window 0 changed every line by an
  H-blank task (the circle's span on each line), outside it a BG2 of one black tile: every line's state differs, so
  the 2D was composed a piece a line (each pass a quad a line, the window regions too), and the margins, outside the
  window, were black. Now under N3DS_DIRECT the game sets up its layers as before but without window 0 and with the
  black BG2 hidden (as inside the window), adds no H-blank task, and gives the progress (0 open to 0x1000 closed)
  at its V-blank to `gpu2d_iris` (recorded with the frame as `gpu2d_margin_affine`; -1 at IrisWipe_End, in the same
  V-blank as the cover and the brightness that follow); pica_display.c draws, after a screen's picture, black
  outside a circle round the screen's middle (64 quads of a ring) whose radius goes from 0 to the screen's corners
  (233 pixels on the top screen, 200 on the bottom one; the DS's 160 to its corners). Checks (Azahar, the continue
  script on wide.sav, `ACWW_RTC` fixed, `ACWW_PICA_SHOT` every 5 frames): the iris closes at frames 2590-2615 and
  opens at 3480-3565 on both screens, round and centred, the wide 3D picture and the sky's margins inside it.
  `ACWW_PICA_STATS` over the 300 frames with the opening: window regions 111 -> 61 kpixels a frame (78 -> 1 quads),
  BG layers 124 -> 82 quads, "as it is" and marks 6 + 6 -> 0 + 2 quads; the ring 0.5 draws, 29 quads. Azahar's frames a
  second unchanged (core 0 bound there); on hardware to be measured. Not changed: the fades and the cut, the plain
  3DS and PC builds (all under N3DS_DIRECT).
- 2026-11-02: R5.3 B2, the ways to the UI at the wide screens' edges weighed (the user: native, cheap on the
  hardware, the original look). Found: the text box's BG2 and window 0 are TalkFrame's (src/main/unk_02065e88.cpp:
  `TalkFrame_ShowBg`/`TalkFrame_HideBg`, the window's rect at lines 0x3c-0xc0, `TalkFrame::setScroll`); the clock's
  OBJs go to OAM buffer A (`Oam_DrawCell` mode 0, `Oam_UseBufferA`: engine A, the bottom screen in town); every OBJ
  of the game goes through the two buffers of src/main/unk_02087e70.cpp (`Oam_LoadBuffers`). The ways: (1) the game
  made to think its screen is 320x240: not possible (OAM's y is 8 bits, unused OBJs hidden at y 192, the maps and
  layouts 256 wide); (2) the compositor guessing elements by position or palette: breaks in menus and with OBJs
  passing by; (3) the DS's picture cut in the middle and each half pushed out: tears the text box and the menus;
  (4) anchors from the game, as the iris gives its progress: under N3DS_DIRECT the game says which OAM entries
  (a side array of 128 tags beside each buffer, set in `Oam_DrawCell` from a "current anchor" the caller sets,
  cleared with the buffers), which BG and window (TalkFrame) belong to an element and to which edges; latched with
  the frame as `iris`; screen_compose adds the offset (from the screen's width: 32 / 72 sideways, 24 up or down) to
  their quads. Same pixels, same quads: no GPU cost. To mind: anchored sprites clipped to the screen instead of
  the DS's 256x192, and an element sliding out past line 192 then visible down to 216 (the clock's `slideY`); the
  touch mapped back through the same anchors (frontend_3ds.c) for the camera button and the choices. Proposed
  order: the clock (one caller, not touched), the text box (TalkFrame, flush with the bottom), then the camera
  button and ChoiceWindow with the touch. Not found yet: the camera button's draw, the text box's OBJ callers.
- 2026-11-02: R5.3 B2 done with anchors (the user: "mach mit ankern"). The game (N3DS_DIRECT, include/n3ds_ui.h) says
  what belongs to a UI element and where: `Oam_Count` / `Oam_Anchor` tag the OAM entries drawn between them (a byte
  beside each of the two OAM buffers, cleared by `Oam_ResetBuffers`, given with the buffers to `gpu2d_obj_anchors` in
  `Oam_LoadBuffers`); `gpu2d_bg_anchor` names BGs and windows (TalkFrame_ShowBg / HideBg: BG2 and window 0), moved only on lines where
  the BGs' BGxCNT are still those at the call (a menu opened during a talk, with its own BG2, isn't moved). The
  anchor is edges (left, right, top, bottom) or "near" an id: the host takes the edges whose outer third of the DS's
  screen the element's OBJs' box is in, each frame (the four HUD panels in `HudController::draw`, HudLinkIcon and
  HudUnkIcon in `HudProc::onDraw`, `MsgUiProc::onDraw`'s InputModeIcon: their places weren't all known; not
  HudUnkSlideIcon, drawn at slideOffset + 0x80: sliding sideways round the middle it could jump from one edge's
  decision to the other's; not seen yet). The speaker's name is BG2's too (its label's tiles go to BG2's characters,
  MsgTextLabel's default vramLoader 2). The green arrow at the
  top right in town is not the camera button (HudCameraButton is the rooms' furniture grid, not shown in town) but
  one of HudProc's icons, drawn at (128, 96) with its place in its cell: `HudUnkIcon` (the PC build's callers of
  `Oam_DrawCell`; which of the three **unverified**). gpu2d.c latches them with the frame (as `iris`); an OAM entry keeps its anchor only while it holds the
  6 bytes it was given with (OAM loaded another way, e.g. NNS's, gets none). pica_display.c (screen_compose) adds the
  margins' width (32 or 72 sideways, 24 up or down) to the anchored sprites', text BGs' and windows' quads, after
  clipping them to the DS's 256x192 as before: an element sliding out of the DS's screen leaves the 3DS's at its edge.
  The touch: the bottom screen's moved boxes (the anchored OBJs' per anchor, the anchored windows' lines) with their
  moves, published by the GPU thread; `pdisp_touch_map` (frontend_3ds.c) maps a touch on a moved element back
  (shortcut: not locked; a touch may go by the frame before's boxes). Same quads as before: no GPU cost. Checked
  (Azahar, the continue script on wide.sav, `ACWW_RTC` fixed, `ACWW_PICA_SHOT` every 50 frames 2200-5200): the title's
  text box and its choices, the house's text boxes ("Preparing", the spinning icon, the arrow; the text is BG2's)
  flush with the bottom screen's bottom; in town the clock in the bottom right corner (sliding in from below), the
  green arrow in the top right one; the iris and the rest as before. The touch (Azahar, a click with xdotool on the
  arrow's new place, beyond the DS's 256x192): the game switches to the touch mode as the PC build does for a touch on
  the arrow's DS place (the pencil icon, now in the top left corner; the player turns; the clock hides and comes
  back). The same with the BGxCNT check (the text boxes still moved). The PC build's code unchanged: the four game
  files' code and data the HEAD's (tools/host_objcmp.sh, 32 and 64 bits), gpu2d.c and gpu2d_obj.c preprocessed
  without N3DS_DIRECT the same, pica_display.c only a stub more; the plain 3DS build: pica_display.c's sprites and
  windows add a 0 to their positions (the same values; its pictures not compared). Not checked: the wallet, the
  countdown, HudLinkIcon (their places), a named villager's talk, other screens' UI, the order of groups (a sprite moved onto lines of a
  later group of other GPU state would be drawn over by that group's backdrop: one group in town and in the text
  box's scenes), the speed on hardware (the same quads, so no GPU cost expected: `ACWW_FPS=1` to measure).
- 2026-11-02: the user's run on hardware: when saving, the touch has to be put a bit lower than "Save" shows to
  reach it (B2's touch mapping and the save menu's choices disagree). Written down only, not fixed (the user's wish).

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Animal Crossing: Wild World (DS, USA Rev 1) as a native program: the game's decompiled C/C++ (`src/`, `include/`)
compiled for the host, on top of a runtime that rebuilds the DS hardware in software (`modern/host/`). No emulator.
This branch (`3ds`, fork `origin` = toomium/acww-decomp) adds a native Nintendo 3DS build; `upstream`
(7ucina/acww-decomp) is the PC port. The matching (ROM-rebuilding) decompilation lives at the `matching` tag, not here.

Read first, depending on the task:
- `docs/3ds.md`: the 3DS plan, rules (section 0) and dated work log (section 6: current state and next steps).
- `docs/host.md`: how the runtime works, every `ACWW_*` option, and how changes are checked.
- `docs/portability.md`: the PC port's plan and log.

## Build

```
python3 tools/configure_host.py --cc gcc-14      # PC, x86-64 -> build_host64.ninja (GCC 13 crashes at New Game)
ninja -f build_host64.ninja                       # build/host64_usa/acww <rom.nds>
python3 tools/configure_host.py --os 3ds          # 3DS (devkitPro in /opt/devkitpro) -> build_host_3ds.ninja
ninja -f build_host_3ds.ninja                     # build/host3ds_usa/acww.3dsx
```

Other targets: `--arch i386` (needs `--sysroot`), `--arch aarch64`, `--os windows|macos`, `--oracle` (melonDS linked in
as a reference renderer). `python3 tools/host_fetch_wgpu.py` fetches wgpu-native for the PC GPU renderer.
The 3DS build's `.3dsx` relocations are checked by `tools/host_3dsx_check.py` (part of the ninja rule).

ROM on this machine: `~/Dokumente/Animal Crossing - Wild World (USA) (Rev 1).nds`.

## Checking changes

The rule: behaviour must not change. Runs are deterministic, so checks are byte-for-byte comparisons.
- `tools/host_oracle.sh <binary> quick|play|record`: screenshots and sound against reference captures
  (`ORACLE_DIR`, `ACWW_ROM` set as in docs/3ds.md T0.2).
- The save script: `tools/host_play_script.py save` emits `ACWW_KEYS` etc.; compare screenshots (`ACWW_SHOT`), save
  file and sound (`ACWW_WAV`) against a baseline. Delete the save file before each run. Headless runs:
  `ACWW_HEADLESS=1 ACWW_FAST=1 ACWW_EXIT_FRAME=<n>`.
- One-file rewrites: `tools/host_objcmp.sh OLD NEW DIR` compares the generated code per function.
- Where two builds diverge: `ACWW_RAMHASH`, `ACWW_RAMDUMP` (docs/host.md).
- 3DS: runs in Azahar (flatpak `org.azahar_emu.Azahar`); its SD card is
  `~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/3ds/acww/` (`acww.3dsx`, `game.nds`, `acww.cfg`,
  `log.txt`). Options come from `acww.cfg` (`NAME=value` lines), not the environment. `ACWW_FPS=1` for timings.
  GPU-thread exactness: `ACWW_PICA_HASH=1`, `ACWW_PICA_DELAY=5` vs `ACWW_PICA_THREAD=0`.
  Azahar ignores SIGTERM: close it with `flatpak kill org.azahar_emu.Azahar` (then `kill -9 $(pgrep -x azahar)`)
  and check `pgrep -x azahar` is empty. Azahar wrongly shows a black window and a missing shadow; hardware does not.

## Architecture

- **Game code** (`src/main`, `src/ov*` overlays, `src/autoload_2` = NitroSDK/NitroSystem/MSL libraries) is compiled
  freestanding with mwcc's type rules (`-DMODERN -DHOST`). All overlays are linked into one program; loading one
  resets its data/bss and runs its constructors (`modern/host/overlay.c`); calls through addresses several overlays
  share go through typed dispatchers (`modern/ds/overlays.c`, `tools/host_shared_calls.py`).
- **Hardware registers**: the game touches them only through `IO_R`/`IO_W`/... (`include/nitro/io_access.h`), which
  call `HostIo_Read`/`HostIo_Write` (`modern/host/hw.c`). Interrupts are delivered at safe points (register
  accesses, waits); time is virtual, so runs repeat exactly.
- **Threads**: the SDK's cooperative OS_Thread switches stacks with `coro_switch.S` (i386, x86-64, Win64, arm64, ARM32).
- **ARM7 side** (reached by PXI tag): `pxi7.c` (touch, RTC, NVRAM, save memory), `snd7/` (sound), `wm7.c` (wireless),
  `card.c` (ROM reads).
- **Renderers**: `gpu2d/` (2D engines, CPU; `gpu2d.wgsl` on WebGPU), `gpu3d/` (geometry engine + rasteriser; backends:
  CPU, `gpu3d_wgpu.c`, `gpu3d_pica.c` for the 3DS's PICA200). Screen compositing: `wgpu_display.c` (PC),
  `pica_display.c` (3DS).
- **Platform and frontend**: `plat.h` with `plat_posix.c` / `plat_win32.c` / `plat_3ds.c`; `frontend.h` with
  `frontend_sdl.c` (64-bit PC), `frontend_viewer.c` + `viewer/` (i386, separate process), `frontend_3ds.c`,
  `frontend_none.c`. Input (keys, touch) reaches the game through the shared `AcwwShm` struct (`frontend.h`).
- **Generated headers**: every source has `include/proto/<dir>/<file>.h` with its definitions' declarations
  (`tools/host_protos.py`; regenerate after changing a signature). `PROTO_LOCAL_<name>` marks a unit's own
  declaration that still mismatches.
- **3DS-only build differences** live in `tools/configure_host.py` (`N3DS`, `N3DS_NATIVE`: the files using libctru keep
  `-fshort-enums`; the rest uses the game's 32-bit enums).

## Rules (docs/3ds.md section 0)

- Shared changes (`src/`, `include/`, shared runtime files, `tools/`) go in their own commits, separate from 3DS-only
  files, each useful alone (upstream PR candidates), and must not change the PC builds' behaviour.
- The game sources are not changed for the 3DS; an ARM32 problem in them is a portability bug fixed for every build.
- Options stay `ACWW_*` environment variables (on the 3DS via `acww.cfg`); no second options system.
- 3DS API calls come from the installed headers (`/opt/devkitpro/libctru/include`) and `/opt/devkitpro/examples/3ds`,
  never from memory.
- Match the surrounding style: comments say what and why in plain sentences; C11 for the runtime.
- Record findings in docs/3ds.md's log with the date; don't add unchecked facts (mark them **unverified**).

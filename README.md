# Animal Crossing GC PS3 Port
<img width="3840" height="2013" alt="image" src="https://github.com/user-attachments/assets/2eb54180-8b64-43d9-ab90-5439c89437e0" />

Last update, late 09/05: Current Progress Status

Significant progress has been made.

Previously:
The game crashed very early during the second frame (JW_EndFrame).
This indicated a basic engine or render loop issue.

Now:
System initialization completes.
Audio initialization completes.
Initial menu setup completes.
Memory arena (48 MB) is allocated.
Filesystem and resources are loading.
The module /foresta.rel.szs is successfully loaded.
Execution proceeds into game logic.
new 09/05: Dualshock 3 gamepad working well, now main character partial rendering. 
update 03.05:
In private, logos try to show and games shows real performance but with corrupted graphics:
<img width="3837" height="1960" alt="{5D20EBC0-4490-4D82-9F4B-30FFC08F64EF}" src="https://github.com/user-attachments/assets/39aa5e2f-bf7b-4872-a210-2404ddb92b5b" />


Current Failure Point

The crash now occurs after loading the REL module (foresta.rel.szs), during execution of module-related code.

This indicates:
The core engine is now running correctly.
The failure is happening in dynamic module handling, not base systems.
New update 01/05/2026 the game dead in the second frame, but is booting on ps3 real hardware and in the RPCS3 EMU. 
Work-in-progress PS3 homebrew port of Animal Crossing (GameCube), based on the
existing decompilation and native port work.

This repository is structured as a porting workspace:

- `src/` and `include/` contain the decompiled game code inherited from the
  base project.
- `pc/` is kept as a working native-port reference for platform replacements.
- `ps3/` is the new PS3 platform layer and packaging workspace.

No commercial game assets, disc images or extracted copyrighted files belong in
this repository. A legally owned copy of the game is required at runtime once
disc loading is implemented.

## Current Status

The PS3 side contains a bootstrap `.pkg` target plus an experimental full-game
CMake target. The bootstrap is meant to validate:

- PSL1GHT toolchain detection.
- PPU ELF build.
- EBOOT/SELF/pkg pipeline, depending on the package tools installed with the
  SDK.

The following game-facing replacements now exist under `ps3/src/`: OS/timing,
VI frame pacing, PAD facade, DVD/disc-image loading, CARD/GCI save handling,
ARAM, matrix/math, first-pass PSL1GHT audio, first-pass RSX display setup with
software GX primitive rasterization, and misc Dolphin SDK stubs.

It does not run the full game yet. The current GX backend is intentionally
limited: it can initialize RSX display buffers, draw simple direct GX
primitives into the framebuffer, and sample a few basic linear texture formats,
but it does not implement real TEV, GameCube tiled/CMPR texture decoding,
display-list decoding, depth, or a real RSX GPU pipeline. The audio and
controller layers are wired to PSL1GHT APIs but still need hardware
verification against an installed SDK and a real console.

## Build PS3 Bootstrap

Install PSL1GHT and make sure the environment exposes:

- `PSL1GHT`
- `ppu-gcc` or `powerpc64-ps3-elf-gcc`
- `make`
- the PSL1GHT package/self tools

Then run one of:

```sh
./build_ps3.sh
```

```powershell
.\build_ps3.ps1
```

The output location depends on PSL1GHT's `ppu_rules`, normally under
`ps3/build/`.

To try the experimental full-game CMake build instead of the bootstrap:

```powershell
.\build_ps3.ps1 -FullGame
```

```sh
ACGC_PS3_FULL_GAME=1 ./build_ps3.sh
```

## Porting Plan

Read [ps3/README.md](ps3/README.md) and [ps3/PORTING.md](ps3/PORTING.md).

The remaining practical order is:

1. Prove the bootstrap package launches on a PS3.
2. Verify `ps3_disc.c`, `ps3_dvd.c`, `ps3_card.c`, `ps3_pad.c` and
   `ps3_audio.c` on hardware.
3. Replace the software GX raster fallback with a real RSX texture/TEV/depth
   renderer.
4. Iterate on the experimental full-game CMake target until it compiles and
   links with the installed SDK.
5. Add a final full-game `.pkg` packaging target once the EBOOT is stable.
   <img width="1534" height="503" alt="image" src="https://github.com/user-attachments/assets/52d3aa00-f45c-4c61-ad92-ff9a9d28874e" />


## PC Reference Build

The copied `pc/` directory remains useful as a reference for which Dolphin SDK
systems were replaced. It is not the PS3 target.

# PS3 Porting Notes

The PC port already proves the game can run outside GameCube hardware by
replacing Dolphin SDK systems with platform code. The PS3 port should follow
the same boundary, not rewrite gameplay code first.

## Replacement Map

| Current PC file | PS3 replacement | Purpose |
| --- | --- | --- |
| `pc/src/pc_main.c` | `ps3/src/ps3_main.c` | Entry point, boot sequence, crash/log setup |
| `pc/src/pc_gx*.c` | `ps3/src/ps3_gx*.c` | GX-to-renderer translation, textures, TEV |
| `pc/src/pc_vi.c` | `ps3/src/ps3_vi.c` | Video timing and frame presentation |
| `pc/src/pc_pad.c` | `ps3/src/ps3_pad.c` | GameCube PAD emulation from PS3 controllers |
| `pc/src/pc_dvd.c` and `pc/src/pc_disc.c` | `ps3/src/ps3_dvd.c` and `ps3/src/ps3_disc.c` | Disc image/filesystem access |
| `pc/src/pc_card.c` and `pc/src/pc_m_card.c` | `ps3/src/ps3_card.c` and `ps3/src/ps3_m_card.c` | Save-card emulation |
| `pc/src/pc_os.c` | `ps3/src/ps3_os.c` | Time, threads, messages, memory, interrupts stubs |
| `pc/src/pc_audio.c` | `ps3/src/ps3_audio.c` | Audio queue/output |
| `pc/src/pc_aram.c` | `ps3/src/ps3_aram.c` | ARAM emulation buffer |
| `pc/src/pc_mtx.c` | `ps3/src/ps3_mtx.c` | Matrix/math compatibility |

## Practical Order

1. Keep the bootstrap compiling and packaging.
2. Verify `ps3_pad.c` on hardware. The mapping now uses PSL1GHT `io/pad.h`
   fields directly and keeps cached analog data because `ioPadGetData` only
   updates when new input is available.
3. Verify `ps3_disc.c`/`ps3_dvd.c` with a user-provided disc image under
   `USRDIR/rom` or `/dev_usb00x/ACGC/rom`.
4. Verify `ps3_card.c`/`ps3_m_card.c` save and load GCI data under
   `USRDIR/save/card_a` and `USRDIR/save/card_b`.
5. Verify `ps3_audio.c` on hardware. It now uses PSL1GHT libaudio, a stereo
   S16 ring buffer and float output blocks.
6. Continue the GX port. `ps3_gx.c` initializes RSX display buffers, draws
   simple direct primitives and samples a few basic linear texture formats with
   a software raster fallback, but it is not a complete TEV/texture/depth
   renderer.

## Build Guard

`ACGC_PS3_BOOTSTRAP_ONLY` remains `ON` by default. Turn it off to try the
experimental full-game source filter and platform target:

```sh
cmake -S ps3 -B ps3/build-full -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-psl1ght.cmake \
  -DACGC_PS3_BOOTSTRAP_ONLY=OFF
ninja -C ps3/build-full
```

## Current Gaps

- `ps3_gx.c` is only a first pass. It does not decode display lists, handle
  GameCube tiled/CMPR texture layouts correctly, implement TEV, implement depth,
  or submit a real RSX GPU primitive pipeline yet.
- `ps3_audio.c` needs timing verification on real hardware; the current pump is
  frame-driven and may need to consume audio notify events more aggressively.
- `ps3_pad.c` uses the confirmed PSL1GHT field names, but button feel, analog
  ranges and rumble still need real controller tests.
- The full-game CMake target is wired but unverified. Expect compile/link
  errors until the remaining Dolphin SDK and game-specific edges are worked
  through with an installed PS3 toolchain.
- The full-game `.pkg` path is not final. The bootstrap Makefile can package,
  while the CMake path currently stages files and can create a debug EBOOT when
  `fself`/`make_fself` is present.

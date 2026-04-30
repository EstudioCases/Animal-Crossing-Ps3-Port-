# Animal Crossing PS3 Port

This directory is the PS3 platform layer for the Animal Crossing GameCube
decompilation/PC port codebase.

The first milestone remains deliberately small: build a PS3 homebrew bootstrap
that can be packaged as a `.pkg`. The `ps3/src` directory now also contains the
first pass of the platform replacement layer for pads, filesystem/disc images,
saves, timing, OS stubs, ARAM, matrix helpers, PSL1GHT audio and an initial
GX/RSX backend.

No commercial game assets are stored here. Runtime disc images or extracted
assets must stay outside git and should come from a legally owned copy.

## Toolchain

Use the open-source PSL1GHT PS3 homebrew SDK. The expected environment is:

- `PSL1GHT` points to the PSL1GHT SDK directory.
- `ppu-gcc` or `powerpc64-ps3-elf-gcc` is available in `PATH`.
- Package/self tools from the same SDK are available in `PATH`.
- `cmake` and `ninja` are installed.

On Windows, run this from a shell where the PS3 toolchain is available. MSYS2,
WSL or a dedicated PSL1GHT shell are all reasonable options.

## Build Bootstrap

From the repository root:

```sh
./build_ps3.sh
```

Or directly:

```sh
cmake -S ps3 -B ps3/build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-psl1ght.cmake \
  -DACGC_PS3_BOOTSTRAP_ONLY=ON
ninja -C ps3/build
```

The bootstrap target proves the PS3 SDK, ELF/EBOOT and package pipeline before
the game layer is wired in.

The `ps3/Makefile` intentionally builds only `ps3/bootstrap`. The experimental
full-game target is CMake-based:

```sh
cmake -S ps3 -B ps3/build-full -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/Toolchain-psl1ght.cmake \
  -DACGC_PS3_BOOTSTRAP_ONLY=OFF
ninja -C ps3/build-full
```

The root build scripts expose the same path through `.\build_ps3.ps1 -FullGame`
or `ACGC_PS3_FULL_GAME=1 ./build_ps3.sh`.

## Port Milestones

1. Bootstrap package launches on PS3.
2. Input layer maps PS3 pads to GameCube PAD state.
3. Filesystem layer loads a user-provided disc image from the package data
   directory or external storage.
4. Save layer stores GCI-compatible data under the PS3 save/data path.
5. VI/timing layer runs the game loop at the expected cadence.
6. Audio layer outputs through PSL1GHT libaudio.
7. GX layer translates GameCube GX calls to PS3 RSX rendering.
8. Full game build compiles, links, produces EBOOT.BIN and is packaged.

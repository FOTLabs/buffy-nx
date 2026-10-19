# Building

## Dependencies

- Python 3.10+ with `capstone`, CMake 3.20+, Ninja and a C11 compiler.
- Linux/WSL: SDL2, Vulkan, glslang and OpenGL/EGL development packages. On Ubuntu:
  `build-essential cmake ninja-build python3-venv libsdl2-dev libvulkan-dev glslang-dev libgl-dev libegl-dev`.
  Install `capstone` in a virtual environment.
- Switch: devkitPro `switch-dev` and `switch-sdl2`, an NVK build from [mesa-switch](https://github.com/danfromtico/mesa-switch), and a Switch build of glslang.

Linux/WSL is the verified development path. A Windows MSVC/D3D11 configuration is kept but has not been revalidated.

## Prepare your game

```sh
python3 tools/prepare_game.py /path/to/your/Buffy-USA.iso
bash tools/regen.sh
```

You can also place the extracted disc in `game/` (with `Default.xbe` at its root). The supported executable hash is in `installer/game.json`; other revisions need fresh analysis, not a bypassed check. Generated code goes to `app/generated/` and is ignored by Git. Allow at least 10 GiB of free space for a build.

## Linux build

```sh
bash tools/build.sh linux
GAME_GAME_DIR="$PWD/game" GAME_SAVE_DIR="$PWD/save" ./build/linux/app/buffy_recomp
```

A working Vulkan driver is required.

## Switch build

```sh
export DEVKITPRO=/opt/devkitpro
export NVK_SDK=/path/to/mesa-switch/install/opt/devkitpro/portlibs/switch
export GLSLANG_NX=/path/to/glslang-switch/install
BUILD_DIR=/path/to/buffy-switch bash tools/build.sh switch
```

The NRO is written to `$BUILD_DIR/app/buffy_recomp.nro`. Select it in the installer, or copy it to `/switch/buffyx/` yourself.

## Tests

```sh
bash tools/test.sh
```

Installer tests need Node.js 20+ (`NODE=/path/to/node` overrides it). Guest math tests need a locally generated game build. Toolkit tests live in `sdk/tests`.

## Releases

`tools/package_release.py` packages a release NRO with its checksum, licence notices and the LGPL relink bundle, and checks that the relink reproduces the same NRO.

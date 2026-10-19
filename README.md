# Buffy the Vampire Slayer for Nintendo Switch

An unofficial native Nintendo Switch port of *Buffy the Vampire Slayer* (2002, original Xbox), built with [XboxRecomp](https://github.com/sp00nznet/xboxrecomp) static recompilation and the NVK Vulkan driver.

It runs at 960×720 with 2× MSAA, targeting the original 30 FPS. Movies, audio, saves, a Handheld Visibility mode and HD Rumble are supported. Performance can still vary by scene.

> [!IMPORTANT]
> You need your own legally obtained copy of the **USA** Xbox release. This repository contains no game files.

## How to install

1. Open the installer at **https://fotlabs.github.io/buffy-nx/** in desktop Chrome or Edge.
2. Select your extracted game folder (containing `Default.xbe`) or your Xbox ISO. The installer checks the game revision, fetches the release NRO, verifies its checksum and builds `buffy-nx.zip` in your browser. Your game files are never uploaded.
3. Extract the ZIP to the root of your SD card.
4. Launch `buffy_recomp.nro` from the Homebrew Menu using title takeover (hold R while opening a game). Applet mode from the Album does not have enough memory.

```text
/switch/buffyx/
  buffy_recomp.nro
  LICENSES.txt
  buffyx_env.txt      optional settings
  game/               your extracted disc (Default.xbe, dvddata/...)
  save/               created on first run
```

## Controls

Each Switch button maps to the Xbox button with the same letter.

| Switch | Xbox |
| --- | --- |
| A / B / X / Y | A / B / X / Y |
| L / R | White / Black |
| ZL / ZR | Left / Right trigger |
| + / − | Start / Back |
| Left / Right stick | Left / Right stick |

Set `RECOMP_PAD_LAYOUT=position` in `buffyx_env.txt` to match the Xbox face-button positions instead of letters.

**Handheld Visibility** brightens dark scenes for the smaller screen. Press **Y** in the pause menu to switch between Handheld and Original. The choice is saved.

## Settings

Optional settings go in `/switch/buffyx/buffyx_env.txt`, one `KEY=VALUE` per line (`#` starts a comment).

| Setting | Effect |
| --- | --- |
| `GAME_SKIP_MOVIES=1` | Skip the intro movies |
| `GAME_VISIBILITY=original` | Start with Handheld Visibility off |
| `GAME_RUMBLE_STRENGTH=0.5` | Rumble strength from `0` (off) to `1` |
| `GAME_CPU_MHZ`, `GAME_GPU_MHZ`, `GAME_MEM_MHZ` | Opt-in clock overrides |

> [!CAUTION]
> Clock overrides are off by default. Raising them increases heat and battery drain; use them at your own risk.

## Build

To build the NRO yourself you need devkitPro (devkitA64, libnx, Switch SDL2), an NVK build from [mesa-switch](https://github.com/danfromtico/mesa-switch), a Switch glslang build, Python 3.10+ and your own copy of the game. See [docs/building.md](docs/building.md).

| Folder | Contents |
| --- | --- |
| `app/` | Game launcher, title fixes and Switch platform code |
| `sdk/` | Vendored XboxRecomp runtime and code generator |
| `tools/` | Game preparation, generation, build, test and release scripts |
| `installer/` | Browser installer (runs locally, uploads nothing) |
| `tests/` | Host and guest regression tests |
| `docs/` | Build guide, legal notes and licence texts |

## Credits

- **The Collective and Electronic Arts**, and everyone who made the original game.
- [**sp00nznet and XboxRecomp contributors**](https://github.com/sp00nznet/xboxrecomp): the static recompilation toolkit and runtime.
- [**xemu contributors**](https://github.com/xemu-project/xemu), including espes, Jannik Vogel and Matt Borgerson: MCPX APU and NV2A definitions.
- [**danfromtico**](https://github.com/danfromtico/mesa-switch) and [**NaGaa95**](https://github.com/NaGaa95/mesa-switch): Mesa/NVK on Switch.
- [**devkitPro**](https://devkitpro.org/) and [**switchbrew/libnx**](https://github.com/switchbrew/libnx): the Switch toolchain and libraries.
- [**StevensND/nfsmw-nx**](https://github.com/StevensND/nfsmw-nx): inspiration for the browser installer.

## Legal

This is an unofficial fan project with no affiliation with or endorsement from the Buffy the Vampire Slayer rights holders, Electronic Arts, Microsoft or Nintendo. All trademarks and characters belong to their respective owners.

No game assets, executables or generated game code are included. You must provide your own legally obtained copy; the installer prepares it locally. Running homebrew requires custom firmware, which can breach Nintendo's terms of service and may lead to a ban.

Project code is released under the [MIT License](LICENSE). The vendored XboxRecomp toolkit is MIT, and the xemu-derived files in `sdk/` remain LGPL-2.1-or-later. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [docs/legal.md](docs/legal.md). None of these licences grant rights to the original game.

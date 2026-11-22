# fCavEX · Two-Wii multiplayer

Two players share a block world. The host Wii simulates the world and owns the save. The second Wii joins as a peer. Each player uses a local controller, camera and renderer. The game sends state over LAN. Showing both video feeds on one TV requires a separate two-input compositor.

[Editable system architecture](docs/fcavex-system-architecture.drawio) · [Editable test design](docs/fcavex-test-design.drawio) · [Wii controls](controls.md)

## What the LAN game supports

The two-player mode streams existing terrain within two columns of each player. Each Wii draws one view and the other player’s avatar. Both players can move, choose a hotbar slot, mine and place stone, cobblestone, dirt, grass, planks, sand and gravel. Mining credits the acting player’s inventory. The host saves both players and the shared terrain. A returning peer receives a fresh snapshot.

Use offline mode for crafting, container interactions, combat and the full inherited entity behavior. The LAN peer does not receive a complete simulation of mobs or minecarts. The two-player build uses one fixed peer slot, trusted private IPv4 and prebuilt region files. Windows Dolphin is the tested configuration. Physical Wii model, SD, controller, LAN and display-chain acceptance are tracked separately. [Hardware acceptance](docs/hardware-acceptance.md)

## Build the Wii game on Windows

Use PowerShell 7, Windows `tar.exe`, a native C compiler and a Wii toolchain. The lab SDK contains `devkitPPC/bin/powerpc-eabi-gcc.exe` (r35) and `libogc/{include,lib/wii}` (1.8.19). The build script fetches the pinned dependencies, checks each archive’s SHA-256 and builds both the game and the ELF-to-DOL converter.

The tested Windows SDK used the archives below. Extract them so that `devkitPPC/bin` and `libogc/include` share the SDK parent shown above. Check the archive hashes before building. Compatibility with newer SDKs is unverified.

| Archive | SHA-256 |
|---|---|
| `devkitPPC_r35-win64.tar.xz` | `28b1e59ea4b617bd569631f832d4e4eb2a697c884ffe44ab7430d6d9e99f1798` |
| `libogc-1.8.19.tar.bz2` | `e0c4680e6639b203148c2ce6a165c260c50cba600e63ed4b091e186067373de4` |

In the repository root, set `FCAVEX_SDK` to the SDK directory and `CC` to the native compiler executable. Then run

```powershell
.\tools\windows\Build-Wii.ps1 -SdkRoot $env:FCAVEX_SDK -HostCompiler $env:CC
.\tests\run.ps1 -Compiler $env:CC
```

After a successful build, the script prints the new hashes and writes `boot.dol`, `boot.elf` and `boot.map` under `build/claw/`. Its GCC settings use GNU99, `-O2` and section garbage collection. Historical release binaries used a different build configuration. Native tests reuse the staged source tree. To reuse another build's dependencies, pass its `build/claw/staging/source` directory to the test runner with `-IncludeDir`. This directory contains the game headers and `cglm/`, `cNBT/` and `m-lib/`.

On claw the native compiler is Clang 23.1.6. To reproduce it, unpack `llvm-mingw-20261117-ucrt-x86_64.zip` and set `CC` to its `bin/clang.exe`. The release SHA-256 is `4d002e330a0f703625aa5ccbc37e96f8f824413e163bfc7336774aabd2c3a412`. [Compiler release](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261117)

## Configure a two-player session

Each console needs its own `sd:/apps/fcavex/` directory containing `boot.dol`, `config.json` and permitted `assets/`. For Wii controls, start with `config_wii.json` and copy it as `config.json`. The host also needs a compatible prebuilt world in `saves/`. Start with a disposable copy. The inherited game uses custom chest and sign persistence. The fixture tool generates a small world, test art and complete example configurations. [Fixture instructions](tools/multiplayer_fixture/README.md)

Set the fields below inside a top-level `network` object in `config.json`, preserving the other settings. Start the host and load its world, then start the peer. Omitting the `network` object selects offline play.

| Setting | Host | Peer |
|---|---|---|
| `role` | `"host"` | `"peer"` |
| `bind` | Host Wii’s private IPv4 address | Peer Wii’s own private IPv4 address |
| `host` | Default | Same private IPv4 address as the host |
| `port` | `"45838"` | `"45838"` |
| `world` | Optional relative save path, e.g. `"saves/Fixture"` | Omit |

The supplied same-PC Dolphin fixture uses `127.0.0.1` on both guests. On physical Wiis, set both local bind addresses and set B’s `host` to A’s LAN address. Set `"trace": "on"` inside `network` to record test events. [Hardware acceptance and evidence](docs/hardware-acceptance.md)

## Exercise both guests

The native suite checks launch choices, authority behavior and on-wire messages. The two-guest test runs the built Wii executable in separate Dolphin profiles on Windows. Install the fixture tool’s Python requirements. Set `DOLPHIN` to the emulator executable, `FCAVEX_CARDS` to a fresh fixture directory and `FCAVEX_RUNS` to a separate directory for disposable runs.

```powershell
python -m unittest discover -s tools/multiplayer_fixture -p test_fixture.py -v
python tools/multiplayer_fixture/make_fixture.py build $env:FCAVEX_CARDS
.\tests\e2e\run.ps1 -Game .\build\claw\boot.elf -Dolphin $env:DOLPHIN -HostCard "$env:FCAVEX_CARDS\A.raw" -PeerCard "$env:FCAVEX_CARDS\B.raw" -OutputRoot $env:FCAVEX_RUNS -Cold
```

`-Cold` checks the saved game after both guests restart. For human-controlled play, replace `-Cold` with `-Play`. A moves with WASD and B with the arrow keys. F11 toggles the two-view projector between windowed and fullscreen. Escape closes the projector and both guests. [Dolphin setup, controls and replay instructions](tests/e2e/README.md)

Each run has a new directory under `FCAVEX_RUNS`. Open `run.json` for build hashes and the outcome, and `gameplay.json` for the gameplay checks when both guests produced logs. To check the logs independently, run `node tests/e2e/verify-game.cjs <host-log> <peer-log>`. It checks for distinct slots, movement observed across the connection in each direction, and an accepted edit from each player applied to both views. Physical Wii memory, capture latency, audio and save recovery have separate acceptance checks in the test diagram.

To exercise delayed and stalled application sends over loopback, run the [five-profile TCP test](tests/wire/profiles/README.md). It reports per-probe delivery times, ordering and checksums.

## Contribute

Game code lives in `source/`. Look in `source/network/` for transport, world authority and persistence, `source/game/` for launch behavior and UI, and `source/platform/` for hardware interfaces. The `tests/`, `tools/` and `docs/` directories hold regression tests, build and fixture tools, and design diagrams.

Add a failing regression case before changing behavior. Run the native suite for protocol or authority changes. Rebuild and exercise both guests for changes affecting gameplay. Include the revision, test inputs and first failing check when reporting a bug. Keep generated executables, packed SD cards and private saves outside commits.

## Credits and licensing

xtreme8000 created Cave Explorer. yyy257 created fCavEX, jilleb extended it, and this work builds on Tristen’s fork at `7023e0e`. The baseline includes farming, redstone, particles and other experimental features. [Original CavEX](https://github.com/xtreme8000/CavEX) · [yyy257/fCavEX](https://github.com/yyy257/fCavEX) · [jilleb/fCavEX](https://github.com/jilleb/fCavEX) · [Tristen’s pinned fork](https://github.com/Tpool1/fCavEX/tree/7023e0e025ac5ad45e389755192e84392ee1b040) · [Earlier changes](CHANGES.md)

CavEX source is licensed under GPL-3.0-or-later. Asset licenses vary by file. The inherited credits list the controls icon license as unknown. The fixture uses generated artwork and the credited bitmap font. [Source license](LICENSE) · [Asset credits](assets/README.md)

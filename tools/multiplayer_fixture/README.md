# Disposable Wii multiplayer fixtures

From the repository root, run these commands with Python 3.12 in a fresh virtual environment. On claw the fixture tools ran with Python 3.12.14, Pillow 13.0.0, NumPy 2.3.5, nbtlib 2.0.4 and pyfatfs 1.1.0.

```sh
python -m pip install -r tools/multiplayer_fixture/requirements.txt
python -m unittest discover -s tools/multiplayer_fixture -p test_fixture.py -v
python tools/multiplayer_fixture/make_fixture.py build ../multiplayer-lab/fixtures/new-run
```

The destination directory must be new. The generator stages the cards together, then publishes them to that directory. Each image is a separate writable 64 MiB FAT16 superfloppy. Image timestamps can vary between builds. Keep generated media outside the repository.

| Image | Role | Initial saved world |
| --- | --- | --- |
| A.raw | Host | Fixture |
| B.raw | Peer | Empty saves directory |
| offline.raw | Offline control | Separate copy of Fixture |

Copy each image into its own **closed disposable Dolphin profile** as `Load/WiiSD.raw`. Select an inserted, writable SD card, keep folder synchronization off, and boot the game DOL using that profile. The DOL is supplied separately by the Wii build. All three cards place `config.json`, assets and saves in `sd:/apps/fcavex`. The network section sets `role` according to the table, `host` and `bind` to `127.0.0.1`, `port` to the string `45838`, `world` to `saves/Fixture` and `trace` to `on`. These addresses support two Dolphin processes on one computer.

The fixture has 121 prebuilt 16 × 128 × 16 columns across four McRegion files. The host starts at eye position (8.5, 65.62, 8.5), facing +Z. The floor is solid through Y=63. Stone markers at (8, 65, 11) and (10, 65, 11) sit on stone bases. Both cells immediately in front at Z=10 start as air. The hotbar begins with 32 cobblestone in slot 0 and 32 planks in slot 1. A striped floor and three different skyline arches make camera motion recognizable. The finite world extends from X/Z −80 through 95.

After a clean shutdown, inspect the exact working copy previously used by the host.

```sh
python tools/multiplayer_fixture/make_fixture.py inspect path/to/closed/ProfileA/Load/WiiSD.raw
```

The inspector opens the image read-only and reports the four marker cells, the legacy `level.dat` player position/inventory and the world’s file list. Cross-check additional multiplayer player records using the game’s save/rejoin telemetry. Copy the same persisted card forward when testing a cold restart.

## Measurement records

The generated cards explicitly set `network.metrics` to `"on"`. Each boot chooses an unused `metrics00` through `metrics99` directory in the app folder. After the emulator releases the card or after a clean SD unmount, read the rows from that specific console and boot.

```sh
python tools/multiplayer_fixture/make_fixture.py metrics path/to/closed/ProfileA/Load/WiiSD.raw
```

`frame.csv` reports game-loop start intervals in milliseconds and successful Wii pre-retrace framebuffer selections in retraces. `tick.csv` reports full server-update work separately from start-to-start tick spacing, including the sleep in the latter. The peer runs no authority tick. Timing uses nonoverlapping windows of 100 samples, nearest-rank p95/p99, maximum and number strictly over the listed limit. The first incomplete window has no row. Timing and SD writes include instrumentation overhead. Each series stops after 10,000 windows.

`memory.csv` samples libogc's unreserved MEM1 and MEM2 arenas every game frame and records each window's minima in bytes. Newlib aggregate reserved, in-use and reusable bytes are snapshots at window boundaries. XFB and GX FIFO payloads are grouped by their actual pointer bank. Newlib can grow across both banks. Retain the allocator's numbers separately from arena headroom. Largest reusable block, per-allocation peaks between samples and physical screen latency require additional observations. A nonzero VI drop counter means the interrupt ring overflowed and timing observations are incomplete. [libogc 1.8.19 heap growth](https://github.com/devkitPro/libogc/blob/v1.8.19/libogc/sbrk.c#L10-L66)

## Asset provenance

The generator draws all terrain, models, controls, environment and UI sprites from original geometric patterns. It reads the explicitly licensed 8×8 font from `assets/default.png` and includes attribution on each card in `sd:/apps/fcavex/CREDITS.txt`. The font is by xtreme8000 under CC BY 4.0. The game’s runtime texture manifest supplies the necessary filenames. [Font attribution](../../assets/README.md) · [Font license](https://creativecommons.org/licenses/by/4.0/)


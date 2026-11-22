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

The fixture has 121 prebuilt 16 × 128 × 16 columns across four McRegion files. The host starts at eye position (8.5, 65.62, 8.5), facing +Z. The host starts at full health on this fork (160 units). The floor is solid through Y=63. Stone markers at (8, 65, 11) and (10, 65, 11) sit on stone bases. Both cells immediately in front at Z=10 start as air. The hotbar begins with 32 cobblestone in slot 0 and 32 planks in slot 1. A striped floor and three different skyline arches make camera motion recognizable. The finite world extends from X/Z −80 through 95.

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

`frame.csv` reports game-loop start intervals in milliseconds and successful Wii pre-retrace framebuffer selections in retraces. `tick.csv` reports full server-update work separately from start-to-start tick spacing, including the sleep in the latter. The peer runs no authority tick. The separate `world_scan_ms` series times only calls to the authority world scan. Timing uses nonoverlapping windows of 100 samples, nearest-rank p95/p99, maximum and number strictly over the listed limit. Tick rows carry the bitwise union of phases seen in that window: 1 means idle or paused, 2 means loading the initial view or missing columns, 4 means playable solo, and 8 means playable paired. Use rows with exactly 8 for steady paired gameplay. A mixed mask covers a transition. The first incomplete window has no row. Timing and SD writes include instrumentation overhead. Each series stops after 10,000 windows.

`memory.csv` samples libogc's unreserved MEM1 and MEM2 arenas every game frame and records each window's minima in bytes. Newlib aggregate reserved, in-use and reusable bytes are snapshots at window boundaries. XFB and GX FIFO payloads are grouped by their actual pointer bank. Newlib can grow across both banks. When it spans their address gap, its raw accounting can exceed total physical RAM. A zero in `newlib_plausible` flags these unusable allocator readings. Retain the allocator's numbers separately from arena headroom. Largest reusable block, per-allocation peaks between samples and physical screen latency require additional observations. A nonzero VI drop counter means the interrupt ring overflowed and timing observations are incomplete. [libogc 1.8.19 heap growth](https://github.com/devkitPro/libogc/blob/v1.8.19/libogc/sbrk.c#L10-L66)

`alloc.csv` records per-bank live and peak payload bytes for client chunks, mesh, network, archive and authority planes. The aggregate row counts only instrumented allocations. Check failure and dropped-entry counters before interpreting a run; any drop makes totals a lower bound. The enabled tracker itself requires 49,392 bytes on the 32-bit build, plus allocator and mutex overhead.

The region writer preserves unrelated NBT in an existing column, including supported entity, tile entity and custom tags. It reads one column at a time and accepts up to 2 MiB of inflated NBT per column. A larger or unreadable target is rejected before its record is replaced. New region payloads close before publication of their indexes. On two-FAT test images, libfat updates the active FAT16 table; a desktop reader may report stale copies in the inactive mirror. Compare the selected table and a genuine cold re-open. [Pinned libfat FAT16 write path](https://github.com/devkitPro/libfat/blob/2f6465447fa0dc7531747ac44b0f9bbd876c13c4/source/file_allocation_table.c#L176-L185)

## Asset provenance

The generator draws all terrain, models, controls, environment and UI sprites from original geometric patterns. It reads the explicitly licensed 8×8 font from `assets/default.png` and includes attribution on each card in `sd:/apps/fcavex/CREDITS.txt`. The font is by xtreme8000 under CC BY 4.0. The game’s runtime texture manifest supplies the necessary filenames. [Font attribution](../../assets/README.md) · [Font license](https://creativecommons.org/licenses/by/4.0/)


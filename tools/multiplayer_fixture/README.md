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

## Asset provenance

The generator draws all terrain, models, controls, environment and UI sprites from original geometric patterns. It reads the explicitly licensed 8×8 font from `assets/default.png` and includes attribution on each card in `sd:/apps/fcavex/CREDITS.txt`. The font is by xtreme8000 under CC BY 4.0. The game’s runtime texture manifest supplies the necessary filenames. [Font attribution](../../assets/README.md) · [Font license](https://creativecommons.org/licenses/by/4.0/)


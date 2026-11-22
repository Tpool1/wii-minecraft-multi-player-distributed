"""Black-box checks on the two independent Wii SD volumes. Run with unittest."""
import gzip
import io
import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

import nbtlib
from PIL import Image
from pyfatfs.PyFatFS import PyFatFS

from make_fixture import build

APP = "/apps/fcavex"


class FixtureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.output = Path(cls.temp.name) / "generated"
        build(cls.output)

    def test_roles_and_isolation(self):
        with PyFatFS(str(self.output / "A.raw")) as a, PyFatFS(str(self.output / "B.raw")) as b:
            for card, role in [(a, "host"), (b, "peer")]:
                cfg = json.loads(card.readbytes(APP + "/config.json"))
                self.assertEqual(cfg["network"], {"role": role, "world": "saves/Fixture",
                                 "bind": "127.0.0.1", "host": "127.0.0.1", "port": "45838", "trace": "on",
                                 "metrics": "on", "probes": [8,65,10,8,65,11,10,65,10,10,65,11]})
                self.assertTrue(cfg["input"]["item_action_left"])
                for name in ("terrain.png", "particles.png", "default.png", "gui_2.png",
                             "entity/minecart.png", "misc/shadow.png", "armor/diamond_2.png"):
                    with Image.open(io.BytesIO(card.readbytes(APP + "/assets/" + name))) as im:
                        im.verify()
                for name in ("terrain.png", "particles.png"):
                    with Image.open(io.BytesIO(card.readbytes(APP + "/assets/" + name))) as im:
                        self.assertEqual(im.size, (256, 256))
            self.assertFalse(b.exists(APP + "/saves/Fixture/level.dat"))
            a.writebytes(APP + "/write-test.txt", b"only on A")
            self.assertFalse(b.exists(APP + "/write-test.txt"))

    def test_spawn_inventory_and_region(self):
        with PyFatFS(str(self.output / "A.raw"), read_only=True) as a:
            root = APP + "/saves/Fixture/"
            level = nbtlib.File.parse(io.BytesIO(gzip.decompress(a.readbytes(root + "level.dat"))))["Data"]
            self.assertEqual(int(level["Player"]["Health"]), 160)
            self.assertEqual(list(level["Player"]["Pos"]), [8.5, 65.62, 8.5])
            self.assertEqual([int(i["Count"]) for i in level["Player"]["Inventory"]], [32, 32])
            for cx, cz in [(0, 0), (-5, -5), (5, 5), (-1, 1)]:
                region = a.readbytes(root + f"region/r.{cx//32}.{cz//32}.mcr")
                slot = (cx % 32 + 32 * (cz % 32)) * 4
                address = int.from_bytes(region[slot:slot+4], "big")
                start, sectors = (address >> 8) * 4096, address & 255
                self.assertGreaterEqual(start, 8192)
                length = struct.unpack_from(">I", region, start)[0]
                self.assertLessEqual(length + 4, sectors * 4096)
                self.assertEqual(region[start+4], 2)
                raw = zlib.decompress(region[start+5:start+4+length])
                chunk = nbtlib.File.parse(io.BytesIO(raw))["Level"]
                self.assertEqual((int(chunk["xPos"]), int(chunk["zPos"])), (cx, cz))
                for field, expected in (("Blocks", 32768), ("Data", 16384), ("SkyLight", 16384),
                                         ("BlockLight", 16384), ("HeightMap", 256)):
                    self.assertEqual(len(chunk[field]), expected)
                if (cx, cz) == (0, 0):
                    blocks = chunk["Blocks"]
                    for x in (8, 10):
                        idx = (x * 16 + 11) * 128
                        self.assertEqual((blocks[idx+64], blocks[idx+65], blocks[idx+66]), (1, 1, 0))
                        self.assertEqual(blocks[(x * 16 + 10) * 128 + 65], 0)

    def test_no_overwrite(self):
        with self.assertRaises(FileExistsError):
            build(self.output)


if __name__ == "__main__":
    unittest.main()


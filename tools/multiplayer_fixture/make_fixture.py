"""Build disposable, loopback-only Wii SD cards. All generated art is original."""
import argparse
import gzip
import io
import json
import re
import struct
import tempfile
import zlib
from pathlib import Path

import nbtlib as n
import numpy as np
from PIL import Image, ImageDraw
from pyfatfs.PyFat import PyFat
from pyfatfs.PyFatFS import PyFatFS

REPO = Path(__file__).resolve().parents[2]
APP = "/apps/fcavex"
WORLD = APP + "/saves/Fixture"
FONT_NOTICE = (
    "default.png: 8x8 tiled bitmap font by xtreme8000. Unmodified. "
    "CC BY 4.0 https://creativecommons.org/licenses/by/4.0/\n"
    "https://github.com/Tpool1/fCavEX/blob/7023e0e025ac5ad45e389755192e84392ee1b040/assets/README.md\n"
    "Other PNGs and world: original procedural fCavEX multiplayer test fixture.\n"
)


def packed(tree):
    out = io.BytesIO()
    n.File(tree).write(out)
    return out.getvalue()


def level():
    inv = [n.Compound(Slot=n.Byte(slot), id=n.Short(block), Count=n.Byte(32), Damage=n.Short(0))
           for slot, block in enumerate((4, 5))]
    player = n.Compound(Pos=n.List[n.Double]([8.5, 65.62, 8.5]), Motion=n.List[n.Double]([0, 0, 0]),
                        Rotation=n.List[n.Float]([0, 0]), Dimension=n.Int(0), Health=n.Short(20),
                        Inventory=n.List[n.Compound](inv))
    data = n.Compound(Player=player, LevelName=n.String("Fixture"), Time=n.Long(6000),
                      LastPlayed=n.Long(0), SizeOnDisk=n.Long(0), RandomSeed=n.Long(37),
                      SpawnX=n.Int(8), SpawnY=n.Int(64), SpawnZ=n.Int(8), version=n.Int(19132))
    return gzip.compress(packed({"Data": data}), mtime=0)


def make_regions():
    regions = {}
    for cx in range(-5, 6):
        for cz in range(-5, 6):
            ids = np.zeros((16, 16, 128), dtype=np.uint8)  # X, Z, Y in McRegion
            ids[:, :, :61] = 1
            ids[:, :, 61:63] = 3
            ids[:, :, 63] = 2
            def place(x, y, z, block):
                if x // 16 == cx and z // 16 == cz:
                    ids[x % 16, z % 16, y] = block

            # The markers and asymmetric skyline make facing and edits visible.
            for x in (8, 10):
                for y in (64, 65):
                    place(x, y, 11, 1)
            for x in range(0, 22):
                place(x, 63, 16, 5)
            for x0, z, material, tall in ((1, 18, 45, 5), (13, 22, 41, 8), (-12, 8, 5, 4)):
                for y in range(64, 64+tall):
                    place(x0, y, z, material)
                    place(x0+4, y, z, material)
                for x in range(x0, x0+5):
                    place(x, 63+tall, z, material)
            height = (128 - np.argmax((ids != 0)[:, :, ::-1], axis=2)).astype(np.uint8)
            sky = (np.arange(128)[None, None, :] >= height[:, :, None]).astype(np.uint8) * 15
            def array(a):
                return n.ByteArray(a.ravel().view(np.int8))
            half = n.ByteArray(np.zeros(16384, dtype=np.int8))
            chunk = n.Compound(xPos=n.Int(cx), zPos=n.Int(cz), Blocks=array(ids), Data=half,
                               SkyLight=array(sky[:, :, ::2] | (sky[:, :, 1::2] << 4)),
                               BlockLight=half, HeightMap=array(height.T), LastUpdate=n.Long(0),
                               TerrainPopulated=n.Byte(1), Entities=n.List[n.Compound]([]),
                               TileEntities=n.List[n.Compound]([]))
            payload = b"\x02" + zlib.compress(packed({"Level": chunk}))
            record = struct.pack(">I", len(payload)) + payload
            region = regions.setdefault((cx//32, cz//32), bytearray(8192))
            sector = len(region)//4096
            count = (len(record)+4095)//4096
            index = (cx % 32 + 32*(cz % 32))*4
            struct.pack_into(">I", region, index, sector*256 + count)
            region.extend(record.ljust(count*4096, b"\0"))
    return {f"region/r.{x}.{z}.mcr": bytes(data) for (x,z), data in regions.items()}


def assets():
    files = {"default.png": (REPO/"assets/default.png").read_bytes()}
    textures = set(re.findall(r'"([^"]+\.png)"', (REPO/"source/platform/texture.c").read_text()))
    for name in sorted(textures - {"default.png"}):
        if name in {"terrain.png", "particles.png", "anim.png", "gui_2.png", "items.png", "mobs.png"} or name.startswith("gui/"):
            size = (256,256)
        elif name.startswith(("mob/", "armor/", "entity/")):
            size = (64,32)
        elif name == "controls.png":
            size = (256,128)
        elif name == "pointer.png":
            size = (96,96)
        else:
            size = (32,32)
        img = Image.new("RGBA", size)
        draw = ImageDraw.Draw(img)
        if name == "terrain.png":
            colors = {(0,0):(200,221,127), (1,0):(129,150,167), (2,0):(138,92,63),
                      (4,0):(202,143,72), (7,0):(172,72,52), (0,1):(96,106,120),
                      (7,1):(235,184,49), (6,2):(79,136,72), (3,0):(122,99,63)}
            for y in range(256):
                for x in range(256):
                    tile = (x//16, y//16)
                    color = colors.get(tile, (90+tile[0]*5, 100+tile[1]*5, 125))
                    noise = ((x*19+y*37+x*y*3) % 17)-8
                    if x%16 == 0 or y%16 == 0:
                        noise -= 13
                    img.putpixel((x,y), tuple(max(0,min(255,c+noise)) for c in color)+(255,))
        elif name == "gui_2.png":
            for k in range(9):
                draw.rectangle((k*20,0,k*20+20,21),fill=(18,30,42,184),outline="slategray",width=1)
            draw.rectangle((208,0,231,23),outline="gold",width=2)
            draw.line((7,232,7,240),fill="white")
            draw.line((3,236,11,236),fill="white")
            for x,c in [(16,"#2c3640"), (52,"#ed5963")]:
                draw.polygon([(x,231),(x+2,229),(x+4,231),(x+6,229),(x+8,231),
                              (x+8,234),(x+4,238),(x,234)],fill=c)
        elif name=="environment/clouds.png":
            draw.rectangle((10,8,22,10),fill=(220,230,235,96))
        elif name in {"terrain/sun.png","terrain/moon.png"}:
            draw.ellipse((4,4,27,27),fill="lemonchiffon" if "sun" in name else "lightgray")
        elif name.startswith(("entity/","mob/","armor/")):
            draw.rectangle((0,0,63,31),fill="#287575")
            draw.rectangle((8,8,15,15),fill="#f2c093")
            draw.point((9,11),fill="black")
            draw.point((14,11),fill="black")
            draw.rectangle((20,20,27,31),fill="#d18134")
        elif name == "pointer.png":
            draw.polygon([(0,0),(8,22),(11,13),(22,8)],fill="white",outline="#244250")
        elif name.startswith("gui/"):
            draw.rounded_rectangle((0,0,175,165),radius=4,fill="#738995",outline="#aec0bc")
        else:
            for yy in range(0,size[1],16):
                for xx in range(0,size[0],16):
                    draw.rectangle((xx+5,yy+5,xx+10,yy+10),fill=(216,185,97,215))
        data=io.BytesIO()
        img.save(data,format="PNG")
        files[name]=data.getvalue()
    return files


def build(output):
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise FileExistsError(f"Choose a new directory for disposable media: {output}")
    art = assets()
    world = {"level.dat": level(), **make_regions()}
    # Load the same configuration distributed with the game.
    base = json.loads((REPO/"config_wii.json").read_text())
    with tempfile.TemporaryDirectory(dir=output.parent) as scratch:
        staged = Path(scratch)/"media"
        staged.mkdir()
        for label, role in (("A","host"), ("B","peer"), ("offline","offline")):
            disk = staged/(label+".raw")
            disk.touch(exist_ok=False)
            fat = PyFat()
            fat.mkfs(str(disk), PyFat.FAT_TYPE_FAT16, size=64*1024*1024, label="FCAVEXLAB")
            fat.close()
            with PyFatFS(str(disk), utc=True) as card:
                cfg = dict(base, network=dict(role=role, bind="127.0.0.1", host="127.0.0.1",
                                              port="45838", world="saves/Fixture", trace="on", metrics="on",
                                              probes=[8,65,10,8,65,11,10,65,10,10,65,11]))
                # Keep a private saves directory even for peers with no world.
                card.makedirs(APP+"/saves",recreate=True)
                card.writebytes(APP+"/config.json", json.dumps(cfg, indent=2).encode())
                card.writebytes(APP+"/CREDITS.txt", FONT_NOTICE.encode())
                files = {"assets/"+k:v for k,v in art.items()}
                if role != "peer":
                    files.update({"saves/Fixture/"+k:v for k,v in world.items()})
                for path,data in files.items():
                    card.makedirs(APP+"/"+path.rsplit("/",1)[0],recreate=True)
                    card.writebytes(APP+"/"+path,data)
        staged.rename(output)


def metrics(image):
    """Read completed telemetry windows from an unmounted card, verbatim."""
    result = {}
    with PyFatFS(str(image), read_only=True) as card:
        for run in sorted(card.listdir(APP)):
            if not re.fullmatch(r"metrics[0-9]{2}", run, re.IGNORECASE):
                continue
            result[run] = {}
            for name in ("frame.csv", "tick.csv", "memory.csv"):
                path = f"{APP}/{run}/{name}"
                if card.isfile(path):
                    result[run][name] = card.readbytes(path).decode("ascii").splitlines()
    print(json.dumps(result, indent=2))


def inspect(image):
    with PyFatFS(str(image),read_only=True) as card:
        report = {"role":json.loads(card.readbytes(APP+"/config.json"))["network"]["role"],
                  "saves":card.listdir(APP+"/saves")}
        if card.exists(WORLD+"/level.dat"):
            level_data = n.File.parse(io.BytesIO(gzip.decompress(card.readbytes(WORLD+"/level.dat"))))["Data"]
            r = card.readbytes(WORLD+"/region/r.0.0.mcr")
            start = (int.from_bytes(r[:4],"big")>>8)*4096
            size = struct.unpack_from(">I",r,start)[0]
            chunk = n.File.parse(io.BytesIO(zlib.decompress(r[start+5:start+4+size])))["Level"]
            report["markers"] = {f"{x},65,{z}": int(chunk["Blocks"][(x*16+z)*128+65])
                                 for x in (8,10) for z in (10,11)}
            report["player"] = {"pos":list(map(float,level_data["Player"]["Pos"])),
                                "inventory":[i.unpack() for i in level_data["Player"]["Inventory"]]}
            report["files"] = card.listdir(WORLD)
        print(json.dumps(report,indent=2))


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["build","inspect","metrics"])
    parser.add_argument("path", type=Path)
    args=parser.parse_args()
    if args.action=="build":
        build(args.path)
    elif args.action=="metrics":
        metrics(args.path)
    else:
        inspect(args.path)


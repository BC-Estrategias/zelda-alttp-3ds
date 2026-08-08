#!/usr/bin/env python3
import re, struct, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "app/jni/src/src/platform/linux/ss_textures.h"
OUT = ROOT / "theme_reference"
OUT.mkdir(exist_ok=True)

text = SRC.read_text(encoding="utf-8")

def write_png(path, w, h, rgba):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        raw.extend(rgba[y*stride:(y+1)*stride])
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)

for name, filename in [("Menu", "tex_menu.png"), ("Parch", "tex_parchment.png"), ("Stone", "tex_stone.png")]:
    mw = re.search(rf"#define kSSTex{name}_W (\d+)", text)
    mh = re.search(rf"#define kSSTex{name}_H (\d+)", text)
    ma = re.search(rf"static const unsigned int kSSTex{name}\[\d+\] = \{{(.*?)\n\}};", text, re.S)
    if not (mw and mh and ma):
        raise SystemExit(f"Could not find {name}")
    w, h = int(mw.group(1)), int(mh.group(1))
    vals = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{8}", ma.group(1))]
    if len(vals) != w*h:
        raise SystemExit(f"{name}: got {len(vals)} pixels, expected {w*h}")
    rgba = bytearray()
    for v in vals:
        a = (v >> 24) & 255
        r = (v >> 16) & 255
        g = (v >> 8) & 255
        b = v & 255
        rgba.extend((r,g,b,a))
    write_png(OUT / filename, w, h, rgba)
    print(filename, w, h)

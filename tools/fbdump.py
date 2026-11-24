#!/usr/bin/env python3
"""Dump the XNU framebuffer (mkxnu.py --fb) over "fastboot getvar mem:" into a PNG.

usage: fbdump.py out.png [rows]    (default: all 1280 rows; ~2 ms per 16 bytes)
"""
import re, subprocess, sys
from PIL import Image

FB, W, H = 0x60000000, 720, 1280


def mem(addr):
    out = subprocess.run(["fastboot", "getvar", f"mem:{addr:#x}"], capture_output=True, text=True, timeout=5)
    m = re.search(r"mem:0x[0-9a-f]+: ((?:[0-9a-f]{8} ?){4})", out.stdout + out.stderr)
    if not m:
        sys.exit(f"read {addr:#x} failed: {out.stderr.strip()}")
    return [int(w, 16) for w in m.group(1).split()]


def main():
    rows = int(sys.argv[2]) if len(sys.argv) > 2 else H
    img = Image.new("RGB", (W, rows))
    px = img.load()
    for y in range(rows):
        for x0 in range(0, W, 4):
            for i, v in enumerate(mem(FB + (y * W + x0) * 4)):  # XRGB8888
                px[x0 + i, y] = (v >> 16 & 0xff, v >> 8 & 0xff, v & 0xff)
        if y % 64 == 63:
            print(f"row {y + 1}/{rows}", file=sys.stderr)
    img.save(sys.argv[1])


main()

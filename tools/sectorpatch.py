#!/usr/bin/env python3
"""U-Boot script that writes only the 512-byte sectors where NEW differs from OLD.

OLD must be what the partition holds now (e.g. fw/rootfs.raw, flashed to SYSTEM). Each
changed sector is rebuilt in RAM with mw.l and written with "mmc write"; the script ends
with a reset. Run it with tools/ubrun.sh -f.

usage: sectorpatch.py OLD NEW LBA > patch.txt
"""
import struct
import sys

BUF = 0x50000000  # above XNU RAM, below the framebuffer


def main():
    old, new, base = open(sys.argv[1], "rb"), open(sys.argv[2], "rb"), int(sys.argv[3], 0)
    out, n = ["mmc dev 0 0"], 0
    while True:
        a, b = old.read(1 << 20), new.read(1 << 20)
        assert len(a) == len(b), "images differ in size"
        if not a:
            break
        for s in range(0, len(a), 512):
            if a[s:s + 512] != b[s:s + 512]:
                lba = base + (n * (1 << 20) + s) // 512
                words = struct.unpack("<128I", b[s:s + 512])
                out += ["mw.l %#x %#x 1" % (BUF + 4 * i, w) for i, w in enumerate(words)]
                out.append("mmc write %#x %#x 1" % (BUF, lba))
        n += 1
    out.append("reset")
    print("\n".join(out))
    print("sectors: %d" % sum(l.startswith("mmc write") for l in out), file=sys.stderr)


main()

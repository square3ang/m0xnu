#!/usr/bin/env python3
"""U-Boot script that writes only the 512-byte sectors where NEW differs from OLD.

OLD must be what the partition holds now (e.g. fw/rootfs.raw, flashed to SYSTEM). Each
changed sector is rebuilt in RAM with mw.l and written with "mmc write"; the script ends
with a reset. Run it with tools/ubrun.sh -f.

usage: sectorpatch.py OLD NEW LBA > patch.txt
"""
import os
import struct
import sys
import tempfile

BUF = 0x50000000  # above XNU RAM, below the framebuffer
MIN_LBA = 34      # protect protective MBR + primary GPT (LBA 0..33)


def make_patch(old_p, new_p, base):
    if base < MIN_LBA:
        raise ValueError(f"refusing base LBA {base} < {MIN_LBA} (protects MBR/GPT)")
    out, n = ["mmc dev 0 0"], 0
    with open(old_p, "rb") as old, open(new_p, "rb") as new:
        while True:
            a, b = old.read(1 << 20), new.read(1 << 20)
            if len(a) != len(b):
                raise ValueError("images differ in size")
            if len(a) % 512 != 0:
                raise ValueError(f"image size not 512-byte aligned (chunk {len(a)} bytes)")
            if not a:
                break
            for s in range(0, len(a), 512):
                if a[s:s + 512] != b[s:s + 512]:
                    lba = base + (n * (1 << 20) + s) // 512
                    words = struct.unpack("<128I", b[s:s + 512])
                    out += ["mw.l %#x %#x 1" % (BUF + 4 * i, w) for i, w in enumerate(words)]
                    out.append("mmc write %#x %#x 1 || exit" % (BUF, lba))
            n += 1
    out.append("reset")
    return out


def selftest():
    with tempfile.TemporaryDirectory() as d:
        old_p, new_p = os.path.join(d, "old"), os.path.join(d, "new")
        open(old_p, "wb").write(bytes(1024))
        open(new_p, "wb").write(bytes(512) + struct.pack("<128I", *range(128)))
        lines = make_patch(old_p, new_p, 100)
        assert lines[0] == "mmc dev 0 0" and lines[-1] == "reset"
        assert "mw.l 0x50000004 0x1 1" in lines
        assert "mmc write 0x50000000 0x65 1 || exit" in lines
        try:
            make_patch(old_p, new_p, 0)
            raise AssertionError("expected ValueError for LBA 0")
        except ValueError:
            pass
        open(old_p, "wb").write(bytes(513))
        open(new_p, "wb").write(bytes(513))
        try:
            make_patch(old_p, new_p, 100)
            raise AssertionError("expected ValueError for unaligned size")
        except ValueError:
            pass
    print("sectorpatch selftest OK")


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        selftest()
        return
    out = make_patch(sys.argv[1], sys.argv[2], int(sys.argv[3], 0))
    print("\n".join(out))
    print("sectors: %d" % sum(l.startswith("mmc write") for l in out), file=sys.stderr)


if __name__ == "__main__":
    main()

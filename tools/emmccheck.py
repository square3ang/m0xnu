#!/usr/bin/env python3
"""Check the eMMC root reads (mkxnu.py --emmc-root) against the image flashed to SYSTEM.

usage: emmccheck.py <emmc log addr from the mkxnu info line> <image>
Reads exy_emmc_log over fastboot and recomputes each logged read's word sum from the image.
"""
import re, struct, subprocess, sys

SYSTEM_LBA = 2277376
USERDATA_LBA = 7716864  # md1, mkxnu --emmc-var


def mem(addr):
    out = subprocess.run(["fastboot", "getvar", f"mem:{addr:#x}"], capture_output=True, text=True, timeout=5)
    m = re.search(r"mem:0x[0-9a-f]+: ((?:[0-9a-f]{8} ?){4})", out.stdout + out.stderr)
    if not m:
        sys.exit(f"read {addr:#x} failed: {out.stderr.strip()}")
    return [int(w, 16) for w in m.group(1).split()]


def main():
    base, img = int(sys.argv[1], 0), open(sys.argv[2], "rb").read()
    reads, errors = mem(base)[:2]
    print(f"{reads} reads, {errors} errors")
    for i in range(max(0, reads - 64), reads):
        lba, count, got, st = mem(base + 16 + 16 * (i % 64))
        if count >> 31 or lba >= USERDATA_LBA:  # md1 or a write: no image to compare
            print(f"{i:4d} lba {lba:#x} {count & 0x7fffffff:#7x} sum {got:08x} st {st:#x} "
                  f"{'write' if count >> 31 else 'read'} md1")
            continue
        off = (lba - SYSTEM_LBA) * 512
        data = img[off:off + count]
        want = sum(struct.unpack(f"<{len(data) // 4}I", data[:len(data) // 4 * 4])) & 0xffffffff if len(data) == count else None
        ok = "ok" if want == got else ("past image" if want is None else f"BAD want {want:08x}")
        print(f"{i:4d} lba {lba:#x} (+{off:#x}) {count:#7x} sum {got:08x} st {st:#x} {ok}")


main()

#!/usr/bin/env python3
"""Read the CPU1 PC-sampler ring (mkxnu.py --sampler) over fastboot and summarize it.

usage: xnupcs.py [last-N]
Prints the newest samples in order and a histogram, with the nearest kernel symbol.
DBGPCSR reads PC + 8 (ARM) or + 4 (Thumb); the raw value is shown.
"""
import bisect, collections, subprocess, sys
from fbmem import mem_words as mem

RING = 0x5EF81000  # mkxnu.py SAMPLER_RING (PHYS + MEMSIZE + 0x81000)
KERNEL = "fw/kernelcache.n94.macho"


def main():
    last = int(sys.argv[1]) if len(sys.argv) > 1 else 24
    idx, when, magic, tcon = mem(RING + 0x400)
    idx &= 0xff
    count, wtcnt = mem(RING + 0x410)[:2]
    print(f"{count} samples, MCT G_TCON {tcon:#x}, WDT WTCNT at the last sample {wtcnt:#x}")
    if magic != 0x4C504D53:
        sys.exit(f"no sampler data (magic {magic:#x})")
    ring = [w for a in range(RING, RING + 0x400, 16) for w in mem(a)]
    order = ring[idx:] + ring[:idx]  # oldest first; idx is the next slot to write
    syms = []
    for l in subprocess.run(["llvm-nm", "-n", KERNEL], capture_output=True, text=True).stdout.splitlines():
        f = l.split()
        if len(f) == 3:
            syms.append((int(f[0], 16), f[2]))
    addrs = [a for a, _ in syms]

    def name(pc):
        i = bisect.bisect_right(addrs, pc) - 1
        return f"{syms[i][1]}+{pc - syms[i][0]:#x}" if i >= 0 and pc - syms[i][0] < 0x100000 else "?"

    print(f"index {idx}, last sample at MCT {when:#x} ({when / 24e6:.2f} s)")
    for pc in order[-last:]:
        print(f"  {pc:08x}  {name(pc)}")
    print("histogram:")
    for pc, n in collections.Counter(order).most_common(12):
        print(f"  {n:4d}  {pc:08x}  {name(pc)}")


if __name__ == "__main__":
    main()

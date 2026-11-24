#!/usr/bin/env python3
"""Decode exy_fault_log (shim.c): user data aborts vm_fault refused (SIGSEGV/SIGBUS), oldest first.

usage: faultlog.py <fault= address from the mkxnu info line> [dyld_shared_cache_armv7]
kern_return: 1 KERN_INVALID_ADDRESS (SIGSEGV), 2 KERN_PROTECTION_FAILURE (SIGBUS), ...
"""
import bisect, re, struct, subprocess, sys

RING = 16  # shim.c exy_fault_log


def mem(addr):
    o = subprocess.run(["fastboot", "getvar", f"mem:{addr:#x}"], capture_output=True, text=True, timeout=5)
    m = re.search(r"mem:0x[0-9a-f]+: ((?:[0-9a-f]{8} ?){4})", o.stdout + o.stderr)
    if not m:
        sys.exit(f"getvar mem:{addr:#x} failed")
    return [int(w, 16) for w in m[1].split()]


def cache_images(path):
    with open(path, "rb") as f:
        h = f.read(0x100)
        ioff, icnt = struct.unpack_from("<II", h, 24)
        f.seek(ioff)
        imgs = [struct.unpack("<QQQII", f.read(32)) for _ in range(icnt)]
        out = []
        for addr, _, _, po, _ in imgs:
            f.seek(po)
            out.append((addr, f.read(256).split(b"\0")[0].decode()))
    return sorted(out)


def main():
    base = int(sys.argv[1], 0)
    imgs = cache_images(sys.argv[2]) if len(sys.argv) > 2 else []
    starts = [a for a, _ in imgs]

    def where(va, slide):
        if not imgs or slide == 0xFFFFFFFF:
            return ""
        i = bisect.bisect_right(starts, va - slide) - 1
        return f" {imgs[i][1].rsplit('/', 1)[-1]}+{va - slide - imgs[i][0]:#x}" if i >= 0 else ""

    count = mem(base)[0]
    print(f"{count} faults")
    for n in range(max(0, count - RING), count):
        e = base + 32 + 32 * (n % RING)
        pid, kr, va, pc = mem(e)
        lr, slide, _, _ = mem(e + 16)
        print(f"{n:3} pid {pid:4} kr {kr:2} va {va:#010x} pc {pc:#010x}{where(pc, slide)}"
              f" lr {lr:#010x}{where(lr, slide)} slide {slide:#x}")


if __name__ == "__main__":
    main()

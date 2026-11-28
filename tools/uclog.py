#!/usr/bin/env python3
"""Decode exy_uc_log (disp.c, mkxnu --disp): IOUserClient::externalMethod calls, oldest first.

usage: uclog.py <uc= address from the mkxnu info line>
"""
import struct, sys
from fbmem import mem_bytes as mem

RING, PIDS = 128, 32  # disp.c UC_RING, UC_PIDS


def cstr(va, cache):
    if not va:
        return "?"
    if va not in cache:
        pa, s = va - 0x40000000, b""  # kernel and kext VA 0x8xxxxxxx -> PA 0x4xxxxxxx
        while b"\0" not in s and len(s) < 64:
            base = pa + len(s) & ~15
            s += mem(base)[(pa + len(s)) - base:]
        cache[va] = s.split(b"\0")[0].decode("latin1")
    return cache[va]


def main():
    base = int(sys.argv[1], 0)
    magic, count = struct.unpack_from("<II", mem(base))
    if magic != 0x7563616C:
        sys.exit(f"no log (magic {magic:#x})")
    ptab = base + 16 + 16 * RING
    names = {}
    for i in range(PIDS):
        e = mem(ptab + 16 * i)
        pid = struct.unpack_from("<I", e)[0]
        if not pid:
            break
        names[pid] = e[4:].split(b"\0")[0].decode("latin1")
    print(f"{count} entries")
    cache = {}
    for n in range(max(0, count - RING), count):
        pid, sel, name, reps = struct.unpack("<4I", mem(base + 16 + 16 * (n % RING)))
        print(f"{n:5} {pid:4} {names.get(pid, '?'):12} {cstr(name, cache):40} sel {sel:3} x{reps}")


if __name__ == "__main__":
    main()

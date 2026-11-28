#!/usr/bin/env python3
"""Read the XNU console captured by the shim (exy_log) via "fastboot getvar mem:".

usage: xnulog.py <log phys addr from mkxnu.py>
"""
import struct, sys
from fbmem import mem_bytes as mem

MAGIC, SIZE = 0x474F4C58, 0x40000  # shim.c LOG_SIZE


def main():
    base = int(sys.argv[1], 0)
    magic, pos = struct.unpack_from("<II", mem(base))
    if magic != MAGIC:
        sys.exit(f"no log (magic {magic:#x})")
    used = min(pos, SIZE)
    buf = b"".join(mem(base + 8 + off) for off in range(0, used, 16))[:used]
    if pos > SIZE:  # ring wrapped: oldest bytes start at the write position
        buf = buf[pos % SIZE:] + buf[:pos % SIZE]
    sys.stdout.write(buf.decode(errors="replace"))
    print(f"\n--- {pos} bytes", file=sys.stderr)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Read the XNU console captured by the shim (exy_log) via "fastboot getvar mem:".

usage: xnulog.py <log phys addr from mkxnu.py>
"""
import re, struct, subprocess, sys

MAGIC, SIZE = 0x474F4C58, 0x40000  # shim.c LOG_SIZE


def mem(addr):
    out = subprocess.run(["fastboot", "getvar", f"mem:{addr:#x}"], capture_output=True, text=True,
                         timeout=5).stderr
    m = re.search(r"mem:0x[0-9a-f]+: ((?:[0-9a-f]{8} ?){4})", out)
    if not m:
        sys.exit(f"getvar mem:{addr:#x} failed: {out.strip()}")
    return struct.pack("<4I", *(int(w, 16) for w in m[1].split()))


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

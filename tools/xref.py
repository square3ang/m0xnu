#!/usr/bin/env python3
"""Find Thumb code that builds a PC-relative address (movw/movt + add rX, pc) in [lo, hi).

usage: xref.py LO HI [TEXT_LO TEXT_HI]
"""
import re, subprocess, sys

KERNEL = "fw/kernelcache.n94.macho"
lo, hi = int(sys.argv[1], 0), int(sys.argv[2], 0)
tlo, thi = (int(x, 0) for x in sys.argv[3:5]) if len(sys.argv) > 4 else (0x80001000, 0x803a0000)
out = subprocess.run(["llvm-objdump", "-d", "--triple=thumbv7", f"--start-address={tlo:#x}",
                      f"--stop-address={thi:#x}", KERNEL], capture_output=True, text=True).stdout
regs = {}
ins = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4}\s){1,2}\s*(\S+)\s+(.*)$")
for line in out.splitlines():
    mt = ins.match(line)
    if not mt:
        continue
    pc, op, args = int(mt[1], 16), mt[2], mt[3]
    a = [x.strip() for x in args.split("@")[0].split(",")]
    if op == "movw" and a[1].startswith("#"):
        regs[a[0]] = int(a[1][1:], 0)
    elif op == "movt" and a[1].startswith("#") and a[0] in regs:
        regs[a[0]] |= int(a[1][1:], 0) << 16
    elif op == "add" and len(a) == 2 and a[1] == "pc" and a[0] in regs:
        v = (regs.pop(a[0]) + pc + 4) & 0xFFFFFFFF
        if lo <= v < hi:
            print(f"{pc:#x} {a[0]} = {v:#x}")

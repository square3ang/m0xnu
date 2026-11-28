"""Shared fastboot 'getvar mem:<addr>' reader (16 bytes / 4 little-endian u32 words)."""
import re, struct, subprocess, sys

_PAT = re.compile(r"mem:0x[0-9a-f]+: ((?:[0-9a-f]{8} ?){4})")


def mem_words(addr):
    out = subprocess.run(["fastboot", "getvar", f"mem:{addr:#x}"],
                         capture_output=True, text=True, timeout=5)
    m = _PAT.search(out.stdout + out.stderr)
    if not m:
        sys.exit(f"getvar mem:{addr:#x} failed: {(out.stderr or out.stdout).strip()}")
    return [int(w, 16) for w in m.group(1).split()]


def mem_bytes(addr):
    return struct.pack("<4I", *mem_words(addr))

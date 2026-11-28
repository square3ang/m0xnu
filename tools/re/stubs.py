"""Kext __stub address -> imported kernel symbol (via __nl_symbol_ptr).
Set M0XNU_KSYMS (default: /tmp/ksyms.txt from: llvm-nm fw/kernelcache.n94.macho | awk '{print $1, $3}')."""
import os, struct

v2f = lambda v: v - 0x804b7000 + 0x46d000


def stubs(lo, size, kernel='fw/kernelcache.n94.macho', ksyms=None):
    m = open(kernel, 'rb').read()
    ks = {}
    with open(ksyms or os.environ.get('M0XNU_KSYMS', '/tmp/ksyms.txt')) as f:
        for l in f:
            a, n = l.split()
            ks.setdefault(int(a, 16) & ~1, n)
    out = {}
    for s in range(lo, lo + size, 16):
        h = struct.unpack_from('<4H', m, v2f(s))
        imm = ((h[0] & 0xf) << 12) | ((h[0] >> 10 & 1) << 11) | ((h[1] >> 12 & 7) << 8) | (h[1] & 0xff)
        imm |= (((h[2] & 0xf) << 12) | ((h[2] >> 10 & 1) << 11) | ((h[3] >> 12 & 7) << 8) | (h[3] & 0xff)) << 16
        got = (imm + s + 12) & 0xffffffff
        tgt = struct.unpack_from('<I', m, v2f(got))[0]
        out[s] = ks.get(tgt & ~1, hex(tgt))
    return out

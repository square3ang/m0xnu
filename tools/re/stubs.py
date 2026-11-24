"""Kext __stub address -> imported kernel symbol (via __nl_symbol_ptr).
Needs /tmp/opencode/ksyms.txt: llvm-nm fw/kernelcache.n94.macho | awk '{print $1, $3}'"""
import struct
m = open('fw/kernelcache.n94.macho', 'rb').read()
v2f = lambda v: v - 0x804b7000 + 0x46d000
ks = {}
for l in open('/tmp/opencode/ksyms.txt'):
    a, n = l.split()
    ks.setdefault(int(a, 16) & ~1, n)
def stubs(lo, size):
    out = {}
    for s in range(lo, lo + size, 16):
        h = struct.unpack_from('<4H', m, v2f(s))
        imm = ((h[0] & 0xf) << 12) | ((h[0] >> 10 & 1) << 11) | ((h[1] >> 12 & 7) << 8) | (h[1] & 0xff)
        imm |= (((h[2] & 0xf) << 12) | ((h[2] >> 10 & 1) << 11) | ((h[3] >> 12 & 7) << 8) | (h[3] & 0xff)) << 16
        got = (imm + s + 12) & 0xffffffff
        tgt = struct.unpack_from('<I', m, v2f(got))[0]
        out[s] = ks.get(tgt & ~1, hex(tgt))
    return out

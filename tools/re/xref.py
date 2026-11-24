"""String xrefs (movw/movt + add pc) in a GNU objdump Thumb listing of a prelinked kext."""
import re, sys
m = open('fw/kernelcache.n94.macho', 'rb').read()
PT_VM, PT_FO = 0x804b7000, 0x46d000
def cstr(v):
    o = v - PT_VM + PT_FO
    if not (0 <= o < len(m)): return None
    e = m.find(b'\0', o); s = m[o:e]
    return s.decode('latin1') if 3 < len(s) < 300 and all(32 <= c < 127 or c in (9, 10) for c in s) else None
def scan(listing):
    lo, hi, fs, out = {}, {}, None, []
    pat = re.compile(r'\s*([0-9a-f]+):\t[0-9a-f ]+\t(\S+)\t?(.*)')
    for l in open(listing):
        mm = pat.match(l)
        if not mm: continue
        a = int(mm.group(1), 16); op = mm.group(2).split('.')[0]; args = mm.group(3)
        if op in ('push', 'stmdb') and 'lr' in args: fs = a
        r = re.match(r'(r\d+|sl|fp|ip), #(\d+)', args)
        if op == 'movw' and r: lo[r.group(1)] = int(r.group(2)); hi.pop(r.group(1), None)
        elif op == 'movt' and r: hi[r.group(1)] = int(r.group(2))
        elif op == 'add':
            r2 = re.match(r'(r\d+|sl|fp|ip), pc$', args.strip())
            if r2 and r2.group(1) in lo:
                v = ((lo[r2.group(1)] | hi.get(r2.group(1), 0) << 16) + a + 4) & 0xffffffff
                s = cstr(v)
                if s: out.append((fs, a, s))
    return out
if __name__ == '__main__':
    key = sys.argv[2] if len(sys.argv) > 2 else ''
    funcs = {}
    for f, a, s in scan(sys.argv[1]):
        if key in s: funcs.setdefault(f, []).append(s.replace('\n', ' ')[:100])
    for f in sorted(funcs, key=lambda x: x or 0):
        print(hex(f or 0), funcs[f][:3])

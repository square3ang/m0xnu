"""Name the vtables of a prelinked kext: getMetaClass (slot 7) -> metaclass object ->
the class name its OSMetaClass constructor call passes in r1. usage: vtnames.py listing vt..."""
import re, struct, sys
sys.path.insert(0, __import__('os').path.dirname(__file__)); import xref
m = xref.m
v2f = lambda v: v - 0x804b7000 + 0x46d000
pat = re.compile(r'\s*([0-9a-f]+):\t([0-9a-f ]+)\t(\S+)\t?(.*)')
def lit(a, args):
    r = re.search(r'\[pc, #(\d+)\]', args)
    return struct.unpack_from('<I', m, v2f(((a + 4) & ~3) + int(r.group(1))))[0] if r else None
def regvals(listing):
    """yield (addr, op, args, vals) with vals = resolved pc-relative register values"""
    val, pend = {}, {}
    for l in open(listing):
        mm = pat.match(l)
        if not mm: continue
        a = int(mm.group(1), 16); op = mm.group(3).split('.')[0]; args = mm.group(4)
        yield a, op, args, val
        rd = args.split(',')[0].strip()
        if op == 'ldr' and '[pc' in args: pend[rd] = lit(a, args); val.pop(rd, None)
        elif op == 'add' and args.strip().endswith('pc') and rd in pend and pend[rd] is not None:
            val[rd] = (pend.pop(rd) + a + 4) & 0xffffffff
        elif op in ('bl', 'blx'): val, pend = {}, {}
        elif rd in val and op not in ('str', 'strd', 'stm', 'push', 'cmp', 'tst', 'b', 'cbz', 'cbnz'):
            val.pop(rd, None)
def names(listing):
    out = {}
    for a, op, args, val in regvals(listing):
        if op in ('bl', 'blx') and 'r0' in val and 'r1' in val:
            s = xref.cstr(val['r1'])
            if s and re.fullmatch(r'[A-Za-z_]\w*', s): out[val['r0']] = s
    return out
def getmeta(fn):
    fn &= ~1
    h = struct.unpack_from('<2H', m, v2f(fn))
    if h[0] & 0xff00 == 0x4800 and h[1] == 0x4478:  # ldr r0, [pc, #i]; add r0, pc
        return (struct.unpack_from('<I', m, v2f(((fn + 4) & ~3) + (h[0] & 0xff) * 4))[0] + fn + 6) & 0xffffffff
if __name__ == '__main__':
    nm = names(sys.argv[1])
    for vt in sys.argv[2:]:
        vt = int(vt, 0)
        g = getmeta(struct.unpack_from('<I', m, v2f(vt + 8 + 4 * 7))[0])
        print(hex(vt), nm.get(g, hex(g or 0)))

#!/usr/bin/env python3
"""Dump an Apple (flattened, non-FDT) DeviceTree as an indented text tree.

Usage: python3 tools/dt.py fw/DeviceTree.n94ap.bin > fw/devicetree.txt
"""
import struct, sys


def parse(buf, off=0):
    """Return (node, next_offset). node = {"props": [(name, bytes)], "children": [...]}."""
    nprops, nchildren = struct.unpack_from("<II", buf, off)
    off += 8
    props = []
    for _ in range(nprops):
        name = buf[off:off + 32].split(b"\0", 1)[0].decode()
        ln = struct.unpack_from("<I", buf, off + 32)[0] & 0x7FFFFFFF  # top bit = placeholder flag
        off += 36
        props.append((name, buf[off:off + ln]))
        off += (ln + 3) & ~3
    children = []
    for _ in range(nchildren):
        child, off = parse(buf, off)
        children.append(child)
    return {"props": props, "children": children}, off


def fmt(v):
    s = v.rstrip(b"\0")
    if s and all(32 <= c < 127 or c == 0 for c in s):
        return repr(s.replace(b"\0", b"|").decode())
    if len(v) % 4 == 0 and 0 < len(v) <= 64:
        return "<" + " ".join(f"{w:#010x}" for w in struct.unpack(f"<{len(v) // 4}I", v)) + ">"
    return v[:64].hex() + ("..." if len(v) > 64 else "") + f" ({len(v)} bytes)"


def dump(node, depth=0):
    name = next((v.rstrip(b"\0").decode() for k, v in node["props"] if k == "name"), "?")
    pad = "  " * depth
    print(f"{pad}{name} {{")
    for k, v in node["props"]:
        if k != "name":
            print(f"{pad}  {k} = {fmt(v)}")
    for c in node["children"]:
        dump(c, depth + 1)
    print(f"{pad}}}")


if __name__ == "__main__":
    buf = open(sys.argv[1], "rb").read()
    root, end = parse(buf)
    assert end <= len(buf), "parse overran buffer"
    dump(root)

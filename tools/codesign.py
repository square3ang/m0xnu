#!/usr/bin/env python3
"""Ad-hoc iOS 9 (32-bit Mach-O, SHA-1) code signer for mkxnu --trust-cache.

Appends or replaces LC_CODE_SIGNATURE with a valid CSMAGIC_EMBEDDED_SIGNATURE
containing a version 0x20100 SHA-1 CodeDirectory (4 KiB pages, CS_ADHOC) and
optional XML entitlements, then prints the 20-byte hex CDHash.

Usage: codesign.py <macho> [--ident com.exy.foo] [--ent entitlements.plist] [-o out]
       codesign.py --selftest
"""
import argparse, hashlib, os, struct, sys, tempfile

MH_MAGIC = 0xFEEDFACE
LC_SEGMENT = 0x1
LC_CODE_SIGNATURE = 0x1D
CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0
CSMAGIC_CODEDIRECTORY = 0xFADE0C02
CSMAGIC_ENTITLEMENTS = 0xFADE7171
CSSLOT_CODEDIRECTORY = 0
CSSLOT_ENTITLEMENTS = 5
CS_ADHOC = 0x00000002
PAGE_SHIFT = 12
PAGE_SIZE = 1 << PAGE_SHIFT


def align(x, a):
    return (x + a - 1) & ~(a - 1)


def _find_commands(data):
    magic, cputype, cpusub, filetype, ncmds, sizeofcmds, flags = struct.unpack_from("<7I", data, 0)
    if magic != MH_MAGIC:
        raise ValueError(f"not a 32-bit Mach-O (magic={magic:#x})")
    off = 28
    linkedit = None
    codesig = None
    first_sect_off = len(data)
    for _ in range(ncmds):
        if off + 8 > len(data):
            raise ValueError(f"load command table overruns file at {off:#x}")
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmdsize < 8 or off + cmdsize > len(data):
            raise ValueError(f"bad load command size {cmdsize:#x} at {off:#x}")
        if cmd == LC_SEGMENT:
            name, vm, vs, fo, fs, mp, ip, nsects, fl = struct.unpack_from("<16s8I", data, off + 8)
            if name.rstrip(b"\0") == b"__LINKEDIT":
                linkedit = (off, vm, vs, fo, fs)
            sp = off + 56
            for _ in range(nsects):
                sfo = struct.unpack_from("<I", data, sp + 40)[0]
                if sfo and sfo < first_sect_off:
                    first_sect_off = sfo
                sp += 68
        elif cmd == LC_CODE_SIGNATURE:
            codesig = (off, *struct.unpack_from("<II", data, off + 8))
        off += cmdsize
    return ncmds, sizeofcmds, first_sect_off, linkedit, codesig


def build_superblob(code_bytes, ident, entitlements=b""):
    ident_b = ident.encode() + b"\0"
    ent_blob = struct.pack(">II", CSMAGIC_ENTITLEMENTS, 8 + len(entitlements)) + entitlements if entitlements else b""
    n_special = CSSLOT_ENTITLEMENTS if ent_blob else 0
    n_code = (len(code_bytes) + PAGE_SIZE - 1) // PAGE_SIZE

    cd_hdr_len = 48  # version 0x20100 (through scatterOffset)
    ident_off = cd_hdr_len
    hash_off = ident_off + len(ident_b) + n_special * 20
    cd_len = hash_off + n_code * 20

    special = bytearray(n_special * 20)
    if ent_blob:
        # slot -5 is at index 0 when n_special == 5
        special[0:20] = hashlib.sha1(ent_blob).digest()

    code_hashes = bytearray()
    for off in range(0, len(code_bytes), PAGE_SIZE):
        code_hashes += hashlib.sha1(code_bytes[off:off + PAGE_SIZE]).digest()

    cd = (struct.pack(">IIIIIIIIIBBBBII",
                      CSMAGIC_CODEDIRECTORY, cd_len, 0x00020100, CS_ADHOC,
                      hash_off, ident_off, n_special, n_code, len(code_bytes),
                      20, 1, 0, PAGE_SHIFT, 0, 0)
          + ident_b + special + code_hashes)
    assert len(cd) == cd_len

    blobs = [(CSSLOT_CODEDIRECTORY, cd)]
    if ent_blob:
        blobs.append((CSSLOT_ENTITLEMENTS, ent_blob))
    hdr_sz = 12 + 8 * len(blobs)
    total = hdr_sz + sum(len(b) for _, b in blobs)
    sb = bytearray(struct.pack(">III", CSMAGIC_EMBEDDED_SIGNATURE, total, len(blobs)))
    cur = hdr_sz
    for slot, b in blobs:
        sb += struct.pack(">II", slot, cur)
        cur += len(b)
    for _, b in blobs:
        sb += b
    return bytes(sb), hashlib.sha1(cd).digest()


def sign_macho(data, ident, entitlements=b""):
    buf = bytearray(data)
    ncmds, sizeofcmds, first_sect_off, linkedit, codesig = _find_commands(buf)
    if not linkedit:
        raise ValueError("Mach-O has no __LINKEDIT segment")
    le_off, le_vm, le_vs, le_fo, _ = linkedit
    if codesig:
        cs_cmd_off, cs_data_off, cs_data_size = codesig
        if cs_data_off < le_fo or cs_data_off > len(buf) or cs_data_off + cs_data_size != len(buf):
            raise ValueError(
                f"existing LC_CODE_SIGNATURE ({cs_data_off:#x}+{cs_data_size:#x}) is not at __LINKEDIT tail (len={len(buf):#x}, le_fo={le_fo:#x})"
            )
        del buf[cs_data_off:]
    else:
        if 28 + sizeofcmds + 16 > first_sect_off:
            raise ValueError("no header padding for LC_CODE_SIGNATURE")
        cs_cmd_off = 28 + sizeofcmds
        ncmds += 1
        sizeofcmds += 16
        struct.pack_into("<II", buf, 16, ncmds, sizeofcmds)
        struct.pack_into("<4I", buf, cs_cmd_off, LC_CODE_SIGNATURE, 16, 0, 0)

    pad = align(len(buf), 16) - len(buf)
    buf += b"\0" * pad
    cs_off = len(buf)

    # Reserve exact signature size and update __LINKEDIT + LC_CODE_SIGNATURE BEFORE hashing
    # because page 0 contains the Mach-O header and load commands.
    dummy_sb, _ = build_superblob(bytes(buf), ident, entitlements)
    cs_size = len(dummy_sb)
    new_fs = (cs_off + cs_size) - le_fo
    new_vs = max(le_vs, align(new_fs, PAGE_SIZE))
    struct.pack_into("<I", buf, le_off + 28, new_vs)
    struct.pack_into("<I", buf, le_off + 36, new_fs)
    struct.pack_into("<II", buf, cs_cmd_off + 8, cs_off, cs_size)

    sb, cdhash = build_superblob(bytes(buf), ident, entitlements)
    assert len(sb) == cs_size
    buf += sb
    return bytes(buf), cdhash


def selftest():
    # Minimal valid 32-bit Mach-O with __TEXT and __LINKEDIT
    text_seg = struct.pack("<II16s8I", LC_SEGMENT, 56 + 68, b"__TEXT", 0x1000, 0x1000, 0, 0x1000, 5, 5, 1, 0)
    text_sec = struct.pack("<16s16s9I", b"__text", b"__TEXT", 0x1200, 4, 0x200, 2, 0, 0, 0, 0, 0)
    le_seg = struct.pack("<II16s8I", LC_SEGMENT, 56, b"__LINKEDIT", 0x2000, 0x1000, 0x1000, 0, 1, 1, 0, 0)
    cmds = text_seg + text_sec + le_seg
    hdr = struct.pack("<7I", MH_MAGIC, 12, 9, 6, 2, len(cmds), 0)
    raw = bytearray(0x1000)
    raw[:28] = hdr
    raw[28:28 + len(cmds)] = cmds
    raw[0x200:0x204] = b"\x70\x47\x00\xbf"

    signed, cdhash = sign_macho(bytes(raw), "com.exy.test", b"<plist/>")
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import mkxnu
    assert mkxnu.macho_cdhash(signed) == cdhash
    # Re-signing must be idempotent
    signed2, cdhash2 = sign_macho(signed, "com.exy.test", b"<plist/>")
    assert signed2 == signed and cdhash2 == cdhash
    # Edge cases: zero cmdsize, zero cs_data_off, and trailing bytes after LC_CODE_SIGNATURE must fail
    bad_cmd = bytearray(raw)
    struct.pack_into("<I", bad_cmd, 28 + 4, 0)
    for label, blob in (
        ("zero cmdsize", bytes(bad_cmd)),
        ("trailing bytes", signed + b"\0"),
    ):
        try:
            sign_macho(blob, "com.exy.test")
            raise AssertionError(f"expected ValueError for {label}")
        except ValueError:
            pass
    bad_cs = bytearray(signed)
    _, _, _, _, cs = _find_commands(bad_cs)
    struct.pack_into("<II", bad_cs, cs[0] + 8, 0, 0)
    try:
        sign_macho(bytes(bad_cs), "com.exy.test")
        raise AssertionError("expected ValueError for zero cs_data_off")
    except ValueError:
        pass
    print("codesign selftest OK:", cdhash.hex())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("macho", nargs="?")
    ap.add_argument("--ident", help="code signing identifier (default: basename)")
    ap.add_argument("--ent", help="XML entitlements plist file")
    ap.add_argument("-o", help="output path (default: overwrite input)")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        selftest()
        return
    if not a.macho:
        ap.error("macho path required")
    ident = a.ident or os.path.basename(a.macho)
    ent = open(a.ent, "rb").read() if a.ent else b""
    out, cdhash = sign_macho(open(a.macho, "rb").read(), ident, ent)
    dst = a.o or a.macho
    dst_dir = os.path.dirname(os.path.abspath(dst))
    fd, tmp = tempfile.mkstemp(dir=dst_dir, prefix=".codesign.")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(out)
        if os.path.exists(dst):
            os.chmod(tmp, os.stat(dst).st_mode & 0o7777)
        os.replace(tmp, dst)
    except Exception:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise
    print(cdhash.hex())


if __name__ == "__main__":
    main()

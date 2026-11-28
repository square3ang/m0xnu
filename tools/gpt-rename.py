#!/usr/bin/env python3
"""Rename GPT partitions in an eMMC head dump and emit the 34-sector image that
U-Boot's "fastboot flash gpt" accepts (protective MBR + header + entry array).

The installed midas U-Boot looks partitions up case-sensitively ("boot"), while
Samsung names them in upper case ("BOOT").

Usage: gpt-rename.py <emmc_head.img> <out.img> OLD=NEW [OLD=NEW ...]
"""
import struct, sys, zlib

SECTOR = 512


def entries_info(hdr):
    lba, n, size = struct.unpack_from("<QII", hdr, 72)
    return lba, n, size


def check(img):
    assert len(img) >= 2 * SECTOR, "image too short for GPT header"
    hdr = bytearray(img[SECTOR:2 * SECTOR])
    assert hdr[:8] == b"EFI PART", "no GPT header at LBA1"
    hsize = struct.unpack_from("<I", hdr, 12)[0]
    assert 92 <= hsize <= SECTOR, f"bad GPT header size {hsize}"
    want = struct.unpack_from("<I", hdr, 16)[0]
    struct.pack_into("<I", hdr, 16, 0)
    assert zlib.crc32(bytes(hdr[:hsize])) == want, "header CRC mismatch"
    lba, n, size = entries_info(hdr)
    assert lba == 2 and size >= 128 and lba * SECTOR + n * size <= len(img), (
        f"GPT entry array out of bounds (lba={lba}, n={n}, size={size}, len={len(img)})"
    )
    ent = img[lba * SECTOR: lba * SECTOR + n * size]
    assert zlib.crc32(ent) == struct.unpack_from("<I", hdr, 88)[0], "entries CRC mismatch"


def names(img):
    lba, n, size = entries_info(img[SECTOR:2 * SECTOR])
    out = []
    for i in range(n):
        e = img[lba * SECTOR + i * size: lba * SECTOR + (i + 1) * size]
        if e[:16] != bytes(16):
            out.append(e[56:128].decode("utf-16le").rstrip("\0"))
    return out


def rename(img, mapping):
    img = bytearray(img)
    check(img)
    lba, n, size = entries_info(img[SECTOR:2 * SECTOR])
    done = set()
    for i in range(n):
        off = lba * SECTOR + i * size
        if img[off:off + 16] == bytes(16):
            continue
        name = img[off + 56: off + 128].decode("utf-16le").rstrip("\0")
        if name in mapping:
            enc = mapping[name].encode("utf-16le")
            if len(enc) > 72:
                raise ValueError(f"GPT name too long (>36 UTF-16 code units): {mapping[name]!r}")
            img[off + 56: off + 128] = enc.ljust(72, b"\0")
            done.add(name)
    assert done == set(mapping), f"not found: {set(mapping) - done}"
    struct.pack_into("<I", img, SECTOR + 88, zlib.crc32(bytes(img[lba * SECTOR: lba * SECTOR + n * size])))
    hsize = struct.unpack_from("<I", img, SECTOR + 12)[0]
    struct.pack_into("<I", img, SECTOR + 16, 0)
    struct.pack_into("<I", img, SECTOR + 16, zlib.crc32(bytes(img[SECTOR: SECTOR + hsize])))
    check(img)
    return bytes(img)


def selftest():
    img = bytearray(34 * SECTOR)
    img[SECTOR:SECTOR + 8] = b"EFI PART"
    struct.pack_into("<I", img, SECTOR + 12, 92)
    struct.pack_into("<QII", img, SECTOR + 72, 2, 128, 128)
    # Entry 0: active BOOT; Entry 1: deleted (zero GUID) with stale name "BOOT"
    img[2 * SECTOR:2 * SECTOR + 16] = b"\x01" * 16
    img[2 * SECTOR + 56:2 * SECTOR + 128] = "BOOT".encode("utf-16le").ljust(72, b"\0")
    img[2 * SECTOR + 128 + 56:2 * SECTOR + 128 + 128] = "BOOT".encode("utf-16le").ljust(72, b"\0")
    struct.pack_into("<I", img, SECTOR + 88, zlib.crc32(bytes(img[2 * SECTOR:2 * SECTOR + 128 * 128])))
    struct.pack_into("<I", img, SECTOR + 16, zlib.crc32(bytes(img[SECTOR:SECTOR + 92])))

    out = rename(bytes(img), {"BOOT": "boot"})
    assert len(out) == 34 * SECTOR and names(out) == ["boot"]
    # Deleted entry must remain untouched
    assert out[2 * SECTOR + 128 + 56:2 * SECTOR + 128 + 64] == "BOOT".encode("utf-16le")
    # >36-char name must fail instead of growing/shifting the GPT
    try:
        rename(bytes(img), {"BOOT": "A" * 37})
        raise AssertionError("expected ValueError for >36-char name")
    except ValueError:
        pass
    print("gpt-rename selftest OK")


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        selftest()
        sys.exit(0)
    src = open(sys.argv[1], "rb").read()[:34 * SECTOR]
    mapping = dict(a.split("=", 1) for a in sys.argv[3:])
    out = rename(src, mapping)
    assert len(out) == 34 * SECTOR
    # only names may differ from the original
    diff = [i for i in range(len(out)) if out[i] != src[i]]
    assert all(SECTOR + 16 <= i < SECTOR + 20 or SECTOR + 88 <= i < SECTOR + 92 or i >= 2 * SECTOR for i in diff)
    open(sys.argv[2], "wb").write(out)
    print(" ".join(names(out)))

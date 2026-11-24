#!/usr/bin/env python3
"""Fetch the iPhone4,1 9.3.5 (13G36) boot files without downloading the whole IPSW,
decrypt their IMG3 containers and unpack the kernelcache to a raw Mach-O.

Usage: python3 tools/fw.py [outdir]   (default: fw/)
"""
import io, os, struct, sys, urllib.request, zipfile, zlib
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

IPSW = ("https://secure-appldnld.apple.com/iOS9.3.5/031-73068-20160825-6A2B99DE-6711-11E6-BB8F-133834D2D062/"
        "iPhone4,1_9.3.5_13G36_Restore.ipsw")

# Keys: https://theapplewiki.com/wiki/Keys:Genoa_13G36_(iPhone4,1)
FILES = {
    "kernelcache.release.n94": ("ac20a4b047a64fe6b4a3a20a71ce784d",
                                "8971653fdb070f532d6e563e5e1cd589fca50d809c35689753556fbb5dd7f928"),
    "Firmware/all_flash/all_flash.n94ap.production/DeviceTree.n94ap.img3":
                               ("21617cab22fd2738cad14f6f1604fae2",
                                "4800e1183963014bdd82a67df14271e5184c47e4b92f9f929e7f0363eeb4ccd2"),
    "058-49032-036.dmg":       ("7842e2c239a2f8a5d8dac384cb308c94",
                                "3de170b9d5d0e4f22b6800f1ca9f82922b1e100c9f868cf32a957039fab21edb"),
}
OUTNAMES = {
    "kernelcache.release.n94": "kernelcache.n94.macho",
    "Firmware/all_flash/all_flash.n94ap.production/DeviceTree.n94ap.img3": "DeviceTree.n94ap.bin",
    "058-49032-036.dmg": "restore_ramdisk.dmg",
}


class HttpFile(io.RawIOBase):
    """Seekable read-only view of a remote file via HTTP Range, enough for zipfile."""

    def __init__(self, url):
        self.url, self.pos = url, 0
        with urllib.request.urlopen(urllib.request.Request(url, method="HEAD")) as r:
            self.size = int(r.headers["Content-Length"])

    def seekable(self): return True
    def readable(self): return True
    def tell(self): return self.pos

    def seek(self, off, whence=0):
        self.pos = {0: off, 1: self.pos + off, 2: self.size + off}[whence]
        return self.pos

    def read(self, n=-1):
        if n < 0: n = self.size - self.pos
        if n == 0 or self.pos >= self.size: return b""
        end = min(self.pos + n, self.size) - 1
        req = urllib.request.Request(self.url, headers={"Range": f"bytes={self.pos}-{end}"})
        with urllib.request.urlopen(req) as r:
            data = r.read()
        self.pos += len(data)
        return data

    def readinto(self, buf):  # used by BufferedReader
        data = self.read(len(buf))
        buf[:len(data)] = data
        return len(data)


def img3_data(blob, iv, key):
    """Return the decrypted DATA payload of an IMG3 file."""
    assert blob[:4] == b"3gmI", "not an IMG3 file"
    off = 20
    while off < len(blob):
        magic, total, dlen = struct.unpack_from("<4sII", blob, off)
        if magic == b"ATAD":
            padded = (dlen + 15) & ~15  # last block is encrypted whole, its padding sits in the tag
            assert 12 + padded <= total, "DATA tag shorter than padded payload"
            data = blob[off + 12: off + 12 + padded]
            d = Cipher(algorithms.AES(bytes.fromhex(key)), modes.CBC(bytes.fromhex(iv))).decryptor()
            return (d.update(data) + d.finalize())[:dlen]
        off += total
    raise ValueError("no DATA tag")


def lzss_decode(src, out_len):
    """Apple/Okumura LZSS: N=4096, F=18, THRESHOLD=2."""
    N, F = 4096, 18
    ring = bytearray(b" " * N)
    r, out, i, flags = N - F, bytearray(), 0, 0
    while i < len(src) and len(out) < out_len:
        flags >>= 1
        if not flags & 0x100:
            flags = src[i] | 0xFF00; i += 1
            if i >= len(src): break
        if flags & 1:
            c = src[i]; i += 1
            out.append(c); ring[r] = c; r = (r + 1) & (N - 1)
        else:
            if i + 1 >= len(src): break
            a, b = src[i], src[i + 1]; i += 2
            pos, ln = a | ((b & 0xF0) << 4), (b & 0x0F) + 3
            for k in range(ln):
                c = ring[(pos + k) & (N - 1)]
                out.append(c); ring[r] = c; r = (r + 1) & (N - 1)
    return bytes(out[:out_len])


def unpack_kernel(data):
    assert data[:8] == b"complzss", f"kernelcache not complzss: {data[:8]!r}"
    csum, ulen, clen = struct.unpack_from(">III", data, 8)
    macho = lzss_decode(data[0x180:0x180 + clen], ulen)
    assert len(macho) == ulen, "short LZSS output"
    assert zlib.adler32(macho) == csum, "LZSS checksum mismatch"
    magic, cputype = struct.unpack_from("<II", macho, 0)
    assert magic == 0xFEEDFACE and cputype == 12, "not a 32-bit ARM Mach-O"
    return macho


def selftest():
    # literal 'abc' then back-reference to ring position N-F (the 'a'), length 3 -> 'abcabc'
    pos = 4096 - 18
    enc = bytes([0b0111, ord("a"), ord("b"), ord("c"), pos & 0xFF, ((pos >> 4) & 0xF0) | 0])
    assert lzss_decode(enc, 6) == b"abcabc", lzss_decode(enc, 6)


def main():
    selftest()
    outdir = sys.argv[1] if len(sys.argv) > 1 else "fw"
    os.makedirs(outdir, exist_ok=True)
    z = zipfile.ZipFile(io.BufferedReader(HttpFile(IPSW), buffer_size=1 << 20))
    for name, (iv, key) in FILES.items():
        out = os.path.join(outdir, OUTNAMES[name])
        if os.path.exists(out):
            print("skip", out); continue
        print("fetch", name, f"({z.getinfo(name).compress_size >> 10} KiB)")
        data = img3_data(z.read(name), iv, key)
        if name.startswith("kernelcache"):
            data = unpack_kernel(data)
        with open(out, "wb") as f:
            f.write(data)
        print("  ->", out, len(data), "bytes")


if __name__ == "__main__":
    main()

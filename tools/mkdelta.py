#!/usr/bin/env python3
"""Android sparse image holding only the 4 KiB blocks where NEW differs from OLD (rest DONT_CARE),
for "fastboot flash SYSTEM" on a device whose partition already holds OLD.

usage: mkdelta.py old.raw new.raw out.simg
"""
import os, struct, sys, tempfile

BS = 4096
SPARSE_MAGIC = 0xED26FF3A
CHUNK_RAW = 0xCAC1
CHUNK_DONT_CARE = 0xCAC3


def make_delta(old_p, new_p, out_p):
    with open(old_p, "rb") as old, open(new_p, "rb") as new:
        size = new.seek(0, 2)
        if size != old.seek(0, 2) or size % BS != 0:
            raise ValueError(f"images must match in size and be {BS}-byte aligned")
        old.seek(0)
        new.seek(0)
        total = size // BS
        nchunks = changed = nruns = 0
        raw_hdr_pos = None
        raw_blocks = 0
        skip_blocks = 0

        with open(out_p, "wb") as o:
            o.write(bytes(28))

            def flush_skip():
                nonlocal skip_blocks, nchunks
                if skip_blocks:
                    o.write(struct.pack("<HHII", CHUNK_DONT_CARE, 0, skip_blocks, 12))
                    skip_blocks = 0
                    nchunks += 1

            def flush_raw():
                nonlocal raw_hdr_pos, raw_blocks, nchunks
                if raw_hdr_pos is not None:
                    end = o.tell()
                    o.seek(raw_hdr_pos)
                    o.write(struct.pack("<HHII", CHUNK_RAW, 0, raw_blocks, 12 + raw_blocks * BS))
                    o.seek(end)
                    raw_hdr_pos = None
                    raw_blocks = 0
                    nchunks += 1

            for _ in range(total):
                a, n = old.read(BS), new.read(BS)
                if a != n:
                    flush_skip()
                    if raw_hdr_pos is None:
                        raw_hdr_pos = o.tell()
                        o.write(bytes(12))
                        nruns += 1
                    o.write(n)
                    raw_blocks += 1
                    changed += 1
                else:
                    flush_raw()
                    skip_blocks += 1

            flush_raw()
            flush_skip()
            o.seek(0)
            o.write(struct.pack("<IHHHHIIII", SPARSE_MAGIC, 1, 0, 28, 12, BS, total, nchunks, 0))
    return changed, nruns, nchunks


def _apply_sparse(base, simg):
    magic, maj, minv, fhs, chs, bs, total, nchunks, _ = struct.unpack_from("<IHHHHIIII", simg, 0)
    assert (magic, maj, minv, fhs, chs, bs) == (SPARSE_MAGIC, 1, 0, 28, 12, BS)
    out = bytearray(base)
    assert len(out) == total * BS
    off, blk = fhs, 0
    for _ in range(nchunks):
        typ, _, n, total_sz = struct.unpack_from("<HHII", simg, off)
        off += chs
        if typ == CHUNK_RAW:
            assert total_sz == chs + n * BS
            out[blk * BS:(blk + n) * BS] = simg[off:off + n * BS]
            off += n * BS
        elif typ == CHUNK_DONT_CARE:
            assert total_sz == chs
        else:
            raise AssertionError(f"unexpected chunk type {typ:#x}")
        blk += n
    assert off == len(simg) and blk == total
    return bytes(out)


def selftest():
    with tempfile.TemporaryDirectory() as d:
        old_p, new_p, out_p = (os.path.join(d, x) for x in ("old", "new", "out.simg"))
        old_b = b"".join(bytes([i]) * BS for i in range(6))
        # Mix of unchanged, single changed, consecutive changed, trailing unchanged
        new_b = bytearray(old_b)
        new_b[1 * BS:2 * BS] = b"\xaa" * BS
        new_b[3 * BS:5 * BS] = b"\xbb" * (2 * BS)
        for o_data, n_data, exp_changed, exp_runs in (
            (old_b, bytes(new_b), 3, 2),
            (old_b, old_b, 0, 0),
        ):
            open(old_p, "wb").write(o_data)
            open(new_p, "wb").write(n_data)
            ch, runs, _ = make_delta(old_p, new_p, out_p)
            assert (ch, runs) == (exp_changed, exp_runs)
            assert _apply_sparse(o_data, open(out_p, "rb").read()) == n_data
    print("mkdelta selftest OK")


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        selftest()
        return
    old_p, new_p, out_p = sys.argv[1:4]
    changed, nruns, nchunks = make_delta(old_p, new_p, out_p)
    print(f"{changed} blocks changed in {nruns} runs, {nchunks} chunks")


if __name__ == "__main__":
    main()

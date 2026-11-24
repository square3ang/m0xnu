#!/usr/bin/env python3
"""Compare the eMMC sectors a sectorpatch.py script writes against an image, before or after.

usage: patchcheck.py PATCH.txt IMAGE LBA
Reads every sector the patch writes into RAM (tools/ubrun.sh, ends with reset), then compares
them over "fastboot getvar mem:" with IMAGE (whose sector 0 is at LBA).
"""
import re, struct, subprocess, sys, time

BUF = 0x5e000000  # sectorpatch.py builds at 0x50000000; stay clear of it


def mem(addr):
    o = subprocess.run(["fastboot", "getvar", f"mem:{addr:#x}"], capture_output=True, text=True, timeout=5)
    m = re.search(r"mem:0x[0-9a-f]+: ((?:[0-9a-f]{8} ?){4})", o.stdout + o.stderr)
    if not m:
        sys.exit(f"getvar mem:{addr:#x} failed")
    return struct.pack("<4I", *(int(w, 16) for w in m[1].split()))


def main():
    patch, image, base = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
    lbas = sorted({int(m, 16) for m in re.findall(r"mmc write 0x[0-9a-f]+ (0x[0-9a-f]+) 1", open(patch).read())})
    cmds = ["mmc dev 0 0"] + [f"mmc read {BUF + 512 * i:#x} {lba:#x} 1" for i, lba in enumerate(lbas)] + ["reset"]
    subprocess.run([sys.path[0] + "/ubrun.sh", "; ".join(cmds)], check=True)
    for _ in range(30):  # wait for the chain U-Boot to come back
        time.sleep(2)
        try:
            if "U-Boot" in subprocess.run(["fastboot", "getvar", "version-bootloader"], capture_output=True,
                                          text=True, timeout=5).stderr:
                break
        except subprocess.TimeoutExpired:  # fastboot waits while the device is away
            pass
    img = open(image, "rb")
    bad = 0
    for i, lba in enumerate(lbas):
        dev = b"".join(mem(BUF + 512 * i + o) for o in range(0, 512, 16))
        img.seek((lba - base) * 512)
        if dev != img.read(512):
            bad += 1
            print(f"{lba:#x} differs")
    print(f"{len(lbas)} sectors, {bad} differ from {image}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()

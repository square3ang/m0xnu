#!/usr/bin/env python3
"""Trigger remote reboot to fastboot on Galaxy S3 running XNU/iOS (USB 18d1:4ee8)."""
import fcntl
import glob
import os
import struct
import sys

USBDEVFS_CONTROL = 0xC0185500  # struct usbdevfs_ctrltransfer on x86_64 Linux

def find_dev(vid="18d1", pid="4ee8"):
    for d in sorted(glob.glob("/sys/bus/usb/devices/*")):
        try:
            v = open(os.path.join(d, "idVendor")).read().strip().lower()
            p = open(os.path.join(d, "idProduct")).read().strip().lower()
            if v == vid and p == pid:
                bus = int(open(os.path.join(d, "busnum")).read().strip())
                dev = int(open(os.path.join(d, "devnum")).read().strip())
                return f"/dev/bus/usb/{bus:03d}/{dev:03d}"
        except OSError:
            pass
    return None

def main():
    path = find_dev()
    if not path:
        print("ExyXNU USB control device (18d1:4ee8) not found", file=sys.stderr)
        return 1
    fd = os.open(path, os.O_RDWR)
    try:
        # bRequestType=0x40 (Vendor Host->Dev), bRequest=0xFB, wValue=0x1337, wIndex=0, wLength=0
        # struct usbdevfs_ctrltransfer on x86_64: u8,u8,u16,u16,u16, u32 timeout, 4B pad, u64 data (24B)
        ctrl = struct.pack("<BBHHHI4xQ", 0x40, 0xFB, 0x1337, 0, 0, 1000, 0)
        fcntl.ioctl(fd, USBDEVFS_CONTROL, ctrl)
        print(f"Sent remote fastboot command to {path} (18d1:4ee8)!")
        return 0
    finally:
        os.close(fd)

if __name__ == "__main__":
    sys.exit(main())

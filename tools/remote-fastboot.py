#!/usr/bin/env python3
"""Trigger remote reboot to fastboot on Galaxy S3 running XNU/iOS (USB 18d1:4ee8)."""
import errno
import fcntl
import glob
import os
import struct
import sys

# struct usbdevfs_ctrltransfer: 24 B on 64-bit Linux (0xC0185500), 16 B on 32-bit Linux (0xC0105500)
_IS_64 = struct.calcsize("P") == 8
USBDEVFS_CONTROL = 0xC0185500 if _IS_64 else 0xC0105500
_CTRL_FMT = "<BBHHHI4xQ" if _IS_64 else "<BBHHHII"

def find_dev(vid="18d1", pid="4ee8"):
    for d in sorted(glob.glob("/sys/bus/usb/devices/*")):
        try:
            with open(os.path.join(d, "idVendor")) as vf, open(os.path.join(d, "idProduct")) as pf:
                v, p = vf.read().strip().lower(), pf.read().strip().lower()
            if v == vid and p == pid:
                with open(os.path.join(d, "busnum")) as bf, open(os.path.join(d, "devnum")) as df:
                    bus, dev = int(bf.read().strip()), int(df.read().strip())
                return f"/dev/bus/usb/{bus:03d}/{dev:03d}"
        except OSError:
            pass
    return None

def main():
    path = find_dev()
    if not path:
        print("ExyXNU USB control device (18d1:4ee8) not found", file=sys.stderr)
        return 1
    try:
        fd = os.open(path, os.O_RDWR)
    except PermissionError:
        print(f"Permission denied opening {path} (run with sudo or add a udev rule for 18d1:4ee8)", file=sys.stderr)
        return 1
    try:
        # bRequestType=0x40 (Vendor Host->Dev), bRequest=0xFB, wValue=0x1337, wIndex=0, wLength=0
        ctrl = struct.pack(_CTRL_FMT, 0x40, 0xFB, 0x1337, 0, 0, 1000, 0)
        fcntl.ioctl(fd, USBDEVFS_CONTROL, ctrl)
        print(f"Sent remote fastboot command to {path} (18d1:4ee8)!")
        return 0
    except OSError as e:
        if e.errno in (errno.ENODEV, errno.EPROTO, errno.ESHUTDOWN, errno.EPIPE):
            print(f"Device disconnected during remote fastboot command on {path} (reboot triggered).")
            return 0
        print(f"USB control transfer failed on {path}: {e}", file=sys.stderr)
        return 1
    finally:
        os.close(fd)

if __name__ == "__main__":
    sys.exit(main())

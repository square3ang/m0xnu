#!/usr/bin/env python3
"""Trigger XNU DWC2 OTG remote-fastboot hook via 3 consecutive USBDEVFS_RESET ioctls."""
import fcntl, os, re, subprocess, sys, time

USBDEVFS_RESET = 0x5514

def find_dev():
    out = subprocess.check_output(["lsusb"], text=True)
    for line in out.splitlines():
        if "18d1:4ee7" in line:
            m = re.match(r"Bus (\d+) Device (\d+):", line)
            if m:
                return f"/dev/bus/usb/{m.group(1)}/{m.group(2)}"
    return None

for i in range(4):
    path = find_dev()
    if not path:
        print(f"[{i}] 18d1:4ee7 not found (may already be rebooting)")
        time.sleep(0.6)
        continue
    try:
        fd = os.open(path, os.O_WRONLY)
        fcntl.ioctl(fd, USBDEVFS_RESET, 0)
        os.close(fd)
        print(f"[{i}] USBDEVFS_RESET sent to {path}")
    except OSError as e:
        print(f"[{i}] reset error on {path}: {e}")
    time.sleep(0.35)

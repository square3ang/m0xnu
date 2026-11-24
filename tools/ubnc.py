#!/usr/bin/env python3
"""U-Boot netconsole over the midas USB RNDIS gadget.

  ubnc.py setup          give the RNDIS interface 192.168.168.1/24 (needs sudo)
  ubnc.py                interactive console
  ubnc.py -c "<cmd>" [-t secs]   send one command, print output until quiet
"""
import argparse, glob, os, select, socket, subprocess, sys, time

BOARD = ("192.168.168.2", 6666)  # CONFIG_IPADDR in include/configs/midas.h, netconsole port
HOST_IP = "192.168.168.1/24"


def rndis_iface():
    for dev in glob.glob("/sys/class/net/*/device/driver"):
        if os.path.basename(os.readlink(dev)) == "rndis_host":
            return dev.split("/")[4]
    return None


def setup():
    iface = rndis_iface()
    if not iface:
        sys.exit("no rndis_host interface: is the chainloaded U-Boot in console mode (green LED)?")
    subprocess.run(["sudo", "ip", "addr", "replace", HOST_IP, "dev", iface], check=True)
    subprocess.run(["sudo", "ip", "link", "set", iface, "up"], check=True)
    print(iface, HOST_IP)


def sock():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("", BOARD[1]))  # board broadcasts output to port 6666 (ncip unset)
    return s


def drain(s, quiet):
    """Print board output until nothing arrives for `quiet` seconds."""
    while select.select([s], [], [], quiet)[0]:
        sys.stdout.write(s.recv(2048).decode(errors="replace"))
        sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", nargs="?")
    ap.add_argument("-c", "--command")
    ap.add_argument("-t", "--quiet", type=float, default=1.5)
    a = ap.parse_args()
    if a.mode == "setup":
        return setup()
    s = sock()
    if a.command is not None:
        s.sendto((a.command + "\n").encode(), BOARD)
        return drain(s, a.quiet)
    while True:
        r = select.select([s, sys.stdin], [], [])[0]
        if s in r:
            sys.stdout.write(s.recv(2048).decode(errors="replace")); sys.stdout.flush()
        if sys.stdin in r:
            line = sys.stdin.readline()
            if not line:
                return
            s.sendto(line.encode(), BOARD)


if __name__ == "__main__":
    main()

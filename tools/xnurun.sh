#!/bin/bash
# Boot an XNU bring-up FIT, wait for the watchdog reset, print the mailbox.
# usage: xnurun.sh [--rebind-usb] <xnu.itb> <mailbox addr from mkxnu.py>
# Mailbox words: magic (584e5542 = ours), checkpoint (1 = trampoline, else stub lr), sp, cpsr,
# then last traced call site and trace count (mkxnu.py --trace),
# then with --vec: 0xEE00000n (exception vector n), DFSR, DFAR, IFSR, SPSR, lr, PAR(DFAR), TTBR0, TTBR1, TTBCR.
set -euo pipefail
rebind_usb=0
if [ "${1:-}" = "--rebind-usb" ]; then rebind_usb=1; shift; fi
itb=$1 mailbox=$2
ver() { timeout 3 fastboot getvar version-bootloader 2>&1 | grep -o "U-Boot [^ ]*" || true; }

[ -n "$(ver)" ] || { echo "no fastboot device" >&2; exit 1; }
fb_pci=
if [ "$rebind_usb" -eq 1 ]; then
	for d in /sys/bus/usb/devices/*; do
		if [ -r "$d/idVendor" ] && [ "$(cat "$d/idVendor")" = "18d1" ]; then
			bus=$(cat "$d/busnum" 2>/dev/null || true)
			[ -n "$bus" ] && fb_pci=$(basename "$(readlink -f "/sys/bus/usb/devices/usb$bus/..")") && break
		fi
	done
fi
t0=$(date +%s)
out=$(timeout 60 fastboot boot "$itb" 2>&1) || { echo "$out" >&2; exit 1; }
if grep -qiE "FAILED|error" <<< "$out"; then echo "$out" >&2; exit 1; fi
sleep 5
rebound=
until v=$(ver); [ -n "$v" ]; do
	# opt-in (--rebind-usb): if host xHCI wedges on re-enumeration, rebind only the phone's controller
	if [ "$rebind_usb" -eq 1 ] && [ -n "$fb_pci" ] && [ -z "$rebound" ] && [ $(( $(date +%s) - t0 )) -gt 680 ]; then
		rebound=1
		echo "$fb_pci" | sudo -n tee /sys/bus/pci/drivers/xhci_hcd/unbind >/dev/null
		sleep 2; echo "$fb_pci" | sudo -n tee /sys/bus/pci/drivers/xhci_hcd/bind >/dev/null
	fi
	[ $(( $(date +%s) - t0 )) -gt 740 ] && { echo "no reset after 740 s: hung without watchdog, power-cycle needed" >&2; exit 2; }
	sleep 1
done
echo "back after $(( $(date +%s) - t0 )) s in $v"
timeout 3 fastboot getvar mem:"$mailbox" 2>&1 | head -1
timeout 3 fastboot getvar mem:"$(printf '%#x' $((mailbox + 16)))" 2>&1 | head -1
timeout 3 fastboot getvar mem:"$(printf '%#x' $((mailbox + 32)))" 2>&1 | head -1
timeout 3 fastboot getvar mem:"$(printf '%#x' $((mailbox + 48)))" 2>&1 | head -1
timeout 3 fastboot getvar mem:"$(printf '%#x' $((mailbox + 64)))" 2>&1 | head -1  # stub: r0..r3

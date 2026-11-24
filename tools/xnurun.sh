#!/bin/bash
# Boot an XNU bring-up FIT, wait for the watchdog reset, print the mailbox.
# usage: xnurun.sh <xnu.itb> <mailbox addr from mkxnu.py>
# Mailbox words: magic (584e5542 = ours), checkpoint (1 = trampoline, else stub lr), sp, cpsr,
# then last traced call site and trace count (mkxnu.py --trace),
# then with --vec: 0xEE00000n (exception vector n), DFSR, DFAR, IFSR, SPSR, lr, PAR(DFAR), TTBR0, TTBR1, TTBCR.
set -u
itb=$1 mailbox=$2
ver() { timeout 3 fastboot getvar version-bootloader 2>&1 | grep -o "U-Boot [^ ]*"; }

[ -n "$(ver)" ] || { echo "no fastboot device"; exit 1; }
t0=$(date +%s)
timeout 60 fastboot boot "$itb" 2>&1 | grep -E "FAILED|error" && exit 1
sleep 5
rebound=
until v=$(ver); [ -n "$v" ]; do
	# the host xHCI sometimes wedges on re-enumeration (descriptor read error -110): rebind it once
	if [ -z "$rebound" ] && [ $(( $(date +%s) - t0 )) -gt 680 ]; then  # WDT: 42 s after the shim stops refilling it
		rebound=1; pci=$(basename "$(readlink -f /sys/bus/usb/devices/usb1/..)")
		echo "$pci" | sudo -n tee /sys/bus/pci/drivers/xhci_hcd/unbind >/dev/null
		sleep 2; echo "$pci" | sudo -n tee /sys/bus/pci/drivers/xhci_hcd/bind >/dev/null
	fi
	[ $(( $(date +%s) - t0 )) -gt 740 ] && { echo "no reset after 740 s: hung without watchdog, power-cycle needed"; exit 2; }
	sleep 1
done
echo "back after $(( $(date +%s) - t0 )) s in $v"
timeout 3 fastboot getvar mem:"$mailbox" 2>&1 | head -1
timeout 3 fastboot getvar mem:"$(printf '%#x' $((mailbox + 16)))" 2>&1 | head -1
timeout 3 fastboot getvar mem:"$(printf '%#x' $((mailbox + 32)))" 2>&1 | head -1
timeout 3 fastboot getvar mem:"$(printf '%#x' $((mailbox + 48)))" 2>&1 | head -1
timeout 3 fastboot getvar mem:"$(printf '%#x' $((mailbox + 64)))" 2>&1 | head -1  # stub: r0..r3

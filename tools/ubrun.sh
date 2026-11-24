#!/bin/bash
# Run U-Boot commands on the chainloaded U-Boot without a console: wrap them as the
# "script" image of a FIT and "fastboot boot" it (see fastboot_bootcmd in midas.h).
# No output comes back; leave results in RAM and read them with "fastboot getvar mem:<addr>".
# usage: ubrun.sh "<cmd>; <cmd>..."   or   ubrun.sh -f script.txt
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
if [ "$1" = -f ]; then cp "$2" "$d/s.txt"; else printf '%s\n' "$1" | tr ';' '\n' > "$d/s.txt"; fi
cat > "$d/s.its" <<EOF
/dts-v1/;
/ {
	description = "U-Boot command script";
	#address-cells = <1>;
	images { script { data = /incbin/("s.txt"); type = "script"; arch = "arm"; compression = "none"; }; };
	configurations { default = "c"; c { }; };
};
EOF
"$root/u-boot/tools/mkimage" -f "$d/s.its" "$d/s.itb" >/dev/null
[ "$(stat -c %s "$d/s.itb")" -ge 4096 ] || truncate -s 4096 "$d/s.itb"  # fastboot: "too short"
timeout 30 fastboot boot "$d/s.itb" 2>&1 | grep -E "FAILED|error" && exit 1 || true

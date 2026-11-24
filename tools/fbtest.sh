#!/bin/bash
# Light the panel with the -xnu-fb chain U-Boot, photograph it, then reset.
# usage: fbtest.sh [16|32|mali|tri] [out.jpg]
#   mali: malitest clears the screen green; tri: malitest draws the RGB triangle
# Fills red/green/blue thirds, runs "xnufb", grabs a camera frame (/dev/video0) while
# U-Boot sleeps, then resets: falling back to the installed U-Boot (fastboot exit or
# re-entry) has wedged its USB twice.
set -eu
cd "$(dirname "$0")/.."
bpp=${1:-32}; out=${2:-/tmp/opencode/fbtest.jpg}
wait_fb() { for _ in $(seq 1 40); do v=$(timeout 3 fastboot getvar version-bootloader 2>&1 | grep -o 'U-Boot.*') && { echo "$v"; return; }; sleep 2; done; echo "no fastboot" >&2; exit 1; }
wait_fb
timeout 30 fastboot boot out/chain.itb >/dev/null 2>&1
sleep 8; v=$(wait_fb); echo "$v"; [[ $v == *-xnu-fb-* ]] || { echo "not the fb U-Boot" >&2; exit 1; }
if [ "$bpp" = mali ] || [ "$bpp" = tri ]; then  # GPU draws; result word survives the reset
	[ "$bpp" = tri ] && arg=tri || arg=ff00ff00
	tools/ubrun.sh "mw.l 0x4ff90000 0 1; mw.l 0x60000000 0 0xe1000; if malitest 60000000 $arg; then mw.l 0x4ff90000 0x600d 1; else mw.l 0x4ff90000 0xbad 1; fi; xnufb 60000000 32; sleep 25; reset"
else
if [ "$bpp" = 16 ]; then r=0xf800f800 g=0x07e007e0 b=0x001f001f n=0x25800 third=0x96000
else r=0x00ff0000 g=0x0000ff00 b=0x000000ff n=0x4b000 third=0x12c000; fi
a2=$(printf '%#x' $((0x60000000 + third))); a3=$(printf '%#x' $((0x60000000 + 2 * third)))  # U-Boot reads hex
tools/ubrun.sh "mw.l 0x60000000 $r $n; mw.l $a2 $g $n; mw.l $a3 $b $n; xnufb 60000000 $bpp; sleep 25; reset"
fi
sleep 14
ffmpeg -loglevel error -y -f v4l2 -i /dev/video0 -vf "select=eq(n\,60)" -frames:v 1 "$out"
echo "frame: $out"
wait_fb
if [ "$bpp" = mali ] || [ "$bpp" = tri ]; then timeout 5 fastboot getvar mem:0x4ff90000 2>&1 | head -1; fi  # 0x600d ok, 0xbad failed

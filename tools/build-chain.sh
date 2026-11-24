#!/bin/bash
# Build the chainloadable U-Boot and wrap it as out/chain.itb, to be flashed to the
# BOOT partition and started by the installed U-Boot's mmcboot (see tools/chain.its).
set -euo pipefail
cd "$(dirname "$0")/.."
root=$PWD

# GCC 14+ rejects this 2020.01 tree by default; restore the old C dialect.
# TAG shows up in "fastboot getvar version-bootloader" to tell builds apart
flags=(CROSS_COMPILE=arm-none-eabi- LOCALVERSION=-xnu${TAG:+-$TAG}
	HOSTCFLAGS="-Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -std=gnu11 -fcommon"
	KCFLAGS="-std=gnu11 -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion")

make -C u-boot "${flags[@]}" midas_defconfig >/dev/null
printf 'CONFIG_MIDAS_CHAINLOAD=y\nCONFIG_CMD_CACHE=y\n' >> u-boot/.config  # dcache off before jumping to XNU
make -C u-boot "${flags[@]}" olddefconfig >/dev/null
grep -q '^CONFIG_MIDAS_CHAINLOAD=y' u-boot/.config
make -C u-boot "${flags[@]}" -j"$(nproc)" -s

mkdir -p out
dtc -q -I dts -O dtb -o out/dummy.dtb tools/dummy.dts
(cd tools && "$root/u-boot/tools/mkimage" -f chain.its "$root/out/chain.itb" >/dev/null)
"$root/u-boot/tools/mkimage" -l out/chain.itb | grep -E "Image 0|Load Address|Entry Point|Data Size"

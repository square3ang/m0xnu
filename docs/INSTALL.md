# m0xnu Build & Installation Guide (Samsung Galaxy S3 3G / SHW-M440S & GT-I9300)

> **Warning**: This procedure overwrites internal eMMC partitions (`boot`, `CACHE`, `SYSTEM`, `USERDATA`) on the **Samsung Galaxy S3 3G (`SHW-M440S` tested / `GT-I9300` untested, Exynos 4412 Midas)**. **Never flash these images on LTE variants (`GT-I9305`, `SHV-E210*`, etc.).** Always back up your `EFS` and bootloader partitions before starting.

---

## 0. Prerequisites

### Hardware
- **Samsung Galaxy S3 3G (`SHW-M440S`, tested)** — Exynos 4412, Midas 3G board
  - *Note for `GT-I9300` (untested)*: Hardware is nearly identical (`m0` / Exynos 4412), but verify your `SYSTEM` and `USERDATA` start LBAs in your PIT (`heimdall print-pit`) against `EMMC_SYSTEM_LBA` / `EMMC_USERDATA_LBA` in `tools/mkxnu.py` before building `out/xnu.itb`.
- Data-capable Micro-USB cable + Linux PC (Arch / Ubuntu / Debian)

### Host Linux Packages
```bash
# Arch Linux example (install equivalent packages on Ubuntu/Debian)
sudo pacman -S base-devel git python python-cryptography clang llvm lld zig \
               arm-none-eabi-gcc arm-none-eabi-binutils dtc uboot-tools \
               android-tools heimdall meson ninja p7zip hfsprogs zstd
```
- **iOS armv7 linker (`arm-apple-darwin11-ld`)**: Place `ld64` from `cctools-port` at `tools/cctools/bin/arm-apple-darwin11-ld` (required by `tools/iosbin/build.sh` and `gpu/ios-gl/build.sh`).
- **iOS 9.3.5 (`13G36`) IPSW** (user-supplied):
  - `iPhone4,1_9.3.5_13G36_Restore.ipsw`
  - Run `python3 tools/fw.py` to fetch and decrypt `fw/kernelcache.n94.macho` and `fw/DeviceTree.n94ap.bin`, and separately extract/decrypt `fw/rootfs.raw` (HFS+ root filesystem image) using public keys from The Apple Wiki (`13G36 (iPhone4,1)`).
- **Wi-Fi firmware (optional)**: `/lib/firmware/brcm/brcmfmac4334-sdio.bin(.zst)` from Linux `linux-firmware`.

---

## 1. Back Up Your Device (Mandatory)

Boot into TWRP (or another recovery with `adb`) and pull critical partitions to your PC:
```bash
mkdir -p backup
adb pull /dev/block/mmcblk0p3 backup/efs.img      # IMEI / MAC calibration (keep private!)
adb pull /dev/block/mmcblk0p4 backup/param.img
adb pull /dev/block/mmcblk0boot0 backup/mmcblk0boot0.img
adb pull /dev/block/mmcblk0boot1 backup/mmcblk0boot1.img
```

---

## 2. Clone Upstream Sources & Apply Patches

### 2.1 U-Boot (Midas Chainloader + `xnufb` + LPM Offline Charging)
```bash
git clone https://github.com/fourkbomb/u-boot.git u-boot
git -C u-boot checkout d78ae52c24500808f16cd9ec788cf4b42af41300
git -C u-boot apply ../patches/u-boot-midas-xnu.patch
```

### 2.2 Mesa Lima (Mali-400 MP4 OpenGL ES 1.1/2.0)
```bash
git clone --filter=blob:none https://gitlab.freedesktop.org/mesa/mesa.git gpu/mesa-lima
git -C gpu/mesa-lima checkout 2860cb498672a6cf5c36202039f9cc3eeed26849
git -C gpu/mesa-lima apply ../../patches/mesa-lima-ios.patch

# Substitute repository root path into Meson cross-file and pkg-config template
sed -i "s|@M0XNU_ROOT@|$PWD|g" \
  gpu/ios-gl/toolchain/ios9-armv7.cross \
  gpu/ios-gl/toolchain/pkgconfig/libdrm.pc
```

---

## 3. Build Components

### 3.1 Chainloadable U-Boot (`out/chain.itb`)
```bash
TAG=standalone bash tools/build-chain.sh
```
Output: `out/chain.itb`
- **Normal power-on**: Auto-boots XNU (`out/xnu.itb`) from the `CACHE` partition (`run xnuboot`), no USB required.
- **Charger plugged in while powered off**: Enters **LPM offline charging mode** (OLED battery gauge with 8 s auto-blank; hold Power ≥ 0.8 s to boot iOS).
- **Hold Volume Down during boot**: Enters `fastboot` mode (recovery escape hatch).

### 3.2 iOS Userspace Daemons (`HorizonSetup`, `ExyTouchd`)
```bash
bash tools/iosbin/build.sh
```
Outputs: `out/iosbin/HorizonSetup`, `out/iosbin/ExyTouchd`, and their `.cdhash` trust-cache files.

### 3.3 Mali-400 OpenGL ES Driver (`Mali400GLDriverEX.bundle`)
```bash
meson setup out/mesa-ios9 gpu/mesa-lima \
  --cross-file gpu/ios-gl/toolchain/ios9-armv7.cross \
  -Dplatforms= -Dgallium-drivers=lima -Dvulkan-drivers= -Dglx=disabled \
  -Degl=disabled -Dgles1=enabled -Dgles2=enabled \
  -Dllvm=disabled -Dbuildtype=release
ninja -C out/mesa-ios9

bash gpu/ios-gl/build.sh
```
Outputs: `out/ios-gl/Mali400GLDriverEX.bundle`, `mali_gld.cdhash`, `mali_test.cdhash`.

---

## 4. Prepare the iOS 9.3.5 Root Filesystem

Copy your decrypted `iPhone4,1` iOS 9.3.5 (`13G36`) HFS+ rootfs to `fw/rootfs-m0xnu.raw`, mount it, and install the `m0xnu` userspace components:

```bash
cp --reflink=auto fw/rootfs.raw fw/rootfs-m0xnu.raw
sudo mkdir -p /tmp/mnt_ios
sudo mount -t hfsplus -o rw,force,loop fw/rootfs-m0xnu.raw /tmp/mnt_ios

# 1) Boot initializer & HID/touch bridge
sudo install -m 0755 out/iosbin/HorizonSetup /tmp/mnt_ios/usr/libexec/HorizonSetup
sudo install -m 0755 out/iosbin/ExyTouchd    /tmp/mnt_ios/usr/libexec/ExyTouchd

# 2) Mali-400 OpenGL ES bundle
sudo cp -a out/ios-gl/Mali400GLDriverEX.bundle /tmp/mnt_ios/System/Library/Extensions/

# 3) (Optional) BCM4334 Wi-Fi firmware & NVRAM
#    Remove unused iPhone 4S BCM4330 firmware first to free space in the 2 GiB HFS+ image
sudo rm -rf /tmp/mnt_ios/usr/share/firmware/wifi/4330b2
sudo mkdir -p /tmp/mnt_ios/usr/share/firmware/wifi
zstd -d -f /lib/firmware/brcm/brcmfmac4334-sdio.bin.zst -o /tmp/brcmfmac4334-sdio.bin
sudo install -m 0644 /tmp/brcmfmac4334-sdio.bin /tmp/mnt_ios/usr/share/firmware/wifi/brcmfmac4334-sdio.bin
#    Optionally edit macaddr= to match your device's EFS MAC before installing
sudo install -m 0644 firmware/brcmfmac4334-sdio.samsung,i9300.txt /tmp/mnt_ios/usr/share/firmware/wifi/brcmfmac4334-sdio.txt

sync
sudo umount /tmp/mnt_ios
```

> **Note (`Setup.app` Initial Setup)**: To complete iOS Initial Setup offline without Apple activation servers, patch `isBricked`, `BuddyActivationLockController::controllerNeedsToRun`, and `deviceHasWiFi` in `Setup.app/Setup` to return `0`, ad-hoc re-sign it with `tools/codesign.py` preserving its original entitlements, and pass its `.cdhash` to `mkxnu.py --trust-cache`.

---

## 5. Build the XNU Kernel FIT Image (`out/xnu.itb`)

```bash
python3 tools/mkxnu.py \
  --emmc-root --emmc-var --fb --disp --gpu \
  --args "cpus=4 serial=3 cs_enforcement_disable=1 amfi_get_out_of_my_way=1" \
  --trust-cache out/iosbin/HorizonSetup.cdhash \
  --trust-cache out/iosbin/ExyTouchd.cdhash \
  --trust-cache out/ios-gl/mali_gld.cdhash \
  --trust-cache out/ios-gl/mali_test.cdhash \
  -o out/xnu.itb
```
*(Include any additional custom-signed `.cdhash` files such as `Setup.cdhash` with `--trust-cache`.)*

---

## 6. Flash to Galaxy S3 3G (`SHW-M440S` / `GT-I9300`)

### 6.1 One-Time Bootstrap: Install Base U-Boot via Heimdall / Odin
1. Enter Samsung Download Mode (**Volume Down + Home + Power**).
2. Flash a non-chainload `fourkbomb/u-boot` build to `BOOT` (or `RECOVERY`) to get a working `fastboot` interface.
3. Boot into `fastboot` and verify with `fastboot devices`.

### 6.2 Partition Flashing (Order Matters)
> **Important**: Always flash `SYSTEM` and `CACHE` (`out/xnu.itb`) **before** flashing `boot` (`out/chain.itb`).

```bash
# 1) Flash rootfs to SYSTEM (convert to Android sparse image first)
img2simg fw/rootfs-m0xnu.raw fw/rootfs-m0xnu.simg
fastboot flash SYSTEM fw/rootfs-m0xnu.simg

# 2) First-time only: flash a blank 1 GiB HFS+ image to USERDATA (/private/var)
#    (Skip this step on upgrades to keep existing iOS user data!)
# fastboot flash USERDATA fw/var.simg

# 3) Flash the XNU FIT image to CACHE
fastboot flash CACHE out/xnu.itb

# 4) Flash the standalone + LPM chainload U-Boot to boot
fastboot flash boot out/chain.itb

# 5) Reboot into standalone iOS 9.3.5
fastboot reboot
```

---

## 7. Daily Usage & Recovery

- **Standalone Boot (No USB)**: Hold Power to boot directly through the `M0XNU` splash screen into iOS 9.3.5.
- **Offline Charging (LPM)**: Plugging in USB/charger while powered off enters LPM charging with a centered battery gauge that auto-blanks after 8 s:
  - Short press **Power** or **Home**: Wake screen to check charge percentage.
  - Hold **Power** (≥ 0.8 s): Boot iOS.
  - Hold **Volume Down** (≥ 0.3 s): Enter `fastboot`.
  - Unplug charger: Clean power-off.
- **Hardware Fastboot Recovery**: Hold **Volume Down** while powering on until the blue LED lights up. If the device is unresponsive, hold **Power + Volume Down for 8 s** to force-reset, then keep holding **Volume Down**.
- **Remote Fastboot from Running iOS**: When connected via USB, the kernel exposes a minimal control endpoint (`18d1:4ee8`, `ExyXNU`). Trigger a clean warm reboot directly into `fastboot` without touching the phone:
  ```bash
  python3 tools/remote-fastboot.py
  ```
- **Kernel Log Dump**: After entering `fastboot`, dump the preserved DRAM ring buffer using the `log=` address printed by `mkxnu.py`:
  ```bash
  python3 tools/xnulog.py 0x4126f8c0 > boot.log
  ```

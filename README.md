# m0xnu — iOS 9.3.5 (XNU) on Samsung Galaxy S3 3G (GT-I9300 / SHW-M440S / Exynos 4412)

> **Experimental bare-metal port of Apple XNU / iOS 9.3.5 (`iPhone4,1`, `13G36`) running natively and standalone from eMMC on the Samsung Galaxy S3 3G (`GT-I9300` / `SHW-M440S`, codename `m0` / `midas`).**

Full Build & Installation Guide: **[docs/INSTALL.md](docs/INSTALL.md)**

---

## Overview

`m0xnu` boots the stock ARMv7 iOS 9.3.5 (`iPhone4,1`) XNU kernelcache and rootfs directly on an Exynos 4412 SoC by patching the kernelcache at build time ([tools/mkxnu.py](tools/mkxnu.py)) and injecting an Exynos 4412 hardware shim layer ([tools/xnushim/](tools/xnushim/)) plus a userspace Mali-400 MP4 OpenGL ES 1.1/2.0 driver built on Mesa Lima ([gpu/ios-gl/](gpu/ios-gl/)).

No Apple, Samsung, or Broadcom proprietary binaries are included in this repository. You must supply your own legal copy of the iPhone 4S iOS 9.3.5 (`13G36`) IPSW and Linux firmware blobs.

---

## Hardware Support Status (`GT-I9300` / `SHW-M440S`)

| Subsystem | Hardware | Status | Implementation |
| --- | --- | --- | --- |
| **SoC / SMP** | Exynos 4412 Quad (Cortex-A9) | Working (`cpus=2`, MCT 24 MHz, GICv1→AIC translation) | [tools/xnushim/shim.c](tools/xnushim/shim.c), [fiq.S](tools/xnushim/fiq.S) |
| **Display** | S6E8AA0 720×1280 AMOLED + FIMD0 + MIPI DSIM0 | Working (zero-copy direct scanout, HW VSYNC latch) | [tools/xnushim/disp.c](tools/xnushim/disp.c), [patches/u-boot-midas-xnu.patch](patches/u-boot-midas-xnu.patch) |
| **GPU (3D / UI)** | Mali-400 MP4 | Working (OpenGL ES 1.1/2.0 via Mesa Lima, SpringBoard/Safari/3D apps) | [tools/xnushim/gpu.c](tools/xnushim/gpu.c), [gpu/ios-gl/](gpu/ios-gl/), [patches/mesa-lima-ios.patch](patches/mesa-lima-ios.patch) |
| **Storage** | Internal eMMC (`dw_mmc` IDMAC) | Working (standalone USB-less boot from `CACHE`/`SYSTEM`/`USERDATA`) | [tools/xnushim/touch.c](tools/xnushim/touch.c), [tools/build-chain.sh](tools/build-chain.sh) |
| **Touch & Keys** | Melfas MMS114 + Home/Power/Vol+/Vol- | Working (`ExyTouchd` HID bridge + Vol HUD) | [tools/xnushim/touch.c](tools/xnushim/touch.c), [tools/iosbin/touchd.c](tools/iosbin/touchd.c) |
| **Power / Thermal** | MAX77686 PMIC + MAX17047 FG + MAX77693 + TMU | Dynamic CPU/GPU DVFS (`466..1400 MHz` / `146..440 MHz`), `IOPMPowerSource`, U-Boot LPM offline charging | [tools/xnushim/drivers.c](tools/xnushim/drivers.c), [patches/u-boot-midas-xnu.patch](patches/u-boot-midas-xnu.patch) |
| **Audio** | Wolfson WM1811 + Exynos EPLL/AUDSS/I2S0 | Experimental (`IOAudio2Device` kernel bridge → 48 kHz stereo I2S0 + WM1811) | [tools/xnushim/audio.c](tools/xnushim/audio.c), [tools/xnushim/drivers.c](tools/xnushim/drivers.c) |
| **Wi-Fi** | Broadcom BCM4334 (SDHCI3) | Experimental (SDHCI3 CMD52/53 + FullMAC FW loader + BSD `en0`) | [tools/xnushim/wifi.c](tools/xnushim/wifi.c), [firmware/](firmware/) |
| **Sensors / Misc** | LSM330DLC, AK8975C, CM36651, AN30259A LED, Haptic, Torch, RTC | Working | [tools/xnushim/drivers.c](tools/xnushim/drivers.c) |
| **Remote Recovery** | Exynos DWC2 USB OTG | Remote fastboot control endpoint (`18d1:4ee8`) | [tools/remote-fastboot.py](tools/remote-fastboot.py) |
| **Cellular / Baseband** | Intel XMM6262 | Not implemented | — |

---

## Repository Layout

```text
tools/
  mkxnu.py              # Patches iPhone4,1 kernelcache + DT, embeds xnushim, packs U-Boot FIT (.itb)
  codesign.py           # Ad-hoc Mach-O code signer & CDHash generator for XNU trust cache
  build-chain.sh        # Builds chainloadable U-Boot FIT (out/chain.itb) with LPM & standalone xnuboot
  remote-fastboot.py    # Triggers clean WDT reboot to fastboot from running iOS via USB 18d1:4ee8
  xnushim/              # Bare-metal ARM/Thumb Exynos 4412 kernel shim drivers
    shim.c, fiq.S, taps.S, disp.c, gpu.c, touch.c, drivers.c, audio.c, wifi.c
  iosbin/               # iOS 9 armv7 userspace daemons (HorizonSetup, ExyTouchd)
gpu/
  mali400/              # Low-level Mali-400 MP4 hardware job/MMU driver
  ios-gl/               # Mesa Lima OpenGL ES bundle (Mali400GLDriverEX.bundle)
patches/
  u-boot-midas-xnu.patch # Diff against fourkbomb/u-boot (d78ae52c)
  mesa-lima-ios.patch    # Diff against mesa/mesa (2860cb49)
firmware/
  brcmfmac4334-sdio.samsung,i9300.txt  # Template NVRAM (placeholder MAC)
notes/
  display-driver.md     # Bring-up notes for FIMD0 / IOMobileFramebuffer
docs/
  INSTALL.md            # Step-by-step build & installation guide (KR/EN)
```

---

## Legal & Safety Notice

- **No proprietary files included**: This repo contains **zero** Apple iOS binaries, Samsung SBOOT/EFS images, or Broadcom firmware binaries.
- **Brick / hardware risk**: Flashing `boot`, `CACHE`, `SYSTEM`, and `USERDATA` partitions can permanently brick your device or wipe all personal data. **Always back up `efs.img`, `param.img`, `mmcblk0boot0/1`, and `pit.txt` first.**
- **License**: Released under the **GNU General Public License v3.0** ([LICENSE](LICENSE)). Upstream patches in `patches/` preserve their respective compatible licenses (`u-boot-midas-xnu.patch`: GPL-2.0+, `mesa-lima-ios.patch`: MIT).

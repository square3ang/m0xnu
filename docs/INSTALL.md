# m0xnu 설치 및 빌드 가이드 (GT-I9300 / Galaxy S3 3G)

> **경고 / Warning**: 이 작업은 Samsung Galaxy S3 3G (`GT-I9300`, Exynos 4412)의 내부 eMMC 파티션(`boot`, `CACHE`, `SYSTEM`, `USERDATA`)을 덮어씁니다. **LTE 모델(`SHV-E210*`, `GT-I9305`)에는 절대 사용하지 마세요.** 작업 전 반드시 `EFS`와 부트로더 파티션을 백업하세요.

---

## 0. 준비물 (Prerequisites)

### 하드웨어
- **Samsung Galaxy S3 3G (`GT-I9300`)** — Exynos 4412, Midas 보드
- 데이터 전송용 Micro-USB 케이블 + Linux PC (Ubuntu / Arch 등)

### 호스트 Linux 패키지
```bash
# Arch Linux 예시 (Ubuntu/Debian은 대응 패키지 설치)
sudo pacman -S base-devel git python clang lld arm-none-eabi-gcc arm-none-eabi-binutils \
               dtc uboot-tools android-tools heimdall meson ninja p7zip hfsprogs zstd
```
- **iOS armv7 링커 (`arm-apple-darwin11-ld`)**: `cctools-port`의 `ld64`를 `tools/cctools/bin/arm-apple-darwin11-ld`에 배치해야 `tools/iosbin/build.sh` 및 `gpu/ios-gl/build.sh`가 동작합니다.
- **iOS 9.3.5 (`13G36`) IPSW** (직접 준비):
  - `iPhone4,1_9.3.5_13G36_Restore.ipsw` (Apple 공식 배포본)
  - 복호화된 커널캐시(`fw/kernelcache.n94.macho`), 디바이스 트리(`fw/DeviceTree.n94ap.bin`), 루트FS HFS+ 이미지(`fw/rootfs.raw`) 필요 (`dmg` / `img3` 복호화 키는 The Apple Wiki `13G36 (iPhone4,1)` 참고).
- **Wi-Fi 펌웨어 (선택)**: Linux `linux-firmware`의 `/lib/firmware/brcm/brcmfmac4334-sdio.bin(.zst)`

---

## 1. 원본 기기 백업 (필수!)

TWRP 등 리커버리에서 `adb`로 아래 파티션을 반드시 PC에 백업해 두세요:
```bash
mkdir -p backup
adb pull /dev/block/mmcblk0p3 backup/efs.img      # IMEI / MAC 정보 (절대 유출 금지)
adb pull /dev/block/mmcblk0p4 backup/param.img
adb pull /dev/block/mmcblk0boot0 backup/mmcblk0boot0.img
adb pull /dev/block/mmcblk0boot1 backup/mmcblk0boot1.img
```

---

## 2. 업스트림 소스 클론 및 패치 적용

### 2-1. U-Boot (Midas chainloader + `xnufb` + LPM 충전모드)
```bash
git clone https://github.com/fourkbomb/u-boot.git u-boot
git -C u-boot checkout d78ae52c24500808f16cd9ec788cf4b42af41300
git -C u-boot apply ../patches/u-boot-midas-xnu.patch
```

### 2-2. Mesa Lima (Mali-400 MP4 OpenGL ES 1.1/2.0)
```bash
git clone --filter=blob:none https://gitlab.freedesktop.org/mesa/mesa.git gpu/mesa-lima
git -C gpu/mesa-lima checkout 2860cb498672a6cf5c36202039f9cc3eeed26849
git -C gpu/mesa-lima apply ../../patches/mesa-lima-ios.patch

# 툴체인 파일의 @M0XNU_ROOT@ 치환
sed -i "s|@M0XNU_ROOT@|$PWD|g" \
  gpu/ios-gl/toolchain/ios9-armv7.cross \
  gpu/ios-gl/toolchain/pkgconfig/libdrm.pc
```

---

## 3. 컴포넌트 빌드

### 3-1. U-Boot 체인로더 (`out/chain.itb`)
```bash
TAG=standalone bash tools/build-chain.sh
```
- 생성물: `out/chain.itb`
  - 일반 전원 켜기: `CACHE` 파티션에서 XNU(`out/xnu.itb`) 자동 부팅 (`run xnuboot`, USB 불필요)
  - 전원 꺼진 상태에서 충전기 연결: **LPM 오프라인 충전 모드** 진입 (OLED 배터리 게이지·8초 자동 소등, 전원 0.8초 길게 눌러 iOS 부팅)
  - **볼륨 아래(Vol-Down)** 누른 채 전원 켜기: `fastboot` 진입 (비상 복구용)

### 3-2. iOS 유저스페이스 데몬 (`HorizonSetup`, `ExyTouchd`)
```bash
bash tools/iosbin/build.sh
```
- 생성물: `out/iosbin/HorizonSetup`, `out/iosbin/ExyTouchd` + 각각의 `.cdhash`

### 3-3. Mali-400 OpenGL ES 드라이버 (`Mali400GLDriverEX.bundle`)
```bash
# Mesa Lima ARMv7-iOS9 정적 빌드 (out/mesa-ios9) 후 번들 링크
meson setup out/mesa-ios9 gpu/mesa-lima \
  --cross-file gpu/ios-gl/toolchain/ios9-armv7.cross \
  -Dplatforms= -Dgallium-drivers=lima -Dvulkan-drivers= -Dglx=disabled \
  -Degl=disabled -Dgles1=enabled -Dgles2=enabled -Dshared-glapi=disabled \
  -Dllvm=disabled -Dbuildtype=release
ninja -C out/mesa-ios9

bash gpu/ios-gl/build.sh
```
- 생성물: `out/ios-gl/Mali400GLDriverEX.bundle`, `mali_gld.cdhash`, `mali_test.cdhash`

---

## 4. iOS 9.3.5 루트FS (`fw/rootfs.raw`) 준비

복호화한 `iPhone4,1` iOS 9.3.5 (`13G36`) 루트FS HFS+ 이미지를 `fw/rootfs-m0xnu.raw`로 복사한 뒤 마운트해 아래 파일을 설치합니다:

```bash
cp --reflink=auto fw/rootfs.raw fw/rootfs-m0xnu.raw
sudo mkdir -p /tmp/mnt_ios
sudo mount -t hfsplus -o rw,force,loop fw/rootfs-m0xnu.raw /tmp/mnt_ios

# 1) 부팅 초기화 & HID/터치 데몬 설치
sudo install -m 0755 out/iosbin/HorizonSetup /tmp/mnt_ios/usr/libexec/HorizonSetup
sudo install -m 0755 out/iosbin/ExyTouchd    /tmp/mnt_ios/usr/libexec/ExyTouchd

# 2) Mali-400 OpenGL ES 번들 설치
sudo cp -a out/ios-gl/Mali400GLDriverEX.bundle /tmp/mnt_ios/System/Library/Extensions/

# 3) (선택) BCM4334 Wi-Fi 펌웨어 & NVRAM 설치
#    ※ 공간 확보를 위해 미사용 iPhone 4S용 4330b2 펌웨어 제거
sudo rm -rf /tmp/mnt_ios/usr/share/firmware/wifi/4330b2
sudo mkdir -p /tmp/mnt_ios/usr/share/firmware/wifi
zstd -d -f /lib/firmware/brcm/brcmfmac4334-sdio.bin.zst -o /tmp/brcmfmac4334-sdio.bin
sudo install -m 0644 /tmp/brcmfmac4334-sdio.bin /tmp/mnt_ios/usr/share/firmware/wifi/brcmfmac4334-sdio.bin
#    본인 기기 EFS의 MAC 주소를 넣으려면 macaddr= 줄 수정 후 설치
sudo install -m 0644 firmware/brcmfmac4334-sdio.samsung,i9300.txt /tmp/mnt_ios/usr/share/firmware/wifi/brcmfmac4334-sdio.txt

sync
sudo umount /tmp/mnt_ios
```

> **참고 (초기 셋업 `Setup.app`)**: 활성화 서버 없이 오프라인으로 초기 설정 화면을 통과하려면 `Setup.app/Setup` 바이너리의 `isBricked`, `BuddyActivationLockController::controllerNeedsToRun`, `deviceHasWiFi` 반환값을 0으로 패치한 뒤 원본 entitlements를 유지해 `tools/codesign.py`로 서명하고 `.cdhash`를 `mkxnu.py --trust-cache`에 추가합니다.

---

## 5. XNU 커널 FIT 이미지 빌드 (`out/xnu.itb`)

```bash
python3 tools/mkxnu.py \
  --emmc-root --emmc-var --fb --disp --gpu \
  --args "cpus=2 serial=3 cs_enforcement_disable=1 amfi_get_out_of_my_way=1" \
  --trust-cache out/iosbin/HorizonSetup.cdhash \
  --trust-cache out/iosbin/ExyTouchd.cdhash \
  --trust-cache out/ios-gl/mali_gld.cdhash \
  --trust-cache out/ios-gl/mali_test.cdhash \
  -o out/xnu.itb
```
*(커스텀 서명한 `Setup.cdhash` 등이 있다면 `--trust-cache`로 함께 추가)*

---

## 6. Galaxy S3 (`GT-I9300`) 플래싱 절차

### 6-1. 최초 1회: Odin/Heimdall로 U-Boot 설치
1. 기기를 Download Mode (**볼륨 아래 + 홈 + 전원**)로 진입.
2. 메인라인 `fourkbomb/u-boot` (non-chainload)를 `BOOT` 또는 `RECOVERY`에 설치해 `fastboot` 진입 환경을 만듭니다.
3. 기기를 `fastboot` 모드로 진입 (`fastboot devices` 확인).

### 6-2. 파티션 플래싱 (순서 중요!)
> **중요**: `boot` 파티션(`out/chain.itb`)은 반드시 `CACHE`(`out/xnu.itb`)와 `SYSTEM` 플래싱이 끝난 **마지막에** 플래싱하세요.

```bash
# 1) 루트FS를 SYSTEM 파티션에 플래싱 (sparse 이미지 변환 후 플래싱 권장)
img2simg fw/rootfs-m0xnu.raw fw/rootfs-m0xnu.simg
fastboot flash SYSTEM fw/rootfs-m0xnu.simg

# 2) 빈 1 GiB HFS+ 이미지를 USERDATA(/private/var)에 최초 1회 플래싱
#    (이미 iOS 데이터가 있다면 USERDATA는 다시 플래싱하지 마세요!)
# fastboot flash USERDATA fw/var.simg

# 3) XNU 커널 이미지를 CACHE 파티션에 플래싱
fastboot flash CACHE out/xnu.itb

# 4) 독립 부팅 + LPM 충전 U-Boot 체인로더를 boot 파티션에 플래싱
fastboot flash boot out/chain.itb

# 5) 재부팅!
fastboot reboot
```

---

## 7. 사용 및 복구 팁

- **일반 부팅 (USB 불필요)**: 전원 버튼을 길게 누르면 `M0XNU` 스플래시 후 iOS 9.3.5로 직접 부팅됩니다.
- **전원 꺼진 채 충전 (LPM)**: 전원이 꺼진 상태에서 충전기/USB를 꽂으면 OLED 배터리 게이지가 8초간 표시된 뒤 자동 소등되며 충전합니다.
  - 전원/홈 짧게 누르기: 충전률 재확인
  - 전원 0.8초 길게 누르기: iOS 부팅
  - 충전기 분리: 자동 전원 종료
- **fastboot 강제 진입 (물리 버튼)**: 재부팅/전원 켜기 시 **볼륨 아래(Vol-Down)** 버튼을 파란 LED가 켜질 때까지 길게 누르세요 (화면 응답 없을 땐 **전원 + 볼륨 아래 8초** 강제 재부팅 후 볼륨 아래만 유지).
- **원격 fastboot 진입 (iOS 실행 중)**: USB 연결 시 호스트에 `18d1:4ee8 (ExyXNU)` 장치가 열거되며, 아래 명령으로 버튼을 누르지 않고 즉시 fastboot로 재부팅할 수 있습니다:
  ```bash
  python3 tools/remote-fastboot.py
  ```
- **커널 로그 덤프**: fastboot 진입 후 `mkxnu.py` 출력의 `log=0x4126f8c0` 주소로 덤프:
  ```bash
  python3 tools/xnulog.py 0x4126f8c0 > boot.log
  ```

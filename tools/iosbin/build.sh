#!/bin/bash
# Build and ad-hoc sign the iOS 9 boot helpers (same toolchain as gpu/ios-gl/build.sh);
# their CDHashes go to mkxnu --trust-cache.
set -euo pipefail
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DIR/../.." && pwd)
LD="$ROOT/tools/cctools/bin/arm-apple-darwin11-ld"
OUT="$ROOT/out/iosbin"
mkdir -p "$OUT"

CFLAGS=(--target=armv7-apple-ios9.0 -mthumb -Os -Wall -Werror -fno-stack-protector)
LDFLAGS=(-arch armv7 -ios_version_min 9.0)

# Stub libSystem so ld64 emits the right LC_LOAD_DYLIB without an Apple SDK.
echo 'void _k(void){}' > "$OUT/kern.c"
clang "${CFLAGS[@]}" -c "$OUT/kern.c" -o "$OUT/kern.o"
"$LD" "${LDFLAGS[@]}" -dylib -install_name /usr/lib/system/libsystem_kernel.dylib -o "$OUT/libsystem_kernel.dylib" "$OUT/kern.o"
cat > "$OUT/libSystem.c" <<'EOF'
void execv(void){} void umask(void){} void sem_open(void){}
void fork(void){} void waitpid(void){} void _exit(void){} void mkdir(void){} void chown(void){} void chmod(void){}
void symlink(void){} void gettimeofday(void){} void settimeofday(void){}
void open(void){} void write(void){} void close(void){} void setsid(void){} void unlink(void){}
void usleep(void){} void snprintf(void){} void strlen(void){} void memset(void){} void memcpy(void){}
void mach_absolute_time(void){} int mach_task_self_; int bootstrap_port;
void mach_thread_self(void){} void thread_policy_set(void){} void setpriority(void){}
void bootstrap_look_up(void){} void dlopen(void){} void dlsym(void){}
__asm__(".align 2\n.globl dyld_stub_binder\ndyld_stub_binder:\n bx lr\n");
EOF
clang "${CFLAGS[@]}" -fno-builtin -c "$OUT/libSystem.c" -o "$OUT/libSystem.o"
"$LD" "${LDFLAGS[@]}" -dylib -install_name /usr/lib/libSystem.B.dylib -o "$OUT/libSystem.B.dylib" "$OUT/libSystem.o" "$OUT/libsystem_kernel.dylib"

cat > "$OUT/IOKit.c" <<'EOF'
int kIOMasterPortDefault;
void IOServiceMatching(void){} void IOServiceGetMatchingService(void){} void IOObjectRelease(void){}
void IORegistryEntryGetRegistryEntryID(void){}
void IOServiceOpen(void){} void IOConnectCallStructMethod(void){} void IOConnectCallScalarMethod(void){}
void IOHIDUserDeviceCreate(void){} void IOHIDUserDeviceHandleReport(void){}
void IOHIDEventSystemClientCreate(void){} void IOHIDEventSystemClientCreateWithType(void){} void IOHIDEventSystemClientDispatchEvent(void){}
void IOHIDEventSystemClientCopyServices(void){} void IOHIDServiceClientGetRegistryID(void){}
void IOHIDEventCreateDigitizerEvent(void){} void IOHIDEventCreateDigitizerFingerEvent(void){}
void IOHIDEventCreateKeyboardEvent(void){} void IOHIDEventAppendEvent(void){}
void IOHIDEventSetIntegerValue(void){} void IOHIDEventSetSenderID(void){}
EOF
clang "${CFLAGS[@]}" -c "$OUT/IOKit.c" -o "$OUT/IOKit.o"
"$LD" "${LDFLAGS[@]}" -dylib -install_name /System/Library/Frameworks/IOKit.framework/Versions/A/IOKit \
  -o "$OUT/IOKit.dylib" "$OUT/IOKit.o" "$OUT/libSystem.B.dylib"

cat > "$OUT/CoreFoundation.c" <<'EOF'
void *kCFBooleanTrue;
int kCFTypeDictionaryKeyCallBacks;
int kCFTypeDictionaryValueCallBacks;
void CFDictionaryCreateMutable(void){} void CFDictionarySetValue(void){}
void CFDataCreate(void){} void CFStringCreateWithCString(void){}
void CFNumberCreate(void){} void CFNumberGetValue(void){}
void CFArrayGetCount(void){} void CFArrayGetValueAtIndex(void){}
void CFRelease(void){}
EOF
clang "${CFLAGS[@]}" -c "$OUT/CoreFoundation.c" -o "$OUT/CoreFoundation.o"
"$LD" "${LDFLAGS[@]}" -dylib -install_name /System/Library/Frameworks/CoreFoundation.framework/CoreFoundation \
  -o "$OUT/CoreFoundation.dylib" "$OUT/CoreFoundation.o" "$OUT/libSystem.B.dylib"

clang "${CFLAGS[@]}" -c "$DIR/horizon.c" -o "$OUT/horizon.o"
"$LD" "${LDFLAGS[@]}" -execute -pie -o "$OUT/HorizonSetup" "$OUT/horizon.o" "$OUT/libSystem.B.dylib"
python3 "$ROOT/tools/codesign.py" "$OUT/HorizonSetup" --ident com.exy.HorizonSetup > "$OUT/HorizonSetup.cdhash"
echo "HorizonSetup cdhash: $(cat "$OUT/HorizonSetup.cdhash")"

clang "${CFLAGS[@]}" -c "$DIR/touchd.c" -o "$OUT/touchd.o"
"$LD" "${LDFLAGS[@]}" -execute -pie -o "$OUT/ExyTouchd" "$OUT/touchd.o" \
  "$OUT/libSystem.B.dylib" "$OUT/IOKit.dylib" "$OUT/CoreFoundation.dylib"
python3 "$ROOT/tools/codesign.py" "$OUT/ExyTouchd" --ident com.exy.ExyTouchd --ent "$DIR/touchd.ent" > "$OUT/ExyTouchd.cdhash"
echo "ExyTouchd cdhash:    $(cat "$OUT/ExyTouchd.cdhash")"

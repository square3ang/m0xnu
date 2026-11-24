#!/bin/bash
# Cross-compile and ad-hoc sign armv7 iOS 9 userspace binaries on Linux (clang + cctools ld64).
set -euo pipefail
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DIR/../.." && pwd)
LD="$ROOT/tools/cctools/bin/arm-apple-darwin11-ld"
OUT="$ROOT/out/ios-gl"
STUB="$OUT/stubs"
BUNDLE="$OUT/Mali400GLDriverEX.bundle"
mkdir -p "$STUB" "$BUNDLE"

CFLAGS=(--target=armv7-apple-ios9.0 -mcpu=cortex-a9 -mfpu=neon -mthumb -O3 -DNDEBUG -Wall -Werror -fno-stack-protector)
LDFLAGS=(-arch armv7 -ios_version_min 9.0)

# Minimal stub dylibs so ld64 emits exact LC_LOAD_DYLIB paths without an Apple SDK or libtapi.
echo 'void _k(void){}' > "$STUB/kern.c"
clang "${CFLAGS[@]}" -c "$STUB/kern.c" -o "$STUB/kern.o"
"$LD" "${LDFLAGS[@]}" -dylib -install_name /usr/lib/system/libsystem_kernel.dylib \
  -o "$STUB/libsystem_kernel.dylib" "$STUB/kern.o"

cat > "$STUB/libSystem.c" <<'EOF'
void calloc(void){} void free(void){} void malloc(void){} void memcpy(void){}
void memset(void){} void memset_pattern16(void){} void printf(void){} void puts(void){} void snprintf(void){} void strcmp(void){}
void strlen(void){} void strncpy(void){} void strstr(void){} void mach_absolute_time(void){} void getprogname(void){} void fork(void){} void execv(void){} void _exit(void){} void setsid(void){} void dlopen(void){} void dlsym(void){} void dlerror(void){} void dladdr(void){} int mach_task_self_;
/* Bind ARM compiler-rt & unwind helpers two-level to libSystem.B.dylib so legacy iOS 4-6 apps linking libgcc_s.1.dylib never fail flat-namespace lookup. */
void __divdi3(void){} void __divsi3(void){} void __moddi3(void){} void __modsi3(void){}
void __udivdi3(void){} void __udivsi3(void){} void __umoddi3(void){} void __umodsi3(void){}
void __fixdfdi(void){} void __fixsfdi(void){} void __fixunsdfdi(void){} void __fixunssfdi(void){}
void __floatdidf(void){} void __floatdisf(void){} void __floatundidf(void){} void __floatundisf(void){}
void _Unwind_SjLj_Register(void){} void _Unwind_SjLj_Resume(void){} void _Unwind_SjLj_Unregister(void){}
__asm__(".align 2\n.globl dyld_stub_binder\ndyld_stub_binder:\n bx lr\n");
EOF
clang "${CFLAGS[@]}" -fno-builtin -c "$STUB/libSystem.c" -o "$STUB/libSystem.o"
"$LD" "${LDFLAGS[@]}" -dylib -install_name /usr/lib/libSystem.B.dylib \
  -o "$STUB/libSystem.B.dylib" "$STUB/libSystem.o" "$STUB/libsystem_kernel.dylib"

cat > "$STUB/IOKit.c" <<'EOF'
int kIOMasterPortDefault;
void IOConnectCallMethod(void){} void IOConnectCallScalarMethod(void){}
void IOConnectCallStructMethod(void){} void IOConnectMapMemory(void){}
void IOObjectRelease(void){} void IOServiceClose(void){}
void IOServiceGetMatchingService(void){} void IOServiceMatching(void){} void IOServiceOpen(void){}
EOF
clang "${CFLAGS[@]}" -c "$STUB/IOKit.c" -o "$STUB/IOKit.o"
"$LD" "${LDFLAGS[@]}" -dylib -install_name /System/Library/Frameworks/IOKit.framework/Versions/A/IOKit \
  -o "$STUB/IOKit.dylib" "$STUB/IOKit.o" "$STUB/libSystem.B.dylib"

LIBS=("$STUB/libSystem.B.dylib" "$STUB/IOKit.dylib")

clang "${CFLAGS[@]}" -c "$DIR/mali_test.c" -o "$OUT/mali_test.o"
"$LD" "${LDFLAGS[@]}" -execute -pie -o "$OUT/mali_test" "$OUT/mali_test.o" "${LIBS[@]}"
python3 "$ROOT/tools/codesign.py" "$OUT/mali_test" --ident com.exy.mali_test > "$OUT/mali_test.cdhash"

clang "${CFLAGS[@]}" -fPIC -c "$DIR/mali_gld.c" -o "$OUT/mali_gld.o"
clang "${CFLAGS[@]}" -fPIC -c "$DIR/pvrtc.c" -o "$OUT/pvrtc.o"
clang "${CFLAGS[@]}" -fPIC -c "$DIR/gl_tramp.S" -o "$OUT/gl_tramp.o"
"$DIR/toolchain/ios9-cc" -O3 -DNDEBUG -Wall -Werror -fno-stack-protector -fPIC -I"$ROOT/gpu/mesa-lima/include" -I"$ROOT/gpu/mesa-lima/include/drm-uapi" -c "$DIR/drm_mali.c" -o "$OUT/drm_mali.o"
"$DIR/toolchain/ios9-cc" -O3 -DNDEBUG -DHAVE_FUNC_ATTRIBUTE_UNUSED -DHAVE_FUNC_ATTRIBUTE_PACKED -DMESA_DEBUG=0 -DHAVE_LIMA -DBUILDING_MESA -Wall -Werror -fno-stack-protector -fPIC \
  -DHAVE_PTHREAD -DHAVE_STRUCT_TIMESPEC -DHAVE_OPENGL=0 -DHAVE_OPENGL_ES_1=1 -DHAVE_OPENGL_ES_2=1 \
  -I"$ROOT/out/mesa-ios9/src/mesa" -I"$ROOT/gpu/mesa-lima/src/mesa" \
  -I"$ROOT/out/mesa-ios9/include" -I"$ROOT/gpu/mesa-lima/include" \
  -I"$ROOT/out/mesa-ios9/src" -I"$ROOT/gpu/mesa-lima/src" \
  -I"$ROOT/gpu/mesa-lima/src/gallium/include" \
  -I"$ROOT/gpu/mesa-lima/src/gallium/drivers/lima" \
  -I"$ROOT/out/mesa-ios9/src/gallium/auxiliary" -I"$ROOT/gpu/mesa-lima/src/gallium/auxiliary" \
  -I"$ROOT/out/mesa-ios9/src/mesa/glapi/glapi/gen" \
  -I"$ROOT/out/mesa-ios9/src/util" -I"$ROOT/out/mesa-ios9/src/compiler/nir" \
  -c "$DIR/gles_mesa.c" -o "$OUT/gles_mesa.o"

# Stub libc++.1, libz.1, libobjc.A, IOSurface, CoreFoundation so dyld resolves exact iOS 9 dylibs.
cat > "$STUB/libcpp.c" <<'EOF'
void _ZNSt3__112__next_primeEm(void){} void _ZNSt3__15mutex4lockEv(void){}
void _ZNSt3__15mutex6unlockEv(void){} void _ZNSt3__15mutexD1Ev(void){}
EOF
clang "${CFLAGS[@]}" -c "$STUB/libcpp.c" -o "$STUB/libcpp.o"
"$LD" "${LDFLAGS[@]}" -dylib -install_name /usr/lib/libc++.1.dylib \
  -o "$STUB/libc++.1.dylib" "$STUB/libcpp.o" "$STUB/libSystem.B.dylib"

cat > "$STUB/libz.c" <<'EOF'
void crc32(void){} void deflate(void){} void deflateInit_(void){} void deflateEnd(void){}
void inflate(void){} void inflateInit_(void){} void inflateEnd(void){} void compress(void){} void uncompress(void){}
EOF
clang "${CFLAGS[@]}" -c "$STUB/libz.c" -o "$STUB/libz.o"
"$LD" "${LDFLAGS[@]}" -dylib -install_name /usr/lib/libz.1.dylib \
  -o "$STUB/libz.1.dylib" "$STUB/libz.o" "$STUB/libSystem.B.dylib"

cat > "$STUB/libobjc.c" <<'EOF'
void objc_getClass(void){} void sel_registerName(void){} void class_getInstanceMethod(void){} void class_getClassMethod(void){}
void method_getImplementation(void){} void method_setImplementation(void){} void objc_msgSend(void){}
EOF
clang "${CFLAGS[@]}" -c "$STUB/libobjc.c" -o "$STUB/libobjc.o"
"$LD" "${LDFLAGS[@]}" -dylib -install_name /usr/lib/libobjc.A.dylib \
  -o "$STUB/libobjc.A.dylib" "$STUB/libobjc.o" "$STUB/libSystem.B.dylib"

cat > "$STUB/IOSurface.c" <<'EOF'
void IOSurfaceLock(void){} void IOSurfaceUnlock(void){} void IOSurfaceGetBaseAddress(void){}
void IOSurfaceGetBaseAddressOfPlane(void){} void IOSurfaceGetBytesPerRow(void){}
void IOSurfaceGetBytesPerRowOfPlane(void){} void IOSurfaceGetPlaneCount(void){}
void IOSurfaceGetWidth(void){} void IOSurfaceGetHeight(void){}
EOF
clang "${CFLAGS[@]}" -c "$STUB/IOSurface.c" -o "$STUB/IOSurface.o"
"$LD" "${LDFLAGS[@]}" -dylib -install_name /System/Library/PrivateFrameworks/IOSurface.framework/IOSurface \
  -o "$STUB/IOSurface.dylib" "$STUB/IOSurface.o" "$STUB/libSystem.B.dylib"

cat > "$STUB/CoreFoundation.c" <<'EOF'
void CFRetain(void){} void CFRelease(void){}
EOF
clang "${CFLAGS[@]}" -c "$STUB/CoreFoundation.c" -o "$STUB/CoreFoundation.o"
"$LD" "${LDFLAGS[@]}" -dylib -install_name /System/Library/Frameworks/CoreFoundation.framework/CoreFoundation \
  -o "$STUB/CoreFoundation.dylib" "$STUB/CoreFoundation.o" "$STUB/libSystem.B.dylib"

MESA="$ROOT/out/mesa-ios9"
MESA_LIBS=(
  "$MESA/src/gallium/drivers/lima/liblima.a"
  "$MESA/src/panfrost/shared/libpanfrost_shared.a"
  "$MESA/src/gallium/auxiliary/libgallium.a"
  "$MESA/src/mesa/libmesa.a"
  "$MESA/src/mesa/glapi/shared-glapi/libglapi.a"
  "$MESA/src/compiler/glsl/libglsl.a"
  "$MESA/src/compiler/glsl/glcpp/libglcpp.a"
  "$MESA/src/compiler/spirv/libvtn.a"
  "$MESA/src/compiler/nir/libnir.a"
  "$MESA/src/compiler/libcompiler.a"
  "$MESA/src/util/libmesa_util.a"
  "$MESA/src/util/blake3/libblake3.a"
  "$MESA/src/c11/impl/libmesa_util_c11.a"
)

"$LD" "${LDFLAGS[@]}" -bundle -undefined dynamic_lookup -dead_strip \
  -o "$BUNDLE/Mali400GLDriverEX" \
  "$OUT/mali_gld.o" "$OUT/pvrtc.o" "$OUT/gles_mesa.o" "$OUT/gl_tramp.o" "$OUT/drm_mali.o" "${MESA_LIBS[@]}" \
  "${LIBS[@]}" "$STUB/libc++.1.dylib" "$STUB/libz.1.dylib" "$STUB/libobjc.A.dylib" \
  "$STUB/IOSurface.dylib" "$STUB/CoreFoundation.dylib"
llvm-strip -x "$BUNDLE/Mali400GLDriverEX"
python3 "$ROOT/tools/codesign.py" "$BUNDLE/Mali400GLDriverEX" --ident com.exy.Mali400GLDriverEX > "$OUT/mali_gld.cdhash"

cat > "$BUNDLE/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>Mali400GLDriverEX</string>
  <key>CFBundleIdentifier</key><string>com.exy.Mali400GLDriverEX</string>
  <key>CFBundlePackageType</key><string>BNDL</string>
  <key>CFBundleVersion</key><string>1.0</string>
  <key>MinimumOSVersion</key><string>9.0</string>
</dict></plist>
EOF

echo "mali_test cdhash:         $(cat "$OUT/mali_test.cdhash")"
echo "Mali400GLDriverEX cdhash: $(cat "$OUT/mali_gld.cdhash")"

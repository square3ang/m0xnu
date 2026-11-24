#!/usr/bin/env python3
"""Build out/xnu.itb: a FIT that the chainloaded U-Boot runs via "fastboot boot".

The FIT holds one flat memory image (kernel segments laid out at their physical
addresses, DeviceTree, boot_args, trampoline, mailbox) and a U-Boot script that
extracts it, turns caches/MMU off and jumps to the trampoline. The trampoline
arms the Exynos watchdog, marks the mailbox and enters XNU like iBoot would
(MMU off, r0 = boot_args).

Bring-up without UART or display: "--cp VADDR:a|t" patches a call to a stub at
that kernel address (ARM or Thumb). The stub stores lr/sp/cpsr in the mailbox and
spins until the watchdog resets the board; afterwards read the mailbox with
"fastboot getvar mem:<MAILBOX>". "--stop" never enters XNU (tests the plumbing).

Usage: mkxnu.py [--cp 0x800c70f8:a ...] [--stop] [--args "-v cpus=1"] [-o out/xnu.itb]
"""
import argparse, hashlib, os, struct, subprocess, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KERNEL = os.path.join(ROOT, "fw/kernelcache.n94.macho")
DTREE = os.path.join(ROOT, "fw/DeviceTree.n94ap.bin")
MKIMAGE = os.path.join(ROOT, "u-boot/tools/mkimage")

PHYS, VIRT = 0x40000000, 0x80000000
MEMSIZE = 0x1EF00000          # 495 MiB XNU RAM (0x40000000..0x5EF00000), below WORK_PA (0x5F000000) & FB_PA (0x60000000)
MAGIC = 0x584E5542            # "BUNX" in the mailbox: written by our code, not leftovers
WDT = 0x10060000              # Exynos4412 watchdog: WTCON, WTDAT, WTCNT
MCT = 0x10050000
PMU_WDT_MASKS = 0x10020408
GICD_PA = 0x10490000
WDT_SECONDS = 21              # nominal (max ~21.4); measured: expires after 42 s (MCT), PCLK must be ~50 MHz.
                              # On expiry CMU/PMU/MCT reset first, the CPUs some seconds later. The shim
                              # refills it from set_decrementer for up to 150 s of kernel time.
PAGE = 0x1000

# Values iBoot fills into the DeviceTree; Exynos4412 clocks (XXTI 24 MHz, ACLK 200, PCLK 100).
# shortcut: one-node-name match, fine for the cpu0 node; use full paths if names collide.
DT_SET = {
    ("cpu0", "timebase-frequency"): 24_000_000,
    ("cpu0", "fixed-frequency"): 24_000_000,
    ("cpu0", "clock-frequency"): 1_400_000_000,
    ("cpu0", "bus-frequency"): 200_000_000,
    ("cpu0", "memory-frequency"): 400_000_000,
    ("cpu0", "peripheral-frequency"): 100_000_000,
    # Apple I/O window moved onto Exynos SFRs; uart0 (S5L UART, same regs) becomes Exynos UART2.
    ("arm-io", "ranges"): (0, 0x10000000, 0x10000000),
    ("uart0", "reg"): (0x03820000, 0x1000),
    # persistent RAM (panic log) above MEMSIZE, untouched by U-Boot: readable after a WDT reset
    ("pram", "reg"): (PHYS + MEMSIZE, 0x10000),  # 1 MB hung early ml_io_map
    ("chosen", "random-seed"): os.urandom,  # early_random panics on an all-zero seed
    # IODTNVRAM walks the CHRP partitions; a zero-length header loops forever
    ("chosen", "nvram-proxy-data"): lambda n: nvram_image(n),
    # BaseBoard BSPlatform / libMobileGestalt require non-zero display-scale & board/chip IDs
    ("chosen", "display-scale"): 2,          # Retina 2x (MainScreenScale)
    ("chosen", "board-id"): 8,               # n94ap (iPhone4,1)
    ("chosen", "chip-id"): 0x8940,           # S5L8940X (A5)
    ("chosen", "unique-chip-id"): (0x4558594E, 0x34343132),  # deterministic ECID for IOPlatformUUID v5
    ("clcd", "dot-pitch"): 306,              # Galaxy S3 306 PPI (IOMobileFramebufferGetDotPitch)
}

# Properties hidden by renaming (no driver looks them up any more).
DT_RENAME = {
    # AppleARMPE: with a prefix matching the root (md0) it waits forever for the
    # SecureRootCallBack of the (disabled) image verification; without one it accepts.
    ("device-tree", "secure-root-prefix"): "x-secure-root-prefix",
    # AppleS5L8940XIO::start touches S5L8940X bus-timeout regs (0x3fb00000) -> data abort on Exynos.
    ("arm-io", "bus-timeout"): "x-bus-timeout",
    # AppleARMCPU::start waits forever in waitForService(pmgr/l2) if these properties exist.
    ("cpu0", "function-cpu_idle"): "x-function-cpu_idle",
    ("cpu0", "function-platform_cache"): "x-function-platform_cache",
    ("cpu0", "function-enable_core"): "x-function-enable_core",
    ("cpu1", "function-cpu_idle"): "x-function-cpu_idle",
    ("cpu1", "function-platform_cache"): "x-function-platform_cache",
    ("cpu1", "function-enable_core"): "x-function-enable_core",
    ("cpu2", "function-cpu_idle"): "x-function-cpu_idle",
    ("cpu2", "function-platform_cache"): "x-function-platform_cache",
    ("cpu2", "function-enable_core"): "x-function-enable_core",
    ("cpu3", "function-cpu_idle"): "x-function-cpu_idle",
    ("cpu3", "function-platform_cache"): "x-function-platform_cache",
    ("cpu3", "function-enable_core"): "x-function-enable_core",
}

# arm-io children whose drivers run on Exynos (the shim backs aic with the GIC);
# every other child gets compatible = "disabled".
ARM_IO_KEEP = {"aic", "uart0"}  # uart0: kernel serial console, reg moved to Exynos UART2
# Top-level board devices, disabled the same way: their interrupt parents (gpio, PMU) are
# off, and IOService::resolveInterrupt waits forever for a missing controller.
TOP_OFF = {"buttons", "dock", "charger", "baseband", "backlight", "sacm"}


def nvram_image(n):
    """CHRP NVRAM image: "common" partition with 16-byte platform-uuid so IOPlatformUUID / gethostuuid() work."""
    def part(sig, size, name, body=b""):
        h = bytearray(struct.pack("<BBH12s", sig, 0, size // 16, name))
        c = h[0]
        for b in h[2:]:  # CHRP header checksum: 8-bit sum with end-around carry
            c += b
            c = (c & 0xFF) + (c >> 8)
        h[1] = c
        return bytes(h) + body + bytes(size - 16 - len(body))
    half = n // 2 & ~15
    # Pure printable ASCII bytes avoid CHRP 0xFF escape sequences; IODTNVRAM parses unknown keys as OSData(16).
    common_vars = b"platform-uuid=S3IOS935EXY4412!\x00"
    img = part(0x70, half, b"common", common_vars) + part(0x7F, n - half, b"wwwwwwwwwwww")
    assert len(img) == n and n % 16 == 0
    return img


def align(x, a):
    return (x + a - 1) & ~(a - 1)


def p2v(p):
    return p - PHYS + VIRT


def v2p(v):
    return v - VIRT + PHYS


def macho_segments(m):
    assert struct.unpack_from("<II", m) == (0xFEEDFACE, 12), "need 32-bit ARM Mach-O"
    segs, entry, off = [], None, 28
    for _ in range(struct.unpack_from("<I", m, 16)[0]):
        cmd, size = struct.unpack_from("<II", m, off)
        if cmd == 1:  # LC_SEGMENT
            name = m[off + 8:off + 24].rstrip(b"\0").decode()
            va, vs, fo, fs = struct.unpack_from("<IIII", m, off + 24)
            if vs:
                segs.append((name, va, vs, fo, fs))
        elif cmd == 5:  # LC_UNIXTHREAD: flavor, count, then r0..r15
            entry = struct.unpack_from("<I", m, off + 16 + 15 * 4)[0]
        off += size
    return sorted(segs, key=lambda s: s[1]), entry


# --- branch encodings for checkpoint patches ---------------------------------

def arm_bl(site, target):
    off = target - (site + 8)
    assert off % 4 == 0 and -(1 << 25) <= off < (1 << 25)
    return 0xEB000000 | ((off >> 2) & 0xFFFFFF)


def arm_bl_target(site, insn):
    imm = insn & 0xFFFFFF
    imm -= (imm & 0x800000) << 1
    return site + 8 + (imm << 2)


def thumb_blx(site, target):
    """BLX imm (T2): Thumb caller -> ARM stub. Returns the two halfwords."""
    assert target % 4 == 0
    off = target - ((site + 4) & ~3)
    assert -(1 << 24) <= off < (1 << 24)
    s = (off >> 24) & 1
    i1, i2 = (off >> 23) & 1, (off >> 22) & 1
    j1, j2 = (~i1 ^ s) & 1, (~i2 ^ s) & 1
    hi, lo = (off >> 12) & 0x3FF, (off >> 2) & 0x3FF
    return 0xF000 | (s << 10) | hi, 0xC000 | (j1 << 13) | (j2 << 11) | (lo << 1)


def thumb_bl(site, target):
    """BL imm (T1): Thumb caller -> Thumb function (target has bit 0 set)."""
    assert target & 1
    off = (target & ~1) - (site + 4)
    assert -(1 << 24) <= off < (1 << 24)
    s = (off >> 24) & 1
    i1, i2 = (off >> 23) & 1, (off >> 22) & 1
    j1, j2 = (~i1 ^ s) & 1, (~i2 ^ s) & 1
    return 0xF000 | (s << 10) | ((off >> 12) & 0x3FF), 0xD000 | (j1 << 13) | (j2 << 11) | ((off >> 1) & 0x7FF)


def thumb_blx_target(site, h1, h2):
    s, hi = (h1 >> 10) & 1, h1 & 0x3FF
    j1, j2, lo = (h2 >> 13) & 1, (h2 >> 11) & 1, (h2 >> 1) & 0x3FF
    i1, i2 = (~(j1 ^ s)) & 1, (~(j2 ^ s)) & 1
    off = (s << 24) | (i1 << 23) | (i2 << 22) | (hi << 12) | (lo << 2)
    off -= (s << 25)
    return ((site + 4) & ~3) + off


def thumb_call_target(site, h1, h2):
    """Target of a Thumb BL (stays Thumb, returns addr|1) or BLX imm (to ARM)."""
    if h2 & 0x1000:  # BL: same math as BLX but no word alignment of the base
        s, hi = (h1 >> 10) & 1, h1 & 0x3FF
        j1, j2, lo = (h2 >> 13) & 1, (h2 >> 11) & 1, h2 & 0x7FF
        i1, i2 = (~(j1 ^ s)) & 1, (~(j2 ^ s)) & 1
        off = (s << 24) | (i1 << 23) | (i2 << 22) | (hi << 12) | (lo << 1)
        off -= (s << 25)
        return (site + 4 + off) | 1
    return thumb_blx_target(site, h1, h2)


def thumb_call_sites(lo, hi):
    """bl/blx sites in Thumb code [lo, hi), from llvm-objdump."""
    out = subprocess.run(["llvm-objdump", "-d", "--triple=thumbv7", f"--start-address={lo:#x}",
                          f"--stop-address={hi:#x}", KERNEL], capture_output=True, text=True).stdout
    sites = []
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 5 and f[0].endswith(":") and f[3] in ("bl", "blx") and f[4].startswith("0x"):
            sites.append(int(f[0][:-1], 16))
    return sites


def selftest():
    # 0x800be856: f7ff fb3f  bl 0x800bded8 ; 0x800bea02: f00a eeae  blx 0x800c9760
    assert thumb_call_target(0x800be856, 0xF7FF, 0xFB3F) == 0x800bded8 | 1
    assert thumb_call_target(0x800bea02, 0xF00A, 0xEEAE) == 0x800c9760
    assert thumb_bl(0x800be856, 0x800bded8 | 1) == (0xF7FF, 0xFB3F)
    assert thumb_bl(0x8037b556, 0x803040d4 | 1) == (0xF788, 0xFDBD)  # far, backward
    assert thumb_call_target(0x80388958, *thumb_bl(0x80388958, 0x800c5111)) == 0x800c5111
    # far and backward targets exercise J1/J2 (llvm-mc: blx to +0xecf76c is f2cf, c3b6)
    assert thumb_blx(0x80388958, 0x812580c8) == (0xF2CF, 0xC3B6)
    assert thumb_blx_target(0x80388958, *thumb_blx(0x80388958, 0x80001000)) == 0x80001000
    for site, tgt in ((0x800c70f8, 0x804b6600), (0x804b0000, 0x80001000)):
        assert arm_bl_target(site, arm_bl(site, tgt)) == tgt
    for site, tgt in ((0x800be822, 0x804b6600), (0x800be820, 0x804b6600), (0x804b0002, 0x80400000)):
        assert thumb_blx_target(site, *thumb_blx(site, tgt)) == tgt
    assert thumb_call_target(AMFI_TC_INIT_SITE, *thumb_bl(AMFI_TC_INIT_SITE, 0x800c5111)) == 0x800c5111
    cd = struct.pack(">II", 0xFADE0C02, 12) + b"TEST"
    sb = struct.pack(">III", 0xFADE0CC0, 20 + len(cd), 1) + struct.pack(">II", 0, 20) + cd
    mh = struct.pack("<7I", 0xFEEDFACE, 12, 9, 2, 1, 16, 0) + struct.pack("<4I", 0x1D, 16, 44, len(sb)) + sb
    assert macho_cdhash(mh) == hashlib.sha1(cd).digest()
    tc = pack_trust_cache([hashlib.sha1(cd).digest(), hashlib.sha1(cd).digest()])
    assert len(tc) == 24 + 20 and struct.unpack_from("<I", tc, 20)[0] == 1


def macho_cdhash(data):
    """Extract the 20-byte SHA-1 CDHash from a 32-bit signed Mach-O's LC_CODE_SIGNATURE."""
    magic, _, _, _, ncmds, _, _ = struct.unpack_from("<7I", data, 0)
    assert magic == 0xFEEDFACE, "not a 32-bit Mach-O"
    off = 28
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmd == 0x1D:  # LC_CODE_SIGNATURE
            doff, dsize = struct.unpack_from("<II", data, off + 8)
            smagic, _, count = struct.unpack_from(">III", data, doff)
            assert smagic == 0xFADE0CC0, "bad SuperBlob magic"
            for i in range(count):
                slot, boff = struct.unpack_from(">II", data, doff + 12 + i * 8)
                bmagic, blen = struct.unpack_from(">II", data, doff + boff)
                if slot == 0 and bmagic == 0xFADE0C02:  # CSSLOT_CODEDIRECTORY
                    return hashlib.sha1(data[doff + boff:doff + boff + blen]).digest()
            raise ValueError("no CodeDirectory in LC_CODE_SIGNATURE")
        off += cmdsize
    raise ValueError("no LC_CODE_SIGNATURE in Mach-O")


def load_trust_cache_hashes(paths):
    """Read 20-byte SHA-1 CDHashes from signed Mach-Os or hex-line text files."""
    hashes = []
    for p in paths:
        data = open(p, "rb").read()
        if data[:4] == b"\xce\xfa\xed\xfe":
            hashes.append(macho_cdhash(data))
        else:
            for line in data.decode().splitlines():
                s = line.split("#", 1)[0].strip()
                if s:
                    h = bytes.fromhex(s)
                    assert len(h) == 20, f"{p}: CDHash must be 20 bytes, got {len(h)}"
                    hashes.append(h)
    return hashes


def pack_trust_cache(hashes):
    """Pack AMFI loaded_trust_caches node: next(0), uuid(16), count(4), hashes(N*20)."""
    uniq = sorted(set(hashes))
    uuid = hashlib.sha1(b"".join(uniq)).digest()[:16]
    return struct.pack("<I16sI", 0, uuid, len(uniq)) + b"".join(uniq)


# --- generated ARM code -------------------------------------------------------

def assemble(src, base):
    with tempfile.TemporaryDirectory() as d:
        s, o, e, b = (os.path.join(d, n) for n in ("a.S", "a.o", "a.elf", "a.bin"))
        open(s, "w").write(".global _start\n_start:\n" + src)
        subprocess.run(["arm-none-eabi-as", "-march=armv7-a", s, "-o", o], check=True)
        subprocess.run(["arm-none-eabi-ld", f"-Ttext={base:#x}", o, "-o", e], check=True)
        subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", e, b], check=True)
        return open(b, "rb").read()


SAMPLER_PA = PHYS + MEMSIZE + 0x80000  # above MEMSIZE + pram: neither the kernel nor U-Boot touches it
SAMPLER_RING = SAMPLER_PA + 0x1000  # 256 PCs; +0x400 index, +0x404 MCT G_CNT_L, +0x408 magic, +0x40c G_TCON, +0x410 samples
CPU0_DBGPCSR = 0x10890084  # Cortex-A9 debug (CoreSight ROM 0x10880000), PC sample register
CPU1_BOOT_REG = 0x02020004  # iRAM: Exynos4412 iROM starts CPU1 here after sev
# Registers the sampler snapshots at its start (+0x420) and when the MCT stops (+0x480).
SNAP = (0x10038950, 0x10038960, 0x10020704, 0x10020708, 0x125B0000, 0x125B0004,  # CMU PERIL/PERIR gates, PMU USB PHY ctrl, USB PHYPWR/PHYCLK
        0x12480804, 0x12480E00, 0x10050110, 0x1005024C, 0x10050244, 0x10050248,  # OTG DCTL, PCGCCTL, MCT G_CNT_WSTAT, G_WSTAT, CSTAT, INT_ENB
        0x10030000 + 0x0C210, 0x10030000 + 0x0C250, 0x10040000 + 0x0500, 0x10040000 + 0x4100,  # CLK_SRC_TOP0/1, CLK_SRC_CPU, APLL_CON0
        WDT + 8, 0x10020408, 0x10020000, 0x10000000)  # WTCNT, PMU WDT mask, PMU OM_STAT, CHIPID


def snapshot(off):
    return "".join(f"\tldr\tr5, ={a:#x}\n\tldr\tr5, [r5]\n\tstr\tr5, [r1, #{off + 4 * i:#x}]\n"
                   for i, a in enumerate(SNAP))


SAMPLER = f"""
	adr	r0, smp_start		@ CPU1 PC sampler: copy, then release CPU1
	ldr	r1, ={SAMPLER_PA:#x}
	adr	r2, smp_end
1:	ldr	r3, [r0], #4
	str	r3, [r1], #4
	cmp	r0, r2
	blo	1b
	dsb
	ldr	r0, ={CPU1_BOOT_REG:#x}
	ldr	r1, ={SAMPLER_PA:#x}
	str	r1, [r0]
	dsb
	sev
	b	smp_end
smp_start:				@ CPU1, MMU and caches off: every 1 ms, CPU0's PC into the ring
	cpsid	if
	ldr	r0, ={CPU0_DBGPCSR:#x}
	ldr	r1, ={SAMPLER_RING:#x}
	ldr	r3, ={MCT + 0x100:#x}	@ MCT G_CNT_L, 24 MHz
	mov	r2, #0
	add	r6, r1, #0x400		@ clear the results of an earlier run
1:	str	r2, [r6], #4
	add	r7, r1, #0x500
	cmp	r6, r7
	blo	1b
	ldr	r6, =0x4c504d53		@ "SMPL"
	str	r6, [r1, #0x408]
	mov	r8, #0			@ MCT at the previous sample
	mov	r9, #0			@ samples since the MCT last moved
{snapshot(0x420)}
2:	ldr	r5, [r3]		@ MCT stopped for 50 samples: keep the ring as it is
	cmp	r5, r8
	mov	r8, r5
	movne	r9, #0
	addeq	r9, r9, #1
	cmp	r5, #0			@ not started yet
	moveq	r9, #0
	cmp	r9, #50
	bne	5f
{snapshot(0x480)}
5:	cmp	r9, #50
	bhs	3f
	ldr	r4, [r0]
	str	r4, [r1, r2, lsl #2]
	add	r2, r2, #1
	and	r2, r2, #0xff
	str	r2, [r1, #0x400]
	ldr	r5, [r3]
	str	r5, [r1, #0x404]
	ldr	r5, [r3, #0x140]	@ G_TCON
	str	r5, [r1, #0x40c]
	ldr	r5, [r1, #0x410]	@ sample count
	add	r5, r5, #1
	str	r5, [r1, #0x410]
3:	ldr	r5, ={WDT + 8:#x}	@ WTCNT: an independent clock
	ldr	r5, [r5]
	str	r5, [r1, #0x414]
	ldr	r7, =30000		@ busy wait (~0.5 ms): the MCT itself is under suspicion
4:	subs	r7, r7, #1
	bne	4b
	b	2b
	.ltorg
smp_end:
"""


def trampoline(mailbox, boot_args, entry, stop, vbar, sampler=False):
    count = int(100_000_000 / 256 / 128 * WDT_SECONDS)  # PCLK 100 MHz, prescaler 256, /128
    assert count < 0x10000
    return f"""
	.syntax unified
	.arm
	.arch_extension sec
	cpsid	if
	ldr	r0, ={mailbox:#x}
	ldr	r1, ={MAGIC:#x}
	mov	r2, #1			@ checkpoint 1: trampoline reached
	mov	r3, #0
	stmia	r0, {{r1, r2, r3}}
	str	r3, [r0, #12]
	str	r3, [r0, #16]		@ trace: last call site
	str	r3, [r0, #20]		@ trace: count
	str	r3, [r0, #24]		@ exception: 0xEE00000n, n = vector
	{f"ldr r1, ={vbar:#x}" if vbar else ""}
	{"mcr p15, 0, r1, c12, c0, 0" if vbar else ""}
	ldr	r0, ={PMU_WDT_MASKS:#x}	@ AUTOMATIC_WDT_RESET_DISABLE, MASK_WDT_RESET_REQUEST:
	str	r3, [r0]		@ without clearing both the watchdog never resets the SoC
	str	r3, [r0, #4]
	ldr	r0, ={WDT:#x}
	ldr	r1, ={count:#x}
	str	r1, [r0, #4]		@ WTDAT
	str	r1, [r0, #8]		@ WTCNT
	ldr	r1, =0xff39		@ prescaler 255, enable, /128, reset enable
	str	r1, [r0]
{SAMPLER if sampler else ""}
	@ GICv1: the secure GICD_CTLR only enables group 0; group 1 (IRQs, IPIs) is enabled
	@ by bit 0 of the non-secure copy. Write it from the non-secure side, come back by smc.
	adr	r0, mon_vectors
	mcr	p15, 0, r0, c12, c0, 1	@ MVBAR
	mrc	p15, 0, r1, c1, c1, 0	@ SCR
	orr	r1, r1, #1		@ NS
	mcr	p15, 0, r1, c1, c1, 0
	isb
	ldr	r0, ={GICD_PA:#x}
	mov	r1, #1
	str	r1, [r0]		@ GICD_CTLR (non-secure copy): enable group 1
	dsb
	smc	#0			@ -> mon_smc: back to secure
	@ Enable Cortex-A9 SCU (0x10500000) before PL310 L2 so all 4 cores share coherent L1/L2.
	ldr	r0, =0x10500000
	movw	r1, #0xffff
	str	r1, [r0, #0x0c]		@ SCU_INV_ALL: invalidate all 4 CPUs' tag RAMs
	ldr	r1, [r0, #0x00]		@ SCU_CTRL
	orr	r1, r1, #1		@ SCU Enable
	str	r1, [r0, #0x00]
	dsb
	@ Enable Exynos4412 PL310 1 MiB L2 cache (0x10502000) in full Write-Back mode + prefetch:
	@ shim/disp/gpu/touch clean WDT log lines through PL310; Mali & FB are uncached/WCOMB.
	ldr	r0, =0x10502000
	mov	r1, #0
	str	r1, [r0, #0x100]	@ CTRL = 0
	movw	r1, #0x110
	str	r1, [r0, #0x108]	@ TAG_RAM_LATENCY
	movw	r1, #0x120
	str	r1, [r0, #0x10c]	@ DATA_RAM_LATENCY
	ldr	r1, [r0, #0x104]	@ AUX_CTRL (16-way, 64KB way = 1 MiB)
	orr	r1, r1, #(1 << 22)	@ Shared attribute override (cache shareable RAM)
	orr	r1, r1, #(7 << 28)	@ Data prefetch + Instr prefetch + Early BRESP
	str	r1, [r0, #0x104]
	ldr	r1, =0x70800007		@ PREFETCH_CTRL: double linefill + incr + I/D prefetch + offset 7
	str	r1, [r0, #0xf60]
	mov	r1, #3			@ POWER_CTRL: dynamic clock gating + standby mode
	str	r1, [r0, #0xf80]
	mov	r1, #0			@ DEBUG_CTRL: 0 (full Write-Back)
	str	r1, [r0, #0xf40]
	movw	r1, #0xffff		@ INV_WAY: invalidate all 16 ways
	str	r1, [r0, #0x77c]
8:	ldr	r2, [r0, #0x77c]
	tst	r2, r1
	bne	8b
	mov	r1, #0
	str	r1, [r0, #0x730]	@ CACHE_SYNC
9:	ldr	r2, [r0, #0x730]
	tst	r2, #1
	bne	9b
	mov	r1, #1
	str	r1, [r0, #0x100]	@ CTRL = 1 (enable PL310)
	dsb
	@ Raise Exynos4412 APLL from 800 MHz -> 1000 MHz (M=125, P=3, S=0; safe at stock VDD_ARM).
	ldr	r0, =0x10044000		@ CMU_CPU
	ldr	r3, =0x04440004		@ MUX_STAT_CPU changing bits
	ldr	r1, =0x01110000		@ MUX_CORE/HPM -> SCLK_MPLL, MUX_APLL -> FIN_PLL
	str	r1, [r0, #0x200]	@ CLK_SRC_CPU
10:	ldr	r2, [r0, #0x400]	@ CLK_MUX_STAT_CPU
	tst	r2, r3
	bne	10b
	ldr	r1, =0x01147520		@ ATB_RATIO=4 (200 MHz @ 1000 MHz), COREM0=/3, COREM1=/6
	str	r1, [r0, #0x500]	@ CLK_DIV_CPU0
11:	ldr	r2, [r0, #0x600]	@ CLK_DIV_STAT_CPU0
	cmp	r2, #0
	bne	11b
	ldr	r1, =0x807d0300		@ APLL_CON0: ENABLE=1, M=125, P=3, S=0 -> 1000 MHz
	str	r1, [r0, #0x100]
12:	ldr	r2, [r0, #0x100]	@ wait PLL_LOCKED (bit 29)
	tst	r2, #(1 << 29)
	beq	12b
	ldr	r1, =0x01000001		@ MUX_CORE/HPM -> MOUT_APLL, MUX_APLL -> FOUT_APLL
	str	r1, [r0, #0x200]	@ CLK_SRC_CPU
13:	ldr	r2, [r0, #0x400]	@ CLK_MUX_STAT_CPU
	tst	r2, r3
	bne	13b
	dsb
	{f"ldr r2, ={WDT + 8:#x}" if stop else ""}		@ --stop: ~10 s (by the WDT count) before the MCT starts
	{"ldr r3, [r2]" if stop else ""}
	{"1: ldr r0, [r2]" if stop else ""}
	{"sub r0, r3, r0" if stop else ""}
	{"cmp r0, #10240" if stop else ""}
	{"blo 1b" if stop else ""}
	{f"ldr r0, ={MCT + 0x240:#x}" if stop else ""}
	{"mov r1, #0x100" if stop else ""}
	{"str r1, [r0]" if stop else ""}		@ --stop: run the MCT anyway (sampler test)
	{"b ." if stop else ""}
	ldr	r0, ={boot_args:#x}
	ldr	pc, ={entry:#x}
	.ltorg

	.balign	32
mon_vectors:
	b	.
	b	.
	b	mon_smc			@ +8: SMC
	b	.
	b	.
	b	.
	b	.
	b	.
mon_smc:
	mrc	p15, 0, r1, c1, c1, 0
	bic	r1, r1, #1		@ NS = 0
	mcr	p15, 0, r1, c1, c1, 0
	isb
	movs	pc, lr			@ resume after the smc, secure SVC
"""


def stub(mailbox):
    """Entered by a patched bl/blx; records where and spins until the watchdog fires."""
    return f"""
	.syntax unified
	.arm
	mrc	p15, 0, r12, c1, c0, 0
	ldr	r11, ={mailbox:#x}
	tst	r12, #1			@ MMU on: use the kernel's virtual alias
	addne	r11, r11, #{VIRT - PHYS:#x}
	ldr	r12, ={MAGIC:#x}
	str	r12, [r11]
	str	lr, [r11, #4]		@ return address identifies the checkpoint
	str	sp, [r11, #8]
	mrs	r12, cpsr
	str	r12, [r11, #12]
	add	r12, r11, #64		@ arguments: mailbox[16..19] = r0..r3
	stmia	r12, {{r0-r3}}
	mcr	p15, 0, r11, c7, c14, 1	@ clean+invalidate the line to PoC (survive reset)
	mcr	p15, 0, r12, c7, c14, 1
	dsb
	b	.
	.ltorg
"""


def dt_add_cpus(dt, ncpus):
    """For cpus > 2, clone /cpus/cpu1 into cpu2..cpu(ncpus-1) and update /arm-io/aic tables."""
    if ncpus <= 2:
        return dt

    def parse_node(buf, off):
        nprops, nchildren = struct.unpack_from("<II", buf, off)
        off += 8
        props = []
        for _ in range(nprops):
            pname = buf[off:off + 32].split(b"\0")[0].decode()
            ln = struct.unpack_from("<I", buf, off + 32)[0] & 0x7FFFFFFF
            val = bytes(buf[off + 36:off + 36 + ln])
            props.append([pname, val])
            off += 36 + align(ln, 4)
        children = []
        for _ in range(nchildren):
            c, off = parse_node(buf, off)
            children.append(c)
        return [props, children], off

    def emit_node(node):
        props, children = node
        out = bytearray(struct.pack("<II", len(props), len(children)))
        for pname, val in props:
            out += pname.encode().ljust(32, b"\0")
            out += struct.pack("<I", len(val))
            out += val + bytes(align(len(val), 4) - len(val))
        for c in children:
            out += emit_node(c)
        return bytes(out)

    def prop_map(props):
        return {p[0]: p for p in props}

    root, end = parse_node(dt, 0)
    assert end <= len(dt)
    for child in root[1]:
        cname = prop_map(child[0])["name"][1].split(b"\0")[0].decode()
        if cname == "cpus":
            cpu1 = next(c for c in child[1] if prop_map(c[0])["name"][1].startswith(b"cpu1\0"))
            for idx in range(2, ncpus):
                clone = [[list(p) for p in cpu1[0]], []]
                pm = prop_map(clone[0])
                pm["name"][1] = f"cpu{idx}\0".encode()
                pm["reg"][1] = struct.pack("<I", idx)
                pm["AAPL,phandle"][1] = struct.pack("<I", (0x1e + idx) << 24)
                ipi_self = 0xc0 + 2 * idx
                ipi_other = ipi_self + 1
                pmu_irq = 0x87 + 4 * idx
                pm["interrupts"][1] = struct.pack("<3I", ipi_self, pmu_irq, ipi_other)
                pm["function-ipi_dispatch"][1] = struct.pack("<3I", 0x0e000000, 0x49504944, ipi_self)
                pm["function-ipi_dispatch_other"][1] = struct.pack("<3I", 0x0e000000, 0x49504944, ipi_other)
                pm["function-enable_core"][1] = struct.pack("<3I", 0x10000000, 0x436f7265, 1 << idx)
                child[1].append(clone)
        elif cname == "arm-io":
            for sub in child[1]:
                sname = prop_map(sub[0])["name"][1].split(b"\0")[0].decode()
                if sname == "aic":
                    pm = prop_map(sub[0])
                    pm["#main-cpus"][1] = struct.pack("<I", ncpus)
                    pm["ipid-mask"][1] = struct.pack("<6I", (1 << (2 * ncpus)) - 1, 0, 0, 0, 0, 0)
                    td = []
                    for idx in range(ncpus):
                        td.extend((0x87 + 4 * idx, 1 << idx))
                    pm["target-destinations"][1] = struct.pack(f"<{len(td)}I", *td)
    return emit_node(root)


def dt_fixup(dt, ramdisk=None, keep=ARM_IO_KEEP):
    """Do what iBoot does to the IPSW DeviceTree before handing it over.

    Placeholder properties carry bit 31 in their length; iBoot fills them in and
    clears the bit. XNU walks properties with the raw length, so a leftover flag
    sends it 2 GB off. Keep the stored value, drop the flag.

    ramdisk = (phys, size): announced as /chosen/memory-map/RAMDisk (what rd=md0
    mounts), in place of the unused MemoryMapReserved-0 slot.
    """
    dt = bytearray(dt)

    def walk(off, parent=None):
        nprops, nchildren = struct.unpack_from("<II", dt, off)
        off += 8
        name, props = None, {}
        for _ in range(nprops):
            ln = struct.unpack_from("<I", dt, off + 32)[0]
            struct.pack_into("<I", dt, off + 32, ln & 0x7FFFFFFF)
            pname = dt[off:off + 32].split(b"\0")[0].decode()
            props[pname] = (off + 36, ln & 0x7FFFFFFF)
            if pname == "name":
                name = dt[off + 36:off + 36 + (ln & 0x7FFFFFFF)].split(b"\0")[0].decode()
            off += 36 + align(ln & 0x7FFFFFFF, 4)
        for (node, pname), val in DT_SET.items():
            if node == name and pname in props:
                if callable(val):
                    val = val(props[pname][1])
                if not isinstance(val, bytes):
                    val = struct.pack(f"<{len(val)}I", *val) if isinstance(val, tuple) else struct.pack("<I", val)
                assert props[pname][1] == len(val)
                dt[props[pname][0]:props[pname][0] + len(val)] = val
        for (node, pname), new in DT_RENAME.items():
            if node == name and pname in props:
                o = props[pname][0] - 36
                dt[o:o + 32] = new.encode().ljust(32, b"\0")
        if ramdisk and name == "memory-map":
            o, ln = props["MemoryMapReserved-0"]
            assert ln == 8
            dt[o - 36:o - 4] = b"RAMDisk".ljust(32, b"\0")
            dt[o:o + 8] = struct.pack("<II", *ramdisk)
        if (parent == "arm-io" and name not in keep) or (parent == "device-tree" and name in TOP_OFF):
            # no driver matches: A5 blocks are absent on Exynos. IONameMatch looks at
            # compatible, device_type and name. Early boot finds some nodes by
            # device_type, so that is only touched on nodes without compatible.
            for k in ["compatible"] if "compatible" in props else ["device_type", "name"]:
                if k in props:
                    o, ln = props[k]
                    dt[o:o + ln] = b"disabled".ljust(ln, b"\0")[:ln - 1].ljust(ln, b"\0")
        for _ in range(nchildren):
            off = walk(off, name)
        return off

    assert walk(0) <= len(dt)
    return bytes(dt)


TRACE_SLOT = 24  # bytes per traced call site, see trace_code


def trace_code(mailbox, sites):
    """Non-terminal checkpoints for bl/blx sites: record the site in mailbox[4],
    count in mailbox[5], then continue to the original callee. Only r12 is
    clobbered, which AAPCS allows across a call."""
    src = f"""
	.syntax unified
	.arm
common:
	mov	r12, r1			@ original callee
	mrc	p15, 0, r1, c1, c0, 0
	tst	r1, #1
	ldr	r1, ={mailbox:#x}
	addne	r1, r1, #{VIRT - PHYS:#x}
	str	r0, [r1, #16]
	ldr	r0, [r1, #20]
	add	r0, r0, #1
	str	r0, [r1, #20]
	mcr	p15, 0, r1, c7, c14, 1
	dsb
	pop	{{r0, r1}}
	bx	r12
	.ltorg
	.balign 8
slots:
"""
    for site, target in sites:  # fixed TRACE_SLOT-byte layout, literals right after the branch
        src += f"""	push	{{r0, r1}}
	ldr	r0, [pc, #4]
	ldr	r1, [pc, #4]
	b	common
	.word	{site:#x}, {target:#x}
"""
    return src


# Kernel fixes: vaddr -> (original bytes, new bytes). On A5 DRAM sits at 0x80000000,
# so gPhysBase == gVirtBase there and the kernel mixes them up; here phys != virt.
KPATCH = {
    # vector page = kvtophys(gPhysBase): ldr.w r0, [r11] -> ldr.w r0, [r11, #-4] (gVirtBase)
    0x800be3a4: (bytes.fromhex("dbf80000"), bytes.fromhex("5bf8040c")),
    # bsd_init (IOKitInitializeTime): wait for IORTC 1 ms instead of 30 s; there is no RTC driver
    0x8027bb8a: (bytes.fromhex("4af60041" "0546" "cff62341" "0622"),
                 bytes.fromhex("44f24021" "0546" "c0f20f01" "0022")),
    # AppleARMCPU::start: pmgr is disabled so function-enable_core is NULL; don't abort cpu1..cpu3
    0x8054c5c2: (bytes.fromhex("59d0"), bytes.fromhex("00bf")),
}

SCTLR_V_SITE, SCTLR_V_ORIG, SCTLR_V_NOHI = 0x800c7400, 0xE3877B0E, 0xE3877B06  # orr r7, #0x3800 -> #0x1800


EXC_VECTORS = 0x800c5000      # kernel ExceptionVectorsBase (copied to the high vector page)
KERNEL_VECTORS = (1, 2, 6, 7)


# --vec fwd data abort: user-mode alignment faults (DFSR FS = 0b00001, SIGBUS) at mailbox +128:
# count, lr (fault PC + 8), DFAR, DFSR; demand-paging faults would bury them in the ring
USER_ALIGN = """	mrs	r12, spsr
	and	r12, r12, #0x1f
	cmp	r12, #0x10
	bne	8f
	mrc	p15, 0, r12, c5, c0, 0
	tst	r12, #0x400
	bne	8f
	and	r12, r12, #0xf
	cmp	r12, #1
	bne	8f
	ldr	r12, [sp, #128]
	add	r12, r12, #1
	str	r12, [sp, #128]
	str	lr, [sp, #132]
	mrc	p15, 0, r12, c6, c0, 0
	str	r12, [sp, #136]
	mrc	p15, 0, r12, c5, c0, 0
	str	r12, [sp, #140]
8:"""


def vectors(mailbox, fwd=False):
    """Exception vectors for --vec (VBAR, kernel's high vectors disabled): record
    mailbox[6..15] = 0xEE00000n (n = vector), DFSR, DFAR, IFSR, SPSR, lr,
    PAR(DFAR), TTBR0, TTBR1, TTBCR, IFAR, PAR(IFAR), r0..r3, lr_svc, sp_svc; spin.

    fwd: aborts record 0xEE00000n, DFSR, DFAR, IFSR, SPSR, lr, count (+48), IFAR (+64),
    ring index (+68), last 6 (lr, DFAR) at +72, frozen once +124 counts 4 aborts
    from the kernel's exception code
    and continue in the kernel's handler, so the last abort before a panic is kept.
    The kernel's abort entry uses sp as scratch too; r12 is saved at +52."""
    src = """
	.syntax unified
	.arm
"""
    if fwd:
        mb = mailbox + VIRT - PHYS  # MMU is on whenever the kernel takes an abort
        src += "".join(f"\tldr\tpc, =k{n}\n" if n not in (3, 4) else f"\tb\th{n}\n" for n in range(8))
        src += "".join(f"\t.equ\tk{n}, {EXC_VECTORS + 4 * n:#x}\n" for n in range(8))
        for n in (3, 4):
            src += f"""h{n}:
	ldr	sp, ={mb:#x}
	str	r12, [sp, #52]
	ldr	r12, ={0xEE000000 | n:#x}
	str	r12, [sp, #24]
	mrc	p15, 0, r12, c5, c0, 0
	str	r12, [sp, #28]
	mrc	p15, 0, r12, c6, c0, 0
	str	r12, [sp, #32]
	mrc	p15, 0, r12, c5, c0, 1
	str	r12, [sp, #36]
	mrs	r12, spsr
	str	r12, [sp, #40]
	str	lr, [sp, #44]
	ldr	r12, [sp, #48]
	add	r12, r12, #1
	str	r12, [sp, #48]
	mrc	p15, 0, r12, c6, c0, 2
	str	r12, [sp, #64]
{USER_ALIGN if n == 4 else ""}
	ldr	r12, [sp, #124]		@ ring frozen after 3 aborts inside the kernel's
	cmp	r12, #3			@ exception code (recursion): keeps what started it
	bhi	9f
	ldr	r12, =0x800c6000
	cmp	lr, r12
	blo	1f
	ldr	r12, =0x800c8000
	cmp	lr, r12
	ldrlo	r12, [sp, #124]
	addlo	r12, r12, #1
	strlo	r12, [sp, #124]
1:	ldr	r12, [sp, #68]		@ ring of the last 6 (lr, DFAR) at +72, index at +68
	add	r12, r12, #1
	cmp	r12, #6
	movhs	r12, #0
	str	r12, [sp, #68]
	add	sp, sp, r12, lsl #3
	str	lr, [sp, #72]
	mrc	p15, 0, r12, c6, c0, 0
	str	r12, [sp, #76]
9:	ldr	sp, ={mb:#x}
	mcr	p15, 0, sp, c7, c14, 1
	add	sp, sp, #32
	mcr	p15, 0, sp, c7, c14, 1
	add	sp, sp, #32
	mcr	p15, 0, sp, c7, c14, 1
	add	sp, sp, #32
	mcr	p15, 0, sp, c7, c14, 1
	add	sp, sp, #32
	mcr	p15, 0, sp, c7, c14, 1
	sub	sp, sp, #128
	dsb
	ldr	r12, [sp, #52]
	ldr	pc, =k{n}
	.ltorg
"""
        return src
    # undef (lazy VFP), svc, irq, fiq go to the kernel's own vectors untouched
    src += "".join(f"\tldr\tpc, =k{n}\n" if n in KERNEL_VECTORS else f"\tb\th{n}\n" for n in range(8))
    src += "".join(f"h{n}:\tmov\tr12, #{n}\n\tb\texc\n" for n in range(8) if n not in KERNEL_VECTORS)
    src += "".join(f"\t.equ\tk{n}, {EXC_VECTORS + 4 * n:#x}\n" for n in KERNEL_VECTORS)
    return src + f"""
exc:
	mrc	p15, 0, r11, c1, c0, 0
	tst	r11, #1
	ldr	r11, ={mailbox:#x}
	addne	r11, r11, #{VIRT - PHYS:#x}
	orr	r12, r12, #0xEE000000
	str	r12, [r11, #24]
	mrc	p15, 0, r12, c5, c0, 0
	str	r12, [r11, #28]
	mrc	p15, 0, r12, c6, c0, 0
	str	r12, [r11, #32]
	mrc	p15, 0, r12, c5, c0, 1
	str	r12, [r11, #36]
	mrs	r12, spsr
	str	r12, [r11, #40]
	str	lr, [r11, #44]
	mrc	p15, 0, r12, c6, c0, 0
	mcr	p15, 0, r12, c7, c8, 0	@ ATS1CPR(DFAR): what the faulting VA maps to
	isb
	mrc	p15, 0, r12, c7, c4, 0
	str	r12, [r11, #48]
	mrc	p15, 0, r12, c2, c0, 0
	str	r12, [r11, #52]
	mrc	p15, 0, r12, c2, c0, 1
	str	r12, [r11, #56]
	mrc	p15, 0, r12, c2, c0, 2
	str	r12, [r11, #60]
	mrc	p15, 0, r12, c6, c0, 2
	str	r12, [r11, #64]		@ IFAR
	mcr	p15, 0, r12, c7, c8, 0	@ ATS1CPR(IFAR)
	isb
	mrc	p15, 0, r12, c7, c4, 0
	str	r12, [r11, #68]		@ PAR(IFAR)
	str	r0, [r11, #72]		@ r0..r3 (shared by all modes)
	str	r1, [r11, #76]
	str	r2, [r11, #80]
	str	r3, [r11, #84]
	cps	#0x13			@ SVC lr/sp: the caller when the kernel faulted in SVC
	str	lr, [r11, #88]
	str	sp, [r11, #92]
	add	r12, r11, #64
	mcr	p15, 0, r12, c7, c14, 1
	mcr	p15, 0, r11, c7, c14, 1
	add	r12, r11, #32
	mcr	p15, 0, r12, c7, c14, 1
	dsb
	b	.
	.ltorg
"""


FB_PA = 0x60000000  # --fb: outside XNU's RAM, above the FIT; U-Boot "xnufb" scans it out
FB_W, FB_H, FB_BPP = 720, 1280, 32


def boot_args(top, dt_phys, dt_len, cmdline, fb=False):
    # Boot_Video: base (phys), display, row bytes, width, height, depth. display 0 makes
    # machine init call initialize_screen(kPETextMode): verbose text; 1 is graphics mode,
    # which only shows the (here missing) boot logo.
    vdisp = 0 if "-v" in cmdline.split() else 1
    video = (FB_PA, vdisp, FB_W * FB_BPP // 8, FB_W, FB_H, FB_BPP) if fb else (0, 0, 0, 0, 0, 0)
    ba = struct.pack("<HHIIII6IIII256sII", 1, 2, VIRT, PHYS, MEMSIZE, top, *video,
                     0, p2v(dt_phys), dt_len, cmdline.encode(), 0, MEMSIZE)
    assert len(ba) == 0x140  # 32-bit struct boot_args, pexpert/pexpert/arm/boot.h
    return ba


STUB_AREA = (0x800c5110, 0x800c6040)  # zeros after ExceptionVectorsBase, in __text: shim code
HDR_AREA = (0x80001a20, 0x80002000)   # zeros after the Mach-O load commands, in __TEXT: stubs, traces, vectors
SHIM_DIR = os.path.join(ROOT, "tools/xnushim")
# __text of com.apple.driver.AppleVXE380 (video encoder): no kext depends on it and its
# only personalities (vxe,s5l894[05]x) are renamed below, so it never loads and its code
# never runs. Its Mach-O header (read by OSKext at boot) stays. Shim drivers (gpu.c) go here.
DRV_AREA = (0x81100a48, 0x81124ef8)
DRV_KEXT_MATCH = (b"vxe,s5l8940x", b"vxe,s5l8945x")  # in __PRELINK_INFO, renamed in place
PRELINK_INFO_VA, PRELINK_INFO_SIZE = 0x811ac000, 0xac000
TIMER_INIT_SITE = 0x80388958  # pe_arm_init_timer: three AIC timer stores, r0 = mapped AIC
TIMER_INIT_ORIG = bytes.fromhex("42f214016ff0004242 50 42f2100101224250 42f21801002c4250".replace(" ", ""))
AMFI_BOOTARG_GATE = 0x80764fec # AMFI init: beq.n 0x807650d2 after PE_i_can_has_debugger (blocks cs_enforcement_disable)
AMFI_BOOTARG_ORIG = bytes.fromhex("71d0")
AMFI_TC_INIT_SITE = 0x807650da  # AMFI init: str.w r8, [r0] (loaded_trust_caches = NULL)
AMFI_TC_INIT_ORIG = bytes.fromhex("c0f80080")
TBD_OPS = 0x803f5790          # A5 tbd_ops: fiq, get_timebase, get_decrementer, set_decrementer
# Kernel functions replaced wholesale by shim functions (4-aligned Thumb entries).
HOOKS = {
    0x803891bc: "exy_putc",        # _serial_putc
    0x80366e40: "exy_cpu_signal",  # _PE_cpu_signal
    0x800287bc: "exy_tap_conslog", # _conslog_putc, continues in the original (taps.S)
    0x80364c94: "exy_gmt_time",    # _PEGetGMTTimeOfDay: read Exynos 4412 RTC (drivers.c)
    0x80364cc4: "exy_set_gmt_time",# _PESetGMTTimeOfDay: write Exynos 4412 RTC (drivers.c)
    0x80363c08: "exy_get_utc_time",# IOPlatformExpert::getUTCTimeOfDay
    0x80363c20: "exy_set_utc_time",# IOPlatformExpert::setUTCTimeOfDay
    0x8055189c: "exy_get_utc_time",# AppleARMPE::getUTCTimeOfDay (eliminates 30s IORTC wait!)
    0x80551938: "exy_set_utc_time",# AppleARMPE::setUTCTimeOfDay (eliminates 30s IORTC wait!)
    0x800ce390: "exy_cons_putc",   # pe_serial serial_putc: cons_ops[0].putc (taps.S)
    0x80364390: "exy_halt_restart",  # _PEHaltRestart: reboot through the WDT
}
USER_FAULT_SITE = 0x800ccbc2    # sleh_abort, user data abort: bl vm_fault_internal -> exy_user_vm_fault
VM_FAULT_INTERNAL = 0x800792c5
EXTMETHOD = 0x803704f4  # is_io_connect_method; --disp logs every IOConnectCallMethod (disp.c exy_uc_log)
EXTMETHOD_HEAD = bytes.fromhex("f0b503af2de9000d")  # push {r4-r7, lr}; add r7, sp, #12; stmdb sp!, {r8, sl, fp}
CONSLOG_HEAD = bytes.fromhex("b0b502af4df69e65")  # bytes taps.S replays
# Virtual methods replaced in prelinked kext vtables: {slot va: (original, shim function)}.
# AppleInterruptController reaches every AIC register through readReg/writeReg; the shim
# translates AIC registers to the GIC.
VTABLE_HOOKS = {
    0x80f0c420: (0x80f0b5a9, "exy_aic_read"),   # AppleInterruptController::readReg(off)
    0x80f0c424: (0x80f0b5c1, "exy_aic_write"),  # AppleInterruptController::writeReg(off, val)
    0x80404908: (0x800d4461, "exy_md_strategy"),  # memdev mdevbdevsw.d_strategy (not a vtable): --emmc-root
    0x80579038: (0x8054cb9d, "exy_cpu_quiesce"),  # AppleARMCPU::quiesceCPU()
    0x8057903c: (0x8054cbcd, "exy_cpu_start"),    # AppleARMCPU::startCPU(start_paddr, arg_paddr)
    0x80b12440: (0x80b0ef21, "exy_audio_start_io"),    # IOAudio2Device::startIO()
    0x80b12444: (0x80b0ef6d, "exy_audio_stop_io"),     # IOAudio2Device::stopIO()
    0x80b1246c: (0x80b0f381, "exy_audio_set_ctrl"),    # IOAudio2Device::setControlValue()
    0x80b12498: (0x80b0f41d, "exy_audio_get_out_buf"), # IOAudio2Device::getOutputBuffer()
}
EMMC_SYSTEM_LBA = 2277376     # GPT "SYSTEM" (backup/pit.txt), 2048 MB
EMMC_ROOT_SIZE = 0x7ffff000   # md0 size for --emmc-root: memdev keeps size << 12 in 32 bits
EMMC_USERDATA_LBA = 7716864   # GPT "USERDATA" (backup/pit.txt), 11 GB
# shortcut: md1 uses the first 1 GiB of USERDATA (fw/var.simg), grow the image to use more
EMMC_VAR_SECTORS = 1 << 21
TBD_OPS_ORIG = struct.pack("<4I", 0x800c8bcc, 0x800c8be0, 0x800c8c0c, 0x800c8c20)


def build_shim(text_va, data_va):
    """Compile tools/xnushim (ARM). Returns (code, data, drv code, {symbol: address})."""
    cflags = ["-mthumb", "-march=armv7-a", "-mfloat-abi=soft", "-fcall-used-r9", "-Os",
              "-ffreestanding", "-fno-builtin", "-nostdlib", "-Wall", "-Werror"]
    with tempfile.TemporaryDirectory() as d:
        elf = os.path.join(d, "shim.elf")
        drvs = []
        # drivers: their own objects, sections renamed .drv.* so shim.ld places the code at DRV_BASE
        for src in ("gpu.c", "disp.c", "touch.c", "drivers.c", "audio.c", "wifi.c"):
            obj = os.path.join(d, src[:-2] + ".o")
            subprocess.run(["arm-none-eabi-gcc", *cflags, "-c", "-o", obj, f"{SHIM_DIR}/{src}"], check=True)
            subprocess.run(["arm-none-eabi-objcopy", "--prefix-alloc-sections=.drv", obj], check=True)
            drvs.append(obj)
        # shim.c builds as Thumb-2 (fits the gap); fiq.S and taps.S say .arm themselves
        subprocess.run(["arm-none-eabi-gcc", *cflags,
                        f"-Wl,--defsym=TEXT_BASE={text_va:#x}", f"-Wl,--defsym=DATA_BASE={data_va:#x}",
                        f"-Wl,--defsym=DRV_BASE={DRV_AREA[0]:#x}",
                        f"-Wl,-T,{SHIM_DIR}/shim.ld", "-Wl,-e,0", "-o", elf,
                        f"{SHIM_DIR}/fiq.S", f"{SHIM_DIR}/taps.S", f"{SHIM_DIR}/shim.c", *drvs, "-lgcc"], check=True)
        # readelf, not nm: nm drops bit 0 of Thumb functions, which their callers need
        st = subprocess.run(["arm-none-eabi-readelf", "-sW", elf], capture_output=True, text=True, check=True).stdout
        syms = {f[7]: int(f[1], 16) for f in (l.split() for l in st.splitlines())
                if len(f) == 8 and f[0][:-1].isdigit() and f[6] != "UND"}
        out = []
        for sec in (".text", ".data", ".drv"):
            b = os.path.join(d, sec[1:] + ".bin")
            subprocess.run(["arm-none-eabi-objcopy", "-O", "binary", "-j", sec, elf, b], check=True)
            out.append(open(b, "rb").read())
        return out[0], out[1], out[2], syms


def build(cps, stop, cmdline, traces=(), vec=False, ramdisk=None, sampler=False, emmc_root=False, fb=False,
          emmc_var=False, gpu_demo=False, trust_cache=(), disp=False, gpu=False):
    m = open(KERNEL, "rb").read()
    segs, entry = macho_segments(m)
    start = v2p(segs[0][1])
    kend = v2p(max(va + vs for _, va, vs, _, _ in segs))

    # Shim code, then bring-up stubs, go into zero padding inside __text: reachable by
    # bl/blx from kernel text and executable for the whole boot. Everything after the
    # kernel image (and the gap after __LINKEDIT) becomes execute-never during IOKit start.
    text_va, gap_end = STUB_AREA
    assert m[text_va - segs[0][1] + segs[0][3]:gap_end - segs[0][1] + segs[0][3]].count(0) == gap_end - text_va

    ncpus = 1
    for tok in cmdline.split():
        if tok.startswith("cpus=") and tok[5:].isdigit():
            ncpus = max(1, min(4, int(tok[5:])))

    dt = dt_add_cpus(open(DTREE, "rb").read(), ncpus)  # size fixed before layout
    dt_phys = align(kend, PAGE)
    ba_phys = align(dt_phys + len(dt), PAGE)
    tr_phys = ba_phys + PAGE
    data_phys = tr_phys + PAGE  # shim data, below topOfKernelData
    shim, shim_data, drv, syms = build_shim(text_va, p2v(data_phys))
    assert len(shim_data) >= syms["exy_mailbox"] + 128 - p2v(data_phys), "shim .bss not emitted"
    assert text_va + len(shim) <= gap_end, f"shim overflows the gap by {text_va + len(shim) - gap_end:#x}"
    assert DRV_AREA[0] + len(drv) <= DRV_AREA[1], f"drivers overflow DRV_AREA by {DRV_AREA[0] + len(drv) - DRV_AREA[1]:#x}"
    stub_va = HDR_AREA[0]  # stubs, traces and vectors: the header gap
    mailbox = v2p(syms["exy_mailbox"])
    rd = open(ramdisk, "rb").read() if ramdisk else b""
    tc = pack_trust_cache(load_trust_cache_hashes(trust_cache)) if trust_cache else b""
    tc_phys = align(data_phys + len(shim_data), PAGE)
    rd_phys = align(tc_phys + len(tc), PAGE)  # below topOfKernelData: statically mapped, never freed
    top = align(rd_phys + len(rd), 0x4000)  # TTB base: kernel builds and clears page tables here
    # --emmc-root: md0 only borrows this range for its size; exy_md_strategy never touches it
    keep = set(ARM_IO_KEEP)
    if disp:
        keep.add("clcd")
    if gpu:
        keep.add("sgx")
    dt = dt_fixup(dt, (rd_phys, len(rd)) if rd else (rd_phys, EMMC_ROOT_SIZE) if emmc_root else None, keep)

    img = bytearray(top - start)

    def put(phys, data):
        img[phys - start: phys - start + len(data)] = data

    for _, va, vs, fo, fs in segs:
        put(v2p(va), m[fo:fo + min(fs, vs)])  # tail up to vs stays zero (bss)
    for va, (old, new) in KPATCH.items():
        o = v2p(va) - start
        assert img[o:o + len(old)] == old, f"KPATCH {va:#x}: unexpected bytes"
        img[o:o + len(new)] = new
    put(v2p(text_va), shim)
    put(data_phys, shim_data)
    put(v2p(syms["exy_ncpus"]), struct.pack("<I", ncpus))
    if any(t.startswith("bench=") for t in cmdline.split()):
        put(v2p(syms["exy_wdt_sec"]), struct.pack("<I", 160))
    put(v2p(syms["exy_smp_boot_info"]) + 16, struct.pack("<I", top))
    put(v2p(DRV_AREA[0]), drv)
    info_lo = v2p(PRELINK_INFO_VA) - start
    for name in DRV_KEXT_MATCH:  # the overwritten kext must never match, load and run
        n = img.count(name, info_lo, info_lo + PRELINK_INFO_SIZE)
        assert n == 1, f"{name}: {n} matches in __PRELINK_INFO"
        o = img.index(name, info_lo, info_lo + PRELINK_INFO_SIZE)
        img[o:o + len(name)] = b"vxe,disabled"
    if gpu_demo:
        put(v2p(syms["exy_gpu_on"]), struct.pack("<I", 1))
    if emmc_root:
        put(v2p(syms["exy_emmc_lba"]), struct.pack("<I", EMMC_SYSTEM_LBA))
    if emmc_var:
        put(v2p(syms["exy_var_lba"]), struct.pack("<I", EMMC_USERDATA_LBA))
        put(v2p(syms["exy_var_sectors"]), struct.pack("<I", EMMC_VAR_SECTORS))
    if tc:
        put(tc_phys, tc)
        put(v2p(syms["exy_tc_head"]), struct.pack("<I", p2v(tc_phys)))
    o = v2p(AMFI_BOOTARG_GATE) - start
    assert img[o:o + len(AMFI_BOOTARG_ORIG)] == AMFI_BOOTARG_ORIG
    img[o:o + 2] = bytes.fromhex("00bf")  # nop: allow cs_enforcement_disable / amfi_get_out_of_my_way boot-args
    o = v2p(AMFI_TC_INIT_SITE) - start
    assert img[o:o + len(AMFI_TC_INIT_ORIG)] == AMFI_TC_INIT_ORIG
    img[o:o + 4] = struct.pack("<HH", *thumb_bl(AMFI_TC_INIT_SITE, syms["exy_amfi_init_tc"]))
    o = v2p(TIMER_INIT_SITE) - start
    assert img[o:o + len(TIMER_INIT_ORIG)] == TIMER_INIT_ORIG
    call = struct.pack("<HH", *thumb_bl(TIMER_INIT_SITE, syms["exy_timer_init"]))
    img[o:o + 26] = call + bytes.fromhex("00bf") * 9 + bytes.fromhex("002c00bf")  # keep cmp r4, #0
    o = v2p(TBD_OPS) - start
    assert img[o:o + 16] == TBD_OPS_ORIG
    img[o:o + 16] = struct.pack("<4I", *(syms[s] for s in (
        "exy_fiq", "exy_get_timebase", "exy_get_decrementer", "exy_set_decrementer")))
    o = v2p(USER_FAULT_SITE) - start
    assert img[o:o + 4] == struct.pack("<HH", *thumb_bl(USER_FAULT_SITE, VM_FAULT_INTERNAL))
    img[o:o + 4] = struct.pack("<HH", *thumb_bl(USER_FAULT_SITE, syms["exy_user_vm_fault"]))
    assert img[v2p(0x800287bc) - start:][:8] == CONSLOG_HEAD
    for va, sym in HOOKS.items():  # ldr.w pc, [pc] ; .word shim function
        assert va % 4 == 0
        o = v2p(va) - start
        img[o:o + 8] = bytes.fromhex("dff800f0") + struct.pack("<I", syms[sym])
    for va, (orig, sym) in VTABLE_HOOKS.items():
        o = v2p(va) - start
        assert img[o:o + 4] == struct.pack("<I", orig), f"vtable slot {va:#x}: unexpected"
        img[o:o + 4] = struct.pack("<I", syms[sym])  # Thumb function: nm keeps bit 0 set
    if disp:
        # Skip exy_tap_extmethod/exy_tap_async_method debug logging (zero overhead in production).
        # AppleKeyStoreUserClient sleeps forever on aks+0x85 (systembag_loaded) without Apple AES/effaceable,
        # deadlocking containermanagerd -> backboardd -> SpringBoard in MKBGetDeviceLockState (sel 17).
        # shortcut: bypass the aks+0x85 commandSleep loops until keybagd/systembag is emulated.
        for aks_va, orig_b, new_b in (
            (0x80fa0986, "00f0f9fc", "002000bf"),  # no-effaceable-storage=1 -> use SW effaceable (fixes securityd sel 10 lock hang)
            (0x80fa125c, "15f002fc", "002000bf"),  # waitForMatchingService("IOAESAccelerator") -> NULL (no hang)
            (0x80fa1b10, "90b5", "7047"),          # wait_for_systembag() -> bx lr (unblocks profiled / sel 10/34)
            (0x80fa24b4, "0020", "0420"),          # sel 7 get_lock_state: init state = 4 (unlocked since boot)
            (0x80fa2504, "fdf7a0ff", "002000bf"),  # sel 7 bl aks_get_lock_state -> movs r0, #0; nop
            (0x80fa36be, "58b9", "0be0"),          # cbnz -> b.n 0x80fa36d8
            (0x80fa36d4, "f4d0", "00bf"),          # beq  -> nop
            (0x80fa374c, "f4d0", "00bf"),          # sel 17 beq -> nop
            (0x80fa37d2, "f4d0", "00bf"),          # beq  -> nop
            (0x80fa38b0, "f4d0", "00bf"),          # beq  -> nop
            (0x80fa3ecc, "72d0", "00bf"),          # beq  -> nop
            (0x80fa3fca, "f4d0", "00bf"),          # beq  -> nop
            # Non-CPROTECT HFS+ /private/var: return 0 instead of ENOTSUP (45) on F_GET/SETPROTECTIONCLASS
            # so containermanagerd succeeds for backboardd, lsd, geod, and SpringBoard.
            (0x8026bfa6, "2d26", "0026"),          # cprotect_get_class: movs r6, #45 -> #0
            (0x8026c0d4, "2d25", "0025"),          # cprotect_set_class: movs r5, #45 -> #0
            (0x8028d554, "2d20", "0020"),          # fcntl cprotect: movs r0, #45 -> #0
            (0x8028d616, "2d20", "0020"),          # fcntl F_GETPROTECTIONLEVEL: movs r0, #45 -> #0
        ):
            o = v2p(aks_va) - start
            orig = bytes.fromhex(orig_b)
            assert img[o:o + len(orig)] == orig, f"AKS {aks_va:#x}: unexpected bytes"
            img[o:o + len(orig)] = bytes.fromhex(new_b)
        # Hook AppleKeyStore get_device_state (0x80f9fef0) -> exy_aks_get_device_state (kMobileLockStateDisabled=3)
        # and sel 31 get_configuration (0x80faf5a4) -> exy_aks_get_config (empty plist, fixes ManagedConfiguration).
        for aks_fn_va, aks_sym in ((0x80f9fef0, "exy_aks_get_device_state"),
                                   (0x80faf5a4, "exy_aks_get_config")):
            aks_o = v2p(aks_fn_va) - start
            img[aks_o:aks_o + 8] = bytes.fromhex("dff800f0") + struct.pack("<I", syms[aks_sym])
        # Skip nonexistent Apple /vram (PurpleGfxMem) & /arm-io/edram (PurpleEDRAM) regions in
        # IOSurfaceRoot::start so expected_region_count stays 0 and IOSurfaceCreate falls back
        # to system RAM instead of sleeping forever in IOSurfaceRoot::lookupMemoryRegion.
        for surf_va, orig_b, new_b in (
            (0x80616806, "d0b1", "1ae0"),  # cbz r0, 0x8061683e -> b.n 0x8061683e
            (0x80616850, "e0b1", "1ce0"),  # cbz r0, 0x8061688c -> b.n 0x8061688c
        ):
            o = v2p(surf_va) - start
            orig = bytes.fromhex(orig_b)
            assert img[o:o + len(orig)] == orig, f"IOSurface {surf_va:#x}: unexpected bytes"
            img[o:o + len(orig)] = bytes.fromhex(new_b)
        # Reset AppleCLCD & AppleMobileCLCD hardware overrides back to IOMobileFramebuffer base,
        # then plug in FIMD0 hooks (clcd,s5l8940x matches AppleMobileCLCD).
        iomfb_vt = v2p(0x80d43700) - start
        for vt_va, s270_orig in ((0x80d6a2f0, 0x80d4beb5), (0x80d6a9e4, 0x80d4d959)):
            clcd_vt = v2p(vt_va) - start
            assert struct.unpack_from("<I", img, clcd_vt + 270 * 4)[0] == s270_orig
            # 211/221/236: AppleMobileCLCD gated register access (236 panicked on backboardd sel 27)
            for slot in (43, 122, 139, 211, 221, *range(236, 312)):
                img[clcd_vt + slot * 4:clcd_vt + slot * 4 + 4] = img[iomfb_vt + slot * 4:iomfb_vt + slot * 4 + 4]
            for slot, val in ((90, syms["exy_clcd_start"]),
                              (146, syms["exy_clcd_new_uc"]),
                              (262, 0x80d3ceb9), (263, 0x80d3ceb9),
                              (270, syms["exy_clcd_start_hardware"]),
                              (272, syms["exy_clcd_swap_begin"]),
                              (275, syms["exy_clcd_verify_swap"]),
                              (278, syms["exy_clcd_s278"]),
                              (312, syms["exy_clcd_noop"])):
                struct.pack_into("<I", img, clcd_vt + slot * 4, val)
    if gpu:
        # Expose Mali-400 via SGXDriver543 (IOAcceleratorES) and point libGFXShared at Mali400GLDriverEX.
        old_b, new_b = b"IMGSGX543GLDriver", b"Mali400GLDriverEX"
        assert len(old_b) == len(new_b)
        assert img.count(old_b, info_lo, info_lo + PRELINK_INFO_SIZE) == 1
        bo = img.index(old_b, info_lo, info_lo + PRELINK_INFO_SIZE)
        img[bo:bo + len(old_b)] = new_b
        sgx_vt, uc_vt = v2p(0x80e012b8) - start, v2p(0x80e01cc4) - start
        for vt, slot, orig, new in (
            (sgx_vt, 90, 0x80df1651, syms["exy_sgx_start"]),
            (sgx_vt, 91, 0x80df26f5, 0x80332409),  # IOService::stop
            (sgx_vt, 146, 0x80df51b1, 0x803397c1),  # IOService::newUserClient
            (uc_vt, 90, 0x80de0a35, syms["exy_mali_uc_start"]),
            (uc_vt, 91, 0x80de0b09, 0x80332409),
            (uc_vt, 208, 0x80de128d, syms["exy_mali_external_method"]),
            (uc_vt, 218, 0x8036e199, syms["exy_mali_client_memory"]),
        ):
            assert struct.unpack_from("<I", img, vt + slot * 4)[0] == orig
            struct.pack_into("<I", img, vt + slot * 4, new)
    put(dt_phys, dt)
    put(rd_phys, rd)
    put(ba_phys, boot_args(top, dt_phys, len(dt), cmdline, fb))
    code = assemble(stub(mailbox), stub_va)
    put(v2p(stub_va), code)

    cp_sites = {va for va, _ in cps}
    assert all(segs[0][1] <= lo < hi <= segs[0][1] + segs[0][4] for lo, hi in traces), "trace only __TEXT"
    sites = [(s, thumb_call_target(s, *struct.unpack_from("<HH", m, s - segs[0][1] + segs[0][3])))
             for lo, hi in traces for s in thumb_call_sites(lo, hi) if s not in cp_sites]
    tr_va = align(stub_va + len(code), 8)
    slots_va = tr_va + len(assemble(trace_code(mailbox, []), tr_va))
    tcode = assemble(trace_code(mailbox, sites), tr_va)
    assert len(tcode) == slots_va - tr_va + TRACE_SLOT * len(sites)
    assert tr_va + len(tcode) <= HDR_AREA[1], f"stubs overflow the header gap: {len(sites)} trace sites"
    put(v2p(tr_va), tcode)
    for i, (s, _) in enumerate(sites):
        o = v2p(s) - start
        img[o:o + 4] = struct.pack("<HH", *thumb_blx(s, slots_va + TRACE_SLOT * i))

    vbar = 0
    if vec:
        vbar = align(tr_va + len(tcode), 32)
        vcode = assemble(vectors(mailbox, vec == "fwd"), vbar)
        assert vbar + len(vcode) <= HDR_AREA[1], "vectors overflow the header gap"
        assert img[v2p(vbar) - start:v2p(vbar) - start + len(vcode)].count(0) == len(vcode)
        put(v2p(vbar), vcode)
        o = v2p(SCTLR_V_SITE) - start
        assert struct.unpack_from("<I", img, o)[0] == SCTLR_V_ORIG
        struct.pack_into("<I", img, o, SCTLR_V_NOHI)
    put(tr_phys, assemble(trampoline(mailbox, ba_phys, v2p(entry), stop, vbar, sampler), tr_phys))

    for va, mode in cps:
        o = v2p(va) - start
        if mode == "a":
            img[o:o + 4] = struct.pack("<I", arm_bl(va, stub_va))
        else:
            img[o:o + 4] = struct.pack("<HH", *thumb_blx(va, stub_va))

    info = dict(start=start, entry=v2p(entry), stub=stub_va, shim=text_va, shim_data=data_phys, dt=dt_phys, boot_args=ba_phys,
                tramp=tr_phys, mailbox=mailbox, top=top, traced=len(sites), vbar=vbar,
                log=v2p(syms["exy_log"]), fiqs=v2p(syms["exy_fiq_sample"]), dbg=v2p(syms["exy_dbg"]), aic=v2p(syms["exy_aic_dbg"]),
                emmc=v2p(syms["exy_emmc_log"]), gpu=v2p(syms["exy_gpu_log"]), disp=v2p(syms["exy_disp_log"]),
                touch=v2p(syms["exy_touch_log"]), drv=v2p(syms["exy_drv_log"]), uc=v2p(syms["exy_uc_log"]), fault=v2p(syms["exy_fault_log"]),
                smp=v2p(syms["exy_smp_entered"]), ncpus=ncpus)
    return bytes(img), info


def _m0xnu_splash_strips():
    """Generate two compact BGRA strips for U-Boot imxtract: M0XNU wordmark + GT-I9300 tag."""
    def render_strip(y_start, y_end):
        h = y_end - y_start
        buf = bytearray(FB_W * h * 4)
        def blend(x, y, a):
            if 0 <= x < FB_W and y_start <= y < y_end and a > 0:
                v = (242 * a) >> 8
                o = ((y - y_start) * FB_W + x) * 4
                if v > buf[o]:
                    buf[o] = buf[o + 1] = buf[o + 2] = v
                    buf[o + 3] = 255
        def seg(x0, y0, x1, y1):
            vx, vy = x1 - x0, y1 - y0
            l2 = vx * vx + vy * vy
            for y in range(max(y_start, min(y0, y1) - 6), min(y_end, max(y0, y1) + 7)):
                wy = y - y0
                for x in range(max(0, min(x0, x1) - 6), min(FB_W, max(x0, x1) + 7)):
                    wx = x - x0
                    t = max(0.0, min(1.0, (wx * vx + wy * vy) / l2)) if l2 else 0.0
                    dx, dy = wx - t * vx, wy - t * vy
                    d16 = int((dx * dx + dy * dy) ** 0.5 * 16)
                    if d16 < 72:
                        blend(x, y, 255 if d16 <= 48 else (72 - d16) * 255 // 24)
        def arc(cx, cy, rad, top):
            y0 = max(y_start, (cy - rad - 6) if top else cy)
            y1 = min(y_end, (cy + 1) if top else (cy + rad + 7))
            for y in range(y0, y1):
                dy = y - cy
                for x in range(max(0, cx - rad - 6), min(FB_W, cx + rad + 7)):
                    dx = x - cx
                    d16 = abs(int((dx * dx + dy * dy) ** 0.5 * 16) - rad * 16)
                    if d16 < 72:
                        blend(x, y, 255 if d16 <= 48 else (72 - d16) * 255 // 24)
        if y_start < 700:
            # M0XNU centered geometric monochrome wordmark (y=584..648, x=197..523)
            seg(197, 648, 197, 584); seg(197, 584, 224, 626); seg(224, 626, 251, 584); seg(251, 584, 251, 648)
            seg(275, 606, 275, 626); seg(319, 606, 319, 626); arc(297, 606, 22, True); arc(297, 626, 22, False)
            seg(297, 616, 297, 616)
            seg(343, 584, 387, 648); seg(387, 584, 343, 648)
            seg(411, 648, 411, 584); seg(411, 584, 455, 648); seg(455, 648, 455, 584)
            seg(479, 584, 479, 626); seg(523, 584, 523, 626); arc(501, 626, 22, False)
        else:
            tag = [
                (0x0e,0x11,0x10,0x13,0x11,0x11,0x0e), (0x1f,0x04,0x04,0x04,0x04,0x04,0x04),
                (0x00,0x00,0x00,0x1f,0x00,0x00,0x00), (0x1f,0x04,0x04,0x04,0x04,0x04,0x1f),
                (0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c), (0x0e,0x11,0x01,0x06,0x01,0x11,0x0e),
                (0x0e,0x11,0x13,0x15,0x19,0x11,0x0e), (0x0e,0x11,0x13,0x15,0x19,0x11,0x0e),
            ]
            x0 = (FB_W - (7 * 16 + 10)) // 2
            for i, g in enumerate(tag):
                for r in range(7):
                    for c in range(5):
                        if g[r] & (1 << (4 - c)):
                            for dy in range(2):
                                o = ((1160 + r * 2 + dy - y_start) * FB_W + x0 + i * 16 + c * 2) * 4
                                buf[o:o + 8] = b"\x5a\x5a\x5a\xff\x5a\x5a\x5a\xff"
        return bytes(buf)
    return (576, render_strip(576, 656)), (1156, render_strip(1156, 1178))


def fit(img, info, out, fb=False):
    sp1_y, sp1_data = (0, b"")
    sp2_y, sp2_data = (0, b"")
    if fb:
        (sp1_y, sp1_data), (sp2_y, sp2_data) = _m0xnu_splash_strips()
    sp1_pa = FB_PA + sp1_y * FB_W * 4
    sp2_pa = FB_PA + sp2_y * FB_W * 4
    script = (f"echo xnu: {len(img)} bytes to {info['start']:#x}\n"
              f"imxtract ${{fitloadaddr}} mem {info['start']:#x}\n"
              # --fb: clear FB, place M0XNU monochrome splash at t=0s, light the panel
              + (f"mw.l {FB_PA:#x} 0 {FB_W * FB_H:#x}\n"
                 f"imxtract ${{fitloadaddr}} sp1 {sp1_pa:#x}\n"
                 f"imxtract ${{fitloadaddr}} sp2 {sp2_pa:#x}\n"
                 f"xnufb {FB_PA:x} {FB_BPP}\n" if fb else "")
              + "dcache off\nicache off\n"
              f"go {info['tramp']:#x}\n")
    with tempfile.TemporaryDirectory() as d:
        open(os.path.join(d, "mem.bin"), "wb").write(img)
        open(os.path.join(d, "boot.scr"), "w").write(script)
        sp_nodes = ""
        if fb:
            open(os.path.join(d, "sp1.bin"), "wb").write(sp1_data)
            open(os.path.join(d, "sp2.bin"), "wb").write(sp2_data)
            sp_nodes = (
                f'\t\tsp1 {{ data = /incbin/("sp1.bin"); type = "firmware"; arch = "arm"; compression = "none"; load = <{sp1_pa:#x}>; }};\n'
                f'\t\tsp2 {{ data = /incbin/("sp2.bin"); type = "firmware"; arch = "arm"; compression = "none"; load = <{sp2_pa:#x}>; }};\n'
            )
        its = f"""/dts-v1/;
/ {{
	description = "XNU bring-up image";
	#address-cells = <1>;
	images {{
		mem {{ data = /incbin/("mem.bin"); type = "firmware"; arch = "arm"; compression = "none"; load = <{info['start']:#x}>; }};
{sp_nodes}		script {{ data = /incbin/("boot.scr"); type = "script"; arch = "arm"; compression = "none"; }};
	}};
	configurations {{ default = "c"; c {{ firmware = "mem"; }}; }};
}};
"""
        open(os.path.join(d, "x.its"), "w").write(its)
        subprocess.run([MKIMAGE, "-f", os.path.join(d, "x.its"), out], check=True, stdout=subprocess.DEVNULL)


def main():
    selftest()
    ap = argparse.ArgumentParser()
    ap.add_argument("--cp", action="append", default=[], help="VADDR:a|t checkpoint")
    ap.add_argument("--stop", action="store_true", help="stop in the trampoline")
    ap.add_argument("--trace", action="append", default=[], help="LO:HI Thumb range; trace its bl/blx")
    ap.add_argument("--vec", nargs="?", const="stop", choices=("stop", "fwd"),
                    help="catch exceptions via VBAR (no high vectors); fwd: record aborts, then let the kernel handle them")
    ap.add_argument("--args", default="-v cpus=1 debug=0x8 serial=3")
    ap.add_argument("--ramdisk", help="raw HFS+ image to boot as root (adds rd=md0)")
    ap.add_argument("--emmc-root", action="store_true",
                    help=f"root on the eMMC from LBA {EMMC_SYSTEM_LBA} (GPT SYSTEM), read-only, as md0")
    ap.add_argument("--gpu-demo", action="store_true",
                    help="on the first root I/O, the Mali-400 draws gpu/mali400/tri_job.h into the framebuffer "
                         "(use with --fb); status at gpu= in the info line")
    ap.add_argument("--emmc-var", action="store_true",
                    help=f"with --emmc-root: /dev/md1 on GPT USERDATA (LBA {EMMC_USERDATA_LBA}), read-write, "
                         f"{EMMC_VAR_SECTORS >> 11} MB (fw/var.simg)")
    ap.add_argument("--normal-boot", action="store_true",
                    help="rootdev=md0 instead of rd=md0: launchd runs its normal Boot tasks (fstab mounts md1 "
                         "on /private/var) but keybagd then needs /private/var/keybags/systembag.kb")
    ap.add_argument("--sampler", action="store_true",
                    help=f"CPU1 samples CPU0's PC every 1 ms into a ring at {SAMPLER_RING:#x} (tools/xnupcs.sh)")
    ap.add_argument("--fb", action="store_true",
                    help=f"panel on (U-Boot xnufb), {FB_W}x{FB_H}x{FB_BPP} framebuffer at {FB_PA:#x} as the video console")
    ap.add_argument("--disp", action="store_true",
                    help="enable clcd in DT and back IOMobileFramebuffer with Exynos FIMD0 (tools/xnushim/disp.c)")
    ap.add_argument("--gpu", action="store_true",
                    help="enable sgx in DT as Mali-400 IOAcceleratorES + UserClient (IOGLESBundleName=Mali400GLDriverEX)")
    ap.add_argument("--trust-cache", action="append", default=[],
                    help="signed Mach-O or hex CDHash list to load into AMFI loaded_trust_caches at boot")
    ap.add_argument("-o", default=os.path.join(ROOT, "out/xnu.itb"))
    a = ap.parse_args()
    cps = [(int(v, 0), mode) for v, mode in (c.split(":") for c in a.cp)]
    assert all(mode in "at" for _, mode in cps)
    traces = [tuple(int(x, 0) for x in t.split(":")) for t in a.trace]
    assert not (a.ramdisk and a.emmc_root), "--ramdisk or --emmc-root"
    assert a.emmc_root or not a.emmc_var, "--emmc-var needs --emmc-root"
    # launchd treats "rd=md0" in kern.bootargs as the restore environment and skips the Boot
    # tasks without PerformInRestore (fstab: no /private/var); IOFindBSDRoot also takes rootdev=
    root = " rootdev=md0" if a.normal_boot else " rd=md0"
    args = a.args + (root if a.ramdisk or a.emmc_root else "")
    assert a.emmc_root or not a.gpu_demo, "--gpu-demo runs on the first eMMC root I/O: needs --emmc-root"
    img, info = build(cps, a.stop, args, traces, a.vec, a.ramdisk, a.sampler, a.emmc_root, a.fb, a.emmc_var,
                      a.gpu_demo, a.trust_cache, a.disp, a.gpu)
    fit(img, info, a.o, a.fb)
    print(" ".join(f"{k}={v:#x}" for k, v in info.items()), f"size={len(img):#x}")


if __name__ == "__main__":
    main()

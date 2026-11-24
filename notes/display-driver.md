# iOS display stack on the A5 (n94) and how it maps to the Exynos 4412

Source: the iOS 9.3.5 kernelcache (`fw/kernelcache.n94.macho`) prelink info and
strings, and `fw/DeviceTree.n94ap.bin`. Nothing here is tested on hardware.

## Drivers and their device-tree nodes

| Kext (load address) | Class | Matches | A5 hardware | Exynos 4412 equivalent |
|---|---|---|---|---|
| `com.apple.iokit.IOMobileGraphicsFamily` (0x80d33000, 0x18000) | `IOMobileFramebuffer` base | | userland API that backboardd opens | keep as-is |
| `com.apple.driver.AppleH4PCLCD` (0x80d4b000, 0x34000) | `AppleMobileCLCD` | `clcd,s5l8940x` | Apple display pipe, reg 0x8a100000/0x7000, 0x89200000/0x2000, 0x89300000/0x1000; irq 0x2a, 0x2b | FIMD0 at 0x11c00000 (different IP) |
| `com.apple.driver.AppleSamsungMIPIDSI` (0x80e70000, 0x6000) | `AppleSamsungMIPIDSIController` | `mipi-dsim-1,samsung` | **Samsung DSIM** at 0x89500000/0x1000 (+0x8f110000/0x1000), irq 0x29 | Samsung DSIM at 0x11c80000: same IP family |
| `com.apple.driver.ApplePinotLCD` (0x8115f000, 0x3000) | `ApplePinotLCD` | `lcd,pinot` (child of mipi-dsim) | iPhone 4S LCD panel, reads a panel ID over DSI | s6e8aa0 AMOLED: different init commands |
| (dart) | DART IOMMU | `dart,s5l8940x` (`dart-rt`, `dart-nrt`) | display DMA goes through dart-rt | none: FIMD uses physical addresses |
| `AppleARMLightEmUp` | backlight | `backlight` | PMU backlight | none: the AMOLED panel has no backlight |

`mkxnu.py` currently disables all of these (`ARM_IO_KEEP`, `TOP_OFF`), so none start.

## What each layer needs

1. **DSIM (`AppleSamsungMIPIDSIController`)**: most promising. Apple licensed the Samsung
   DSIM. If the register layout matches the Exynos 4412 DSIM (compare its `_waitStatus`
   masks and register offsets with `samsung-dsim.c`), it can run unmodified with the
   `mipi-dsim` node's `reg` pointed at 0x11c80000. Two things still need checking:
   - its PLL and clock setup (SAC, "slow adaptive clocking", generates PLL frequency lists);
   - PHY power, which on Exynos is in the PMU (0x10020710), not in the DSIM.
2. **Panel (`ApplePinotLCD`)**: sends Pinot-specific DCS commands and checks the panel ID.
   It must be replaced or hooked to send the s6e8aa0 sequence (`u-boot/drivers/video/s6e8ax0.c`
   or Linux `panel-samsung-s6e8aa0.c`). Easiest path: U-Boot `xnufb` already initialises the
   panel, so make the panel methods no-ops that report success.
3. **Display pipe (`AppleMobileCLCD`)**: Apple's own hardware with no Exynos equivalent. It
   needs the AIC-style approach: hook its hardware-touching methods (`start_hardware`,
   `enable_clcd_interrupts`, `set_ditherTable_state`, `printRegs`,
   `do_power_state_change_gated`, the swap/flip path) and back them with FIMD window 0.
   Its framebuffers come from IOSurface through dart-rt, so the DART mapper must also be
   replaced by an identity mapper, or surfaces must be contiguous.
4. **IOMobileFramebuffer userland**: needs no changes, as long as `AppleMobileCLCD` reports
   720x1280 and accepts swaps.

## AppleH4PCLCD internals (static analysis)

Classes, identified through vtable slot 7 (`getMetaClass`) and the class name each
OSMetaClass constructor call passes (helper scripts in `tools/re/`: `vtnames.py`,
`stubs.py`, `xref.py`, `prelink.py`; run from the repo root; disassemble the kext with
`arm-none-eabi-objdump -D -b binary -marm -M force-thumb --adjust-vma=0x80d4b000`):

| vtable | class | slots |
|---|---|---|
| 0x80d6a2e8 | `AppleCLCD` (most derived) | 387 |
| 0x80d6a9dc | `AppleMobileCLCD` | 390 |
| 0x80d6b0a8 | `AppleH4DisplayPipe` | 379 |
| 0x80d6b6e8 | `Block` | 366 |
| 0x80d6c16c / 0x80d6c514 / 0x80d6cb20 | `IOMFBParamFIFO`, `ADPParamFIFO`, `ADBEParamFIFO` | ~214 |
| 0x80d6c8e4 / 0x80d6c9f0 | `MIEDriver` | 46 |

- Imports are only generic kernel calls (IOLog, IOMalloc, IOMemoryDescriptor, OSMetaClass,
  timers). Register mapping goes through virtual methods and through `AppleARMPlatform`
  (0x8054cXXX) and `IOMobileGraphicsFamily` (0x80d3XXXX) helpers, not kernel exports.
- Its strings mention `fRegisters` ("DisplayPipe fRegisters virtual = %p, physical = 0x%llx"),
  `read_register`/`write_register`/`write_registers`, and the DPU registers DPCENAB,
  DPUnSRCFMT, DPUnSRCBASE, DPUnMMUCNTL and DPUnMMUPGADDR0. Display planes go through their
  own MMU (DPUnMMU*), apart from the DART.
- The CLCD block uses Samsung FIMD names ("ENVID_F_READY_DOWN"), but the layout differs:
  the function at 0x80d4f0f0 polls bit 1 of register 0x50 through a pointer kept at
  `this+0xbff8`. FIMD has no such status at 0x50, so the driver cannot run unmodified
  against FIMD.
- Next step: find where `this+0xbff8` (and the DisplayPipe `fRegisters`) are set, and list
  every function that dereferences them. That list is the set of methods to hook.

## Order of work

1. Keep `xnufb` as the panel and DSIM initialisation (already done in U-Boot).
2. Enable only `clcd` in the device tree. Hook `AppleMobileCLCD` so `start_hardware`
   succeeds, interrupts come from FIMD (VSYNC = GIC SPI for LCD0), and swaps write the
   surface address to FIMD `VIDW00ADD0B0`/`ADD1B0`.
3. Replace the DART with an identity mapping (`dart-rt` mapper), or allocate
   contiguous display surfaces.
4. backboardd then opens IOMobileFramebuffer. Rendering still needs the GPU (`gpu/mali400`)
   or CoreImage's software path.

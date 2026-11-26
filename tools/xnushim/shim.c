/*
 * Exynos4412 platform shim for the iPhone4,1 (A5) XNU kernelcache.
 *
 * The kernel's pexpert keeps its timer behind four callbacks (tbd_ops):
 * FIQ handler, timebase, get/set decrementer. On A5 they drive the AIC timer;
 * here they drive the MCT: the 64-bit global counter is the timebase (24 MHz,
 * matches cpu0 timebase-frequency in the DT) and global comparator 0 is the
 * decrementer. Its interrupt (SPI 57, ID 89) is GIC group 0, which a secure CPU
 * receives as FIQ, the same path the AIC timer used.
 *
 * Calling conventions come from the kernel's callers: get_timebase and the
 * decrementer ops get cpu_data in r3 (4th argument).
 */
#include <stdint.h>

#define MCT_PA          0x10050000
#define GICD_PA         0x10490000
#define GICC_PA         0x10480000  /* CPU0; Exynos4 GIC is not banked, CPUn at +n*0x4000 */
#define PMU_PA          0x10020000
#define SYSRAM_PA       0x02020000
#define PL310_PA        0x10502000
/* MCT Local Timers L0..L3 (MCT + 0x300 + cpu*0x100) -> per-CPU GIC PPI 12 (ID 28). */
#define TIMER_PPI_ID    28u
#define L_BASE(cpu)     (0x300u + ((cpu) << 8))
#define L_TCNTB         0x00u
#define L_ICNTB         0x08u
#define L_TCON          0x20u
#define L_INT_CSTAT     0x30u
#define L_INT_ENB       0x34u
#define L_WSTAT         0x40u

#define G_CNT_L         0x100
#define G_CNT_U         0x104
#define G_CNT_WSTAT     0x110
#define G_COMP0_L       0x200
#define G_COMP0_U       0x204
#define G_TCON          0x240
#define G_INT_CSTAT     0x244
#define G_INT_ENB       0x248
#define G_WSTAT         0x24c
#define G_TCON_START    (1 << 8)
#define G_TCON_COMP0_AUTO_INC (1 << 1)
#define G_TCON_COMP0_EN (1 << 0)

#define GICD_CTLR       0x000
#define GICD_TYPER      0x004
#define GICD_IGROUPR    0x080
#define GICD_ISENABLER  0x100
#define GICD_ICENABLER  0x180
#define GICD_ICPENDR    0x280
#define GICD_IPRIORITYR 0x400
#define GICD_ITARGETSR  0x800
#define GICC_CTLR       0x00
#define GICC_PMR        0x04

#define CPU_DECREMENTER 0x70        /* cpu_data: soft copy of the decrementer */
#define MIN_DECREMENTER 240         /* 10 us: comparator must be ahead of the counter */

typedef uintptr_t (*ml_io_map_t)(uintptr_t pa, uint32_t size);
#define ml_io_map ((ml_io_map_t)0x800c0bf5)  /* Thumb */
#define cpu_signal_handler ((void (*)(void))0x800bd86d)
/* buf KPI (Thumb) */
typedef void *buf_t;
#define buf_map       ((int (*)(buf_t, void **))0x800dad01)
#define buf_unmap     ((int (*)(buf_t))0x800dad55)
#define buf_biodone   ((void (*)(buf_t))0x800db15d)
#define buf_seterror  ((void (*)(buf_t, int))0x800da991)
#define buf_setresid  ((void (*)(buf_t, uint32_t))0x800daa0d)
#define buf_count     ((uint32_t (*)(buf_t))0x800da9f9)
#define buf_blkno     ((int64_t (*)(buf_t))0x800dac91)
#define buf_flags     ((int32_t (*)(buf_t))0x800da9cd)
#define buf_device    ((uint32_t (*)(buf_t))0x800dacb9)
#define mdevstrategy  ((void (*)(buf_t))0x800d4461)
#define MDEV_SECSIZE(minor) (*(uint32_t *)(0x8043d2bc + 0x24 * (minor) + 0x10))  /* memdev.c mdev[] */
#define B_READ        0x1

#define CPU_DATA_FROM_THREAD 0x5ac
#define CPU_IRQ_HANDLER 0xbc        /* cpu_data->interrupt_handler(target, refCon, nub, source) */
#define GICD_SGIR       0xf00
#define GICC_IAR        0x0c
#define GICC_EOIR       0x10
#define IPI_SGI         0

#define WDT_PA          0x10060000
#define WTDAT           0x04
#define WTCNT           0x08
#define WDT_PET_LIMIT   (86400ull * 24000000) /* MCT ticks (24 h live boot) */

#define MSHC_PA         0x12550000  /* eMMC: DesignWare MMC 2.40a, left in transfer state by U-Boot */
#define MSHC_CTRL       0x000
#define MSHC_BLKSIZ     0x01c
#define MSHC_BYTCNT     0x020
#define MSHC_CMDARG     0x028
#define MSHC_CMD        0x02c
#define MSHC_RINTSTS    0x044
#define MSHC_STATUS     0x048
#define MSHC_BMOD       0x080
#define MSHC_DATA       0x200

uintptr_t mct, gicd, gicc, wdt, mshc, exy_pl310, pmu_va, sysram_va, boot_magic_va;
uint32_t exy_ncpus = 1;             /* patched by mkxnu.py from --args cpus=N (1..4) */
uint32_t exy_wdt_sec = 0;           /* 0=unlimited; patched by mkxnu.py to 160 when bench=1 */
volatile uint32_t exy_usb_hb;       /* incremented by exy_usb_poll_thread; stalls stop WDT petting */
uint64_t exy_deadline[4];           /* per-CPU deadlines read by exy_get_decrementer (fiq.S) */
uint32_t exy_smp_boot_info[5] __attribute__((aligned(32))) = {
    0x00020001u, 0x80000000u, 0x40000000u, 0x1ef00000u, 0u
};
uint32_t exy_smp_cpu_data_pa[4] __attribute__((aligned(32)));
uint32_t exy_smp_entered[4] __attribute__((aligned(32)));
uint32_t exy_smp_started __attribute__((aligned(32)));
extern char exy_smp_entry[];
static volatile uint32_t mct_lock, emmc_lock;
static uint64_t boot_mct0;
static int smp_ptd_cleaned;

/* Bring-up mailbox (trampoline, checkpoint stubs, trace, exception vectors; see
 * mkxnu.py). Lives here because the kernel keeps the shim mapped, while the
 * pages after it (boot_args, DT) may be freed once the kernel has copied them. */
uint32_t exy_mailbox[40] __attribute__((aligned(64)));  /* +128: user alignment faults (--vec fwd) */

/* debug: timer state, two cache lines, read with getvar mem:<dbg> */
struct {
    uint32_t sets, v, deadline, cnt, tcon, cstat, comp, signals;  /* last set_decrementer */
    uint32_t active, pending, rpr, hppir, tb_calls, tb_last, dec_calls, irqs;
    uint32_t irq_handler, cpu_signal, pad[6];
} exy_dbg __attribute__((aligned(32)));

static void clean_line(volatile void *p)
{
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 1\ndsb" :: "r"(p) : "memory");
    uintptr_t va = (uintptr_t)p;
    if (exy_pl310 && va >= 0x80000000u && va < 0xa0000000u) {
        *(volatile uint32_t *)(exy_pl310 + 0x7b0u) = (uint32_t)((va - 0x40000000u) & ~31u);
        *(volatile uint32_t *)(exy_pl310 + 0x730u) = 0u;
        __asm__ volatile("dsb" ::: "memory");
    }
}

static inline uint32_t rd(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void wr8(uintptr_t a, uint8_t v) { *(volatile uint8_t *)a = v; }

static inline uint32_t cur_cpu_id(void)
{
    uint32_t mpidr;
    __asm__ volatile("mrc p15, 0, %0, c0, c0, 5" : "=r"(mpidr));
    return mpidr & 3u;
}

static void spin_lock(volatile uint32_t *lk)
{
    uint32_t tmp;
    __asm__ volatile(
        "1: ldrex %0, [%1]\n"
        "   cmp   %0, #0\n"
        "   bne   1b\n"
        "   mov   %0, #1\n"
        "   strex %0, %0, [%1]\n"
        "   cmp   %0, #0\n"
        "   bne   1b\n"
        "   dmb   ish\n"
        : "=&r"(tmp) : "r"(lk) : "cc", "memory");
}

static void spin_unlock(volatile uint32_t *lk)
{
    __asm__ volatile("dmb ish" ::: "memory");
    *lk = 0;
    __asm__ volatile("dsb\nsev" ::: "memory");
}

__attribute__((section(".drv.text")))
static void clean_range_pa(const void *p, uint32_t pa, uint32_t len)
{
    uintptr_t va0 = (uintptr_t)p & ~31u;
    uint32_t pa0 = pa & ~31u;
    uint32_t n = ((uintptr_t)p - va0) + len;
    for (uint32_t off = 0; off < n; off += 32u)
        __asm__ volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(va0 + off) : "memory");
    __asm__ volatile("dsb" ::: "memory");
    if (exy_pl310 && pa0 >= 0x40000000u && pa0 < 0x80000000u) {
        for (uint32_t off = 0; off < n; off += 32u)
            *(volatile uint32_t *)(exy_pl310 + 0x7b0u) = pa0 + off;
        *(volatile uint32_t *)(exy_pl310 + 0x730u) = 0u;
        __asm__ volatile("dsb" ::: "memory");
    }
}

/* MCT registers in the 24 MHz domain: wait until the write is synced, then clear the flag. */
static void mct_wr(uint32_t off, uint32_t v)
{
    uint32_t stat, bit;
    if (off == G_CNT_L) {
        stat = G_CNT_WSTAT; bit = 1u << 0;
    } else if (off == G_CNT_U) {
        stat = G_CNT_WSTAT; bit = 1u << 1;
    } else if (off >= G_COMP0_L && off < G_TCON) {
        uint32_t c = (off - G_COMP0_L) >> 4;
        uint32_t hi = (off & 4u) ? 1u : 0u;
        stat = G_WSTAT; bit = 1u << (4u * c + hi);
    } else {
        stat = G_WSTAT; bit = 1u << 16;  /* G_TCON */
    }
    wr(mct + off, v);
    for (int i = 0; i < 100000 && !(rd(mct + stat) & bit); i++)
        ;
    wr(mct + stat, bit);
}

static uint64_t mct_count(void)
{
    uint32_t hi, lo;
    do {
        hi = rd(mct + G_CNT_U);
        lo = rd(mct + G_CNT_L);
    } while (hi != rd(mct + G_CNT_U));
    return (uint64_t)hi << 32 | lo;
}

static uint8_t *cpu_data(void)
{
    uintptr_t thread;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 4" : "=r"(thread));
    return *(uint8_t **)(thread + CPU_DATA_FROM_THREAD);
}

#define CPU_DATA_ENTRIES_PTR (*(uint32_t **)0x803e60b0u)

static int find_cpu_by_id(void *iocpu, uint32_t *out_pa, uint8_t **out_va)
{
    uint32_t *e = CPU_DATA_ENTRIES_PTR;
    if (!e || !iocpu)
        return -1;
    for (int i = 0; i < 4; i++) {
        uint8_t *va = (uint8_t *)(uintptr_t)e[i * 4 + 1];
        if (va && *(void **)(va + 0x2c) == iocpu) {
            if (out_pa) *out_pa = e[i * 4 + 0];
            if (out_va) *out_va = va;
            return i;
        }
    }
    return -1;
}

/* Replaces PE_cpu_signal(source, target): send IPI SGI 0 to target CPU (or self). */
void exy_cpu_signal(void *source, void *target)
{
    (void)source;
    uint32_t me = cur_cpu_id();
    int dst = find_cpu_by_id(target, 0, 0);
    uint32_t mask = (dst >= 0) ? (1u << (uint32_t)dst) : (1u << me);
    exy_dbg.signals++;
    __asm__ volatile("dsb" ::: "memory"); /* ensure cpu_signal store is visible before IPI */
    wr(gicd + (me << 14) + GICD_SGIR, (mask << 16) | (1u << 15) | IPI_SGI);
}

/* Replaces AppleARMCPU::startCPU(start_paddr, arg_paddr) via vtable 0x8057903c. */
__attribute__((section(".drv.text")))
int exy_cpu_start(void *self, uint32_t start_paddr, uint32_t arg_paddr)
{
    (void)start_paddr; (void)arg_paddr;
    uint32_t cpu_pa = 0;
    uint8_t *cpu_va = 0;
    int cpu = find_cpu_by_id(self, &cpu_pa, &cpu_va);
    if (cpu <= 0 || cpu >= 4 || !cpu_pa || !cpu_va || !pmu_va || !sysram_va)
        return 0;

    /* Copy timer callbacks (tbd_ops + mct + irq_handler) from CPU0 into secondary cpu_data. */
    uint8_t *cpu0 = (uint8_t *)(uintptr_t)CPU_DATA_ENTRIES_PTR[1];
    if (cpu0) {
        for (uint32_t off = 0x74; off <= 0x88; off += 4)
            *(uint32_t *)(cpu_va + off) = *(uint32_t *)(cpu0 + off);
        if (!*(void **)(cpu_va + CPU_IRQ_HANDLER))
            *(void **)(cpu_va + CPU_IRQ_HANDLER) = *(void **)(cpu0 + CPU_IRQ_HANDLER);
    }
    clean_range_pa(cpu_va, cpu_pa, 0x2c0);
    if (!smp_ptd_cleaned) {
        smp_ptd_cleaned = 1;
        clean_line((void *)0x800c7440u);
        if (exy_smp_boot_info[4])
            clean_range_pa((void *)(exy_smp_boot_info[4] + 0x40000000u), exy_smp_boot_info[4], 0x8000u);
    }
    exy_smp_cpu_data_pa[cpu] = cpu_pa;
    exy_smp_started |= (1u << (uint32_t)cpu);
    clean_line(exy_smp_cpu_data_pa);
    clean_line(exy_smp_boot_info);
    clean_line(&exy_smp_started);

    /* Write physical secondary entry into Exynos 4412 SYSRAM slot (0x02020000 + 4*cpu). */
    wr(sysram_va + (uint32_t)cpu * 4u, (uint32_t)(uintptr_t)exy_smp_entry - 0x40000000u);
    __asm__ volatile("dsb" ::: "memory");

    /* Power on ARM_COREn via Exynos 4412 PMU (0x10022000 + cpu*0x80). */
    uintptr_t cfg = pmu_va + 0x2000u + (uint32_t)cpu * 0x80u;
    if ((rd(cfg + 4u) & 3u) != 3u) {
        wr(cfg, 3u);
        for (int i = 0; i < 100000 && (rd(cfg + 4u) & 3u) != 3u; i++)
            ;
    }
    __asm__ volatile("dsb\nsev" ::: "memory");
    uint32_t me = cur_cpu_id();
    wr(gicd + (me << 14) + GICD_SGIR, (1u << (16u + (uint32_t)cpu)) | (1u << 15) | IPI_SGI);
    return 0;
}

/* Replaces AppleARMCPU::quiesceCPU() via vtable 0x80579038 (avoids NULL function-enable_core). */
__attribute__((section(".drv.text")))
void exy_cpu_quiesce(void *self)
{
    (void)self;
}

/* Replaces PEGetGMTTimeOfDay / PESetGMTTimeOfDay and IOPlatformExpert::get/setUTCTimeOfDay
 * using Exynos 4412 internal RTC (drivers.c), eliminating both 30s IORTC boot timeouts. */
extern uint32_t exy_rtc_read_epoch(void);
extern void exy_rtc_write_epoch(uint32_t sec);

uint32_t exy_gmt_time(void)
{
    return exy_rtc_read_epoch();
}

void exy_set_gmt_time(uint32_t sec)
{
    exy_rtc_write_epoch(sec);
}

int exy_get_utc_time(void *self, uint32_t *sec, uint32_t *usec)
{
    (void)self;
    if (sec) *sec = exy_rtc_read_epoch();
    if (usec) *usec = 0u;
    return 0;
}

void exy_set_utc_time(void *self, uint32_t sec, uint32_t usec)
{
    (void)self; (void)usec;
    exy_rtc_write_epoch(sec);
}

/* Replaces PEHaltRestart(type): halt (0), restart (1) and UPS halt (3) reset the board
 * through the bring-up watchdog (RAM, and so the log, survives); other types are no-ops.
 * The platform's own haltRestart has no reset device here and times out into a panic. */
__attribute__((section(".drv.text")))
int exy_halt_restart(unsigned type)
{
    if (type != 0 && type != 1 && type != 3)
        return 0;
    /* Preserve MODE_FASTBOOT if set by exy_reboot_fastboot(), else mark warm reboot (MODE_SKIP_LPM). */
    if (boot_magic_va) {
        volatile uint32_t *dm = (volatile uint32_t *)(boot_magic_va + 0xff0u);
        if (*dm != 0x12345671u) {
            *dm = 0x12345670u;
            __asm__ volatile("dsb" ::: "memory");
        }
    }
    /* Restore full boot voltages before warm reset so U-Boot (800 MHz) never browns out. */
    extern void exy_cpu_prep_reboot(void) __attribute__((weak));
    if (exy_cpu_prep_reboot)
        exy_cpu_prep_reboot();
    /* Flush recent console/panic log lines from L1/L2 to DRAM before WDT reset. */
    extern void exy_log_flush(void);
    exy_log_flush();
    wr(wdt + WTDAT, 1);
    wr(wdt + WTCNT, 1);
    for (;;)
        __asm__ volatile("wfi");
}

/* Write midas U-Boot boot-mode magic to BOTH PMU INFORM2 (0x808) and reserved DRAM (0x5ef7fff0,
 * mapped uncached via ml_io_map above XNU MEMSIZE+pram so XNU never allocates/overwrites it).
 * BL2 stage-1 U-Boot clears PMU INFORM/RST_STAT before chainloading boot, whereas 0x5ef7fff0
 * survives warm WDT reset intact so out/chain.itb enters fastboot (0x12345671) or skips LPM (0x12345670). */
__attribute__((section(".drv.text")))
static void exy_set_boot_magic(uint32_t magic)
{
    if (boot_magic_va) {
        wr(boot_magic_va + 0xff0u, magic);
        __asm__ volatile("dsb" ::: "memory");
    }
    if (pmu_va) {
        wr(pmu_va + 0x808u, magic);
        __asm__ volatile("dsb" ::: "memory");
    }
}

__attribute__((section(".drv.text")))
void exy_reboot_fastboot(void)
{
    exy_set_boot_magic(0x12345671u);
    exy_halt_restart(1u);
}

/*
 * eMMC root (mkxnu --emmc-root): md0 keeps the kernel's RAM-disk plumbing (rd=md0,
 * /chosen/memory-map/RAMDisk sizes it) but its strategy routine reads the eMMC from
 * exy_emmc_lba on. Polled FIFO transfers; U-Boot set up clock, bus width and card.
 * mkxnu --emmc-var adds md1 (block node only), read-write, on GPT USERDATA for
 * /private/var. md0 stays read-only.
 * shortcut: polled on the calling thread; add DMA and interrupts when I/O gets too slow.
 */
uint32_t exy_emmc_lba;  /* patched by mkxnu; 0: md0 is a plain RAM disk */
uint32_t exy_var_lba, exy_var_sectors;  /* patched by mkxnu --emmc-var; 0: no md1 */
uint32_t exy_gpu_on;  /* patched by mkxnu --gpu-demo */
void exy_gpu_demo(void);  /* gpu.c, in the .drv region */
uint32_t exy_emmc_idma_xfer(uint32_t lba, uint32_t n, uint32_t *p, int write); /* touch.c, in .drv */
/* debug: transfers, errors, md1 reads, md1 writes, then a ring of the last 64 {lba, bytes | write << 31, sum of words, status} */
struct {
    uint32_t reads, errors, pad[2];
    uint32_t ring[64][4];
} exy_emmc_log __attribute__((aligned(32)));

#define MSHC_ERRS  0xbfc2  /* RE RCRC DCRC RTO DRTO HTO FRUN HLE SBE EBE */
#define MSHC_SPIN  20000000

#define MSHC_FIFO_DEPTH 0x80  /* words, exynos4412 dw_mmc fifo-depth */

__attribute__((section(".drv.text")))
static uint32_t emmc_xfer(uint32_t lba, uint32_t n, uint32_t *p, int write)
{
    uint32_t words = n * 128, st = 0, i;
    for (i = 0; i < MSHC_SPIN && (rd(mshc + MSHC_STATUS) & (1 << 9)); i++)  /* data busy */
        ;
    wr(mshc + MSHC_BMOD, 0);  /* U-Boot left the internal DMA on */
    wr(mshc + MSHC_CTRL, (rd(mshc + MSHC_CTRL) & ~(1u << 25 | 1u << 5)) | 1u << 1);  /* PIO, FIFO reset */
    for (i = 0; i < MSHC_SPIN && (rd(mshc + MSHC_CTRL) & (1u << 1)); i++)
        ;
    wr(mshc + MSHC_RINTSTS, ~0u);
    wr(mshc + MSHC_BLKSIZ, 512);
    wr(mshc + MSHC_BYTCNT, n * 512);
    wr(mshc + MSHC_CMDARG, lba);  /* high capacity: block address */
    /* start, hold reg, wait prev data, data, response + CRC; CMD18/25 with auto CMD12 */
    wr(mshc + MSHC_CMD, 1u << 31 | 1u << 29 | 1u << 13 | (write ? 1u << 10 : 0) | 1u << 9 |
                        1u << 8 | 1u << 6 | (n > 1 ? 1u << 12 | (write ? 25 : 18) : (write ? 24 : 17)));
    for (i = 0; i < MSHC_SPIN; i++) {
        st = rd(mshc + MSHC_RINTSTS);
        if (st & MSHC_ERRS)
            return st;
        if (write) {
            for (uint32_t c; words && (c = (rd(mshc + MSHC_STATUS) >> 17) & 0x1fff) < MSHC_FIFO_DEPTH; ) {
                uint32_t avail = MSHC_FIFO_DEPTH - c;
                if (avail > words) avail = words;
                words -= avail;
                while (avail--)
                    wr(mshc + MSHC_DATA, *p++);
            }
        } else {
            for (uint32_t c; words && (c = (rd(mshc + MSHC_STATUS) >> 17) & 0x1fff); ) {
                if (c > words) c = words;
                words -= c;
                while (c >= 4) {
                    p[0] = rd(mshc + MSHC_DATA);
                    p[1] = rd(mshc + MSHC_DATA);
                    p[2] = rd(mshc + MSHC_DATA);
                    p[3] = rd(mshc + MSHC_DATA);
                    p += 4;
                    c -= 4;
                }
                while (c--)
                    *p++ = rd(mshc + MSHC_DATA);
            }
        }
        if ((st & (1 << 3)) && !words)  /* DTO, all moved */
            break;
    }
    for (; n > 1 && i < MSHC_SPIN && !(rd(mshc + MSHC_RINTSTS) & (1 << 14)); i++)  /* auto stop done */
        ;
    for (; write && i < MSHC_SPIN && (rd(mshc + MSHC_STATUS) & (1 << 9)); i++)  /* card programming */
        ;
    wr(mshc + MSHC_RINTSTS, ~0u);
    return i < MSHC_SPIN ? 0 : (st | 1u << 31);
}

/* memdev.c mdevadd for md1, as inlined in IOFindBSDRoot (0x8037b584): mdev[1] and the
 * block node only; the char node's mdevrw would copy RAM at mdev.base, not the eMMC. */
#define MDEV(minor)       ((uint32_t *)(0x8043d2bc + 0x24 * (minor)))
#define MDEV_BMAJOR       (*(int *)0x80403be0)
#define devfs_make_node   ((void *(*)(uint32_t, int, uint32_t, uint32_t, int, const char *, ...))0x80111c35)

static const char md_node_fmt[] __attribute__((section(".drv.rodata"))) = "md%d";

__attribute__((section(".drv.text")))
static void md1_add(void)
{
    static int done;
    uint32_t *md = MDEV(1);

    if (done || !exy_var_lba || (md[3] & 1))
        return;
    done = 1;
    md[0] = md[1] = 0;
    md[2] = exy_var_sectors >> 3;  /* pages */
    md[4] = 512;
    md[5] = 1 | (uint32_t)MDEV_BMAJOR << 24;
    md[7] = (uint32_t)devfs_make_node(md[5], 1 /* DEVFS_BLOCK */, 0, 5 /* operator */, 0600, md_node_fmt, 1);
    md[3] |= 1;  /* mdInited */
}

/* Replaces mdevbdevsw.d_strategy. */
#define shim_iolock_alloc   ((void *(*)(void))0x8032969d)
#define shim_iolock_mach    ((void *(*)(void *))0x80329749)
#define shim_lck_mtx_lock   ((void (*)(void *))0x800c2ae9)
#define shim_lck_mtx_unlock ((void (*)(void *))0x800c2b79)
static void *emmc_mtx;
volatile uint32_t exy_last_emmc_mct;

__attribute__((section(".drv.text")))
void exy_md_strategy(buf_t bp)
{
    uint32_t minor = buf_device(bp) & 0xffffff;
    md1_add();  /* first I/O comes after IOFindBSDRoot made md0: devfs is up */
    if (exy_gpu_on) {  /* once, in thread context: Mali draws into the console framebuffer */
        exy_gpu_on = 0;
        exy_gpu_demo();
    }
    if (!exy_emmc_lba || minor > 1 || (minor == 1 && !exy_var_lba)) {
        mdevstrategy(bp);
        return;
    }
    uint32_t count = buf_count(bp), err = 0, base = minor ? exy_var_lba : exy_emmc_lba;
    uint64_t off = (uint64_t)buf_blkno(bp) * MDEV_SECSIZE(minor);
    uint64_t limit = minor ? (uint64_t)exy_var_sectors << 9 : (uint64_t)MDEV(0)[2] << 12;
    int write = !(buf_flags(bp) & B_READ);
    void *va;
    int map_err = 0;
    if (write && minor == 0)
        err = 30;  /* EROFS */
    else if (((off | count) & 511) || off + count > limit)
        err = 22;  /* EINVAL: never touch sectors outside the partition */
    else if (count && (map_err = buf_map(bp, &va)) == 0) {
        uint32_t lba = base + (uint32_t)(off >> 9), st, sum = 0;
        /* Use an XNU mutex so IRQ/FIQ (VSYNC, scheduler, IPIs) stay unmasked across eMMC I/O. */
        if (!emmc_mtx) {
            void *l = shim_iolock_alloc();
            void *m = l ? shim_iolock_mach(l) : 0;
            if (m)
                (void)__sync_val_compare_and_swap(&emmc_mtx, (void *)0, m);
        }
        uint32_t cpsr = 0;
        int used_mtx = (emmc_mtx != 0);
        if (used_mtx) {
            shim_lck_mtx_lock(emmc_mtx);
        } else {
            __asm__ volatile("mrs %0, cpsr\ncpsid if" : "=r"(cpsr) :: "memory");
            spin_lock(&emmc_lock);
        }
        uint32_t t0 = rd(mct + G_CNT_L);
        exy_last_emmc_mct = t0;
        if ((st = exy_emmc_idma_xfer(lba, count >> 9, va, write)) &&
            (st = emmc_xfer(lba, count >> 9, va, write))) {
            exy_emmc_log.errors++;
            err = 5;  /* EIO */
        }
        exy_emmc_log.pad[0] += rd(mct + G_CNT_L) - t0; /* hardware transfer ticks (excluding mutex wait) */
        exy_emmc_log.pad[1] += count;                  /* total bytes transferred */
        uint32_t idx = exy_emmc_log.reads++;
        uint32_t *r = exy_emmc_log.ring[idx % 64];
        r[0] = lba; r[1] = count | (uint32_t)write << 31; r[2] = sum; r[3] = st;
        if (used_mtx) {
            shim_lck_mtx_unlock(emmc_mtx);
        } else {
            spin_unlock(&emmc_lock);
            __asm__ volatile("msr cpsr_c, %0" :: "r"(cpsr) : "memory");
        }
        buf_unmap(bp);
    } else if (count) {
        err = 5;
        uint32_t *r = exy_emmc_log.ring[exy_emmc_log.reads++ % 64];
        r[0] = (uint32_t)(off >> 9); r[1] = count | (uint32_t)write << 31; r[2] = (uint32_t)map_err; r[3] = 0xbb000005u;
        exy_emmc_log.errors++;
    }
    if (err && err != 5) {
        uint32_t *r = exy_emmc_log.ring[exy_emmc_log.reads++ % 64];
        r[0] = (uint32_t)(off >> 9); r[1] = count | (uint32_t)write << 31; r[2] = minor; r[3] = 0xee000000u | err;
        exy_emmc_log.errors++;
    }
    if (err || (exy_emmc_log.reads & 63u) == 0u)
        clean_line(&exy_emmc_log);
    if (err)
        buf_seterror(bp, err);
    buf_setresid(bp, err ? count : 0);
    buf_biodone(bp);
}

/*
 * AIC (A5, "aic,1") registers on top of the GIC. AppleInterruptController reaches
 * every register through its readReg/writeReg virtuals, which point here (mkxnu
 * VTABLE_HOOKS). AIC external interrupt n is GIC SPI n (ID n + 32), so AIC
 * bitmap word k is GIC distributor word k + 1. AIC masks an interrupt when IACK
 * returns it and the driver unmasks it after the handler: do the same with the
 * GIC enable bit, then EOI right away.
 */
#define AIC_INFO        0x0004
#define AIC_CONFIG      0x0010
#define AIC_WHOAMI      0x2000
#define AIC_IACK        0x2004
#define AIC_IPI_SET     0x2008
#define AIC_TARGET      0x3000      /* + 4n */
#define AIC_SW_SET      0x4000      /* + 4k: bitmaps */
#define AIC_SW_CLR      0x4080
#define AIC_MASK_SET    0x4100
#define AIC_MASK_CLR    0x4180
#define AIC_HW_STATE    0x4200
#define AIC_EXT_INTS    192         /* DT ipid-mask has 6 words; GIC IDs past 159 read as zero */
#define AIC_IACK_HW     (1u << 16)
#define AIC_IACK_IPI    (4u << 16)
#define AIC_IACK_OTHER  1u
#define GICD_ISPENDR    0x200
#define GIC_WORDS       5           /* Exynos4412: 160 interrupt IDs */

/* SPIs Apple drivers may enable. Their DT interrupt numbers are A5 sources, which
 * land on unrelated Exynos lines, so none for now.
 * shortcut: empty list; add an entry with each Exynos driver/DT remap. */
static const uint32_t aic_allow[GIC_WORDS];

static uint32_t aic_config;

struct {
    uint32_t reads, writes, iacks, last_iack, unk_rd, unk_wr, unk_val, hw;
} exy_aic_dbg __attribute__((aligned(32)));

/* Bits of distributor word w the AIC driver may change: never SGIs/PPIs (w == 0). */
static uint32_t aic_bits(uint32_t w, uint32_t v)
{
    if (w == 0 || w >= GIC_WORDS)
        return 0;
    return v;
}

extern int exy_mali_try_irq(uint32_t gic_id) __attribute__((weak));

static uint32_t aic_iack(void)
{
    uint32_t me = cur_cpu_id();
    uintptr_t my_gicc = gicc + (me << 14);
    for (;;) {
        uint32_t iar = rd(my_gicc + GICC_IAR), id = iar & 0x3ff;
        if (id == 1023)
            return 0;
        if (id == TIMER_PPI_ID) { /* group 0 local timer seen first by IRQ path: hand back to FIQ */
            wr(gicd + (me << 14) + GICD_ISPENDR, 1u << TIMER_PPI_ID);
            wr(my_gicc + GICC_EOIR, iar);
            return 0;
        }
        if (id < 16) {
            wr(my_gicc + GICC_EOIR, iar);
            return AIC_IACK_IPI | AIC_IACK_OTHER;
        }
        if (id < 32) {
            wr(my_gicc + GICC_EOIR, iar);
            continue;
        }
        if (id >= 155u && id <= 159u && exy_mali_try_irq && exy_mali_try_irq(id)) {
            wr(my_gicc + GICC_EOIR, iar);
            continue;
        }
        wr(gicd + GICD_ICENABLER + 4 * (id / 32), 1u << (id % 32));
        wr(my_gicc + GICC_EOIR, iar);
        exy_aic_dbg.hw++;
        return AIC_IACK_HW | (id - 32);
    }
}

uint32_t exy_aic_read(void *self, uint32_t off)
{
    (void)self;
    uint32_t v = 0, w = (off & 0x7f) / 4 + 1;
    exy_aic_dbg.reads++;
    if (off == AIC_INFO)
        v = ((exy_ncpus - 1u) << 16) | AIC_EXT_INTS;
    else if (off == AIC_CONFIG)
        v = aic_config;
    else if (off == AIC_WHOAMI)
        v = cur_cpu_id();
    else if (off == AIC_IACK) {
        v = aic_iack();
        exy_aic_dbg.iacks++;
        exy_aic_dbg.last_iack = v;
    } else if (off >= AIC_TARGET && off < AIC_TARGET + 4 * AIC_EXT_INTS) {
        uint32_t id = (off - AIC_TARGET) / 4 + 32;
        v = id < 32 * GIC_WORDS ? *(volatile uint8_t *)(gicd + GICD_ITARGETSR + id) : 0;
    } else if (off >= AIC_MASK_SET && off < AIC_MASK_CLR + 0x80)
        v = w < GIC_WORDS ? ~rd(gicd + GICD_ISENABLER + 4 * w) : ~0u;
    else if ((off >= AIC_HW_STATE && off < AIC_HW_STATE + 0x80) ||
             (off >= AIC_SW_SET && off < AIC_SW_CLR + 0x80))
        v = w < GIC_WORDS ? rd(gicd + GICD_ISPENDR + 4 * w) : 0;
    else
        exy_aic_dbg.unk_rd = off;
    return v;
}

void exy_aic_write(void *self, uint32_t off, uint32_t v)
{
    (void)self;
    uint32_t w = (off & 0x7f) / 4 + 1;
    exy_aic_dbg.writes++;
    if (off == AIC_CONFIG)
        aic_config = v;
    else if (off == AIC_IPI_SET) {
        uint32_t me = cur_cpu_id();
        uint32_t mask = (v & 0x0fu) | ((v & 0x80000000u) ? (1u << me) : 0u);
        if (!mask) mask = 1u << me;
        __asm__ volatile("dsb" ::: "memory");
        wr(gicd + (me << 14) + GICD_SGIR, (mask << 16) | (1u << 15) | IPI_SGI);
    } else if (off >= AIC_TARGET && off < AIC_TARGET + 4 * AIC_EXT_INTS) {
        uint32_t id = (off - AIC_TARGET) / 4 + 32;
        if (id < 32 * GIC_WORDS)
            wr8(gicd + GICD_ITARGETSR + id, v & 0xff);
    } else if (off >= AIC_SW_SET && off < AIC_SW_SET + 0x80)
        wr(gicd + GICD_ISPENDR + 4 * w, aic_bits(w, v));
    else if (off >= AIC_SW_CLR && off < AIC_SW_CLR + 0x80)
        wr(gicd + GICD_ICPENDR + 4 * w, aic_bits(w, v));
    else if (off >= AIC_MASK_SET && off < AIC_MASK_SET + 0x80)
        wr(gicd + GICD_ICENABLER + 4 * w, aic_bits(w, v));
    else if (off >= AIC_MASK_CLR && off < AIC_MASK_CLR + 0x80)
        wr(gicd + GICD_ISENABLER + 4 * w, aic_bits(w, v & (w < GIC_WORDS ? aic_allow[w] : 0)));
    else if (off != 0x200c && !(off >= 0x5000 && off < 0x6000)) {  /* IPI ack/mask: SGIs need neither */
        exy_aic_dbg.unk_wr = off;
        exy_aic_dbg.unk_val = v;
    }
}

/* IRQ dispatch until IOKit installs its interrupt controller: only IPIs exist yet. */
void exy_irq(void *target, void *refcon, void *nub, int source)
{
    (void)target; (void)refcon; (void)nub; (void)source;
    exy_dbg.irqs++;
    clean_line(&exy_dbg.active);
    for (uint32_t ev; (ev = aic_iack()); )   /* also hands a timer seen here back to the FIQ */
        if (ev == (AIC_IACK_IPI | AIC_IACK_OTHER))
            cpu_signal_handler();
}

/* Replaces the AIC timer setup in pe_arm_init_timer; the return value becomes
 * timer_base (r10 in the FIQ handler). */
uintptr_t exy_timer_init(uintptr_t aic_unused)
{
    (void)aic_unused;
    mct = ml_io_map(MCT_PA, 0x1000);
    gicd = ml_io_map(GICD_PA, 0x10000);
    gicc = ml_io_map(GICC_PA, 0x10000);
    wdt = ml_io_map(WDT_PA, 0x1000);
    mshc = ml_io_map(MSHC_PA, 0x1000);
    exy_pl310 = ml_io_map(PL310_PA, 0x1000);
    pmu_va = ml_io_map(PMU_PA, 0x4000);
    sysram_va = ml_io_map(SYSRAM_PA, 0x1000);
    boot_magic_va = ml_io_map(0x5ef7f000u, 0x1000u);

    /* Pre-arm MODE_FASTBOOT in reserved DRAM (0x5ef7fff0, outside XNU MEMSIZE) & INFORM2 so any
     * unexpected hardware WDT reset returns directly to fastboot (never LPM) with RAM logs intact. */
    if (boot_magic_va) {
        wr(boot_magic_va + 0xff0u, 0x12345671u);
        __asm__ volatile("dsb" ::: "memory");
    }
    if (pmu_va)
        wr(pmu_va + 0x808u, 0x12345671u);

    wr(mct + G_INT_ENB, 0);
    wr(mct + G_INT_CSTAT, 0xf);
    uint32_t tcon = rd(mct + G_TCON) & ~0xffu; /* disable global comparators */
    if (!(tcon & G_TCON_START)) {
        mct_wr(G_CNT_L, 0);
        mct_wr(G_CNT_U, 0);
    }
    mct_wr(G_TCON, tcon | G_TCON_START);
    boot_mct0 = mct_count();

    /* Initialize per-CPU MCT local timers L0..L3 (TCNTB=1 -> 12 MHz ICNT tick, one-shot FIQ via PPI 12). */
    for (uint32_t c = 0; c < 4u; c++) {
        uintptr_t lb = mct + L_BASE(c);
        wr(lb + L_INT_ENB, 0u);
        wr(lb + L_TCON, 0u);
        for (int i = 0; i < 100000 && !(rd(lb + L_WSTAT) & (1u << 3)); i++) ;
        wr(lb + L_WSTAT, 1u << 3);
        wr(lb + L_INT_CSTAT, 3u);
        wr(lb + L_TCNTB, 1u);
        for (int i = 0; i < 100000 && !(rd(lb + L_WSTAT) & (1u << 0)); i++) ;
        wr(lb + L_WSTAT, 1u << 0);
        wr(lb + L_INT_ENB, 1u);
    }

    /* Everything group 1 (IRQ) and off; CPU0 PPI 12 (ID 28) is group 0 (FIQ) at priority 0x00. */
    wr(gicd + GICD_CTLR, 0);
    uint32_t regs = (rd(gicd + GICD_TYPER) & 0x1f) + 1;
    for (uint32_t i = 0; i < regs; i++) {
        wr(gicd + GICD_ICENABLER + 4 * i, ~0u);
        wr(gicd + GICD_ICPENDR + 4 * i, ~0u);
        wr(gicd + GICD_IGROUPR + 4 * i, ~0u);
    }
    /* IRQs below the timer: with a tie, IAR in the FIQ handler would return a pending
     * IPI (lower ID) instead of the timer. */
    for (uint32_t i = 0; i < 8 * regs; i++)
        wr(gicd + GICD_IPRIORITYR + 4 * i, 0x80808080);
    wr(gicd + GICD_IGROUPR, ~(1u << TIMER_PPI_ID));
    wr8(gicd + GICD_IPRIORITYR + TIMER_PPI_ID, 0x00);
    wr(gicd + GICD_CTLR, 3);                /* EnableGrp0 | EnableGrp1 */
    wr(gicc + GICC_PMR, 0xf0);
    wr(gicc + GICC_CTLR, 0xf);              /* EnableGrp0 | EnableGrp1 | AckCtl | FIQEn */
    wr(gicd + GICD_ISENABLER, (1u << TIMER_PPI_ID) | (1u << IPI_SGI));

    uint8_t *cpu = cpu_data();
    if (!*(void **)(cpu + CPU_IRQ_HANDLER))
        *(void **)(cpu + CPU_IRQ_HANDLER) = (void *)exy_irq;
    return mct;
}

/* exy_get_timebase, exy_get_decrementer: fiq.S (must not touch the stack) */

/* Console capture: replaces serial_putc. No UART cable, so the log goes to RAM,
 * line-cleaned to memory so it survives a watchdog reset; read it back with
 * "fastboot getvar mem:" (tools/xnulog.sh). */
#define LOG_MAGIC 0x474f4c58  /* "XLOG" */
#define LOG_SIZE  0x40000  /* tools/xnulog.py SIZE; early launchd output survives the sandbox log spam */
struct {
    uint32_t magic, pos;
    char buf[LOG_SIZE];
} exy_log;

void exy_log_flush(void)
{
    uint32_t end = exy_log.pos;
    uint32_t start = (end > 8192u) ? (end - 8192u) : 0u;
    for (uint32_t p = start & ~31u; p <= end; p += 32u)
        clean_line(&exy_log.buf[p % LOG_SIZE]);
    clean_line(&exy_log);
    __asm__ volatile("dsb" ::: "memory");
}

void exy_putc(char c)
{
    /* Drop "AppleKeyStore: o..." lines: thousands per boot evict the whole ring.
     * Kernel lines end "\n\r", so the text starts 1 byte after line_start.
     * shortcut: spot-checks 2 chars ("A" at 1, "o" at 16) and rewinds without locking; debug log only. */
    static uint32_t line_start, skip;
    if (skip) {
        skip = c != '\n';
        return;
    }
    uint32_t p = __sync_fetch_and_add(&exy_log.pos, 1u);
    char *slot = &exy_log.buf[p % LOG_SIZE];
    *slot = c;
    exy_log.magic = LOG_MAGIC;
    if (c == 'o' && p - line_start == 16u && exy_log.buf[(line_start + 1u) % LOG_SIZE] == 'A') {
        exy_log.pos = line_start;
        skip = 1;
    }
    if (c == '\n') {
        line_start = p + 1u;
    }
}

static inline void mct_l_wr(uintptr_t lb, uint32_t off, uint32_t v, uint32_t wbit)
{
    wr(lb + off, v);
    for (int i = 0; i < 100000 && !(rd(lb + L_WSTAT) & wbit); i++)
        ;
    wr(lb + L_WSTAT, wbit);
}

/* Fire per-CPU MCT local timer Ln in v 24 MHz ticks (lock-free, PPI 12 = FIQ). */
static void arm_comparator(uint32_t cpu, uint32_t v)
{
    if (v < MIN_DECREMENTER)
        v = MIN_DECREMENTER;
    /* Cap idle decrementer on CPU0 to 10 s so WDT (~42 s) never falsely fires if CPU0 stays idle. */
    if (cpu == 0u && v > 10u * 24000000u)
        v = 10u * 24000000u;
    uint32_t icnt = (v + 1u) >> 1; /* TCNTB=1 -> 12 MHz ICNT tick */
    if (icnt < 1u)
        icnt = 1u;
    if (icnt > 0x7fffffffu)
        icnt = 0x7fffffffu;
    uintptr_t lb = mct + L_BASE(cpu);
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr\ncpsid if" : "=r"(cpsr) :: "memory");
    exy_deadline[cpu] = mct_count() + v;
    mct_l_wr(lb, L_TCON, 0u, 1u << 3);
    wr(lb + L_INT_CSTAT, 3u);
    mct_l_wr(lb, L_ICNTB, icnt | 0x80000000u, 1u << 1);
    mct_l_wr(lb, L_TCON, 3u, 1u << 3); /* TIMER_START | INT_START (one-shot) */
    __asm__ volatile("msr cpsr_c, %0" :: "r"(cpsr) : "memory");
}

void exy_set_decrementer(uint32_t v, uint32_t b, uint32_t c, uint8_t *cpu)
{
    (void)b; (void)c;
    uint32_t id = *(volatile uint16_t *)cpu & 3u;
    *(volatile uint32_t *)(cpu + CPU_DECREMENTER) = v;
    arm_comparator(id, v);
    if (id == 0u) {
        static uint32_t last_hb;
        static uint64_t last_hb_mct;
        uint64_t now = mct_count();
        uint32_t hb = exy_usb_hb;
        if (hb != last_hb || !last_hb_mct) {
            last_hb = hb;
            last_hb_mct = now;
        }
        int alive = ((now - boot_mct0) < 35ull * 24000000ull) || ((now - last_hb_mct) < 25ull * 24000000ull);
        if (alive && (!exy_wdt_sec || (now - boot_mct0) < (uint64_t)exy_wdt_sec * 24000000ull))
            wr(wdt + WTCNT, rd(wdt + WTDAT));
    }

    uint32_t lr = (uint32_t)(uintptr_t)__builtin_return_address(0);
    exy_dbg.sets++;
    exy_dbg.v = v;
    exy_dbg.deadline = lr;
    if (lr == 0x800bd4cdu) exy_dbg.cnt++;   /* machine_idle exit */
    if (lr == 0x800cbe33u) exy_dbg.comp++;  /* idle_timeout entry */
    exy_dbg.pad[id]++;
    if (v <= 2400u) {
        exy_dbg.pad[4]++;
        exy_dbg.pad[5] = lr;
    }
}

/* Patched into AMFI init (0x807650da) in place of storing NULL to loaded_trust_caches. */
void *exy_tc_head;

void exy_amfi_init_tc(void **head)
{
    *head = exy_tc_head;
}

/*
 * User data aborts that fail in vm_fault (mkxnu USER_FAULT_SITE): sleh_abort turns them into
 * EXC_BAD_ACCESS, i.e. SIGSEGV (KERN_INVALID_ADDRESS) or SIGBUS (anything else). Ring of the
 * last 16 {pid, kern_return, fault page, user pc, user lr, shared cache slide};
 * address: fault= in the info line.
 */
#define vm_fault_internal ((uint32_t (*)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))0x800792c5)
#define fault_selfpid ((int (*)(void))0x802a72c9)
#define fault_task ((uint8_t *(*)(void))0x8003f97d)  /* current_task */
struct {
    uint32_t count, pad[7];
    uint32_t ring[16][8];
} exy_fault_log __attribute__((aligned(32)));

__attribute__((section(".drv.text")))
uint32_t exy_user_vm_fault_c(void *map, uint32_t va, uint32_t prot, uint32_t wiring,
                             uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, const uint32_t *ss)
{
    uint32_t kr = vm_fault_internal(map, va, prot, wiring, a4, a5, a6, a7);
    if (kr && kr != 14) {  /* 14: KERN_ABORTED, retried by sleh_abort */
        uint32_t *r = exy_fault_log.ring[exy_fault_log.count++ % 16];
        r[0] = (uint32_t)fault_selfpid();
        r[1] = kr;
        r[2] = va;
        r[3] = ss ? ss[0x3c / 4] : 0;  /* user pc */
        r[4] = ss ? ss[0x38 / 4] : 0;  /* user lr */
        /* task->shared_region->slide, as the panic log's "Slide" (0x800caeac) */
        uint8_t *sr = *(uint8_t **)(fault_task() + 0x208);
        r[5] = sr ? *(uint32_t *)(sr + 0x68) : ~0u;
        clean_line(r);
        clean_line(&exy_fault_log);
    }
    return kr;
}

/* Call site entry: copy the 4 stack args and add sleh_abort's saved user state pointer,
 * which it keeps at sp[0x70] (see 0x800ccbf2). */
__attribute__((naked)) uint32_t exy_user_vm_fault(void)
{
    __asm__ volatile(
        "push {r4, lr}\n"
        "sub sp, sp, #24\n"
        "ldr r12, [sp, #32]\n"
        "str r12, [sp]\n"
        "ldr r12, [sp, #36]\n"
        "str r12, [sp, #4]\n"
        "ldr r12, [sp, #40]\n"
        "str r12, [sp, #8]\n"
        "ldr r12, [sp, #44]\n"
        "str r12, [sp, #12]\n"
        "ldr r12, [sp, #144]\n"     /* 32 + 0x70 */
        "str r12, [sp, #16]\n"
        "bl exy_user_vm_fault_c\n"
        "add sp, sp, #24\n"
        "pop {r4, pc}\n");
}

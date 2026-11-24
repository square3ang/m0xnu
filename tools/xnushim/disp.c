/*
 * Minimal FIMD0 display backend for IOMobileFramebuffer (mkxnu --disp).
 * Built into DRV_AREA alongside gpu.c.
 *
 * U-Boot's "xnufb" already powers the s6e8aa0 panel, DSIM and FIMD0 window 0.
 * mkxnu resets AppleCLCD's hardware-touching vtable overrides back to
 * IOMobileFramebuffer's base methods and plugs these three hooks in:
 *   - exy_clcd_noop           (slot 312, +0x4e0)
 *   - exy_clcd_start_hardware (slot 270, +0x438)
 *   - exy_clcd_swap_begin     (slot 272, +0x440)
 */
#include <stdint.h>

typedef uintptr_t (*ml_io_map_t)(uintptr_t pa, uint32_t size);
#define ml_io_map ((ml_io_map_t)0x800c0bf5)  /* Thumb */

/* IOMobileFramebuffer::swap_complete_gated(this, bool completed), Thumb */
typedef void (*iomfb_swap_complete_t)(void *self, int completed);
#define iomfb_swap_complete ((iomfb_swap_complete_t)0x80d39a55)
typedef void (*iomfb_vbl_notify_t)(void *self, uint32_t period_ns);
#define iomfb_vbl_notify ((iomfb_vbl_notify_t)0x80d3a375)
typedef uint32_t (*iomfb_new_uc_t)(void *self, void *task, void *sec, uint32_t type, void *props, void *handler);
#define iomfb_new_uc ((iomfb_new_uc_t)0x80d37099)
typedef int (*iomfb_start_t)(void *self, void *provider);
#define iomfb_start ((iomfb_start_t)0x80d360b9)
typedef void (*iomfb_s278_t)(void *self);
#define iomfb_s278 ((iomfb_s278_t)0x80d39481)
#define tc_alloc ((void *(*)(void (*)(void *, void *), void *))0x800456e9)
/* thread_call_enter_delayed_internal(call, 0, 0, 0, dl_lo, dl_hi, lw_lo, lw_hi, flags):
 * flags=1 (TIMER_CALL_SYS_CRITICAL) bypasses XNU's 1.0ms timer coalescing slop (0x8004840a). */
typedef int (*tc_enter_delayed_int_t)(void *, uint32_t, uint32_t, uint32_t,
                                      uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
#define tc_enter_delayed_int ((tc_enter_delayed_int_t)0x800465e9)
#define tc_enter_delayed(tc, dl) \
    tc_enter_delayed_int((tc), 0u, 0u, 0u, (uint32_t)(dl), (uint32_t)((dl) >> 32), 0u, 0u, 1u)
#define clk_int_deadline ((void (*)(uint32_t, uint32_t, uint64_t *))0x8001edbd)

#define FIMD0_PA        0x11c00000
#define FIMD_VIDCON0    0x000u
#define FIMD_VIDCON1    0x004u
#define FIMD_SHADOWCON  0x034u
#define SHADOWCON_C0_EN (1u << 0)
#define SHADOWCON_W0_PR (1u << 10)
#define VIDW00ADD0B0    0x0a0u
#define VIDW00ADD1B0    0x0d0u
#define VIDW00ADD2      0x100u
#define FIMD_VIDINTCON0 0x130u
#define FIMD_VIDINTCON1 0x134u
/* FRAMESEL0=VSYNC(1<<15) | INTFRMEN(1<<12) | INTEN(1<<0); VIDINTCON1 bit 1 = INTFRMPEND (W1C) */
#define VIDINTCON0_VSYNC_EN ((1u << 15) | (1u << 12) | 1u)
#define VIDINTCON1_FRMPEND  (1u << 1)
#define CMU_TOP_LCD_PA  0x1003c000u
#define CMU_GATE_IP_LCD 0x934u

#define DISP_FB_PA      0x60000000  /* mkxnu FB_PA */
#define DISP_FB2_PA     0x60800000  /* ping-pong blit FB (in unused 0x60800000..0x62000000 carveout) */
#define DISP_W          720
#define DISP_H          1280
#define DISP_STRIDE     (DISP_W * 4)
#define DISP_FB_SIZE    (DISP_STRIDE * DISP_H)
#define FOURCC_BGRA     0x42475241u
#define PHYS_TO_KV_OFF  0x40000000u /* XNU ARM32 linear RAM map: PA 0x40000000 -> VA 0x80000000 */
#define iomem_with_phys ((void *(*)(uint32_t, uint32_t, uint32_t))0x8035a161)
#define iomem_map_opt   ((void *(*)(void *, uint32_t))0x8035fc55)
#define iomap_get_va    ((uintptr_t (*)(void *))0x8035f955)
#define KIO_DIR_INOUT   3u
#define KIO_MAP_WCOMB   0x00000400u

/* RAM log (survives WDT reset; address printed as disp= by mkxnu.py). */
struct {
    uint32_t magic;     /* "disp" = 0x64697370 */
    uint32_t started;
    uint32_t self;
    uint32_t swaps;
    uint32_t last_pa;
    uint32_t last_len;
    uint32_t copied;
    uint32_t pad;       /* VSYNCs delivered */
    uint32_t ticks;     /* VSYNC timer fires */
    uint32_t no_gate;   /* fires without a command gate */
    uint32_t clients;   /* fires with VSYNC subscribers (0x92c) */
    uint32_t tc;        /* thread_call, 0 if allocation failed */
    uint32_t last_swap_ms;
    uint32_t last_vsync_ms;
    uint32_t swap_wait_us;
    uint32_t swap_defers;
} exy_disp_log __attribute__((aligned(32)));

static uintptr_t fimd_va;
static uint8_t *disp_fb_vas[2];
static const uint32_t disp_fb_pas[2] = { DISP_FB_PA, DISP_FB2_PA };
static uint32_t disp_fb_idx;

extern uintptr_t mct;
extern void exy_clean_line(const void *p);
extern struct exy_direct_fb { uint32_t surf_pa, fb_pa, gen, stride; } exy_direct_fbs[12];
extern uint32_t exy_direct_fb_gen;

static void disp_clean(const void *p)
{
    exy_clean_line(p);
}

volatile uint32_t exy_last_disp_mct;

static void fimd_set_fb(uint32_t pa, uint32_t stride)
{
    if (!fimd_va)
        return;
    if (stride < DISP_STRIDE || stride > DISP_STRIDE * 2u)
        stride = DISP_STRIDE;
    uint32_t offsize = stride - DISP_STRIDE;
    /* Protect Window 0 shadow registers so ADD0B0/ADD1B0/ADD2 never tear across a VSYNC boundary,
     * and always keep SHADOWCON C0_EN_F (bit 0) set even if readback was 0. */
    uint32_t sc = (*(volatile uint32_t *)(fimd_va + FIMD_SHADOWCON)) | SHADOWCON_C0_EN;
    *(volatile uint32_t *)(fimd_va + FIMD_SHADOWCON) = sc | SHADOWCON_W0_PR;
    *(volatile uint32_t *)(fimd_va + VIDW00ADD0B0) = pa;
    *(volatile uint32_t *)(fimd_va + VIDW00ADD1B0) = pa + stride * DISP_H;
    *(volatile uint32_t *)(fimd_va + VIDW00ADD2)   = (offsize << 13) | DISP_STRIDE;
    *(volatile uint32_t *)(fimd_va + FIMD_SHADOWCON) = sc & ~SHADOWCON_W0_PR;
    /* Arm hardware VSYNC frame-pending latch and W1C it AFTER programming ADD0B0.
     * Once INTFRMPEND reads 1, a genuine FIMD0 VSYNC edge has latched `pa`. */
    *(volatile uint32_t *)(fimd_va + FIMD_VIDINTCON0) = VIDINTCON0_VSYNC_EN;
    *(volatile uint32_t *)(fimd_va + FIMD_VIDINTCON1) = VIDINTCON1_FRMPEND;
    __asm__ volatile("dsb" ::: "memory");
}

void exy_clcd_noop(void)
{
}

static void ps_puts(const char *s);
static void ps_putx(uint32_t v);
extern void exy_putc(char c);

static void *vsync_tc;
static uint64_t vsync_next_dl, vsync_step;
static volatile uint32_t swap_vc1, swap_mct, swap_fb_changed, swap_target_pa;
static int fimd_has_shd;   /* 1 if SHD_VIDW00ADD0B0 (+0x40a0) tracks live scanout PA */
static uint32_t fimd_frame_us = 16615u; /* measured live hardware frame period in us (Exynos4412 s6e8aa0) */

/* Exynos 4412 FIMD0 VIDCON1.LINECNT counts UP 0..1279 during ACTIVE (verified on hardware: 349->505 in 2ms). */
static inline int32_t fimd_scan_pos(uint32_t vc1)
{
    int32_t ln = (int32_t)((vc1 >> 16) & 0x7ffu);
    if (ln > 1279) ln = 1279;
    return ln;
}

/* IOMobileFramebuffer VSYNC delivery to the 0x92c subscriber list; Apple ABI packs the u64s
 * (vbl count, timestamp, delta) unaligned, so pass them as 32-bit halves. */
typedef void (*iomfb_vbl_clients_t)(void *self, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
#define iomfb_vbl_clients ((iomfb_vbl_clients_t)0x80d376ed)

void exy_clcd_swap_begin(uint8_t *self);

/* Mirrors AppleCLCD's VBL interrupt (0x80d51d8a, 0x80d515ae): complete pending swap, kick next queued, notify clients. */
static int vbl_gated_action(void *owner, void *a0, void *a1, void *a2, void *a3)
{
    (void)a2; (void)a3;
    uint32_t *s = (uint32_t *)owner;
    void *pend = *(void **)((uint8_t *)owner + 0x918);
    if (pend && (uintptr_t)a1) {
        swap_fb_changed = 0u;
        iomfb_swap_complete(owner, 1);
        if (*(void **)((uint8_t *)owner + 0x91c))
            exy_clcd_swap_begin((uint8_t *)owner);
    }
    iomfb_vbl_notify(owner, (uint32_t)(uintptr_t)a0);
    if (s[0x92c / 4])
        iomfb_vbl_clients(owner, s[0x58 / 4], s[0x5c / 4], s[0x60 / 4], s[0x64 / 4], s[0x68 / 4], s[0x6c / 4]);
    return 0;
}

static void exy_vsync_tick(void *p0, void *p1)
{
    (void)p1;
    uint8_t *self = (uint8_t *)p0;
    uint32_t vc1 = fimd_va ? *(volatile uint32_t *)(fimd_va + FIMD_VIDCON1) : 0u;
    if (vsync_tc) {
        uint64_t now = 0;
        clk_int_deadline(0, 1000, &now);
        if (!vsync_step) {
            vsync_step = (uint64_t)fimd_frame_us * 24ull; /* XNU ARMv7 timebase = 24MHz MCT */
        }
        /* Phase-lock software VSYNC tick to real Exynos 4412 FIMD0 hardware VSYNC.
         * Subtract thread_call wakeup latency (now - vsync_next_dl) from LINECNT so 1-frame CPU
         * scheduling delays NEVER yank vsync_next_dl backward before hardware VSYNC.
         * Target sched_pos=35 (~0.45ms into ACTIVE, safely after SHD_VIDW00ADD0B0 latches at pos=0). */
        int32_t adj_us = 0;
        uint32_t vstatus = (vc1 >> 13) & 3u;
        uint32_t linecnt = (vc1 >> 16) & 0x7ffu;
        if (fimd_va && (vc1 & 0x07ffe000u) != 0u && linecnt <= 1280u) {
            uint64_t lat_ticks = (vsync_next_dl && now > vsync_next_dl) ? (now - vsync_next_dl) : 0ull;
            int32_t lat_lines = (int32_t)(lat_ticks / 308ull); /* 12.82 us/line * 24 ticks/us */
            if (lat_lines <= 250) {
                int32_t cur_pos = (vstatus == 2u) ? fimd_scan_pos(vc1) : 1288;
                int32_t sched_pos = cur_pos - lat_lines;
                while (sched_pos < 0) sched_pos += 1296;

                if (sched_pos >= 640) {
                    /* Scheduled before hardware VSYNC: push forward past VSYNC to pos=18. */
                    adj_us = (1314 - sched_pos) * 10;
                    if (adj_us > 3500) adj_us = 3500;
                } else if (sched_pos < 12) {
                    /* Too close to VSYNC edge: nudge later so shadow latch always finishes first. */
                    adj_us = (18 - sched_pos) * 8;
                } else if (sched_pos > 45) {
                    /* Scheduled late after VSYNC: pull back toward pos=18. */
                    adj_us = -((sched_pos - 18) * 6);
                    if (adj_us < -2000) adj_us = -2000;
                } else {
                    /* Locked in [12..45]: gentle micro-trim keeps sched_pos pinned at 18. */
                    adj_us = -((sched_pos - 18) * 3);
                }
            }
            exy_disp_log.last_len = vc1;
        }
        vsync_next_dl += vsync_step;
        if (adj_us) {
            vsync_next_dl = (uint64_t)((int64_t)vsync_next_dl + (int64_t)adj_us * 24ll);
        }
        if (vsync_next_dl <= now || (vsync_next_dl - now) > vsync_step * 4ull)
            vsync_next_dl = now + vsync_step;
        tc_enter_delayed(vsync_tc, vsync_next_dl);
    }
    exy_disp_log.ticks++;
    if (!self)
        return;
    void *gate = *(void **)(self + 0x970);
    if (!gate) {
        exy_disp_log.no_gate++;
        return;
    }
    if (*(void **)(self + 0x92c))
        exy_disp_log.clients++;
    typedef int (*run_action_t)(void *g, int (*fn)(void *, void *, void *, void *, void *),
                                void *a0, void *a1, void *a2, void *a3);
    void **gvt = *(void ***)gate;
    /* Hardware Zero-Tear Shadow Latch Guard: never release the previous scanout buffer until
     * FIMD0 has genuinely latched swap_target_pa onto the active scanout pipeline.
     * Primary: live SHD_VIDW00ADD0B0 (+0x40a0) == swap_target_pa.
     * Secondary: hardware VIDINTCON1.INTFRMPEND (bit 1), cleared right after fimd_set_fb().
     * If we woke up right before VSYNC, spin briefly (<=1.8ms) so we latch on this tick without dropping to 30 FPS. */
    uint32_t can_complete = 1u;
    if (swap_fb_changed && fimd_va) {
        if (!fimd_has_shd && swap_target_pa) {
            uint32_t s0_now = *(volatile uint32_t *)(fimd_va + 0x40a0u);
            if (s0_now == swap_target_pa)
                fimd_has_shd = 1;
        }
        uint32_t now_mct = mct ? *(volatile uint32_t *)(mct + 0x100u) : 0u;
        uint32_t dt_mct = mct ? (now_mct - swap_mct) : (24000u * 40u);
        int latched = fimd_has_shd
            ? (*(volatile uint32_t *)(fimd_va + 0x40a0u) == swap_target_pa)
            : ((*(volatile uint32_t *)(fimd_va + FIMD_VIDINTCON1) & VIDINTCON1_FRMPEND) != 0u);
        if (!latched && mct && dt_mct < 24000u * 35u) {
            uint32_t cvs = (vc1 >> 13) & 3u;
            int32_t cpos = fimd_scan_pos(vc1);
            if (cvs != 2u || cpos >= 1150 || dt_mct >= 24000u * 14u) {
                uint32_t w0 = *(volatile uint32_t *)(mct + 0x100u);
                while (*(volatile uint32_t *)(mct + 0x100u) - w0 < 24000u * 1800u / 1000u) {
                    if (fimd_has_shd) {
                        if (*(volatile uint32_t *)(fimd_va + 0x40a0u) == swap_target_pa) {
                            latched = 1;
                            break;
                        }
                    } else if (*(volatile uint32_t *)(fimd_va + FIMD_VIDINTCON1) & VIDINTCON1_FRMPEND) {
                        latched = 1;
                        break;
                    }
                }
            }
        }
        if (!latched && dt_mct < 24000u * 35u) {
            can_complete = 0u;
            exy_disp_log.swap_defers++;
        }
    }
    ((run_action_t)gvt[0x70 / 4])(gate, vbl_gated_action, (void *)(uintptr_t)(fimd_frame_us * 1000u), (void *)(uintptr_t)can_complete, 0, 0);
    exy_disp_log.pad++;
    if (mct)
        exy_disp_log.last_vsync_ms = *(volatile uint32_t *)(mct + 0x100u) / 24000u;
    if ((exy_disp_log.pad & 31u) == 0u) {
        disp_clean(&exy_disp_log);
        disp_clean(&exy_disp_log.ticks);
    }
}

uint32_t exy_clcd_new_uc(void *self, void *task, void *sec, uint32_t type, void *props, void *handler)
{
    return iomfb_new_uc(self, task, sec, type, props, handler);
}

int exy_clcd_start(void *self, void *provider)
{
    exy_disp_log.magic = 0x64697370u;
    exy_disp_log.started = 0x55u;
    disp_clean(&exy_disp_log);
    int r = iomfb_start(self, provider);
    exy_disp_log.started = 0x100u | (uint32_t)(r & 0xff);
    disp_clean(&exy_disp_log);
    /* user clients come from IOUserClientClass, not newUserClient: start VSYNC here */
    if (r && !vsync_tc) {
        vsync_tc = tc_alloc(exy_vsync_tick, self);
        exy_disp_log.tc = (uint32_t)(uintptr_t)vsync_tc;
        disp_clean(&exy_disp_log.ticks);
        if (vsync_tc) {
            uint64_t dl = 0;
            clk_int_deadline(100, 1000000, &dl);
            tc_enter_delayed(vsync_tc, dl);
        }
    }
    return r;
}

void exy_clcd_s278(uint8_t *self)
{
    /* Mirror IOMobileFramebuffer slot 278 (0x80d39480) struct init, but skip the
     * PurpleGfxMem / default-surface lookup that blocks forever without Apple's VRAM. */
    for (uint32_t i = 0; i < 0x220; i += 4)
        *(uint32_t *)(self + 0x88 + i) = 0;
    for (uint32_t i = 0; i < 0x224; i += 4)
        *(uint32_t *)(self + 0x6f0 + i) = 0;
    *(uint32_t *)(self + 0xa6c) = 0;
    *(uint32_t *)(self + 0x138) = 0;
    *(uint32_t *)(self + 0x128) = ~0u;
    *(uint32_t *)(self + 0x12c) = ~0u;
    exy_disp_log.last_len = 0x2781u;
    disp_clean(&exy_disp_log);
}

int exy_clcd_start_hardware(uint8_t *self, void *provider)
{
    (void)provider;
    /* Ensure Exynos 4412 FIMD0 bus clock gate (CMU_GATE_IP_LCD bit 0) is unmasked before probing. */
    uintptr_t cmu_lcd = ml_io_map(CMU_TOP_LCD_PA, 0x1000u);
    if (cmu_lcd)
        *(volatile uint32_t *)(cmu_lcd + CMU_GATE_IP_LCD) |= 1u;
    fimd_va = ml_io_map(FIMD0_PA, 0x5000);
    /* Probe SHD_VIDW00ADD0B0 (+0x40a0), arm VIDINTCON0 VSYNC pending latch, and measure frame period. */
    if (fimd_va) {
        uint32_t vc0 = *(volatile uint32_t *)(fimd_va + FIMD_VIDCON0);
        uint32_t vc1 = *(volatile uint32_t *)(fimd_va + FIMD_VIDCON1);
        uint32_t sc  = *(volatile uint32_t *)(fimd_va + FIMD_SHADOWCON);
        uint32_t a0  = *(volatile uint32_t *)(fimd_va + VIDW00ADD0B0);
        uint32_t s0  = *(volatile uint32_t *)(fimd_va + 0x40a0u);
        *(volatile uint32_t *)(fimd_va + FIMD_SHADOWCON) = sc | SHADOWCON_C0_EN;
        *(volatile uint32_t *)(fimd_va + FIMD_VIDINTCON0) = VIDINTCON0_VSYNC_EN;
        *(volatile uint32_t *)(fimd_va + FIMD_VIDINTCON1) = VIDINTCON1_FRMPEND;
        fimd_has_shd = (a0 != 0u && s0 == a0) ? 1 : 0;
        if (mct) {
            /* Measure exact hardware frame period via two consecutive INTFRMPEND VSYNC edges (24MHz MCT). */
            uint32_t ta = 0, tb = 0;
            uint32_t w0 = *(volatile uint32_t *)(mct + 0x100u);
            while (*(volatile uint32_t *)(mct + 0x100u) - w0 < 24000u * 30u) {
                if (*(volatile uint32_t *)(fimd_va + FIMD_VIDINTCON1) & VIDINTCON1_FRMPEND) {
                    *(volatile uint32_t *)(fimd_va + FIMD_VIDINTCON1) = VIDINTCON1_FRMPEND;
                    ta = *(volatile uint32_t *)(mct + 0x100u);
                    break;
                }
            }
            w0 = *(volatile uint32_t *)(mct + 0x100u);
            while (ta && *(volatile uint32_t *)(mct + 0x100u) - w0 < 24000u * 30u) {
                if (*(volatile uint32_t *)(fimd_va + FIMD_VIDINTCON1) & VIDINTCON1_FRMPEND) {
                    *(volatile uint32_t *)(fimd_va + FIMD_VIDINTCON1) = VIDINTCON1_FRMPEND;
                    tb = *(volatile uint32_t *)(mct + 0x100u);
                    break;
                }
            }
            if (ta && tb) {
                uint32_t fus = (tb - ta) / 24u;
                if (fus >= 15000u && fus <= 18500u)
                    fimd_frame_us = fus;
            }
        }
        ps_puts("FIMD PROBE shd="); ps_putx((uint32_t)fimd_has_shd);
        ps_puts(" a0=0x"); ps_putx(a0); ps_puts(" s0=0x"); ps_putx(s0);
        ps_puts(" vc0=0x"); ps_putx(vc0); ps_puts(" vc1=0x"); ps_putx(vc1);
        ps_puts(" sc=0x"); ps_putx(sc); ps_puts(" fr="); ps_putx(fimd_frame_us);
        ps_puts("\n");
    }

    *(uint32_t *)(self + 0x998) = DISP_W;
    *(uint32_t *)(self + 0x99c) = DISP_H;
    *(uint32_t *)(self + 0x9bc) = 1;            /* current power state = on (checked by swap_wait mode 3) */
    *(uint32_t *)(self + 0x9c0) = 1;            /* target power state = on */
    *(uint32_t *)(self + 0x9e4) = FOURCC_BGRA;
    *(uint32_t *)(self + 0x9e8) = 1;
    *(uint32_t *)(self + 0x9ec) = 4;            /* bytes per pixel */
    *(uint32_t *)(self + 0x9f4) = DISP_STRIDE;  /* plane 0 stride (used by slot 278) */
    *(uint32_t *)(self + 0xa04) = DISP_STRIDE;
    *(uint8_t  *)(self + 0xa18) = 1;            /* isMainDisplay */
    *(uint8_t  *)(self + 0xa1a) = 1;

    exy_disp_log.magic = 0x64697370u;
    exy_disp_log.started = 1;
    exy_disp_log.self = (uint32_t)(uintptr_t)self;
    disp_clean(&exy_disp_log);
    return 1;
}

static void copy_words_only(uint32_t *dst, const uint32_t *src, uint32_t nbytes)
{
    uint32_t n = nbytes >> 2;
    while (n >= 8) {
        register uint32_t *r_dst __asm__("r0") = dst;
        register const uint32_t *r_src __asm__("r1") = src;
        __asm__ volatile(
            "ldmia %1!, {r2, r3, r4, r5, r6, r8, r9, r10}\n"
            "stmia %0!, {r2, r3, r4, r5, r6, r8, r9, r10}\n"
            : "+r"(r_dst), "+r"(r_src)
            :: "r2", "r3", "r4", "r5", "r6", "r8", "r9", "r10", "memory");
        dst = r_dst;
        src = r_src;
        n -= 8;
    }
    while (n--)
        *dst++ = *src++;
}

static uint8_t *map_fb_wc(uint32_t pa, int *is_wc)
{
    void *md = iomem_with_phys(pa, DISP_FB_SIZE, KIO_DIR_INOUT);
    if (md) {
        void *m = iomem_map_opt(md, KIO_MAP_WCOMB);
        if (m) {
            uintptr_t va = iomap_get_va(m);
            if (va) {
                if (is_wc) *is_wc = 1;
                return (uint8_t *)va;
            }
        }
    }
    if (is_wc) *is_wc = 0;
    return (uint8_t *)ml_io_map(pa, DISP_FB_SIZE);
}

static int disp_fb_is_wc = 0;

static uint32_t blit_surface(void *surf, uint32_t *out_pa)
{
    typedef void *(*get_md_t)(void *surf, uint32_t plane);
    typedef uint32_t (*get_off_t)(void *surf, uint32_t plane);
    typedef uint32_t (*get_phys_t)(void *md, uint32_t off, uint32_t *len);
    extern void exy_mali_wait_idle(void);
    extern void exy_dma_inv_l2_nosync(uint32_t pa, uint32_t len);
    extern void exy_dma_inv_l2_sync(void);

    void **svt = *(void ***)surf;
    void *md = ((get_md_t)svt[0x64 / 4])(surf, 0);
    if (!md)
        return 0;
    uint32_t base_off = ((get_off_t)svt[0x6c / 4])(surf, 0);
    uint32_t src_stride = *(const uint32_t *)((const uint8_t *)surf + 0x68);
    if (src_stride < DISP_STRIDE || src_stride > DISP_STRIDE * 4)
        src_stride = DISP_STRIDE;
    void **mvt = *(void ***)md;
    get_phys_t get_phys = (get_phys_t)mvt[0x84 / 4];

    uint32_t seg_len = 0;
    uint32_t pa = get_phys(md, base_off, &seg_len);
    exy_disp_log.last_pa = pa;
    exy_disp_log.last_len = src_stride;
    if (!pa)
        return 0;

    /* Ping-pong double-buffered Write-Combining FBs (0x60000000 <-> 0x60800000) for zero-tear fast blit. */
    uint32_t idx = disp_fb_idx ^ 1u;
    if (!disp_fb_vas[idx])
        disp_fb_vas[idx] = map_fb_wc(disp_fb_pas[idx], &disp_fb_is_wc);
    uint8_t *dst_va = disp_fb_vas[idx];
    if (!dst_va)
        return 0;

    exy_mali_wait_idle();

    /* Pass 1: batch PL310 L2 invalidate across the whole IOSurface with a single CACHE_SYNC. */
    uint32_t total_src = DISP_H * src_stride;
    for (uint32_t off = 0; off < total_src; ) {
        uint32_t sl = 0;
        uint32_t p = get_phys(md, base_off + off, &sl);
        if (!p || !sl || p < 0x40000000u || p >= 0x60000000u)
            break;
        uint32_t chunk = total_src - off;
        if (sl < chunk)
            chunk = sl;
        exy_dma_inv_l2_nosync(p, chunk);
        off += chunk;
    }
    exy_dma_inv_l2_sync();

    /* Pass 2: L1 invalidate + single-pass 32B-burst copy into Write-Combining scanout buffer. */
    uint32_t rows_done = 0;
    for (uint32_t y = 0; y < DISP_H; y++) {
        uint32_t row_off = base_off + y * src_stride;
        uint32_t row_dst = y * DISP_STRIDE;
        uint32_t x = 0;
        while (x < DISP_STRIDE) {
            uint32_t sl = 0;
            uint32_t p = get_phys(md, row_off + x, &sl);
            if (!p || !sl || p < 0x40000000u || p >= 0x60000000u)
                goto done_blit;
            uint32_t chunk = DISP_STRIDE - x;
            if (sl < chunk)
                chunk = sl;
            uintptr_t kv = (uintptr_t)(p + PHYS_TO_KV_OFF);
            uintptr_t a = kv & ~31u;
            uintptr_t end = kv + chunk;
            for (uintptr_t l = a; l < end; l += 32u)
                __asm__ volatile("mcr p15, 0, %0, c7, c6, 1" :: "r"(l) : "memory");
            __asm__ volatile("dsb" ::: "memory");
            copy_words_only((uint32_t *)(dst_va + row_dst + x), (const uint32_t *)kv, chunk);
            x += chunk;
        }
        rows_done++;
    }
done_blit:
    __asm__ volatile("dsb" ::: "memory");
    disp_fb_idx = idx;
    if (out_pa)
        *out_pa = disp_fb_pas[idx];
    if (rows_done)
        exy_disp_log.copied++;
    return rows_done;
}

/*
 * IOKit call log (mkxnu --disp): every IOUserClient::externalMethod call (the base method
 * Apple's user clients reach through super::externalMethod) as {pid, selector, class name,
 * repeat count}, consecutive repeats merged; plus pid -> process name. Address: uc= in the
 * info line; decode with tools/uclog.py.
 */
#define getClassName ((const char *(*)(const void *))0x80305bb1)  /* OSMetaClass::getClassName */
#define proc_selfpid ((int (*)(void))0x802a72c9)
#define proc_selfname ((void (*)(char *, int))0x802a7451)
extern void exy_putc(char c);

static void ps_puts(const char *s)
{
    while (*s)
        exy_putc(*s++);
}

static void ps_putu(uint32_t v)
{
    char b[12];
    int i = 0;
    if (!v) { exy_putc('0'); return; }
    while (v) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i--) exy_putc(b[i]);
}

static void ps_putx(uint32_t v)
{
    static const char h[] = "0123456789abcdef";
    for (int s = 28; s >= 0; s -= 4)
        exy_putc(h[(v >> s) & 0xf]);
}
#define UC_RING 128
#define UC_PIDS 32
struct {
    uint32_t magic, count, pad[2];  /* "ucal" */
    uint32_t ring[UC_RING][4];
    struct { uint32_t pid; char name[12]; } pids[UC_PIDS];
} exy_uc_log __attribute__((aligned(32)));

static uint32_t bb_pid = 0;

void exy_log_extmethod(void **self, uint32_t sel, const uint64_t *sc, uint32_t sc_cnt)
{
    if (exy_disp_log.swaps < 717u || !self)
        return;
    uint32_t pid = (uint32_t)proc_selfpid();
    if (bb_pid && pid != bb_pid)
        return;
    static uint32_t uclog = 0;
    if (uclog >= 60u)
        return;
    const void *meta = ((const void *(*)(void *))(*(void ***)self)[7])(self);
    const char *cname = meta ? getClassName(meta) : "?";
    /* Skip noisy IOHID calls unless low count */
    if (cname[0] == 'I' && cname[1] == 'O' && cname[2] == 'H' && uclog >= 8u)
        return;
    uclog++;
    ps_puts("EXY UC #"); ps_putu(uclog);
    ps_puts(" sw="); ps_putu(exy_disp_log.swaps);
    ps_puts(" pid="); ps_putu(pid);
    ps_puts(" sel="); ps_putx(sel);
    if (sel == 6u && sc && sc_cnt >= 2u && sc_cnt <= 8u) {
        uint8_t *fb = (uint8_t *)(uintptr_t)exy_disp_log.self;
        ps_puts(" id="); ps_putu((uint32_t)sc[0]);
        ps_puts(" fl="); ps_putx((uint32_t)sc[1]);
        if (fb) {
            ps_puts(" a4="); ps_putu(*(uint32_t *)(fb + 0x9a4));
            ps_puts(" bc="); ps_putu(*(uint32_t *)(fb + 0x9bc));
        }
    }
    ps_puts(" "); ps_puts(cname);
    exy_putc('\n');
}

/* Entry of is_io_connect_method (0x803704f4) jumps here (mkxnu --disp): log, then
 * replay the 8 overwritten bytes and continue in the original. */
__attribute__((naked)) void exy_tap_extmethod(void)
{
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "bl exy_log_extmethod\n"
        "pop {r0-r3, r12, lr}\n"
        "push {r4, r5, r6, r7, lr}\n"       /* displaced: push {r4-r7, lr} */
        "add r7, sp, #12\n"                 /*            add r7, sp, #12 */
        "push.w {r8, r10, r11}\n"           /*            stmdb sp!, {r8, sl, fp} */
        "ldr.w pc, =0x803704fd\n"           /* continue (Thumb) */
        ".ltorg\n");
}

/* Entry of is_io_connect_async_method (0x8037064c): log sel | 0x10000, replay 8B. */
__attribute__((naked)) void exy_tap_async_method(void)
{
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n"
        "orr r1, r1, #0x10000\n"
        "bl exy_log_extmethod\n"
        "pop {r0-r3, r12, lr}\n"
        "push {r4, r5, r6, r7, lr}\n"
        "add r7, sp, #12\n"
        "push.w {r8, r10, r11}\n"
        "ldr.w pc, =0x80370655\n"
        ".ltorg\n");
}

int exy_clcd_verify_swap(void *self, uint8_t *req)
{
    (void)self;
    return req ? 1 : 0;
}

static uint32_t get_surface_pa(void *surf)
{
    typedef void *(*get_md_t)(void *surf, uint32_t plane);
    typedef uint32_t (*get_off_t)(void *surf, uint32_t plane);
    typedef uint32_t (*get_phys_t)(void *md, uint32_t off, uint32_t *len);
    void **svt = *(void ***)surf;
    void *md = ((get_md_t)svt[0x64 / 4])(surf, 0);
    if (!md) return 0;
    uint32_t base_off = ((get_off_t)svt[0x6c / 4])(surf, 0);
    void **mvt = *(void ***)md;
    uint32_t seg_len = 0;
    return ((get_phys_t)mvt[0x84 / 4])(md, base_off, &seg_len);
}

void exy_clcd_swap_begin(uint8_t *self)
{
    /* Already have a pending swap, or queue empty. */
    if (*(void **)(self + 0x918))
        return;
    uint8_t *req = *(uint8_t **)(self + 0x91c);
    if (!req)
        return;

    /* Pop req from TAILQ (tqh_first at +0x91c, tqh_last at +0x920; req+12=next, req+16=prev). */
    void *next = *(void **)(req + 12);
    void **prev = *(void ***)(req + 16);
    if (next)
        *(void ***)((uint8_t *)next + 16) = prev;
    else
        *(void ***)(self + 0x920) = prev;
    *prev = next;
    *(void **)(self + 0x918) = req;

    void *s0 = *(void **)(req + 20);
    void *s1 = *(void **)(req + 24);
    void *s2 = *(void **)(req + 28);
    void *surf0 = s0 ? s0 : (s1 ? s1 : s2);
    uint32_t t0 = mct ? *(volatile uint32_t *)(mct + 0x100u) : 0u;
    uint32_t nz = 0;
    static uint32_t last_fimd_pa, last_fimd_stride, last_fb_gen, same_surf_cnt;
    static void *last_fallback_surf;
    int did_blit = 0;
    if (surf0) {
        uint32_t spa = get_surface_pa(surf0);
        exy_disp_log.last_pa = spa;
        uint32_t direct_fb = 0, direct_gen = 0, direct_stride = DISP_STRIDE;
        for (int i = 0; i < 12; i++) {
            if (spa && exy_direct_fbs[i].surf_pa == spa) {
                direct_fb = exy_direct_fbs[i].fb_pa;
                direct_gen = exy_direct_fbs[i].gen;
                if (exy_direct_fbs[i].stride)
                    direct_stride = exy_direct_fbs[i].stride;
                break;
            }
        }
        if (direct_fb) {
            extern void exy_mali_wait_idle(void);
            exy_mali_wait_idle();
            if (direct_fb != last_fimd_pa || direct_stride != last_fimd_stride || direct_gen != last_fb_gen) {
                fimd_set_fb(direct_fb, direct_stride);
                if (fimd_va) swap_vc1 = *(volatile uint32_t *)(fimd_va + FIMD_VIDCON1);
                if (mct) { swap_mct = *(volatile uint32_t *)(mct + 0x100u); exy_last_disp_mct = swap_mct; }
                swap_target_pa = direct_fb;
                swap_fb_changed = (direct_fb != last_fimd_pa) ? 1u : 0u;
                last_fimd_pa = direct_fb;
                last_fimd_stride = direct_stride;
                last_fb_gen = direct_gen;
                exy_disp_log.copied++;
                nz = 1280u;
            }
        } else {
            /* Fallback double-buffered WC blit for non-direct full-screen app IOSurfaces (3D CAEAGLLayer, Safari). */
            uint32_t blit_pa = 0;
            if (exy_disp_log.swaps >= 64u || surf0 != last_fallback_surf || (++same_surf_cnt & 31u) == 1u) {
                last_fallback_surf = surf0;
                nz = blit_surface(surf0, &blit_pa);
                did_blit = 1;
            }
            if (blit_pa) {
                fimd_set_fb(blit_pa, DISP_STRIDE);
                if (fimd_va) swap_vc1 = *(volatile uint32_t *)(fimd_va + FIMD_VIDCON1);
                if (mct) swap_mct = *(volatile uint32_t *)(mct + 0x100u);
                swap_target_pa = blit_pa;
                swap_fb_changed = (blit_pa != last_fimd_pa) ? 1u : 0u;
                last_fimd_pa = blit_pa;
                last_fimd_stride = DISP_STRIDE;
                last_fb_gen = 0;
            }
        }
    }
    uint32_t t1 = mct ? *(volatile uint32_t *)(mct + 0x100u) : 0u;

    exy_disp_log.swaps++;
    exy_disp_log.last_swap_ms = t0 / 24000u;
    exy_disp_log.swap_wait_us += (t1 - t0) / 24u;
    if (exy_disp_log.swaps <= 16u || (exy_disp_log.swaps & 63u) == 0u) {
        disp_clean(&exy_disp_log);
        disp_clean(&exy_disp_log.ticks);
    }
    if (did_blit && exy_disp_log.swaps >= 64u) {
        static uint32_t fb_log = 0;
        if (++fb_log <= 8u) {
            ps_puts("EXY BLIT #"); ps_putu(fb_log);
            ps_puts(" sw="); ps_putu(exy_disp_log.swaps);
            ps_puts(" b="); ps_putu((t1 - t0) / 24000u);
            ps_puts("ms wc="); ps_putu((uint32_t)disp_fb_is_wc);
            ps_puts(" pa="); ps_putx(last_fimd_pa);
            ps_puts(" nz="); ps_putu(nz);
            exy_putc('\n');
        }
    }
    if (exy_disp_log.swaps <= 16u) {
        ps_puts("EXY SWAP #"); ps_putu(exy_disp_log.swaps);
        ps_puts(" t="); ps_putu(t0 / 24000u);
        ps_puts("ms b="); ps_putu((t1 - t0) / 24000u);
        ps_puts("ms s0="); ps_putx((uint32_t)(uintptr_t)s0);
        ps_puts(" s1="); ps_putx((uint32_t)(uintptr_t)s1);
        ps_puts(" pa="); ps_putx(exy_disp_log.last_pa);
        ps_puts(" nz="); ps_putu(nz);
        exy_putc('\n');
    }

    /* Swap completes asynchronously on the next 60.00 Hz VSYNC tick (vbl_gated_action). */
}

/* shortcut: AppleKeyStore get_device_state (sel 17, 0x80f9fef0) reports no passcode & unlocked since boot
 * until hardware keybag/effaceable storage is emulated, so slide-to-unlock enters Home Screen directly. */
int exy_aks_get_device_state(void *aks, void *uc, uint32_t handle, uint32_t flags, uint8_t *out42)
{
    (void)aks; (void)uc; (void)handle; (void)flags;
    if (out42) {
        for (unsigned i = 0; i < 42u; i++)
            out42[i] = 0u;
        out42[4] = 3u;   /* kMobileLockStateDisabled: no passcode set */
        out42[20] = 4u;  /* state_flags: unlocked since boot */
    }
    return 0;
}

/* shortcut: AppleKeyStore sel 31 get_configuration (0x80faf5a4): return empty XML plist so
 * MKBDeviceGetGracePeriod returns {} instead of (CFDictionaryRef)-1 (fixes ManagedConfiguration crashes). */
#define aks_iomalloc ((void *(*)(uint32_t))0x80fb6775) /* IOMalloc inside AppleKeyStore, Thumb */
int exy_aks_get_config(void *aks, uint32_t handle, void **out_buf, uint32_t *out_len)
{
    static const char kEmptyPlist[] = "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict/></plist>";
    (void)aks; (void)handle;
    if (!out_buf || !out_len)
        return 0;
    uint32_t n = (uint32_t)(sizeof(kEmptyPlist) - 1u);
    char *p = (char *)aks_iomalloc(n);
    if (!p)
        return -1;
    for (uint32_t i = 0; i < n; i++)
        p[i] = kEmptyPlist[i];
    *out_buf = p;
    *out_len = n;
    return 0;
}

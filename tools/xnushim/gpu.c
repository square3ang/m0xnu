/*
 * Mali-400 under XNU (mkxnu --gpu-demo). Built into the ".drv" code region: the
 * __text of AppleVXE380, a kext nothing depends on whose personalities mkxnu renames,
 * so it never loads (see DRV_AREA in mkxnu.py).
 *
 * exy_gpu_demo() maps the GPU, CMU, PMU and a work area outside XNU RAM, then replays
 * gpu/mali400/tri_job.h into the video console framebuffer. Status goes to exy_gpu_log
 * (RAM, survives the reset; address in the mkxnu info line as gpu=).
 */
#include <stdint.h>
#include "../../gpu/mali400/mali400.c"
#include "../../gpu/mali400/tri_job.h"

typedef uintptr_t (*ml_io_map_t)(uintptr_t pa, uint32_t size);
#define ml_io_map ((ml_io_map_t)0x800c0bf5)  /* Thumb */

#define GPU_PA     0x13000000
#define CMU_PA     0x10030000
#define PMU_PA     0x10020000
#define WORK_PA    0x5f000000  /* above XNU RAM (0x4ff00000), below the framebuffer */
#define MALI_STATUS_PA 0x5f100000u /* 4 KiB uncached shared live done_seq page */
#define FB_PA      0x60000000  /* mkxnu FB_PA: the video console */

extern uintptr_t mct;  /* shim.c, mapped by exy_timer_init */
extern void exy_clean_line(const void *p);
extern void exy_putc(char c);
struct exy_touch_state;
struct exy_drv_status;
extern void exy_pmic_and_touch_init(void);
extern void exy_touch_poll(struct exy_touch_state *out);
extern void exy_drivers_init(void *parent_service);
extern void exy_drv_poll(struct exy_drv_status *out);
extern uint32_t exy_drv_poll_sz(void *out, uint32_t max_sz);
extern int exy_drv_set_brightness(uint8_t level);
extern int exy_drv_set_led(uint8_t r, uint8_t g, uint8_t b);
extern int exy_drv_vibrate(uint32_t ms);
extern void exy_drv_power_sync(void);
extern int exy_drv_set_volume(uint8_t vol);
extern int exy_drv_audio_tone(uint16_t hz, uint16_t ms);
extern int exy_drv_set_torch(uint8_t level);
extern void exy_dma_clean_inv_pa(uint32_t pa, uint32_t len);
extern int exy_halt_restart(unsigned type);

/* debug: magic, init status, job status, last INT_RAWSTAT, MCT ticks, pad[3], cumulative GP/PP us */
struct { uint32_t magic, init, job, rawstat, ticks, pad[3], gp_us, pp_us; } exy_gpu_log __attribute__((aligned(32)));

static uint32_t ticks(void)
{
    return *(volatile uint32_t *)(mct + 0x100);  /* MCT G_CNT_L, 24 MHz */
}

static void gpu_udelay(unsigned us)
{
    uint32_t t = ticks();
    while (ticks() - t < 24 * us)
        ;
}

static void gpu_flush(void *p, uint32_t len)
{
    (void)p; (void)len;
    __asm__ volatile("dsb" ::: "memory");  /* work area is an uncached device mapping */
}

static struct mali_env mali_env;
static int mali_ready;
static int cur_gpu_opp = 2;
volatile uint32_t exy_last_gpu_mct;
extern int exy_gpu_set_vdd(uint8_t code);

/* Glitch-free Exynos 4412 Mali-400 DVFS via CMU_DIV_G3D + BUCK4 (VPLL locked at 440 MHz):
 *   opp 0: 146 MHz @ 0.900V (BUCK4=0x18, DIV=/3) - idle / hot >=75C
 *   opp 1: 220 MHz @ 0.950V (BUCK4=0x1c, DIV=/2) - warm >=66C
 *   opp 2: 440 MHz @ 1.075V (BUCK4=0x26, DIV=/1) - active 3D/UI burst */
int exy_gpu_set_opp(int opp)
{
    if (!mali_ready || !mali_env.cmu)
        return -1;
    if (opp < 0) opp = 0;
    if (opp > 2) opp = 2;
    if (opp == cur_gpu_opp)
        return 0;
    static const uint8_t buck_tbl[3] = { 0x18u, 0x1cu, 0x26u };
    static const uint32_t div_tbl[3] = { 2u, 1u, 0u };
    if (opp > cur_gpu_opp) {
        if (exy_gpu_set_vdd(buck_tbl[opp]) != 0)
            return -1;
        gpu_udelay(200);
        wr(mali_env.cmu + CMU_DIV_G3D, (rd(mali_env.cmu + CMU_DIV_G3D) & ~0xfu) | div_tbl[opp]);
        (void)poll(&mali_env, mali_env.cmu + CMU_DIV_STAT_G3D, 1, 0, 1000);
    } else {
        wr(mali_env.cmu + CMU_DIV_G3D, (rd(mali_env.cmu + CMU_DIV_G3D) & ~0xfu) | div_tbl[opp]);
        (void)poll(&mali_env, mali_env.cmu + CMU_DIV_STAT_G3D, 1, 0, 1000);
        (void)exy_gpu_set_vdd(buck_tbl[opp]);
    }
    cur_gpu_opp = opp;
    return 0;
}
static volatile uint32_t *mali_status_va;

static int mali_ensure_init(void)
{
    if (mali_ready)
        return MALI_OK;
    mali_env.gpu = ml_io_map(GPU_PA, 0x10000);
    mali_env.cmu = ml_io_map(CMU_PA, 0x10000);
    mali_env.pmu = ml_io_map(PMU_PA, 0x4000);
    mali_env.mem = (uint8_t *)ml_io_map(WORK_PA, MALI_MEM_SIZE);
    mali_status_va = (volatile uint32_t *)ml_io_map(MALI_STATUS_PA, 0x1000u);
    if (mali_status_va)
        mali_status_va[0] = 0u;
    mali_env.mem_pa = WORK_PA;
    mali_env.udelay = gpu_udelay;
    mali_env.flush = gpu_flush;

    exy_gpu_log.magic = 0x6d616c69;  /* "mali" */
    exy_gpu_log.init = mali_init(&mali_env);
    exy_gpu_log.rawstat = mali_last_status;
    if (exy_gpu_log.init == MALI_OK) {
        /* Map double-buffered 4 MiB linear FIMD0 scanout FBs (0x60000000, 0x60400000) at GPU VA 0x03000000. */
        if (map_va(&mali_env, 0x03000000u, FB_PA, 0x00800000u) == MALI_OK) {
            for (unsigned i = 0; i < 5; i++)
                wr(mali_env.gpu + M_MMU(i) + MMU_COMMAND, MMU_CMD_ZAP_CACHE);
            l2_flush(&mali_env);
        }
        mali_ready = 1;
    }
    exy_clean_line(&exy_gpu_log);
    return (int)exy_gpu_log.init;
}

void exy_gpu_demo(void)
{
    const struct mali_fb fb = { FB_PA, 720, 1280, 2880 };
    uint32_t t;

    if (mali_ensure_init() == MALI_OK) {
        t = ticks();
        exy_gpu_log.job = mali_run_job(&mali_env, &tri_job, &fb);
        exy_gpu_log.ticks = ticks() - t;
        exy_gpu_log.rawstat = mali_last_status;
    }
    exy_clean_line(&exy_gpu_log);
}

/* --- IOKit IOAcceleratorES hooks (mkxnu --gpu) ----------------------------- */
#define ioservice_start     ((int  (*)(void *, void *))0x80332405)
#define ioservice_register  ((void (*)(void *, uint32_t))0x80333001)
#define ioreg_set_prop_str  ((int  (*)(void *, const char *, const char *))0x8032e299)
#define iomem_with_phys     ((void *(*)(uint32_t, uint32_t, uint32_t))0x8035a161)
#define iolock_alloc        ((void *(*)(void))0x8032969d)
#define iolock_mach         ((void *(*)(void *))0x80329749)
#define lck_mtx_lock_fn     ((void (*)(void *))0x800c2ae9)
#define lck_mtx_unlock_fn   ((void (*)(void *))0x800c2b79)
#define current_task_fn     ((void *(*)(void))0x8003f97d)
#define get_task_pmap_fn    ((void *(*)(void *))0x800517dd)
#define pmap_find_phys_fn   ((uint32_t (*)(void *, uint32_t, uint32_t))0x800b7cd5)
#define proc_selfname_fn    ((void (*)(char *, int))0x802a7451)
#define iosleep_fn          ((void (*)(uint32_t))0x80328fd1) /* _IOSleep(ms), Thumb */
#define kernel_thread_start_fn ((int (*)(void (*)(void *, int), void *, void **))0x80042c15)
#define lck_mtx_sleep_fn    ((int (*)(void *, uint32_t, void *, uint32_t))0x80026db9)
#define thread_wakeup_prim_fn ((int (*)(void *, int, int))0x8002ca11)

struct exy_direct_fb {
    uint32_t surf_pa;
    uint32_t fb_pa;
    uint32_t gen;
    uint32_t stride;
};
struct exy_direct_fb exy_direct_fbs[12];
uint32_t exy_direct_fb_gen;

#define KIO_DIR_INOUT       3u          /* kIODirectionInOut */
/* kIOMapWriteCombineCache: Normal non-cacheable, so GPU-coherent like kIOMapInhibitCache, but
 * unaligned CPU access works (Device memory from InhibitCache SIGBUSed lima's memcpy) */
#define KIO_MAP_WCOMB       0x00000400u
/* Dedicated 448 MiB GPU pool (0x62000000..0x7e000000):
 *   slot 0 (backboardd):   80 MiB (0x62000000..0x67000000) - compositor surfaces
 *   slot 1 (SpringBoard):  32 MiB (0x67000000..0x69000000)
 *   slots 2..4 (apps):   3x112 MiB (0x69000000..0x7e000000), round-robin for heavy 3D apps */
#define MALI_BB_POOL_SIZE   0x05000000u /*  80 MiB */
#define MALI_SB_POOL_SIZE   0x02000000u /*  32 MiB */
#define MALI_APP_POOL_SIZE  0x07000000u /* 112 MiB */
#define MALI_POOL_BASE_PA   0x62000000u
#define MALI_MAX_CLIENTS    5u
#define FB_SIZE_BYTES       (720u * 1280u * 4u)

static void *mali_mtx;
static struct { void *uc; uint32_t pa; uint32_t size; } mali_clients[MALI_MAX_CLIENTS];
static uint32_t mali_next_app_slot = 2u;

static void mali_lock(void)
{
    if (!mali_mtx) {
        void *l = iolock_alloc();
        if (l) mali_mtx = iolock_mach(l);
    }
    if (mali_mtx)
        lck_mtx_lock_fn(mali_mtx);
}

static void mali_unlock(void)
{
    if (mali_mtx)
        lck_mtx_unlock_fn(mali_mtx);
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static void mali_client_pool(void *uc, uint32_t *out_pa, uint32_t *out_size)
{
    for (unsigned i = 0; i < MALI_MAX_CLIENTS; i++) {
        if (mali_clients[i].uc == uc && mali_clients[i].pa) {
            *out_pa = mali_clients[i].pa;
            *out_size = mali_clients[i].size;
            return;
        }
    }
    char pname[20];
    for (unsigned i = 0; i < sizeof(pname); i++) pname[i] = 0;
    proc_selfname_fn(pname, (int)sizeof(pname));

    unsigned slot;
    uint32_t pa, sz;
    if (str_eq(pname, "backboardd")) {
        slot = 0u;
        pa = MALI_POOL_BASE_PA;
        sz = MALI_BB_POOL_SIZE;
    } else if (str_eq(pname, "SpringBoard")) {
        slot = 1u;
        pa = MALI_POOL_BASE_PA + MALI_BB_POOL_SIZE;
        sz = MALI_SB_POOL_SIZE;
    } else {
        slot = 2u + ((mali_next_app_slot++ - 2u) % (MALI_MAX_CLIENTS - 2u));
        pa = MALI_POOL_BASE_PA + MALI_BB_POOL_SIZE + MALI_SB_POOL_SIZE + (slot - 2u) * MALI_APP_POOL_SIZE;
        sz = MALI_APP_POOL_SIZE;
    }
    mali_clients[slot].uc = uc;
    mali_clients[slot].pa = pa;
    mali_clients[slot].size = sz;
    *out_pa = pa;
    *out_size = sz;
}

#define KIORETURN_SUCCESS      0
#define KIORETURN_BAD_ARG      0xe00002c2u
#define KIORETURN_UNSUPPORTED  0xe00002c7u
#define KIORETURN_IO_ERROR     0xe00002cau

struct io_ext_args {
    uint32_t version, selector, async_port;
    uint64_t *async_ref;
    uint32_t async_ref_cnt;
    const uint64_t *scalar_in;
    uint32_t scalar_in_cnt;
    const void *struct_in;
    uint32_t struct_in_cnt;
    void *struct_in_desc;
    uint64_t *scalar_out;
    uint32_t scalar_out_cnt;
    void *struct_out;
    uint32_t struct_out_cnt;
    void *struct_out_desc;
    uint32_t struct_out_max;
};

struct mali_pp_submit {
    uint32_t num_pp;
    uint32_t plbu[4];
    uint32_t stack[4];
    uint32_t frame[MALI_PP_FRAME_REGS];
    uint32_t wb[3 * MALI_PP_WB_REGS];
};

struct mali_gp_pp_submit {
    uint32_t gp[6];
    struct mali_pp_submit pp;
};

int exy_sgx_start(void *self, void *provider)
{
    if (!ioservice_start(self, provider))
        return 0;
    if (!mali_mtx) {
        void *l = iolock_alloc();
        if (l) mali_mtx = iolock_mach(l);
    }
    /* Publish IOPlatformUUID (without IONVRAM, which wakes AppleARMBacklight and hangs backboardd),
     * unblocking sys_gethostuuid() -> configd -> CaptiveNetworkSupport -> MobileSafari. */
    void *plat = *(void *const *)0x804587c4;
    if (plat) {
        typedef void *(*osstr_t)(const char *);
        typedef void  (*pubres_t)(const char *, void *);
        void *str = ((osstr_t)0x8031a9a1)("5333494F-5339-5335-8558-593434313221");
        if (str) {
            void *(*get_prov)(void *) = *(void *(**)(void *))(*(uintptr_t *)plat + 0x1ccu);
            void *prov = get_prov(plat);
            if (prov) {
                int (*set_prop)(void *, const char *, void *) = *(int (**)(void *, const char *, void *))(*(uintptr_t *)prov + 0x60u);
                set_prop(prov, "IOPlatformUUID", str);
            }
            ((pubres_t)0x80333d61)("IOPlatformUUID", str);
            void (*rel)(void *) = *(void (**)(void *))(*(uintptr_t *)str + 0x14u);
            rel(str);
        }
    }
    exy_pmic_and_touch_init();
    mali_ensure_init();
    exy_drivers_init(self);
    ioreg_set_prop_str(self, "IOUserClientClass", "IMGSGXSharedUserClient");
    ioservice_register(self, 0);
    return 1;
}

int exy_mali_uc_start(void *self, void *provider)
{
    int ok = ioservice_start(self, provider);
    exy_gpu_log.pad[0]++;
    exy_clean_line(&exy_gpu_log);
    return ok;
}

static int gp_reset_pending;

static int mali_submit_gp(const uint32_t gp_regs[6])
{
    uintptr_t gp = mali_env.gpu;
    uint32_t cmd = 0, want = 0;
    if (gp_regs[0] != gp_regs[1]) { cmd |= GP_CMD_START_VS;   want |= GP_IRQ_VS_END; }
    if (gp_regs[2] != gp_regs[3]) { cmd |= GP_CMD_START_PLBU; want |= GP_IRQ_PLBU_END; }
    if (!cmd)
        return MALI_OK;
    __asm__ volatile("dsb" ::: "memory"); /* drain CPU WCOMB store buffer before GP DMA */
    wr(gp + M_L2 + L2_COMMAND, 1); /* start L2 clear in parallel with GP soft reset */
    if (!gp_reset_pending) {
        wr(gp + GP_INT_MASK, 0);
        wr(gp + GP_INT_CLEAR, GP_IRQ_RESET_DONE);
        wr(gp + GP_CMD, GP_CMD_SOFT_RESET);
    }
    uint32_t rt0 = ticks();
    while (rd(gp + M_L2 + L2_STATUS) & 1u) {
        if (ticks() - rt0 > 24u * 1000u) { gp_reset_pending = 0; return MALI_ERR_L2; }
        __asm__ volatile("yield");
    }
    while (!(rd(gp + GP_INT_RAWSTAT) & GP_IRQ_RESET_DONE)) {
        if (ticks() - rt0 > 24u * 1000u) { gp_reset_pending = 0; return MALI_ERR_RESET; }
        __asm__ volatile("yield");
    }
    gp_reset_pending = 0;
    wr(gp + GP_INT_CLEAR, GP_IRQ_ALL);
    for (unsigned i = 0; i < 6; i++)
        wr(gp + GP_FRAME + 4 * i, gp_regs[i]);
    wr(gp + GP_CMD, GP_CMD_UPDATE_ALLOC);
    wr(gp + GP_CMD, cmd);
    uint32_t t0 = ticks();
    for (unsigned sleeps = 0;;) {
        uint32_t s = rd(gp + GP_INT_RAWSTAT);
        mali_last_status = s;
        if (s & GP_IRQ_ERROR) {
            exy_gpu_log.ticks = rd(gp + M_MMU(0) + 0x10);
            return MALI_ERR_JOB;
        }
        if ((s & want) == want)
            break;
        /* Tight spin up to 8ms with yield to eliminate IOSleep(1) overshoot without burning CPU/heat. */
        if (ticks() - t0 < 24u * 8000u) {
            __asm__ volatile("yield");
            continue;
        }
        if (++sleeps > 2000u) {
            exy_gpu_log.ticks = rd(gp + M_MMU(0) + 0x10);
            return MALI_ERR_TIMEOUT;
        }
        iosleep_fn(1);
    }
    exy_gpu_log.gp_us += (ticks() - t0) / 24u;
    wr(gp + GP_INT_CLEAR, GP_IRQ_ALL);
    /* Start async GP soft reset now so it completes while PP + CPU run next. */
    wr(gp + GP_CMD, GP_CMD_SOFT_RESET);
    gp_reset_pending = 1;
    return MALI_OK;
}

static int pp_hard_reset_parallel(unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        uintptr_t pp = mali_env.gpu + M_PP(i);
        wr(pp + PP_PERF_CNT_0_LIMIT, 0xc0ffe000u);
        wr(pp + PP_INT_MASK, 0);
        wr(pp + PP_CTRL, PP_CTRL_STOP_BUS);
    }
    uint32_t t0 = ticks();
    for (uint32_t ok = 0, want = (1u << n) - 1u; ok != want;) {
        for (unsigned i = 0; i < n; i++)
            if (!(ok & (1u << i)) && (rd(mali_env.gpu + M_PP(i) + PP_STATUS) & (1u << 4)))
                ok |= (1u << i);
        if (ticks() - t0 > 24u * 1000u)
            return MALI_ERR_RESET;
    }
    for (unsigned i = 0; i < n; i++)
        wr(mali_env.gpu + M_PP(i) + PP_CTRL, PP_CTRL_FORCE_RESET);
    t0 = ticks();
    for (uint32_t ok = 0, want = (1u << n) - 1u; ok != want;) {
        for (unsigned i = 0; i < n; i++) {
            if (ok & (1u << i)) continue;
            uintptr_t pp = mali_env.gpu + M_PP(i);
            wr(pp + PP_PERF_CNT_0_LIMIT, 0xc01a0000u);
            if (rd(pp + PP_PERF_CNT_0_LIMIT) == 0xc01a0000u) {
                wr(pp + PP_PERF_CNT_0_LIMIT, 0);
                wr(pp + PP_INT_CLEAR, PP_IRQ_ALL);
                ok |= (1u << i);
            }
        }
        if (ticks() - t0 > 24u * 1000u)
            return MALI_ERR_RESET;
    }
    return MALI_OK;
}

#define PP_CTRL_SOFT_RESET     (1u << 7)
#define PP_IRQ_RESET_COMPLETED (1u << 12)

static void pp_soft_reset_begin(unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        uintptr_t pp = mali_env.gpu + M_PP(i);
        wr(pp + PP_INT_MASK, 0);
        wr(pp + PP_INT_CLEAR, PP_IRQ_ALL);
        wr(pp + PP_CTRL, PP_CTRL_SOFT_RESET);
    }
}

static int pp_soft_reset_finish(unsigned n)
{
    uint32_t t0 = ticks();
    for (uint32_t ok = 0, want = (1u << n) - 1u; ok != want;) {
        for (unsigned i = 0; i < n; i++) {
            if (ok & (1u << i)) continue;
            uintptr_t pp = mali_env.gpu + M_PP(i);
            if (rd(pp + PP_INT_RAWSTAT) & PP_IRQ_RESET_COMPLETED) {
                wr(pp + PP_INT_CLEAR, PP_IRQ_ALL);
                ok |= (1u << i);
            }
        }
        if (ticks() - t0 > 24u * 200u)
            return pp_hard_reset_parallel(n);
    }
    return MALI_OK;
}

static int pp_soft_reset_parallel(unsigned n)
{
    pp_soft_reset_begin(n);
    return pp_soft_reset_finish(n);
}

static int pp_in_flight;
static unsigned pp_in_flight_n;
static uint32_t pp_in_flight_t0;
static int pp_needs_hard_reset = 1;
volatile uint32_t exy_mali_pp_seq;
volatile uint32_t exy_mali_pp_done_seq;

static int mali_wait_pp_locked(void)
{
    if (!pp_in_flight)
        return MALI_OK;
    unsigned n = pp_in_flight_n;
    uint32_t want = (1u << n) - 1u;
    uint32_t done = 0;
    int err = MALI_OK;
    unsigned err_pp = 0;
    uint32_t t0 = pp_in_flight_t0;
    for (unsigned sleeps = 0;;) {
        for (unsigned i = 0; i < n; i++) {
            if (done & (1u << i))
                continue;
            uintptr_t pp = mali_env.gpu + M_PP(i);
            uint32_t s = rd(pp + PP_INT_RAWSTAT);
            mali_last_status = s;
            if (s & PP_IRQ_ERROR) {
                err = MALI_ERR_JOB;
                err_pp = i;
                break;
            }
            if (s & PP_IRQ_END_OF_FRAME) {
                wr(pp + PP_INT_CLEAR, PP_IRQ_ALL);
                done |= (1u << i);
            }
        }
        if (err != MALI_OK || done == want)
            break;
        if (ticks() - t0 < 24u * 8000u) {
            __asm__ volatile("yield");
            continue;
        }
        if (++sleeps > 2000u) {
            err = MALI_ERR_TIMEOUT;
            for (unsigned i = 0; i < n; i++)
                if (!(done & (1u << i))) { err_pp = i; break; }
            break;
        }
        iosleep_fn(1);
    }
    exy_gpu_log.pp_us += (ticks() - t0) / 24u;
    pp_in_flight = 0;
    exy_mali_pp_done_seq = exy_mali_pp_seq;
    if (err != MALI_OK) {
        pp_needs_hard_reset = 1;
        uintptr_t mmu = mali_env.gpu + M_MMU(1 + err_pp);
        exy_gpu_log.ticks = rd(mmu + 0x10);
        mali_last_status = (rd(mmu + 0x14) << 16) | (rd(mmu + 0x04) & 0xffff);
        if (rd(mmu + 0x14)) {
            /* Recover stalled MMU after page fault so subsequent jobs can still run. */
            wr(mmu + 0x18, 3);
            mmu_init(&mali_env, mmu);
        }
        exy_gpu_log.job = (uint32_t)err;
        exy_gpu_log.rawstat = mali_last_status;
        exy_clean_line(&exy_gpu_log);
        return err;
    }
    return MALI_OK;
}

/* Async kernel Mali-400 worker thread (runs on CPU1 overlapped with CPU0 backboardd/Mesa). */
#define MALI_Q_SIZE 16u
#define MALI_Q_MASK (MALI_Q_SIZE - 1u)
struct mali_q_entry {
    uint8_t is_gpp;
    uint32_t seq;
    struct mali_gp_pp_submit req;
};
static struct mali_q_entry mali_q[MALI_Q_SIZE];
static volatile uint32_t mali_q_head, mali_q_tail;
static volatile uint32_t mali_q_busy;
static volatile uint32_t mali_q_waiters, mali_idle_waiters;
static volatile uint32_t mali_enq_seq, mali_pp_cur_seq;
static int mali_worker_started;
static int mali_last_err;

static int mali_start_pp_locked(const struct mali_pp_submit *req, int need_l2_flush, int pp_reset_started);

static inline void gpu_copy_words(void *dst, const void *src, uint32_t bytes)
{
    uint32_t *d = (uint32_t *)dst;
    const uint32_t *s = (const uint32_t *)src;
    for (uint32_t i = 0; i < (bytes >> 2); i++)
        d[i] = s[i];
}

static void mali_worker_fn(void *arg, int wait_result)
{
    (void)arg; (void)wait_result;
    for (;;) {
        mali_lock();
        for (;;) {
            if (mali_q_head != mali_q_tail) {
                struct mali_q_entry ent;
                gpu_copy_words(&ent, &mali_q[mali_q_tail & MALI_Q_MASK], sizeof(ent));
                mali_q_tail++;
                if (mali_q_waiters) {
                    mali_q_waiters = 0;
                    thread_wakeup_prim_fn((void *)&mali_q_tail, 0, 0);
                }
                mali_q_busy = 1;
                mali_unlock();

                /* Run hardware GP+PP unlocked so CPU0 enqueues never block on mali_mtx. */
                (void)mali_wait_pp_locked();
                if (mali_pp_cur_seq) {
                    exy_mali_pp_done_seq = mali_pp_cur_seq;
                    if (mali_status_va)
                        mali_status_va[0] = mali_pp_cur_seq;
                    mali_pp_cur_seq = 0;
                }
                int err = MALI_OK;
                if (ent.is_gpp) {
                    int pp_pre_reset = (!pp_needs_hard_reset &&
                                        ent.req.pp.num_pp >= 1u && ent.req.pp.num_pp <= 4u);
                    if (pp_pre_reset)
                        pp_soft_reset_begin(ent.req.pp.num_pp);
                    err = mali_submit_gp(ent.req.gp);
                    exy_gpu_log.pad[1]++;
                    if (err == MALI_OK) {
                        err = mali_start_pp_locked(&ent.req.pp, 0, pp_pre_reset);
                        exy_gpu_log.pad[2]++;
                    }
                } else {
                    err = mali_start_pp_locked(&ent.req.pp, 1, 0);
                    exy_gpu_log.pad[2]++;
                }
                if (err == MALI_OK) {
                    mali_pp_cur_seq = ent.seq;
                } else {
                    exy_mali_pp_done_seq = ent.seq;
                    if (mali_status_va)
                        mali_status_va[0] = ent.seq;
                }
                mali_last_err = err;
                exy_gpu_log.job = (uint32_t)err;
                exy_gpu_log.rawstat = mali_last_status;
                if (err || (exy_gpu_log.pad[2] & 63u) == 0u) {
                    exy_clean_line(&exy_gpu_log);
                    exy_clean_line(&exy_gpu_log.gp_us);
                }

                mali_lock();
                continue;
            }
            if (pp_in_flight) {
                /* Wait for final in-flight PP unlocked so newly arriving submits don't stall. */
                mali_unlock();
                (void)mali_wait_pp_locked();
                if (mali_pp_cur_seq) {
                    exy_mali_pp_done_seq = mali_pp_cur_seq;
                    if (mali_status_va)
                        mali_status_va[0] = mali_pp_cur_seq;
                    mali_pp_cur_seq = 0;
                }
                mali_lock();
                continue;
            }
            mali_q_busy = 0;
            if (mali_idle_waiters) {
                mali_idle_waiters = 0;
                thread_wakeup_prim_fn((void *)&mali_q_busy, 0, 0);
            }
            lck_mtx_sleep_fn(mali_mtx, 0u, (void *)&mali_q_head, 0u);
        }
    }
}

static void mali_ensure_worker_locked(void)
{
    if (!mali_worker_started) {
        void *th = 0;
        if (kernel_thread_start_fn(mali_worker_fn, 0, &th) == 0)
            mali_worker_started = 1;
    }
}

static int mali_drain_idle_locked(void)
{
    while (mali_q_head != mali_q_tail || mali_q_busy) {
        mali_idle_waiters = 1;
        lck_mtx_sleep_fn(mali_mtx, 0u, (void *)&mali_q_busy, 0u);
    }
    int err = mali_wait_pp_locked();
    return (err != MALI_OK) ? err : mali_last_err;
}

void exy_mali_wait_idle(void)
{
    if (!mali_ready)
        return;
    if (!mali_q_busy && mali_q_head == mali_q_tail && !pp_in_flight)
        return;
    mali_lock();
    (void)mali_drain_idle_locked();
    mali_unlock();
}

static int mali_start_pp_locked(const struct mali_pp_submit *req, int need_l2_flush, int pp_reset_started)
{
    uint32_t f[MALI_PP_FRAME_REGS];
    unsigned n = req->num_pp;
    if (!n || n > 4)
        return MALI_ERR_MAP;
    (void)mali_wait_pp_locked();
    if (need_l2_flush) {
        __asm__ volatile("dsb" ::: "memory"); /* drain CPU WCOMB store buffer before PP DMA */
        /* Overlap L2 cache tag clear with PP soft reset */
        wr(mali_env.gpu + M_L2 + L2_COMMAND, 1);
    }
    for (unsigned i = 0; i < MALI_PP_FRAME_REGS; i++)
        f[i] = req->frame[i];
    int rerr = pp_needs_hard_reset ? pp_hard_reset_parallel(n)
             : (pp_reset_started ? pp_soft_reset_finish(n) : pp_soft_reset_parallel(n));
    if (need_l2_flush && poll(&mali_env, mali_env.gpu + M_L2 + L2_STATUS, 1, 0, 1000))
        return MALI_ERR_L2;
    if (rerr != MALI_OK)
        return MALI_ERR_RESET;
    pp_needs_hard_reset = 0;
    for (unsigned i = 0; i < n; i++) {
        f[0] = req->plbu[i];
        f[12] = req->stack[i];
        pp_start(mali_env.gpu + M_PP(i), f, req->wb);
    }
    pp_in_flight_t0 = ticks();
    exy_last_gpu_mct = pp_in_flight_t0;
    pp_in_flight_n = n;
    exy_mali_pp_seq++;
    pp_in_flight = 1;
    return MALI_OK;
}

uint32_t exy_mali_external_method(void *self, uint32_t selector, struct io_ext_args *args,
                                  void *disp, void *target, void *ref)
{
    (void)self; (void)disp; (void)target; (void)ref;
    if (!args)
        return KIORETURN_BAD_ARG;
    if (mali_ensure_init() != MALI_OK)
        return KIORETURN_IO_ERROR;

    switch (selector) {
    case 0: { /* MALI_CMD_INFO */
        if (!args->scalar_out || args->scalar_out_cnt < 4)
            return KIORETURN_BAD_ARG;
        uint32_t cpa = 0, csz = 0;
        mali_lock();
        mali_client_pool(self, &cpa, &csz);
        args->scalar_out[0] = 0x0400;        /* Mali-400 */
        args->scalar_out[1] = 4;             /* 4 PP cores */
        args->scalar_out[2] = cpa;
        args->scalar_out[3] = csz;
        args->scalar_out_cnt = 4;
        mali_unlock();
        return KIORETURN_SUCCESS;
    }

    case 1: { /* MALI_CMD_CLEAR: (fb_pa, w, h, stride, rgba) */
        if (!args->scalar_in || args->scalar_in_cnt < 5)
            return KIORETURN_BAD_ARG;
        struct mali_fb fb = {
            (uint32_t)args->scalar_in[0], (uint32_t)args->scalar_in[1],
            (uint32_t)args->scalar_in[2], (uint32_t)args->scalar_in[3],
        };
        mali_lock();
        (void)mali_drain_idle_locked();
        int err = mali_pp_clear(&mali_env, &fb, (uint32_t)args->scalar_in[4]);
        exy_gpu_log.job = (uint32_t)err;
        exy_gpu_log.rawstat = mali_last_status;
        mali_unlock();
        return err == MALI_OK ? KIORETURN_SUCCESS : KIORETURN_IO_ERROR;
    }

    case 2: { /* MALI_CMD_MAP_VA: (va, pa, size) */
        if (!args->scalar_in || args->scalar_in_cnt < 3)
            return KIORETURN_BAD_ARG;
        mali_lock();
        (void)mali_drain_idle_locked();
        int err = map_va(&mali_env, (uint32_t)args->scalar_in[0],
                         (uint32_t)args->scalar_in[1], (uint32_t)args->scalar_in[2]);
        if (err) {
            mali_unlock();
            return KIORETURN_IO_ERROR;
        }
        for (unsigned i = 0; i < 5; i++)
            wr(mali_env.gpu + M_MMU(i) + MMU_COMMAND, MMU_CMD_ZAP_CACHE);
        err = l2_flush(&mali_env);
        mali_unlock();
        return err == MALI_OK ? KIORETURN_SUCCESS : KIORETURN_IO_ERROR;
    }

    case 3: /* MALI_CMD_SUBMIT_GP: struct_in = uint32_t gp[6] */
        if (!args->struct_in || args->struct_in_cnt != 6 * sizeof(uint32_t))
            return KIORETURN_BAD_ARG;
        {
            mali_lock();
            (void)mali_drain_idle_locked();
            int err = mali_submit_gp((const uint32_t *)args->struct_in);
            exy_gpu_log.pad[1]++;
            exy_gpu_log.job = (uint32_t)err;
            exy_gpu_log.rawstat = mali_last_status;
            if (err || (exy_gpu_log.pad[1] & 63u) == 0u)
                exy_clean_line(&exy_gpu_log);
            mali_unlock();
            return err == MALI_OK ? KIORETURN_SUCCESS : KIORETURN_IO_ERROR;
        }

    case 4: /* MALI_CMD_SUBMIT_PP: enqueue async PP job onto CPU1 worker ring */
        if (!args->struct_in || args->struct_in_cnt != sizeof(struct mali_pp_submit))
            return KIORETURN_BAD_ARG;
        {
            mali_lock();
            mali_ensure_worker_locked();
            if (mali_worker_started) {
                while (mali_q_head - mali_q_tail >= MALI_Q_SIZE) {
                    mali_q_waiters = 1;
                    lck_mtx_sleep_fn(mali_mtx, 0u, (void *)&mali_q_tail, 0u);
                }
                uint32_t seq = ++mali_enq_seq;
                struct mali_q_entry *e = &mali_q[mali_q_head & MALI_Q_MASK];
                e->is_gpp = 0;
                e->seq = seq;
                gpu_copy_words(&e->req.pp, args->struct_in, sizeof(e->req.pp));
                mali_q_head++;
                int wake = !mali_q_busy;
                mali_q_busy = 1;
                if (wake)
                    thread_wakeup_prim_fn((void *)&mali_q_head, 1, 0);
                uint32_t done = exy_mali_pp_done_seq;
                mali_unlock();
                if (args->struct_out && args->struct_out_cnt >= 8u) {
                    uint32_t *out = (uint32_t *)args->struct_out;
                    out[0] = seq;
                    out[1] = done;
                    args->struct_out_cnt = 8u;
                }
                return KIORETURN_SUCCESS;
            }
            (void)mali_wait_pp_locked();
            int err = mali_start_pp_locked((const struct mali_pp_submit *)args->struct_in, 1, 0);
            exy_gpu_log.pad[2]++;
            exy_gpu_log.job = (uint32_t)err;
            exy_gpu_log.rawstat = mali_last_status;
            mali_unlock();
            return err == MALI_OK ? KIORETURN_SUCCESS : KIORETURN_IO_ERROR;
        }

    case 5: /* MALI_CMD_POLL_TOUCH: optional struct_in = uint32_t dev_ptr, struct_out = struct exy_touch_state (56B) */
        if (!args->struct_out || args->struct_out_cnt < 56u)
            return KIORETURN_BAD_ARG;
        if (args->struct_in && args->struct_in_cnt >= 4u) {
            extern void exy_touch_set_dev(uint32_t dev_ptr);
            exy_touch_set_dev(*(const uint32_t *)args->struct_in);
        }
        exy_touch_poll((struct exy_touch_state *)args->struct_out);
        args->struct_out_cnt = 56u;
        return KIORETURN_SUCCESS;

    case 6: /* MALI_CMD_KLOG: struct_in = short diagnostic string -> kernel exy_putc ring */
        if (args->struct_in && args->struct_in_cnt > 0 && args->struct_in_cnt <= 160u) {
            const char *s = (const char *)args->struct_in;
            for (uint32_t i = 0; i < args->struct_in_cnt && s[i]; i++)
                exy_putc(s[i]);
        }
        return KIORETURN_SUCCESS;

    case 7: { /* MALI_CMD_BIND_SCANOUT: (user_va, fb_idx_or_pa) -> binds IOSurface first-page PA to FIMD0 FB */
        if (!args->scalar_in || args->scalar_in_cnt < 2)
            return KIORETURN_BAD_ARG;
        uint32_t uva = (uint32_t)args->scalar_in[0];
        uint32_t arg1 = (uint32_t)args->scalar_in[1];
        uint32_t fpa;
        if (arg1 < 2u)
            fpa = FB_PA + arg1 * 0x00400000u;
        else if (arg1 >= FB_PA && arg1 < 0x7e000000u && (arg1 & 0xfffu) == 0u)
            fpa = arg1;
        else
            return KIORETURN_BAD_ARG;
        if (!uva)
            return KIORETURN_BAD_ARG;
        void *task = current_task_fn();
        void *pmap = task ? get_task_pmap_fn(task) : 0;
        uint32_t ppn = pmap ? pmap_find_phys_fn(pmap, uva, 0u) : 0u;
        uint32_t spa = ppn << 12;
        if (spa < 0x40000000u || spa >= 0x60000000u)
            return KIORETURN_BAD_ARG;
        mali_lock();
        uint32_t gen = ++exy_direct_fb_gen;
        int slot = -1;
        for (int i = 0; i < 12; i++) {
            if (exy_direct_fbs[i].surf_pa == spa) { slot = i; break; }
            if (slot < 0 && !exy_direct_fbs[i].surf_pa) slot = i;
        }
        if (slot < 0) slot = (int)(gen % 12u);
        exy_direct_fbs[slot].surf_pa = spa;
        exy_direct_fbs[slot].fb_pa = fpa;
        exy_direct_fbs[slot].gen = gen;
        exy_direct_fbs[slot].stride = 2880u;
        mali_unlock();
        if (args->scalar_out && args->scalar_out_cnt >= 1) {
            args->scalar_out[0] = spa;
            args->scalar_out_cnt = 1;
        }
        return KIORETURN_SUCCESS;
    }

    case 8: /* MALI_CMD_SUBMIT_GP_PP: enqueue onto CPU1 async Mali worker ring (returns immediately). */
        if (!args->struct_in || args->struct_in_cnt != sizeof(struct mali_gp_pp_submit))
            return KIORETURN_BAD_ARG;
        {
            const struct mali_gp_pp_submit *req = (const struct mali_gp_pp_submit *)args->struct_in;
            mali_lock();
            mali_ensure_worker_locked();
            if (mali_worker_started) {
                while (mali_q_head - mali_q_tail >= MALI_Q_SIZE) {
                    mali_q_waiters = 1;
                    lck_mtx_sleep_fn(mali_mtx, 0u, (void *)&mali_q_tail, 0u);
                }
                uint32_t seq = ++mali_enq_seq;
                struct mali_q_entry *e = &mali_q[mali_q_head & MALI_Q_MASK];
                e->is_gpp = 1;
                e->seq = seq;
                gpu_copy_words(&e->req, req, sizeof(e->req));
                mali_q_head++;
                int wake = !mali_q_busy;
                mali_q_busy = 1;
                if (wake)
                    thread_wakeup_prim_fn((void *)&mali_q_head, 1, 0);
                uint32_t done = exy_mali_pp_done_seq;
                mali_unlock();
                if (args->struct_out && args->struct_out_cnt >= 8u) {
                    uint32_t *out = (uint32_t *)args->struct_out;
                    out[0] = seq;
                    out[1] = done;
                    args->struct_out_cnt = 8u;
                }
                return KIORETURN_SUCCESS;
            }
            (void)mali_wait_pp_locked();
            int pp_pre_reset = (!pp_needs_hard_reset &&
                                req->pp.num_pp >= 1u && req->pp.num_pp <= 4u);
            if (pp_pre_reset)
                pp_soft_reset_begin(req->pp.num_pp);
            int err = mali_submit_gp(req->gp);
            exy_gpu_log.pad[1]++;
            if (err == MALI_OK) {
                err = mali_start_pp_locked(&req->pp, 0, pp_pre_reset);
                exy_gpu_log.pad[2]++;
            }
            exy_gpu_log.job = (uint32_t)err;
            exy_gpu_log.rawstat = mali_last_status;
            mali_unlock();
            return err == MALI_OK ? KIORETURN_SUCCESS : KIORETURN_IO_ERROR;
        }

    case 9: /* MALI_CMD_WAIT_IDLE: drain async worker ring and wait for final PP completion */
        {
            mali_lock();
            int err = mali_drain_idle_locked();
            mali_unlock();
            return err == MALI_OK ? KIORETURN_SUCCESS : KIORETURN_IO_ERROR;
        }

    case 10: { /* MALI_CMD_MAP_IOSURF: (user_va, size, create) -> zero-copy Mali VA at 0x80000000+slot*4M */
        if (!args->scalar_in || args->scalar_in_cnt < 3 || !args->scalar_out || args->scalar_out_cnt < 1)
            return KIORETURN_BAD_ARG;
        uint32_t uva = (uint32_t)args->scalar_in[0];
        uint32_t sz = (uint32_t)args->scalar_in[1];
        uint32_t create = (uint32_t)args->scalar_in[2];
        if (!uva || (uva & 0xfffu) || !sz || (sz & 0xfffu) || sz > 0x00400000u)
            return KIORETURN_BAD_ARG;
        void *task = current_task_fn();
        void *pmap = task ? get_task_pmap_fn(task) : 0;
        if (!pmap)
            return KIORETURN_IO_ERROR;
        uint32_t pa0 = pmap_find_phys_fn(pmap, uva, 0u) << 12;
        uint32_t pam = pmap_find_phys_fn(pmap, uva + ((sz >> 1) & ~0xfffu), 0u) << 12;
        uint32_t pae = pmap_find_phys_fn(pmap, uva + sz - 0x1000u, 0u) << 12;
        if (pa0 < 0x40000000u || pa0 >= 0x60000000u ||
            pam < 0x40000000u || pam >= 0x60000000u ||
            pae < 0x40000000u || pae >= 0x60000000u) {
            args->scalar_out[0] = 0;
            args->scalar_out_cnt = 1;
            return KIORETURN_SUCCESS;
        }
        #define MALI_ZC_SLOTS 12u
        #define MALI_ZC_BASE  0x80000000u
        #define MALI_ZC_DIRECT_PA 0x60c00000u /* 4x4 MiB contiguous zero-copy scanout slots (0x60c00000..0x61c00000) */
        static struct { uint32_t pa0, pam, pae, sz, direct_pa; } zc_slots[MALI_ZC_SLOTS];
        static uint32_t zc_next, zc_direct_next;
        uint32_t gva = 0;
        mali_lock();
        for (uint32_t i = 0; i < MALI_ZC_SLOTS; i++) {
            if (zc_slots[i].sz == sz && zc_slots[i].pa0 == pa0 &&
                zc_slots[i].pam == pam && zc_slots[i].pae == pae) {
                gva = MALI_ZC_BASE + i * 0x00400000u;
                break;
            }
        }
        if (!gva && create) {
            (void)mali_drain_idle_locked();
            uint32_t slot = zc_next;
            uint32_t base_va = MALI_ZC_BASE + slot * 0x00400000u;
            uint32_t direct_pa = 0u, direct_stride = 0u;
            if (sz == 0x003c0000u) {
                direct_pa = MALI_ZC_DIRECT_PA + ((zc_direct_next++) & 3u) * 0x00400000u;
                direct_stride = 3072u;
            } else if (sz == 0x00384000u) {
                direct_pa = MALI_ZC_DIRECT_PA + ((zc_direct_next++) & 3u) * 0x00400000u;
                direct_stride = 2880u;
            }
            int ok = 1;
            if (direct_pa) {
                if (map_va(&mali_env, base_va, direct_pa, sz) != MALI_OK)
                    ok = 0;
            } else {
                for (uint32_t off = 0; off < sz; off += 0x1000u) {
                    uint32_t pa = pmap_find_phys_fn(pmap, uva + off, 0u) << 12;
                    if (pa < 0x40000000u || pa >= 0x60000000u ||
                        map_va(&mali_env, base_va + off, pa, 0x1000u) != MALI_OK) {
                        ok = 0;
                        break;
                    }
                    exy_dma_clean_inv_pa(pa, 0x1000u);
                }
            }
            if (ok) {
                for (unsigned i = 0; i < 5; i++)
                    wr(mali_env.gpu + M_MMU(i) + MMU_COMMAND, MMU_CMD_ZAP_CACHE);
                l2_flush(&mali_env);
                zc_slots[slot].pa0 = pa0;
                zc_slots[slot].pam = pam;
                zc_slots[slot].pae = pae;
                zc_slots[slot].sz = sz;
                zc_slots[slot].direct_pa = direct_pa;
                zc_next = (slot + 1u) % MALI_ZC_SLOTS;
                gva = base_va;
                if (direct_pa) {
                    uint32_t gen = ++exy_direct_fb_gen;
                    int dslot = -1;
                    for (int i = 0; i < 12; i++) {
                        if (exy_direct_fbs[i].surf_pa == pa0) { dslot = i; break; }
                        if (dslot < 0 && !exy_direct_fbs[i].surf_pa) dslot = i;
                    }
                    if (dslot < 0) dslot = (int)(gen % 12u);
                    exy_direct_fbs[dslot].surf_pa = pa0;
                    exy_direct_fbs[dslot].fb_pa = direct_pa;
                    exy_direct_fbs[dslot].gen = gen;
                    exy_direct_fbs[dslot].stride = direct_stride;
                }
            }
        }
        mali_unlock();
        args->scalar_out[0] = gva;
        args->scalar_out_cnt = 1;
        return KIORETURN_SUCCESS;
    }

    case 11: /* MALI_CMD_BENCH_REBOOT: clean WDT warm reset back to fastboot with intact DRAM */
        exy_clean_line(&exy_gpu_log);
        exy_clean_line(&exy_gpu_log.gp_us);
        {
            extern void exy_reboot_fastboot(void);
            exy_reboot_fastboot();
        }
        return KIORETURN_SUCCESS;

    case 12: /* MALI_CMD_DRV_POLL: struct_out = struct exy_drv_status (64..80 bytes) */
        if (!args->struct_out || args->struct_out_cnt < 64u)
            return KIORETURN_BAD_ARG;
        args->struct_out_cnt = exy_drv_poll_sz(args->struct_out, args->struct_out_cnt);
        return KIORETURN_SUCCESS;

    case 13: /* MALI_CMD_DRV_BRIGHTNESS: scalar_in[0] = 0..255 */
        if (!args->scalar_in || args->scalar_in_cnt < 1)
            return KIORETURN_BAD_ARG;
        return exy_drv_set_brightness((uint8_t)args->scalar_in[0]) == 0 ? KIORETURN_SUCCESS : KIORETURN_IO_ERROR;

    case 14: /* MALI_CMD_DRV_LED: scalar_in[0..2] = R, G, B (0..255) */
        if (!args->scalar_in || args->scalar_in_cnt < 3)
            return KIORETURN_BAD_ARG;
        return exy_drv_set_led((uint8_t)args->scalar_in[0], (uint8_t)args->scalar_in[1], (uint8_t)args->scalar_in[2]) == 0
               ? KIORETURN_SUCCESS : KIORETURN_IO_ERROR;

    case 15: /* MALI_CMD_DRV_VIBRATE: scalar_in[0] = duration ms */
        if (!args->scalar_in || args->scalar_in_cnt < 1)
            return KIORETURN_BAD_ARG;
        return exy_drv_vibrate((uint32_t)args->scalar_in[0]) == 0 ? KIORETURN_SUCCESS : KIORETURN_IO_ERROR;

    case 16: /* MALI_CMD_DRV_POWER_SYNC: poll MAX17047 + MAX77693 and update IOPMPowerSource */
        exy_drv_power_sync();
        return KIORETURN_SUCCESS;

    case 17: /* MALI_CMD_DRV_AUDIO_VOL: scalar_in[0] = 0..100 */
        if (!args->scalar_in || args->scalar_in_cnt < 1)
            return KIORETURN_BAD_ARG;
        return exy_drv_set_volume((uint8_t)args->scalar_in[0]) == 0 ? KIORETURN_SUCCESS : KIORETURN_IO_ERROR;

    case 18: /* MALI_CMD_DRV_AUDIO_TONE: scalar_in[0] = hz, scalar_in[1] = ms */
        if (!args->scalar_in || args->scalar_in_cnt < 2)
            return KIORETURN_BAD_ARG;
        return exy_drv_audio_tone((uint16_t)args->scalar_in[0], (uint16_t)args->scalar_in[1]) == 0
               ? KIORETURN_SUCCESS : KIORETURN_IO_ERROR;

    case 19: /* MALI_CMD_DRV_TORCH: scalar_in[0] = 0..16 */
        if (!args->scalar_in || args->scalar_in_cnt < 1)
            return KIORETURN_BAD_ARG;
        return exy_drv_set_torch((uint8_t)args->scalar_in[0]) == 0 ? KIORETURN_SUCCESS : KIORETURN_IO_ERROR;

    case 22: /* MALI_CMD_WIFI_OP: BCM4334 SDIO control/firmware/CDC bridge (wifi.c) */
        if (!args->struct_in || !args->struct_out)
            return KIORETURN_BAD_ARG;
        {
            extern int exy_wifi_handle_op(const void *, uint32_t, void *, uint32_t *);
            uint32_t olen = args->struct_out_cnt;
            if (exy_wifi_handle_op(args->struct_in, args->struct_in_cnt, args->struct_out, &olen) != 0)
                return KIORETURN_IO_ERROR;
            args->struct_out_cnt = olen;
            return KIORETURN_SUCCESS;
        }

    default:
        return KIORETURN_UNSUPPORTED;
    }
}

uint32_t exy_mali_client_memory(void *self, uint32_t type, uint32_t *options, void **memory)
{
    if (!memory)
        return KIORETURN_BAD_ARG;
    if (mali_ensure_init() != MALI_OK)
        return KIORETURN_IO_ERROR;
    uint32_t pa, size;
    if (type == 0) {
        mali_lock();
        mali_client_pool(self, &pa, &size);
        mali_unlock();
    } else if (type == 1) {
        pa = FB_PA;
        size = FB_SIZE_BYTES;
    } else if (type == 2) {
        pa = MALI_STATUS_PA;
        size = 0x1000u;
    } else {
        return KIORETURN_BAD_ARG;
    }
    void *md = iomem_with_phys(pa, size, KIO_DIR_INOUT);
    if (!md)
        return KIORETURN_IO_ERROR;
    if (options)
        *options = KIO_MAP_WCOMB;
    *memory = md;
    return KIORETURN_SUCCESS;
}

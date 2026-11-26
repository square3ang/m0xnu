/*
 * Exynos 4412 (Galaxy S3 i9300 / midas) PMIC (MAX77686 on I2C7),
 * 1.4 GHz APLL boost, Melfas MMS114 touchscreen (I2C3 @ 0x48),
 * and physical GPIO buttons (Home, Power, Vol+, Vol-).
 */
#include <stdint.h>

typedef uintptr_t (*ml_io_map_t)(uintptr_t pa, uint32_t size);
#define ml_io_map ((ml_io_map_t)0x800c0bf5) /* Thumb */

extern uintptr_t mct, mshc, exy_pl310;

void exy_clean_line(const void *p)
{
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 1\ndsb" :: "r"(p) : "memory");
    uintptr_t va = (uintptr_t)p;
    if (exy_pl310 && va >= 0x80000000u && va < 0xa0000000u) {
        *(volatile uint32_t *)(exy_pl310 + 0x7b0u) = (uint32_t)((va - 0x40000000u) & ~31u);
        *(volatile uint32_t *)(exy_pl310 + 0x730u) = 0u;
        __asm__ volatile("dsb" ::: "memory");
    }
}

static void dma_clean_range(const void *p, uint32_t len)
{
    uintptr_t a = (uintptr_t)p & ~31u;
    uintptr_t end = (uintptr_t)p + len;
    for (uintptr_t x = a; x < end; x += 32u)
        __asm__ volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(x) : "memory");
    __asm__ volatile("dsb" ::: "memory");
    if (exy_pl310) {
        for (uintptr_t x = a; x < end; x += 32u)
            *(volatile uint32_t *)(exy_pl310 + 0x7b0u) = (uint32_t)(x - 0x40000000u);
        *(volatile uint32_t *)(exy_pl310 + 0x730u) = 0u;
        __asm__ volatile("dsb" ::: "memory");
    }
}

static void dma_clean_inv_range(const void *p, uint32_t len)
{
    uintptr_t a = (uintptr_t)p & ~31u;
    uintptr_t end = (uintptr_t)p + len;
    for (uintptr_t x = a; x < end; x += 32u)
        __asm__ volatile("mcr p15, 0, %0, c7, c14, 1" :: "r"(x) : "memory");
    __asm__ volatile("dsb" ::: "memory");
    if (exy_pl310) {
        for (uintptr_t x = a; x < end; x += 32u)
            *(volatile uint32_t *)(exy_pl310 + 0x7f0u) = (uint32_t)(x - 0x40000000u);
        *(volatile uint32_t *)(exy_pl310 + 0x730u) = 0u;
        __asm__ volatile("dsb" ::: "memory");
    }
}

void exy_dma_clean_inv_pa(uint32_t pa, uint32_t len)
{
    if (pa >= 0x40000000u && pa + len <= 0x60000000u)
        dma_clean_inv_range((const void *)(uintptr_t)(pa + 0x40000000u), len);
}

static void dma_inv_range(const void *p, uint32_t len)
{
    uintptr_t a = (uintptr_t)p & ~31u;
    uintptr_t end = (uintptr_t)p + len;
    if (exy_pl310) {
        for (uintptr_t x = a; x < end; x += 32u)
            *(volatile uint32_t *)(exy_pl310 + 0x770u) = (uint32_t)(x - 0x40000000u);
        *(volatile uint32_t *)(exy_pl310 + 0x730u) = 0u;
        __asm__ volatile("dsb" ::: "memory");
    }
    for (uintptr_t x = a; x < end; x += 32u)
        __asm__ volatile("mcr p15, 0, %0, c7, c6, 1" :: "r"(x) : "memory");
    __asm__ volatile("dsb" ::: "memory");
}

void exy_dma_inv_pa(uint32_t pa, uint32_t len)
{
    if (pa >= 0x40000000u && pa + len <= 0x60000000u)
        dma_inv_range((const void *)(uintptr_t)(pa + 0x40000000u), len);
}

void exy_dma_inv_l2_nosync(uint32_t pa, uint32_t len)
{
    if (!exy_pl310 || pa < 0x40000000u || pa + len > 0x60000000u)
        return;
    uint32_t a = pa & ~31u;
    uint32_t end = pa + len;
    for (uint32_t x = a; x < end; x += 32u)
        *(volatile uint32_t *)(exy_pl310 + 0x770u) = x;
}

void exy_dma_inv_l2_sync(void)
{
    if (exy_pl310)
        *(volatile uint32_t *)(exy_pl310 + 0x730u) = 0u;
    __asm__ volatile("dsb" ::: "memory");
}

#define CMU_LEFTBUS_PA  0x10034000u
#define CMU_RIGHTBUS_PA 0x10038000u
#define CMU_TOP_PA      0x1003c000u
#define CMU_DMC_PA      0x10040000u
#define CMU_CPU_PA      0x10044000u
#define PL310_PA        0x10502000u
#define GPIO_LEFT_PA    0x11400000u /* GPA1 @ +0x020 (I2C3: GPA1[2]=SDA3, GPA1[3]=SCL3) */
#define GPIO_RIGHT_PA   0x11000000u /* GPM2 @ +0x2a0 (GPM2[3]=TSP_INT), GPX0..3 @ +0xc00..0xc60 */
#define I2C3_PA         0x13890000u /* Touchscreen (Melfas MMS114 @ 0x48 / Atmel @ 0x4a) */
#define I2C7_PA         0x138d0000u /* MAX77686 PMIC @ 0x09 */

/* S3C2440 / Exynos4 I2C registers */
#define IICCON          0x00u
#define IICSTAT         0x04u
#define IICADD          0x08u
#define IICDS           0x0cu
#define IICLC           0x10u

#define I2CCON_ACKGEN   (1u << 7)
#define I2CCON_IRPND    (1u << 4)
#define I2CSTAT_NACK    (1u << 0)
#define I2CSTAT_BSY     (1u << 5)
#define I2C_MODE_MR     0x80u
#define I2C_MODE_MT     0xc0u
#define I2C_TXRX_ENA    0x10u
#define I2C_START_STOP  0x20u

/* MAX77686 PMIC (I2C7 @ 7-bit 0x09) */
#define MAX77686_ADDR       0x09u
#define MAX77686_REG_ID     0x00u
#define MAX77686_BUCK1OUT   0x11u /* vdd_mif: 750 mV + val*50 mV -> 0x06 = 1.05 V */
#define MAX77686_BUCK2DVS1  0x14u /* vdd_arm: 8 DVS slots 0x14..0x1b; 600 mV + val*12.5 mV */
#define MAX77686_BUCK3DVS1  0x1eu /* vdd_int: 8 DVS slots 0x1e..0x25; 600 mV + val*12.5 mV */
#define MAX77686_BUCK4DVS1  0x28u /* vdd_g3d: 8 DVS slots 0x28..0x2f; 600 mV + val*12.5 mV -> 0x26 = 1.075 V */
#define MAX77686_LDO23CTRL1 0x56u /* TSP_AVDD_3.3V: 0xc0 | ((3300-800)/50) = 0xf2 */
#define MAX77686_LDO24CTRL1 0x57u /* TSP_VDD_1.8V:  0xc0 | ((1800-800)/50) = 0xd4 */

/* Melfas MMS114 (I2C3 @ 7-bit 0x48) */
#define MMS114_ADDR         0x48u
#define MMS114_MODE_CTRL    0x01u
#define MMS114_XY_RES_H     0x02u
#define MMS114_PACKET_SIZE  0x0fu
#define MMS114_INFORMATION  0x10u
#define MMS114_TSP_REV      0xf0u

#define TOUCH_MAX_FINGERS   5u

struct exy_finger {
    uint8_t id;
    uint8_t down;
    uint16_t x;
    uint16_t y;
    uint16_t pad;
};

struct exy_touch_state {
    uint32_t seq;
    uint32_t chip;       /* 0x114 = MMS114, 0x224 = mXT224, 0 = none */
    uint32_t buttons;    /* bit0=Home, bit1=Power, bit2=VolUp, bit3=VolDown */
    uint32_t count;      /* active touches */
    struct exy_finger f[TOUCH_MAX_FINGERS];
};

struct {
    uint32_t magic;      /* "tuch" = 0x74756368 */
    uint32_t pmic_id;    /* MAX77686 ID byte + status */
    uint32_t apll;       /* APLL_CON0 after boost */
    uint32_t chip;       /* detected touch IC */
    uint32_t rev;        /* TSP rev bytes */
    uint32_t irqs;       /* touch packets read */
    uint32_t pkt0;       /* first 4 bytes of last packet */
    uint32_t pkt1;       /* next 4 bytes of last packet */
    uint32_t polls;      /* exy_touch_poll calls */
    uint32_t last_ms;    /* MCT ms of last poll */
} exy_touch_log __attribute__((aligned(32)));

uintptr_t i2c7_va, i2c3_va, gpio_l_va, gpio_r_va, cmu_cpu_va;
static int tsp_probed;
static struct exy_touch_state cur_state;

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

uint32_t exy_mct_lo(void)
{
    return mct ? rd32(mct + 0x100u) : 0u;
}

void exy_udelay(uint32_t us)
{
    if (!mct) {
        for (volatile uint32_t i = 0; i < us * 100u; i++) ;
        return;
    }
    uint32_t t0 = exy_mct_lo(), dt = us * 24u;
    while (exy_mct_lo() - t0 < dt)
        ;
}

#define mct_lo exy_mct_lo
#define udelay exy_udelay

void exy_i2c_init_hw(uintptr_t base)
{
    /* PCLK ~100 MHz: prescaler /512 (bit6=1), div=1 -> ~97 kHz; ACKGEN + INTEN */
    wr32(base + IICCON, 0xe1u);
    wr32(base + IICSTAT, 0u);
    wr32(base + IICADD, 0x10u);
    wr32(base + IICLC, 0x05u); /* SDA filter + 5-clock delay */
    wr32(base + IICSTAT, I2C_MODE_MT | I2C_TXRX_ENA);
}

#define i2c_init_hw exy_i2c_init_hw

static int i2c_wait(uintptr_t base)
{
    uint32_t t0 = mct_lo();
    while (mct_lo() - t0 < 24u * 10000u) { /* 10 ms timeout */
        if (rd32(base + IICCON) & I2CCON_IRPND)
            return (rd32(base + IICSTAT) & I2CSTAT_NACK) ? -2 : 0;
    }
    return -1;
}

static void i2c_stop(uintptr_t base, uint32_t mode)
{
    wr32(base + IICSTAT, mode | I2C_TXRX_ENA);
    wr32(base + IICCON, rd32(base + IICCON) & ~I2CCON_IRPND);
    udelay(40);
}

static int i2c_wait_idle(uintptr_t base)
{
    uint32_t t0 = mct_lo();
    while (rd32(base + IICSTAT) & I2CSTAT_BSY) {
        if (mct_lo() - t0 >= 24u * 10000u) {
            i2c_init_hw(base);
            return -1;
        }
    }
    return 0;
}

int exy_i2c_write(uintptr_t base, uint8_t addr7, const uint8_t *buf, uint32_t len)
{
    if (i2c_wait_idle(base) < 0)
        return -1;
    wr32(base + IICCON, rd32(base + IICCON) | I2CCON_ACKGEN);
    wr32(base + IICDS, (uint32_t)addr7 << 1);
    wr32(base + IICSTAT, I2C_MODE_MT | I2C_TXRX_ENA | I2C_START_STOP);
    int r = i2c_wait(base);
    for (uint32_t i = 0; i < len && r == 0; i++) {
        wr32(base + IICDS, buf[i]);
        wr32(base + IICCON, rd32(base + IICCON) & ~I2CCON_IRPND);
        r = i2c_wait(base);
    }
    i2c_stop(base, I2C_MODE_MT);
    return r;
}

int exy_i2c_read_reg(uintptr_t base, uint8_t addr7, uint8_t reg, uint8_t *out, uint32_t len)
{
    if (!len || i2c_wait_idle(base) < 0)
        return -1;
    wr32(base + IICCON, rd32(base + IICCON) | I2CCON_ACKGEN);
    wr32(base + IICDS, (uint32_t)addr7 << 1);
    wr32(base + IICSTAT, I2C_MODE_MT | I2C_TXRX_ENA | I2C_START_STOP);
    int r = i2c_wait(base);
    if (r != 0) {
        i2c_stop(base, I2C_MODE_MT);
        return r;
    }
    wr32(base + IICDS, reg);
    wr32(base + IICCON, rd32(base + IICCON) & ~I2CCON_IRPND);
    r = i2c_wait(base);
    if (r != 0) {
        i2c_stop(base, I2C_MODE_MT);
        return r;
    }
    /* Repeated START for Master Receive */
    wr32(base + IICDS, ((uint32_t)addr7 << 1) | 1u);
    wr32(base + IICSTAT, I2C_MODE_MR | I2C_TXRX_ENA | I2C_START_STOP);
    wr32(base + IICCON, rd32(base + IICCON) & ~I2CCON_IRPND);
    r = i2c_wait(base);
    if (r != 0) {
        i2c_stop(base, I2C_MODE_MR);
        return r;
    }
    for (uint32_t i = 0; i < len; i++) {
        uint32_t con = rd32(base + IICCON) & ~I2CCON_IRPND;
        if (i + 1 == len)
            con &= ~I2CCON_ACKGEN; /* NACK last byte */
        wr32(base + IICCON, con);
        /* Note: last byte intentionally returns NACK status; wait for IRPND only */
        uint32_t t0 = mct_lo();
        while (!(rd32(base + IICCON) & I2CCON_IRPND)) {
            if (mct_lo() - t0 >= 24u * 10000u) {
                i2c_stop(base, I2C_MODE_MR);
                return -1;
            }
        }
        out[i] = (uint8_t)rd32(base + IICDS);
    }
    i2c_stop(base, I2C_MODE_MR);
    return 0;
}

int exy_i2c_read_reg16(uintptr_t base, uint8_t addr7, uint16_t reg16, uint8_t *out, uint32_t len)
{
    if (!len || i2c_wait_idle(base) < 0)
        return -1;
    wr32(base + IICCON, rd32(base + IICCON) | I2CCON_ACKGEN);
    wr32(base + IICDS, (uint32_t)addr7 << 1);
    wr32(base + IICSTAT, I2C_MODE_MT | I2C_TXRX_ENA | I2C_START_STOP);
    int r = i2c_wait(base);
    if (r != 0) { i2c_stop(base, I2C_MODE_MT); return r; }
    wr32(base + IICDS, (uint8_t)(reg16 >> 8));
    wr32(base + IICCON, rd32(base + IICCON) & ~I2CCON_IRPND);
    r = i2c_wait(base);
    if (r != 0) { i2c_stop(base, I2C_MODE_MT); return r; }
    wr32(base + IICDS, (uint8_t)(reg16 & 0xffu));
    wr32(base + IICCON, rd32(base + IICCON) & ~I2CCON_IRPND);
    r = i2c_wait(base);
    if (r != 0) { i2c_stop(base, I2C_MODE_MT); return r; }
    wr32(base + IICDS, ((uint32_t)addr7 << 1) | 1u);
    wr32(base + IICSTAT, I2C_MODE_MR | I2C_TXRX_ENA | I2C_START_STOP);
    wr32(base + IICCON, rd32(base + IICCON) & ~I2CCON_IRPND);
    r = i2c_wait(base);
    if (r != 0) { i2c_stop(base, I2C_MODE_MR); return r; }
    for (uint32_t i = 0; i < len; i++) {
        uint32_t con = rd32(base + IICCON) & ~I2CCON_IRPND;
        if (i + 1 == len) con &= ~I2CCON_ACKGEN;
        wr32(base + IICCON, con);
        uint32_t t0 = mct_lo();
        while (!(rd32(base + IICCON) & I2CCON_IRPND)) {
            if (mct_lo() - t0 >= 24u * 10000u) { i2c_stop(base, I2C_MODE_MR); return -1; }
        }
        out[i] = (uint8_t)rd32(base + IICDS);
    }
    i2c_stop(base, I2C_MODE_MR);
    return 0;
}

#define i2c_write exy_i2c_write
#define i2c_read_reg exy_i2c_read_reg

static volatile uint32_t pmic_i2c_lock;

static void pmic_lock(void)
{
    while (__sync_lock_test_and_set(&pmic_i2c_lock, 1u))
        __asm__ volatile("yield" ::: "memory");
}

static void pmic_unlock(void)
{
    __sync_lock_release(&pmic_i2c_lock);
}

int exy_pmic_write_reg(uint8_t reg, uint8_t val)
{
    if (!i2c7_va) return -1;
    uint8_t b[2] = { reg, val };
    int rc = -1;
    pmic_lock();
    for (int tries = 0; tries < 4; tries++) {
        if (i2c_write(i2c7_va, MAX77686_ADDR, b, 2) == 0) {
            rc = 0;
            break;
        }
        udelay(300);
        i2c_init_hw(i2c7_va);
    }
    pmic_unlock();
    return rc;
}

#define pmic_write_reg exy_pmic_write_reg

static int cur_cpu_opp = -1;
volatile uint32_t exy_last_touch_mct;
static uint8_t cur_buck2_code = 0x2au;
static uint8_t cur_buck4_code = 0x26u;

static int cpu_set_vdd(uint8_t code)
{
    if (code == cur_buck2_code)
        return 0;
    int ok = 1;
    for (uint8_t r = MAX77686_BUCK2DVS1; r < MAX77686_BUCK2DVS1 + 8u; r++)
        if (pmic_write_reg(r, code) != 0)
            ok = 0;
    if (ok)
        cur_buck2_code = code;
    return ok ? 0 : -1;
}

int exy_gpu_set_vdd(uint8_t code)
{
    if (code == cur_buck4_code)
        return 0;
    int ok = 1;
    for (uint8_t r = MAX77686_BUCK4DVS1; r < MAX77686_BUCK4DVS1 + 8u; r++)
        if (pmic_write_reg(r, code) != 0)
            ok = 0;
    if (ok)
        cur_buck4_code = code;
    return ok ? 0 : -1;
}

/* Restore full boot voltages before warm reset so U-Boot (800 MHz) never browns out. */
void exy_cpu_prep_reboot(void)
{
    (void)cpu_set_vdd(0x2au);
    (void)exy_gpu_set_vdd(0x26u);
}

/* Fixed 1000 MHz @ 1.125V set single-core before SMP bring-up (no runtime CLK_DIV_CPU0 SMP scaling);
 * thermal/idle power management scales Mali-400 GPU (147/220/440 MHz) + idle clock-gating. */
int exy_cpu_set_opp(int opp)
{
    (void)opp;
    return 0;
}

static void boost_apll_1000(void)
{
    if (!cmu_cpu_va)
        return;
    /* Switch CPU to SCLK_MPLL before changing dividers & APLL */
    wr32(cmu_cpu_va + 0x200u, 0x01110000u);
    while (rd32(cmu_cpu_va + 0x400u) & 0x04440004u)
        ;
    /* Safe Exynos 4412 dividers at 1000 MHz: CORE=/1 (1000), COREM0=/4 (250), COREM1=/8 (125), ATB=/6 (167), APLL=/3 */
    wr32(cmu_cpu_va + 0x500u, 0x02157730u);
    while (rd32(cmu_cpu_va + 0x600u) != 0u)
        ;
    wr32(cmu_cpu_va + 0x504u, 0x00000505u);
    while (rd32(cmu_cpu_va + 0x604u) != 0u)
        ;
    /* APLL = 24 MHz * 125 / 3 / 1 = 1000 MHz (M=0x7d, P=3, S=0): cool & rock-solid across 4 SMP cores */
    wr32(cmu_cpu_va + 0x100u, 0x807d0300u);
    while (!(rd32(cmu_cpu_va + 0x100u) & (1u << 29)))
        ;
    /* Switch CPU back to APLL */
    wr32(cmu_cpu_va + 0x200u, 0x01000001u);
    while (rd32(cmu_cpu_va + 0x400u) & 0x04440004u)
        ;
    cur_cpu_opp = 2;
    cur_buck2_code = 0x2au;
    exy_touch_log.apll = rd32(cmu_cpu_va + 0x100u);
}

static void boost_buses_400(void)
{
    /* Unthrottle SBOOT DRAM (100 -> 400 MHz) + AXI data buses (ACP/GDL/GDR: 100 -> 200 MHz),
     * while keeping APB peripheral clocks (ACP_PCLK/GPL/GPR) at SBOOT's exact 50 MHz
     * (GPL/GPR=100 MHz via 0x13 broke PERIR/MCT FIQ delivery at disp=0x55). */
    uintptr_t cmu_dmc = ml_io_map(CMU_DMC_PA, 0x1000);
    uintptr_t cmu_sys = ml_io_map(0x10030000u, 0x10000);
    if (!cmu_dmc)
        return;
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr\ncpsid if" : "=r"(cpsr) :: "memory");
    exy_touch_log.pkt0 = 0xb301u;
    exy_clean_line(&exy_touch_log);
    wr32(cmu_dmc + 0x91cu, 1u);          /* ENABLE_DMC_PAUSE */
    wr32(cmu_dmc + 0x500u, 0x00111133u); /* DMC=/2 (400), DMCD=/2 (200), DMCP=/2 (100), DPHY=/2 (400), ACP=/4 (200), ACP_PCLK=/4 (50) */
    while (rd32(cmu_dmc + 0x600u) != 0u)
        ;
    if (cmu_sys) {
        wr32(cmu_sys + 0x4500u, 0x00000033u); /* LEFTBUS: GDL=/4 (200 MHz), GPL=/4 (50 MHz) */
        while (rd32(cmu_sys + 0x4600u) != 0u)
            ;
        wr32(cmu_sys + 0x8500u, 0x00000033u); /* RIGHTBUS: GDR=/4 (200 MHz), GPR=/4 (50 MHz) */
        while (rd32(cmu_sys + 0x8600u) != 0u)
            ;
        wr32(cmu_sys + 0xc510u, 0x012154f3u); /* TOP: ACLK_200=/4 (200), ACLK_133=/6 (133), ACLK_160=/5 (160), ACLK_100=/16 (50) */
        while (rd32(cmu_sys + 0xc610u) != 0u)
            ;
    }
    __asm__ volatile("msr cpsr_c, %0" :: "r"(cpsr) : "memory");
    exy_touch_log.pkt0 = 0xb304u;
    exy_clean_line(&exy_touch_log);
}

void exy_pmic_and_touch_init(void)
{
    exy_pl310 = ml_io_map(PL310_PA, 0x1000);
    exy_touch_log.magic = 0x74756368u; /* "tuch" */
    exy_touch_log.pmic_id = 0x0001u;
    exy_clean_line(&exy_touch_log);
    i2c7_va    = ml_io_map(I2C7_PA, 0x1000);
    i2c3_va    = ml_io_map(I2C3_PA, 0x1000);
    gpio_l_va  = ml_io_map(GPIO_LEFT_PA, 0x1000);
    gpio_r_va  = ml_io_map(GPIO_RIGHT_PA, 0x1000);
    cmu_cpu_va = ml_io_map(CMU_CPU_PA, 0x1000);

    if (i2c7_va) {
        i2c_init_hw(i2c7_va);
        uint8_t id = 0;
        for (int tries = 0; tries < 4; tries++) {
            if (i2c_read_reg(i2c7_va, MAX77686_ADDR, MAX77686_REG_ID, &id, 1) == 0 && id != 0)
                break;
            udelay(300);
            i2c_init_hw(i2c7_va);
        }
        if (id != 0) {
            exy_touch_log.pmic_id = 0x100u | id;
            exy_clean_line(&exy_touch_log);
            /* Power on touchscreen rails: LDO23 = 3.3V (AVDD), LDO24 = 1.8V (VDD) */
            pmic_write_reg(MAX77686_LDO23CTRL1, 0xf2u);
            pmic_write_reg(MAX77686_LDO24CTRL1, 0xd4u);
            /* Raise VDD_MIF (BUCK1) = 1.10V (0x07) and VDD_INT (BUCK3) = 1.10V (0x28) for 400 MHz DMC / 200 MHz buses */
            int bus_ok = (pmic_write_reg(MAX77686_BUCK1OUT, 0x07u) == 0);
            for (uint8_t r = MAX77686_BUCK3DVS1; r < MAX77686_BUCK3DVS1 + 8u; r++)
                if (pmic_write_reg(r, 0x28u) != 0)
                    bus_ok = 0;
            /* Set VDD_G3D (BUCK4) = 1.075V (0x26) for stock 440 MHz Mali-400 MP4 (DVFS lowers to 0.90V idle) */
            for (uint8_t r = MAX77686_BUCK4DVS1; r < MAX77686_BUCK4DVS1 + 8u; r++)
                pmic_write_reg(r, 0x26u);
            cur_buck4_code = 0x26u;
            /* Set VDD_ARM (BUCK2) = 1.125V (0x2a) for rock-solid 1.0 GHz Exynos 4412 */
            int ok = 1;
            for (uint8_t r = MAX77686_BUCK2DVS1; r < MAX77686_BUCK2DVS1 + 8u; r++)
                if (pmic_write_reg(r, 0x2au) != 0)
                    ok = 0;
            uint8_t rb = 0;
            int rb_ok = 0;
            if (ok) {
                for (int tries = 0; tries < 4; tries++) {
                    if (i2c_read_reg(i2c7_va, MAX77686_ADDR, MAX77686_BUCK2DVS1, &rb, 1) == 0) {
                        rb_ok = 1;
                        break;
                    }
                    udelay(300);
                    i2c_init_hw(i2c7_va);
                }
            }
            if (rb_ok) {
                exy_touch_log.pkt0 = 0xb200u | rb;
                exy_clean_line(&exy_touch_log);
                if (rb == 0x2au) {
                    udelay(5000);
                    boost_apll_1000();
                    if (bus_ok)
                        boost_buses_400();
                }
            }
        } else {
            exy_touch_log.pmic_id = 0xee00u | id;
        }
    }

    /* Pinmux GPA1[2]=I2C3_SDA, GPA1[3]=I2C3_SCL (func 3), pull disabled (external pull-ups) */
    if (gpio_l_va) {
        uint32_t con = rd32(gpio_l_va + 0x20u);
        con = (con & ~0x0000ff00u) | 0x00003300u;
        wr32(gpio_l_va + 0x20u, con);
        uint32_t pud = rd32(gpio_l_va + 0x28u);
        pud &= ~0x000000f0u;
        wr32(gpio_l_va + 0x28u, pud);
    }
    /* GPM2[3] = TSP_INT (input, pull-up) */
    if (gpio_r_va) {
        uint32_t con = rd32(gpio_r_va + 0x2a0u);
        con &= ~0x0000f000u; /* input */
        wr32(gpio_r_va + 0x2a0u, con);
        uint32_t pud = rd32(gpio_r_va + 0x2a8u);
        pud = (pud & ~0x000000c0u) | 0x000000c0u; /* pull-up */
        wr32(gpio_r_va + 0x2a8u, pud);
    }
    if (i2c3_va)
        i2c_init_hw(i2c3_va);
    exy_clean_line(&exy_touch_log);
}

static uint32_t tsp_max_x = 720u, tsp_max_y = 1280u;
extern void exy_putc(char c);

static void tsp_putx(uint32_t v)
{
    static const char h[] = "0123456789abcdef";
    for (int s = 28; s >= 0; s -= 4)
        exy_putc(h[(v >> s) & 0xfu]);
}

static void probe_tsp_once(void)
{
    if (tsp_probed >= 5 || cur_state.chip || !i2c3_va)
        return;
    tsp_probed++;
    uint8_t rev[3] = { 0, 0, 0 };
    if (i2c_read_reg(i2c3_va, MMS114_ADDR, MMS114_TSP_REV, rev, 3) == 0) {
        cur_state.chip = 0x114u;
        exy_touch_log.chip = 0x114u;
        exy_touch_log.rev = ((uint32_t)rev[0] << 16) | ((uint32_t)rev[1] << 8) | rev[2];
        /* Read panel's native XY resolution (0x02..0x04) and log it for exact calibration. */
        uint8_t res[3] = { 0, 0, 0 };
        if (i2c_read_reg(i2c3_va, MMS114_ADDR, MMS114_XY_RES_H, res, 3) == 0) {
            uint32_t r0 = ((uint32_t)(res[0] & 0x0fu) << 8) | res[1];
            uint32_t r1 = ((uint32_t)(res[0] >> 4) << 8) | res[2];
            uint32_t lo = (r0 < r1) ? r0 : r1, hi = (r0 < r1) ? r1 : r0;
            if (lo >= 320u && hi <= 4096u) {
                tsp_max_x = lo;
                tsp_max_y = hi;
            }
            const char *p = "[TSP] res=";
            while (*p) exy_putc(*p++);
            tsp_putx(((uint32_t)res[0] << 16) | ((uint32_t)res[1] << 8) | res[2]);
            exy_putc(' ');
            tsp_putx((tsp_max_x << 16) | tsp_max_y);
            exy_putc('\n');
        }
        /* Put MMS114 into Active mode (0x02) */
        uint8_t cmd[2] = { MMS114_MODE_CTRL, 0x02u };
        i2c_write(i2c3_va, MMS114_ADDR, cmd, 2);
    } else {
        /* Fallback probe: Atmel maXTouch @ 0x4a */
        uint8_t info[7] = { 0 };
        if (i2c_read_reg(i2c3_va, 0x4au, 0x00u, info, 7) == 0 && info[0] != 0) {
            cur_state.chip = 0x224u;
            exy_touch_log.chip = 0x224u;
            exy_touch_log.rev = ((uint32_t)info[0] << 16) | ((uint32_t)info[1] << 8) | info[2];
        } else {
            exy_touch_log.chip = 0xdead0000u | (uint32_t)tsp_probed;
        }
    }
    exy_clean_line(&exy_touch_log);
}

static uint32_t read_buttons(void)
{
    if (!gpio_r_va)
        return 0;
    uint32_t b = 0;
    if (!(rd32(gpio_r_va + 0xc04u) & (1u << 1))) b |= 1u << 0; /* GPX0[1]: HOME */
    if (!(rd32(gpio_r_va + 0xc44u) & (1u << 7))) b |= 1u << 1; /* GPX2[7]: POWER */
    if (!(rd32(gpio_r_va + 0xc44u) & (1u << 2))) b |= 1u << 2; /* GPX2[2]: VOL_UP */
    if (!(rd32(gpio_r_va + 0xc64u) & (1u << 3))) b |= 1u << 3; /* GPX3[3]: VOL_DOWN */
    return b;
}

/* Host USB synthetic button/touch injection for autonomous verification (drivers.c 0xfc/0xfd) */
static volatile uint32_t synth_btn_mask;
static volatile uint32_t synth_btn_until_mct;
static volatile uint32_t synth_touch_pending;
static volatile uint32_t synth_touch_down;
static volatile uint16_t synth_touch_x, synth_touch_y;

void exy_synth_btn_inject(uint32_t mask)
{
    synth_btn_mask = mask & 0x0fu;
    synth_btn_until_mct = mct_lo() + 24000u * 150u; /* 150 ms press */
}

void exy_synth_touch_inject(uint16_t x, uint16_t y, uint32_t down)
{
    synth_touch_x = (x > 719u) ? 719u : x;
    synth_touch_y = (y > 1279u) ? 1279u : y;
    synth_touch_down = down ? 1u : 0u;
    synth_touch_pending = 1u;
}

static void poll_mms114(void)
{
    /* GPM2[3] is active-low TSP_INT; read 1 packet per poll so tap-down + lift-off never collapse. */
    if (!gpio_r_va || (rd32(gpio_r_va + 0x2a4u) & (1u << 3)))
        return;

    uint8_t pkt_sz = 0;
    if (i2c_read_reg(i2c3_va, MMS114_ADDR, MMS114_PACKET_SIZE, &pkt_sz, 1) != 0)
        return;
    if (pkt_sz == 0 || pkt_sz > 80u)
        return;

    uint8_t buf[80];
    if (i2c_read_reg(i2c3_va, MMS114_ADDR, MMS114_INFORMATION, buf, pkt_sz) != 0)
        return;

    exy_touch_log.irqs++;
    exy_touch_log.pkt0 = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                         ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);

    /* MMS114/136 on i9300 always emits 6-byte finger records (including 4-finger 24-byte packets). */
    uint32_t esz = (pkt_sz % 6u == 0u) ? 6u : 8u;
    uint32_t n = pkt_sz / esz;

    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = buf + i * esz;
        uint8_t id = e[0] & 0x0fu;
        uint8_t type = (e[0] >> 5) & 0x03u;
        uint8_t down = (e[0] >> 7) & 0x01u;
        if (type != 1u || id == 0u || id > TOUCH_MAX_FINGERS)
            continue;
        /* Verified from live i9300 MMS114 (rev 0x420144) wraparound trace (x=248->269, 507->516):
         * e[1] low nibble pairs with e[2] (screen X 0..719); e[1] high nibble pairs with e[3] (screen Y 0..1279). */
        uint32_t x = (uint32_t)e[2] | ((uint32_t)(e[1] & 0x0fu) << 8);
        uint32_t y = (uint32_t)e[3] | ((uint32_t)((e[1] >> 4) & 0x0fu) << 8);
        /* Swipes from the bezel first report y=1245..1260, never the last ~13 pt iOS needs for
         * the Control Center edge: stretch the bottom 80 px so 1250+ lands on the edge. */
        if (y > 1200u)
            y = 1200u + (y - 1200u) * 79u / 50u;
        if (x > 719u) x = 719u;
        if (y > 1279u) y = 1279u;
        struct exy_finger *f = &cur_state.f[id - 1u];
        f->id = id;
        f->down = down;
        f->x = (uint16_t)x;
        f->y = (uint16_t)y;
    }
    uint32_t cnt = 0;
    for (uint32_t i = 0; i < TOUCH_MAX_FINGERS; i++)
        if (cur_state.f[i].down)
            cnt++;
    cur_state.count = cnt;
    cur_state.seq++;
}

void exy_touch_set_dev(uint32_t dev_ptr)
{
    if (exy_touch_log.pkt1 != dev_ptr) {
        exy_touch_log.pkt1 = dev_ptr;
        exy_clean_line(&exy_touch_log);
    }
}

extern struct {
    uint32_t magic, started, self, swaps, last_pa, last_len, copied;
} exy_disp_log;

extern void exy_drv_otg_check(void);

void exy_touch_poll(struct exy_touch_state *out)
{
    exy_touch_log.polls++;
    exy_touch_log.last_ms = mct_lo() / 24000u;
    exy_drv_otg_check();
    probe_tsp_once();
    uint32_t prev_seq = cur_state.seq;
    uint32_t btn = read_buttons();
    if (synth_btn_mask) {
        if ((int32_t)(mct_lo() - synth_btn_until_mct) < 0)
            btn |= synth_btn_mask;
        else
            synth_btn_mask = 0u;
    }
    if (btn != cur_state.buttons) {
        cur_state.buttons = btn;
        cur_state.seq++;
        exy_putc('['); exy_putc('B'); exy_putc(']'); tsp_putx(btn); exy_putc('\n');
    }
    uint32_t was_down = cur_state.count;
    if (synth_touch_pending) {
        synth_touch_pending = 0u;
        cur_state.f[0].id = 1u;
        cur_state.f[0].down = (uint8_t)synth_touch_down;
        cur_state.f[0].x = synth_touch_x;
        cur_state.f[0].y = synth_touch_y;
        cur_state.count = synth_touch_down ? 1u : 0u;
        cur_state.seq++;
    } else if (cur_state.chip == 0x114u)
        poll_mms114();
    if (!was_down && cur_state.count) {  /* touch-down position: is a bottom-edge swipe near y=1279? */
        exy_putc('['); exy_putc('T'); exy_putc(']');
        tsp_putx((cur_state.f[0].x << 16) | cur_state.f[0].y); exy_putc('\n');
    }
    if (cur_state.seq != prev_seq || cur_state.count > 0u)
        exy_last_touch_mct = mct_lo();
    if (out) {
        *out = cur_state;
        out->chip = (cur_state.chip & 0xffffu) | ((exy_disp_log.copied & 0xffffu) << 16);
    }
}

/* --- Exynos 4412 dw_mmc (MSHC_0) Internal DMA (IDMAC) ---------------------- */
struct dwmci_idmac {
    uint32_t flags;
    uint32_t cnt;
    uint32_t addr;
    uint32_t next_addr;
};

#define EMMC_DMA_MAX_SECTORS 128u /* 64 KiB bounce buffer */
static uint8_t emmc_dma_buf[EMMC_DMA_MAX_SECTORS * 512u] __attribute__((aligned(4096)));
static struct dwmci_idmac emmc_idmac[16] __attribute__((aligned(64)));
static int emmc_idmac_inited;

static void fast_memcpy32(void *dst, const void *src, uint32_t bytes)
{
    uint32_t *d = (uint32_t *)dst;
    const uint32_t *s = (const uint32_t *)src;
    uint32_t w = bytes >> 2;
    while (w >= 8u) {
        __asm__ volatile(
            "ldmia %1!, {r2, r3, r4, r5, r6, r8, r9, r10}\n"
            "stmia %0!, {r2, r3, r4, r5, r6, r8, r9, r10}\n"
            : "+r"(d), "+r"(s)
            :: "r2", "r3", "r4", "r5", "r6", "r8", "r9", "r10", "memory");
        w -= 8u;
    }
    while (w--)
        *d++ = *s++;
}

static void emmc_set_clkdiv(uint32_t div)
{
    const uint32_t UPD = (1u << 31) | (1u << 21) | (1u << 13); /* START | UPD_CLK | PRV_DAT_WAIT */
    wr32(mshc + 0x10u, 0u);          /* CLKENA off */
    wr32(mshc + 0x0cu, 0u);          /* CLKSRC = 0 */
    wr32(mshc + 0x08u, div);         /* CLKDIV: 100 MHz CIU / (2*1) = 50 MHz High Speed */
    wr32(mshc + 0x2cu, UPD);
    for (uint32_t i = 0; i < 100000u && (rd32(mshc + 0x2cu) & (1u << 31)); i++)
        ;
    wr32(mshc + 0x10u, 0x00010001u); /* CLKENA on + low-power */
    wr32(mshc + 0x2cu, UPD);
    for (uint32_t i = 0; i < 100000u && (rd32(mshc + 0x2cu) & (1u << 31)); i++)
        ;
}

#define touch_iosleep ((void (*)(uint32_t))0x80328fd1)

static uint32_t emmc_idma_chunk(uint32_t lba, uint32_t n, uint8_t *buf, int write)
{
    const uint32_t MSHC_SPIN = 10000000u;
    const uint32_t MSHC_ERRS = 0xbfc2u;
    uint32_t bytes = n * 512u, i, st = 0;
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    int can_sleep = !(cpsr & 0x80u) && mct != 0u;

    if (!exy_pl310)
        exy_pl310 = ml_io_map(PL310_PA, 0x1000);
    if (!emmc_idmac_inited) {
        wr32(mshc + 0x80u, 1u); /* BMOD SW reset */
        for (i = 0; i < 100000u && (rd32(mshc + 0x80u) & 1u); i++)
            ;
        /* U-Boot set CLKDIV=4 assuming 400 MHz CIU, ignoring Exynos4412's /4 CIU -> 12.5 MHz.
         * CLKDIV=1 runs at full 50 MHz 8-bit High Speed (4x clock). */
        emmc_set_clkdiv(1u);
        dma_clean_inv_range(emmc_dma_buf, sizeof(emmc_dma_buf));
        emmc_idmac_inited = 1;
    }

    for (i = 0; i < MSHC_SPIN && (rd32(mshc + 0x48u) & (1u << 9)); i++) /* data busy */
        ;

    /* Reset FIFO and DMA */
    wr32(mshc + 0x00u, (rd32(mshc + 0x00u) | (1u << 1) | (1u << 2)));
    for (i = 0; i < MSHC_SPIN && (rd32(mshc + 0x00u) & ((1u << 1) | (1u << 2))); i++)
        ;

    uint32_t buf_pa = (uint32_t)((uintptr_t)emmc_dma_buf - 0x40000000u);
    uint32_t desc_pa = (uint32_t)((uintptr_t)emmc_idmac - 0x40000000u);
    uint32_t rem = n, d = 0;
    while (rem) {
        uint32_t chunk = rem > 8u ? 8u : rem;
        uint32_t flags = (1u << 31) | (1u << 4); /* OWN | CH */
        if (d == 0u) flags |= (1u << 3);         /* FS */
        if (rem <= 8u) flags |= (1u << 2);       /* LD */
        emmc_idmac[d].flags = flags;
        emmc_idmac[d].cnt = chunk * 512u;
        emmc_idmac[d].addr = buf_pa + d * 4096u;
        emmc_idmac[d].next_addr = desc_pa + (d + 1u) * (uint32_t)sizeof(struct dwmci_idmac);
        rem -= chunk;
        d++;
    }
    dma_clean_range(emmc_idmac, d * (uint32_t)sizeof(struct dwmci_idmac));

    if (write) {
        fast_memcpy32(emmc_dma_buf, buf, bytes);
        dma_clean_inv_range(emmc_dma_buf, bytes);
    }

    wr32(mshc + 0x8cu, 0xffffffffu); /* IDSTS clear */
    wr32(mshc + 0x88u, desc_pa);     /* DBADDR */
    wr32(mshc + 0x00u, rd32(mshc + 0x00u) | (1u << 25) | (1u << 5)); /* IDMAC_EN | DMA_EN */
    wr32(mshc + 0x80u, (1u << 7) | (1u << 1)); /* BMOD: IDMAC_EN | FB */
    wr32(mshc + 0x44u, ~0u);         /* RINTSTS clear */
    wr32(mshc + 0x1cu, 512u);        /* BLKSIZ */
    wr32(mshc + 0x20u, bytes);       /* BYTCNT */
    wr32(mshc + 0x28u, lba);         /* CMDARG */
    wr32(mshc + 0x2cu, (1u << 31) | (1u << 29) | (1u << 13) | (write ? (1u << 10) : 0u) |
                       (1u << 9) | (1u << 8) | (1u << 6) |
                       (n > 1u ? ((1u << 12) | (write ? 25u : 18u)) : (write ? 24u : 17u)));
    wr32(mshc + 0x84u, 1u);          /* PLDMND */

    uint32_t t0 = mct ? mct_lo() : 0u;
    int dto_ok = 0;
    for (i = 0; i < MSHC_SPIN; i++) {
        st = rd32(mshc + 0x44u);
        if (st & MSHC_ERRS) {
            wr32(mshc + 0x00u, rd32(mshc + 0x00u) & ~(1u << 5));
            return st;
        }
        if (st & (1u << 3)) { /* DTO */
            dto_ok = 1;
            break;
        }
        if (can_sleep) {
            uint32_t el = mct_lo() - t0;
            if (el >= 24000u * 2000u)
                break;
            /* Fast-poll up to 4 ms (covers full 64 KiB HS reads at ~15+ MB/s without IOSleep stalls). */
            if (el >= 24u * 4000u)
                touch_iosleep(1u);
            else
                __asm__ volatile("yield" ::: "memory");
        }
    }
    if (!dto_ok) {
        wr32(mshc + 0x00u, rd32(mshc + 0x00u) & ~(1u << 5));
        return st | (1u << 31);
    }

    uint32_t idmask = write ? (1u << 0) : (1u << 1); /* TI : RI */
    for (i = 0; i < MSHC_SPIN && !(rd32(mshc + 0x8cu) & idmask); i++)
        ;
    for (; n > 1u && i < MSHC_SPIN && !(rd32(mshc + 0x44u) & (1u << 14)); i++) /* ACD */
        ;
    if (write && i < MSHC_SPIN) {
        uint32_t tw = mct ? mct_lo() : 0u;
        for (; i < MSHC_SPIN && (rd32(mshc + 0x48u) & (1u << 9)); i++) { /* card prog */
            if (can_sleep) {
                uint32_t el = mct_lo() - tw;
                if (el >= 24000u * 2000u) { i = MSHC_SPIN; break; }
                if (el >= 24u * 1500u)
                    touch_iosleep(1u);
                else
                    __asm__ volatile("yield" ::: "memory");
            }
        }
    }

    wr32(mshc + 0x8cu, 0xffffffffu);
    wr32(mshc + 0x00u, rd32(mshc + 0x00u) & ~(1u << 5));
    wr32(mshc + 0x44u, ~0u);
    if (i >= MSHC_SPIN)
        return st | (1u << 31);

    if (!write) {
        dma_inv_range(emmc_dma_buf, bytes);
        fast_memcpy32(buf, emmc_dma_buf, bytes);
    }
    return 0u;
}

uint32_t exy_emmc_idma_xfer(uint32_t lba, uint32_t n, uint32_t *p, int write)
{
    static uint32_t cur_div = 1u;
    uint8_t *b = (uint8_t *)p;
    while (n) {
        uint32_t chunk = n > EMMC_DMA_MAX_SECTORS ? EMMC_DMA_MAX_SECTORS : n;
        uint32_t st = emmc_idma_chunk(lba, chunk, b, write);
        if (st && cur_div < 4u) {
            cur_div <<= 1;
            emmc_set_clkdiv(cur_div);
            exy_touch_log.pkt0 = (exy_touch_log.pkt0 & 0xffffu) | (cur_div << 16);
            exy_clean_line(&exy_touch_log);
            st = emmc_idma_chunk(lba, chunk, b, write);
        }
        if (st)
            return st;
        lba += chunk;
        b += chunk * 512u;
        n -= chunk;
    }
    return 0u;
}

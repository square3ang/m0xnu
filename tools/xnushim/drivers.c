/*
 * Galaxy S3 (GT-I9300 / Exynos 4412 midas) Peripheral Hardware Drivers for XNU.
 * Built into DRV_AREA alongside gpu.c, disp.c, and touch.c.
 *
 * Drivers implemented:
 *   1. Open-drain bit-bang i2c-gpio engine (shared across 6 midas I2C buses)
 *   2. AN30259A RGB Notification LED (GPF2[7:6] @ 0x30)
 *   3. MAX17047 Fuel Gauge (GPF1[5:4] @ 0x36) + MAX77693 Charger/MUIC (GPM2[0:1] @ 0x66/0x25)
 *      wired into native Apple IOPMPowerSource so iOS shows real battery % & charging status
 *   4. Exynos 4412 Internal RTC (0x10070000) + MAX77686 Battery-Backed RTC (I2C7 @ 0x06)
 *   5. s6e8aa0 AMOLED Brightness & Sleep/Wake via MIPI DSIM0 (0x11C80000)
 *   6. MAX77693 Haptic Motor (GPM2[0:1] @ 0x48) + Exynos PWM0 (0x139D0000, GPD0[0])
 *   7. LSM330DLC 6-Axis Accel + Gyro (I2C1 @ 0x13870000, 0x19/0x6B)
 *   8. AK8975C 3-Axis Magnetometer (GPY2[4:5] @ 0x0C)
 *   9. CM36651 Proximity + Ambient Light Sensor (GPF0[0:1] @ 0x18, GPJ0[5] LED_A)
 *  10. Cypress Capacitive Touchkeys (GPL0[2:1] @ 0x20, GPJ0[3] INT)
 *  11. Wolfson WM1811 Audio Codec (I2C4 @ 0x138A0000 @ 0x1A, GPJ0[4] LDOENA) + I2S0 (0x03830000)
 *  12. Hardware probes for microSD (SDHCI2 0x12530000), Wi-Fi (SDHCI3 0x12540000), USB OTG (DWC2 0x12480000)
 */
#include <stdint.h>

typedef uintptr_t (*ml_io_map_t)(uintptr_t pa, uint32_t size);
#define ml_io_map ((ml_io_map_t)0x800c0bf5) /* Thumb */

/* Kernel IOKit / IOPMPowerSource / thread_call symbols (n94ap iOS 9.3.5, Thumb | 1) */
#define pm_ps_create          ((void *(*)(void))0x8034aa8d)
#define pm_ps_update_status   ((void  (*)(void *))0x8034ad6d)
#define pm_ps_set_ext_conn    ((void  (*)(void *, int))0x8034ae61)
#define pm_ps_set_ext_cap     ((void  (*)(void *, int))0x8034aebd)
#define pm_ps_set_batt_inst   ((void  (*)(void *, int))0x8034af19)
#define pm_ps_set_charging    ((void  (*)(void *, int))0x8034af75)
#define pm_ps_set_warn        ((void  (*)(void *, int))0x8034afd1)
#define pm_ps_set_crit        ((void  (*)(void *, int))0x8034b02d)
#define pm_ps_set_cur_cap     ((void  (*)(void *, uint32_t))0x8034b089)
#define pm_ps_set_max_cap     ((void  (*)(void *, uint32_t))0x8034b0dd)
#define pm_ps_set_time_rem    ((void  (*)(void *, int))0x8034b131)
#define pm_ps_set_amperage    ((void  (*)(void *, int))0x8034b189)
#define pm_ps_set_voltage     ((void  (*)(void *, uint32_t))0x8034b1e1)
#define pm_ps_set_adapter     ((void  (*)(void *, int))0x8034b291)
#define pm_ps_set_prop        ((void  (*)(void *, const void *, void *))0x8034ae1d)
#define ioservice_attach      ((int   (*)(void *, void *))0x803326dd)
#define ioservice_register    ((void  (*)(void *, uint32_t))0x80333001)
#define ossym_with_cstr       ((void *(*)(const char *))0x8031be39)
#define tc_alloc              ((void *(*)(void (*)(void *, void *), void *))0x800456e9)
#define tc_enter_delayed_raw  ((int   (*)(void *, uint32_t, uint32_t))0x80045cc5)
#define tc_enter_delayed(tc, dl) tc_enter_delayed_raw((tc), (uint32_t)(dl), (uint32_t)((dl) >> 32))
#define clk_int_deadline      ((void  (*)(uint32_t, uint32_t, uint64_t *))0x8001edbd)
#define K_OS_BOOL_TRUE        (*(void *const *)0x80458660)
#define K_OS_BOOL_FALSE       (*(void *const *)0x80458664)

/* Shared symbols from touch.c & shim.c & mali400.c */
extern uintptr_t mct, i2c7_va, gpio_l_va, gpio_r_va;
extern uint32_t mali_vpll_con0;
extern void exy_clean_line(const void *p);
extern uint32_t exy_mct_lo(void);
extern void exy_udelay(uint32_t us);
extern void exy_i2c_init_hw(uintptr_t base);
extern int exy_i2c_write(uintptr_t base, uint8_t addr7, const uint8_t *buf, uint32_t len);
extern int exy_i2c_read_reg(uintptr_t base, uint8_t addr7, uint8_t reg, uint8_t *out, uint32_t len);
extern int exy_i2c_read_reg16(uintptr_t base, uint8_t addr7, uint16_t reg16, uint8_t *out, uint32_t len);
extern int exy_pmic_write_reg(uint8_t reg, uint8_t val);
extern int exy_halt_restart(unsigned type);

/* Exynos 4412 Peripheral Physical Addresses */
#define RTC_PA          0x10070000u
#define PMU_PA          0x10020000u
#define TMU_PA          0x100c0000u
#define DSIM0_PA        0x11c80000u
#define OTG_PA          0x12480000u
#define SDHCI2_PA       0x12530000u
#define SDHCI3_PA       0x12540000u
#define ADC_PA          0x126c0000u
#define UART0_PA        0x13800000u
#define UART2_PA        0x13820000u
#define I2C1_PA         0x13870000u
#define I2C4_PA         0x138a0000u
#define PWM_PA          0x139d0000u
#define AUDSS_PA        0x03810000u
#define I2S0_PA         0x03830000u
#define GPZ_PA          0x03860000u

/* MAX77686 PMIC extra LDO regs (I2C7 @ 0x09) & RTC (@ 0x06) */
#define MAX77686_LDO21CTRL1 0x54u /* VTF_2.8V microSD */
#define MAX77686_LDO26CTRL1 0x59u /* MOTOR_VCC_3.0V haptic */
#define MAX77686_RTC_ADDR   0x06u

/* Probe mask bits */
#define DRV_BIT_LED     (1u << 0)
#define DRV_BIT_FG      (1u << 1)
#define DRV_BIT_CHG     (1u << 2)
#define DRV_BIT_MUIC    (1u << 3)
#define DRV_BIT_HAPTIC  (1u << 4)
#define DRV_BIT_ACCEL   (1u << 5)
#define DRV_BIT_GYRO    (1u << 6)
#define DRV_BIT_MAG     (1u << 7)
#define DRV_BIT_ALS     (1u << 8)
#define DRV_BIT_TKEY    (1u << 9)
#define DRV_BIT_WM1811  (1u << 10)
#define DRV_BIT_RTC     (1u << 11)
#define DRV_BIT_SD2     (1u << 12)
#define DRV_BIT_WIFI    (1u << 13)
#define DRV_BIT_OTG     (1u << 14)
#define DRV_BIT_DSIM    (1u << 15)
#define DRV_BIT_PMPS    (1u << 16)
#define DRV_BIT_FLASH   (1u << 17)
#define DRV_BIT_BT      (1u << 18)
#define DRV_BIT_TMU     (1u << 19)
#define DRV_BIT_ADC     (1u << 20)
#define DRV_BIT_I2S0    (1u << 21)

struct exy_drv_status {
    uint32_t probe_mask;
    uint16_t batt_soc;
    uint16_t batt_mv;
    int16_t  batt_ma;
    int16_t  batt_temp;
    uint8_t  chg_state;    /* 0=discharging, 1=charging, 2=full */
    uint8_t  cable_type;   /* 0=none, 1=usb/ac, 2=jig */
    uint8_t  brightness;   /* 0..255 */
    uint8_t  touchkeys;    /* bit0=Menu, bit1=Back */
    int16_t  accel[3];
    int16_t  gyro[3];
    int16_t  mag[3];
    uint16_t als_lux;
    uint16_t prox_raw;
    uint32_t rtc_epoch;
    uint16_t wm1811_id;
    uint8_t  sd_present;
    uint8_t  pad0;
    uint32_t vpll_con0;
    uint32_t otg_id;
    uint32_t sd3_present;
    uint32_t led_rgb;
    /* Extended v2 telemetry (bytes 64..79) */
    int16_t  cpu_temp_c;
    uint16_t adc_ap;
    uint8_t  hp_jack;      /* 1=3.5mm plugged */
    uint8_t  volume;       /* 0..100 */
    uint8_t  torch_lvl;    /* 0..16 */
    uint8_t  bt_hci_ok;    /* 1=HCI Reset OK */
    uint32_t sd3_ocr;      /* BCM4334 SDIO CMD5 OCR */
    uint32_t otg_resets;   /* DWC2 USB host reset count */
};

/* Survives WDT warm reset; address printed by mkxnu.py as drv= */
struct {
    uint32_t magic; /* "drvs" = 0x64727673 */
    uint32_t init_done;
    struct exy_drv_status st;
} exy_drv_log __attribute__((aligned(32)));

static uintptr_t rtc_va, dsim_va, i2c1_va, i2c4_va, pwm_va, i2s0_va, pmu_drv_va;
static uintptr_t sd2_va, sd3_va, otg_va, tmu_va, adc_va, uart0_va, uart2_va, gpz_va, audss_va;
static uint8_t tmu_trim25;
static int otg_gadget_started;
static uint8_t accel_addr, gyro_addr;
static void *pm_power_src;
static void *pm_poll_tc;
static void *vib_stop_tc;
static int drv_inited;

static inline uint32_t drd(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void dwr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

/* --- 1. GPIO & Open-Drain Bit-Bang I2C Engine ------------------------------ */

struct bb_i2c {
    uintptr_t bank;
    uint8_t sda;
    uint8_t scl;
};

static void gpio_dir_out(uintptr_t bank, uint8_t pin, int val)
{
    uint32_t dat = drd(bank + 4u);
    if (val) dat |= (1u << pin);
    else     dat &= ~(1u << pin);
    dwr(bank + 4u, dat);
    uint32_t con = drd(bank);
    con = (con & ~(0xfu << (pin * 4u))) | (1u << (pin * 4u));
    dwr(bank, con);
}

static void gpio_dir_in(uintptr_t bank, uint8_t pin)
{
    uint32_t con = drd(bank);
    con &= ~(0xfu << (pin * 4u));
    dwr(bank, con);
}

static int gpio_get(uintptr_t bank, uint8_t pin)
{
    return (int)((drd(bank + 4u) >> pin) & 1u);
}

static void gpio_set_pud(uintptr_t bank, uint8_t pin, uint32_t pud)
{
    uint32_t r = drd(bank + 8u);
    r = (r & ~(3u << (pin * 2u))) | ((pud & 3u) << (pin * 2u));
    dwr(bank + 8u, r);
}

static void bb_sda(const struct bb_i2c *b, int hi)
{
    if (hi) gpio_dir_in(b->bank, b->sda);
    else    gpio_dir_out(b->bank, b->sda, 0);
}

static void bb_scl(const struct bb_i2c *b, int hi)
{
    if (hi) {
        gpio_dir_in(b->bank, b->scl);
        for (uint32_t i = 0; i < 200u && !gpio_get(b->bank, b->scl); i++)
            exy_udelay(2);
    } else {
        gpio_dir_out(b->bank, b->scl, 0);
    }
}

static void bb_init(const struct bb_i2c *b)
{
    if (!b->bank) return;
    gpio_set_pud(b->bank, b->sda, 0u);
    gpio_set_pud(b->bank, b->scl, 0u);
    bb_sda(b, 1);
    bb_scl(b, 1);
    exy_udelay(5);
}

static void bb_start(const struct bb_i2c *b)
{
    bb_sda(b, 1); bb_scl(b, 1); exy_udelay(4);
    bb_sda(b, 0); exy_udelay(4);
    bb_scl(b, 0); exy_udelay(3);
}

static void bb_stop(const struct bb_i2c *b)
{
    bb_sda(b, 0); exy_udelay(3);
    bb_scl(b, 1); exy_udelay(4);
    bb_sda(b, 1); exy_udelay(4);
}

static int bb_tx_byte(const struct bb_i2c *b, uint8_t v)
{
    for (int i = 7; i >= 0; i--) {
        bb_sda(b, (v >> i) & 1);
        exy_udelay(2);
        bb_scl(b, 1);
        exy_udelay(4);
        bb_scl(b, 0);
        exy_udelay(2);
    }
    bb_sda(b, 1);
    exy_udelay(2);
    bb_scl(b, 1);
    exy_udelay(4);
    int nack = gpio_get(b->bank, b->sda);
    bb_scl(b, 0);
    exy_udelay(2);
    return nack ? -1 : 0;
}

static uint8_t bb_rx_byte(const struct bb_i2c *b, int ack)
{
    uint8_t v = 0;
    bb_sda(b, 1);
    for (int i = 7; i >= 0; i--) {
        exy_udelay(2);
        bb_scl(b, 1);
        exy_udelay(3);
        if (gpio_get(b->bank, b->sda))
            v |= (uint8_t)(1u << i);
        exy_udelay(1);
        bb_scl(b, 0);
        exy_udelay(2);
    }
    bb_sda(b, ack ? 0 : 1);
    exy_udelay(2);
    bb_scl(b, 1);
    exy_udelay(4);
    bb_scl(b, 0);
    bb_sda(b, 1);
    exy_udelay(2);
    return v;
}

static int bb_write(const struct bb_i2c *b, uint8_t addr7, const uint8_t *buf, uint32_t len)
{
    if (!b->bank) return -1;
    bb_start(b);
    if (bb_tx_byte(b, (uint8_t)(addr7 << 1)) != 0) {
        bb_stop(b);
        return -1;
    }
    for (uint32_t i = 0; i < len; i++) {
        if (bb_tx_byte(b, buf[i]) != 0) {
            bb_stop(b);
            return -1;
        }
    }
    bb_stop(b);
    return 0;
}

static int bb_read_reg(const struct bb_i2c *b, uint8_t addr7, uint8_t reg, uint8_t *out, uint32_t len)
{
    if (!b->bank || !len) return -1;
    bb_start(b);
    if (bb_tx_byte(b, (uint8_t)(addr7 << 1)) != 0 || bb_tx_byte(b, reg) != 0) {
        bb_stop(b);
        return -1;
    }
    bb_start(b);
    if (bb_tx_byte(b, (uint8_t)((addr7 << 1) | 1u)) != 0) {
        bb_stop(b);
        return -1;
    }
    for (uint32_t i = 0; i < len; i++)
        out[i] = bb_rx_byte(b, i + 1 < len);
    bb_stop(b);
    return 0;
}

static int bb_read_raw(const struct bb_i2c *b, uint8_t addr7, uint8_t *out, uint32_t len)
{
    if (!b->bank || !len) return -1;
    bb_start(b);
    if (bb_tx_byte(b, (uint8_t)((addr7 << 1) | 1u)) != 0) {
        bb_stop(b);
        return -1;
    }
    for (uint32_t i = 0; i < len; i++)
        out[i] = bb_rx_byte(b, i + 1 < len);
    bb_stop(b);
    return 0;
}

static struct bb_i2c bus_led(void) { struct bb_i2c b = { gpio_l_va ? gpio_l_va + 0x1c0u : 0, 7, 6 }; return b; }
static struct bb_i2c bus_fg(void)  { struct bb_i2c b = { gpio_l_va ? gpio_l_va + 0x1a0u : 0, 5, 4 }; return b; }
static struct bb_i2c bus_m77(void) { struct bb_i2c b = { gpio_r_va ? gpio_r_va + 0x2a0u : 0, 0, 1 }; return b; }
static struct bb_i2c bus_mag(void) { struct bb_i2c b = { gpio_r_va ? gpio_r_va + 0x160u : 0, 4, 5 }; return b; }
static struct bb_i2c bus_als(void) { struct bb_i2c b = { gpio_l_va ? gpio_l_va + 0x180u : 0, 0, 1 }; return b; }
static struct bb_i2c bus_tk(void)  { struct bb_i2c b = { gpio_r_va ? gpio_r_va + 0x0c0u : 0, 2, 1 }; return b; }

/* --- 2. AN30259A RGB Notification LED (i2c10 @ 0x30) ----------------------- */

int exy_drv_set_led(uint8_t r, uint8_t g, uint8_t b)
{
    struct bb_i2c bus = bus_led();
    uint8_t c0[2] = { 0x03u, r };
    uint8_t c1[2] = { 0x04u, g };
    uint8_t c2[2] = { 0x05u, b };
    uint8_t on = (r ? 1u : 0u) | (g ? 2u : 0u) | (b ? 4u : 0u);
    uint8_t lon[2] = { 0x01u, on };
    if (bb_write(&bus, 0x30u, c0, 2) != 0)
        return -1;
    bb_write(&bus, 0x30u, c1, 2);
    bb_write(&bus, 0x30u, c2, 2);
    bb_write(&bus, 0x30u, lon, 2);
    exy_drv_log.st.led_rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    return 0;
}

static void led_init(void)
{
    struct bb_i2c bus = bus_led();
    bb_init(&bus);
    uint8_t rst[2] = { 0x00u, 0x01u }; /* SRESET */
    if (bb_write(&bus, 0x30u, rst, 2) == 0) {
        exy_drv_log.st.probe_mask |= DRV_BIT_LED;
        exy_udelay(200);
        exy_drv_set_led(0, 0, 0); /* Turn off U-Boot's blinding blue LED */
    }
}

/* --- 3. Exynos 4412 Internal RTC (0x10070000) ------------------------------ */

static uint32_t bcd2bin(uint32_t v) { return ((v >> 4) & 0xfu) * 10u + (v & 0xfu); }
static uint32_t bin2bcd(uint32_t v) { return ((v / 10u) << 4) | (v % 10u); }

static int is_leap(uint32_t y) { return (y % 4u == 0u && y % 100u != 0u) || (y % 400u == 0u); }

static uint32_t ymdhms_to_epoch(uint32_t y, uint32_t mo, uint32_t d, uint32_t h, uint32_t mi, uint32_t s)
{
    static const uint16_t mdays[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    if (y < 1970u || mo < 1u || mo > 12u || d < 1u || d > 31u)
        return 0u;
    uint32_t days = 0u;
    for (uint32_t yr = 1970u; yr < y; yr++)
        days += is_leap(yr) ? 366u : 365u;
    days += mdays[mo - 1u] + (d - 1u);
    if (mo > 2u && is_leap(y))
        days++;
    return days * 86400u + h * 3600u + mi * 60u + s;
}

static void epoch_to_ymdhms(uint32_t ep, uint32_t *y, uint32_t *mo, uint32_t *d,
                            uint32_t *h, uint32_t *mi, uint32_t *s)
{
    static const uint8_t mlen[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    *s = ep % 60u; ep /= 60u;
    *mi = ep % 60u; ep /= 60u;
    *h = ep % 24u;
    uint32_t days = ep / 24u;
    uint32_t yr = 1970u;
    for (;;) {
        uint32_t yd = is_leap(yr) ? 366u : 365u;
        if (days < yd) break;
        days -= yd;
        yr++;
    }
    uint32_t m = 0u;
    for (; m < 11u; m++) {
        uint32_t ml = mlen[m] + ((m == 1u && is_leap(yr)) ? 1u : 0u);
        if (days < ml) break;
        days -= ml;
    }
    *y = yr; *mo = m + 1u; *d = days + 1u;
}

void exy_rtc_write_epoch(uint32_t ep);

uint32_t exy_rtc_read_epoch(void)
{
    if (!rtc_va)
        rtc_va = ml_io_map(RTC_PA, 0x1000);
    if (!rtc_va)
        return 1763596800u;
    uint32_t s1, s2, mi, h, d, mo, y;
    do {
        s1 = bcd2bin(drd(rtc_va + 0x70u) & 0x7fu);
        mi = bcd2bin(drd(rtc_va + 0x74u) & 0x7fu);
        h  = bcd2bin(drd(rtc_va + 0x78u) & 0x3fu);
        d  = bcd2bin(drd(rtc_va + 0x7cu) & 0x3fu);
        mo = bcd2bin(drd(rtc_va + 0x84u) & 0x1fu);
        y  = bcd2bin(drd(rtc_va + 0x88u) & 0xffu) + 2000u;
        s2 = bcd2bin(drd(rtc_va + 0x70u) & 0x7fu);
    } while (s1 != s2);
    uint32_t ep = ymdhms_to_epoch(y, mo, d, h, mi, s1);
    if (ep >= 1451606400u && ep < 4102444800u) { /* 2016..2100 */
        exy_drv_log.st.probe_mask |= DRV_BIT_RTC;
        exy_drv_log.st.rtc_epoch = ep;
        return ep;
    }
    /* Hardware RTC uninitialized (e.g. post battery drain): seed 2025-11-20 UTC so it ticks */
    exy_rtc_write_epoch(1763596800u);
    return 1763596800u;
}

void exy_rtc_write_epoch(uint32_t ep)
{
    if (ep < 1451606400u || ep >= 4102444800u)
        return;
    if (!rtc_va)
        rtc_va = ml_io_map(RTC_PA, 0x1000);
    if (!rtc_va)
        return;
    uint32_t y, mo, d, h, mi, s;
    epoch_to_ymdhms(ep, &y, &mo, &d, &h, &mi, &s);
    if (y < 2000u || y > 2099u)
        return;
    uint32_t con = drd(rtc_va + 0x40u);
    dwr(rtc_va + 0x40u, con | 1u); /* CTLEN = 1 */
    dwr(rtc_va + 0x70u, bin2bcd(s));
    dwr(rtc_va + 0x74u, bin2bcd(mi));
    dwr(rtc_va + 0x78u, bin2bcd(h));
    dwr(rtc_va + 0x7cu, bin2bcd(d));
    dwr(rtc_va + 0x84u, bin2bcd(mo));
    dwr(rtc_va + 0x88u, bin2bcd(y - 2000u));
    dwr(rtc_va + 0x40u, con & ~1u); /* CTLEN = 0 */
    exy_drv_log.st.probe_mask |= DRV_BIT_RTC;
    exy_drv_log.st.rtc_epoch = ep;
    exy_clean_line(&exy_drv_log);
    exy_clean_line(&exy_drv_log.st.rtc_epoch);
}

/* --- 4. MAX17047 Fuel Gauge + MAX77693 Charger/MUIC + IOPMPowerSource ------ */

static int fg_read16(const struct bb_i2c *b, uint8_t reg, uint16_t *out)
{
    uint8_t d[2];
    if (bb_read_reg(b, 0x36u, reg, d, 2) != 0)
        return -1;
    *out = (uint16_t)((uint16_t)d[0] | ((uint16_t)d[1] << 8));
    return 0;
}

void exy_drv_power_sync(void)
{
    struct bb_i2c fg = bus_fg();
    struct bb_i2c m77 = bus_m77();
    uint16_t v16 = 0;

    if (fg_read16(&fg, 0x09u /* VCELL */, &v16) == 0) {
        exy_drv_log.st.probe_mask |= DRV_BIT_FG;
        exy_drv_log.st.batt_mv = (uint16_t)((((uint32_t)(v16 >> 3)) * 625u) / 1000u);
        uint16_t soc = 0;
        if (fg_read16(&fg, 0x06u /* SOCREP */, &soc) == 0 && (soc >> 8) <= 100u && soc != 0u) {
            exy_drv_log.st.batt_soc = soc >> 8;
        } else if (fg_read16(&fg, 0xffu /* VFSOC */, &soc) == 0) {
            uint16_t pct = soc >> 8;
            if (pct > 100u) pct = 100u;
            exy_drv_log.st.batt_soc = pct;
        }
        uint16_t cur = 0;
        if (fg_read16(&fg, 0x0bu /* AVG_CURRENT */, &cur) == 0) {
            /* 156.25 uA/LSB on 0.01 ohm sense resistor -> /6.4 ~= *5/32 mA */
            int32_t sc = (int16_t)cur;
            exy_drv_log.st.batt_ma = (int16_t)((sc * 5) / 32);
        }
        uint16_t tmp = 0;
        if (fg_read16(&fg, 0x08u /* TEMP */, &tmp) == 0) {
            int32_t st = (int16_t)tmp;
            exy_drv_log.st.batt_temp = (int16_t)((st * 10) / 256); /* 0.1 deg C */
        }
    }

    uint8_t chg_det = 0;
    if (bb_read_reg(&m77, 0x66u, 0xb4u /* CHG_DETAILS_01 */, &chg_det, 1) == 0) {
        exy_drv_log.st.probe_mask |= DRV_BIT_CHG;
        exy_drv_log.st.pad0 = chg_det;
        uint8_t st = chg_det & 0xfu;
        if (st == 4u)
            exy_drv_log.st.chg_state = 2u; /* full */
        else if (st <= 3u || st == 5u)
            exy_drv_log.st.chg_state = 1u; /* charging */
        else
            exy_drv_log.st.chg_state = 0u; /* discharging */
    }

    uint8_t muic[2] = { 0, 0 };
    if (bb_read_reg(&m77, 0x25u, 0x04u /* MUIC_STATUS1 */, muic, 2) == 0) {
        exy_drv_log.st.probe_mask |= DRV_BIT_MUIC;
        if ((muic[1] & 0x07u) != 0u)
            exy_drv_log.st.cable_type = 1u; /* USB/AC charger */
        else
            exy_drv_log.st.cable_type = 0u;
    }

    /* Enable 500 mA USB charging on MAX77693 whenever USB/AC cable is connected and charger is off.
     * Clear any latched TIMER_EXPIRED (det=0x26) by disabling FCHGTIME and toggling CHG_EN 0->1. */
    if ((exy_drv_log.st.probe_mask & DRV_BIT_CHG) && exy_drv_log.st.cable_type == 1u &&
        exy_drv_log.st.chg_state == 0u && exy_drv_log.st.batt_soc < 99u) {
        uint8_t c00 = 0;
        bb_read_reg(&m77, 0x66u, 0xb7u, &c00, 1);
        uint8_t unlock[2] = { 0xbdu /* CHG_CNFG_06 */, 0x0cu };
        uint8_t timer[2]  = { 0xb8u /* CHG_CNFG_01 */, 0x80u }; /* PQEN=1, FCHGTIME=0 (disable timer) */
        uint8_t ilim[2]   = { 0xc0u /* CHG_CNFG_09 */, 0x19u }; /* 500 mA input limit */
        uint8_t ichg[2]   = { 0xb9u /* CHG_CNFG_02 */, 0x0fu }; /* 500 mA fast charge */
        uint8_t off[2]    = { 0xb7u /* CHG_CNFG_00 */, (uint8_t)((c00 & ~0x0fu) | 0x04u) };
        uint8_t on[2]     = { 0xb7u /* CHG_CNFG_00 */, (uint8_t)((c00 & ~0x0fu) | 0x05u) };
        bb_write(&m77, 0x66u, unlock, 2);
        bb_write(&m77, 0x66u, timer, 2);
        bb_write(&m77, 0x66u, ilim, 2);
        bb_write(&m77, 0x66u, ichg, 2);
        bb_write(&m77, 0x66u, off, 2);
        exy_udelay(5000);
        bb_write(&m77, 0x66u, on, 2);
        exy_udelay(2000);
        if (bb_read_reg(&m77, 0x66u, 0xb4u, &chg_det, 1) == 0) {
            exy_drv_log.st.pad0 = chg_det;
            uint8_t st = chg_det & 0xfu;
            exy_drv_log.st.chg_state = (st == 4u) ? 2u : ((st <= 3u || st == 5u) ? 1u : 0u);
        }
    }

    /* Push real battery & charger state into native Apple IOPMPowerSource */
    if (pm_power_src && (exy_drv_log.st.probe_mask & DRV_BIT_FG)) {
        uint32_t soc = exy_drv_log.st.batt_soc;
        if (soc > 100u) soc = 100u;
        int ext = (exy_drv_log.st.cable_type == 1u || exy_drv_log.st.chg_state != 0u) ? 1 : 0;
        int chg = (exy_drv_log.st.chg_state == 1u) ? 1 : 0;
        int full = (exy_drv_log.st.chg_state == 2u || (ext && soc >= 100u)) ? 1 : 0;
        pm_ps_set_batt_inst(pm_power_src, 1);
        pm_ps_set_max_cap(pm_power_src, 100u);
        pm_ps_set_cur_cap(pm_power_src, soc);
        pm_ps_set_voltage(pm_power_src, exy_drv_log.st.batt_mv);
        pm_ps_set_amperage(pm_power_src, exy_drv_log.st.batt_ma);
        pm_ps_set_ext_conn(pm_power_src, ext);
        pm_ps_set_ext_cap(pm_power_src, ext);
        pm_ps_set_charging(pm_power_src, chg);
        pm_ps_set_warn(pm_power_src, (!ext && soc <= 15u) ? 1 : 0);
        pm_ps_set_crit(pm_power_src, (!ext && soc <= 2u) ? 1 : 0);
        pm_ps_set_time_rem(pm_power_src, chg ? 60 : (int)(soc * 5u));
        pm_ps_set_adapter(pm_power_src, ext ? 0x1f4 : 0);
        void *fsym = ossym_with_cstr("FullyCharged");
        if (fsym) {
            pm_ps_set_prop(pm_power_src, fsym, full ? K_OS_BOOL_TRUE : K_OS_BOOL_FALSE);
            void (*rel)(void *) = *(void (**)(void *))(*(uintptr_t *)fsym + 0x14u);
            rel(fsym);
        }
        pm_ps_update_status(pm_power_src);
    }
}

extern int exy_cpu_set_opp(int opp);
extern int exy_gpu_set_opp(int opp);
extern volatile uint32_t exy_last_touch_mct;
extern volatile uint32_t exy_last_gpu_mct;
extern volatile uint32_t exy_last_disp_mct;
extern volatile uint32_t exy_last_emmc_mct;
static void tmu_adc_poll(void);
static uint32_t pm_tick_cnt;

static int therm_tier;

static void dvfs_thermal_update(void)
{
    uint32_t now = exy_mct_lo();
    /* Effective SoC temperature: trust TMU if in sane [20..115]C range, else map AP NTC thermistor ADC */
    int temp_c = (int)exy_drv_log.st.cpu_temp_c;
    if (temp_c < 20 || temp_c > 115) {
        uint16_t a = exy_drv_log.st.adc_ap;
        if (a > 0u && a < 380u)      temp_c = 90;
        else if (a > 0u && a < 460u) temp_c = 82;
        else if (a > 0u && a < 540u) temp_c = 74;
        else if (a > 0u && a < 640u) temp_c = 64;
        else                         temp_c = 48;
    }

    /* 4-tier thermal state with 3C hysteresis: <68 / 68-75 / 76-81 / >=82 */
    if (temp_c >= 82)
        therm_tier = 3;
    else if (temp_c >= 76) {
        if (therm_tier < 2 || temp_c < 79) therm_tier = 2;
    } else if (temp_c >= 68) {
        if (therm_tier < 1 || temp_c < 73) therm_tier = 1;
    } else if (temp_c < 65) {
        therm_tier = 0;
    } else if (therm_tier > 1) {
        therm_tier = 1;
    }

    uint32_t dt_touch = exy_last_touch_mct ? (uint32_t)(now - exy_last_touch_mct) : ~0u;
    uint32_t dt_gpu   = exy_last_gpu_mct   ? (uint32_t)(now - exy_last_gpu_mct)   : ~0u;
    uint32_t dt_disp  = exy_last_disp_mct  ? (uint32_t)(now - exy_last_disp_mct)  : ~0u;
    uint32_t dt_emmc  = exy_last_emmc_mct  ? (uint32_t)(now - exy_last_emmc_mct)  : ~0u;

    int touch_act = dt_touch < 24000u * 1500u;
    int gpu_act   = dt_gpu   < 24000u * 400u;
    int disp_act  = dt_disp  < 24000u * 600u;
    int emmc_act  = dt_emmc  < 24000u * 600u;
    int recent    = dt_touch < 24000u * 3000u || dt_disp < 24000u * 3000u ||
                    dt_gpu   < 24000u * 1500u || dt_emmc < 24000u * 1500u;

    int cpu_opp = (touch_act || gpu_act || disp_act || emmc_act) ? 2 : (recent ? 1 : 0);
    int gpu_opp = gpu_act ? 2 : 0;

    static const int cpu_cap[4] = { 2, 2, 1, 0 }; /* 1200, 1200, 600, 400 MHz */
    static const int gpu_cap[4] = { 2, 1, 1, 0 }; /*  440,  220, 220, 147 MHz */
    if (cpu_opp > cpu_cap[therm_tier]) cpu_opp = cpu_cap[therm_tier];
    if (gpu_opp > gpu_cap[therm_tier]) gpu_opp = gpu_cap[therm_tier];

    (void)exy_cpu_set_opp(cpu_opp);
    (void)exy_gpu_set_opp(gpu_opp);
}

static void pm_poll_tick(void *p0, void *p1)
{
    extern volatile uint32_t exy_usb_hb;
    (void)p0; (void)p1;
    exy_usb_hb++;
    pm_tick_cnt++;
    if ((pm_tick_cnt & 3u) == 1u)
        tmu_adc_poll(); /* every 1s */
    dvfs_thermal_update();
    if (pm_tick_cnt % 60u == 0u)
        exy_drv_power_sync(); /* every 15s */
    if (pm_poll_tc) {
        uint64_t dl = 0;
        clk_int_deadline(250u, 1000000u, &dl); /* 250 ms DVFS/thermal tick */
        tc_enter_delayed(pm_poll_tc, dl);
    }
}

/* --- 5. s6e8aa0 AMOLED Brightness & Sleep/Wake (MIPI DSIM0 @ 0x11C80000) --- */

static int dsim_wait_tx_empty(void)
{
    for (uint32_t i = 0; i < 5000u; i++) {
        if (drd(dsim_va + 0x44u /* fifoctrl */) & (1u << 22)) /* SFR_HEADER_EMPTY */
            return 0;
        exy_udelay(2);
    }
    return -1;
}

static int dsim_send_dcs(uint8_t dtype, const uint8_t *data, uint32_t len)
{
    if (!dsim_va) return -1;
    if (dsim_wait_tx_empty() != 0)
        return -1;
    if (dtype == 0x05u || dtype == 0x15u) {
        uint32_t hdr = (uint32_t)dtype |
                       ((uint32_t)(len > 0 ? data[0] : 0u) << 8) |
                       ((uint32_t)(len > 1 ? data[1] : 0u) << 16);
        dwr(dsim_va + 0x34u /* pkthdr */, hdr);
        return 0;
    }
    /* Long write (0x39): push 32-bit LE payload words, then header with 16-bit byte count */
    for (uint32_t i = 0; i < len; i += 4u) {
        uint32_t w = (uint32_t)data[i];
        if (i + 1u < len) w |= (uint32_t)data[i + 1u] << 8;
        if (i + 2u < len) w |= (uint32_t)data[i + 2u] << 16;
        if (i + 3u < len) w |= (uint32_t)data[i + 3u] << 24;
        dwr(dsim_va + 0x38u /* payload */, w);
    }
    uint32_t hdr = (uint32_t)dtype | ((len & 0xffu) << 8) | (((len >> 8) & 0xffu) << 16);
    dwr(dsim_va + 0x34u /* pkthdr */, hdr);
    return 0;
}

/* Calibrated s6e8aa0 2.2-gamma tables (0xFA, 26 bytes) for 4 brightness tiers: 30/100/180/250 cd/m2 */
static const uint8_t s6e8_gamma[4][26] = {
    { 0xfa, 0x01, 0x60, 0x10, 0x60, 0xf5, 0x00, 0xff, 0xad, 0xaf, 0xba, 0xc3, 0xd8,
      0xc5, 0x9f, 0xc6, 0x9e, 0xc1, 0xdc, 0xc0, 0x00, 0x61, 0x00, 0x5a, 0x00, 0x74 },
    { 0xfa, 0x01, 0x50, 0x10, 0x50, 0xee, 0x00, 0xf6, 0xb8, 0xb9, 0xc1, 0xc9, 0xdb,
      0xc9, 0xae, 0xcc, 0xad, 0xc6, 0xdf, 0xc6, 0x00, 0x8e, 0x00, 0x86, 0x00, 0xa8 },
    { 0xfa, 0x01, 0x43, 0x10, 0x43, 0xe8, 0x00, 0xef, 0xc0, 0xc0, 0xc6, 0xcd, 0xde,
      0xcd, 0xb8, 0xd0, 0xb7, 0xc9, 0xe2, 0xca, 0x00, 0xb2, 0x00, 0xab, 0x00, 0xd2 },
    { 0xfa, 0x01, 0x35, 0x10, 0x35, 0xe2, 0x00, 0xe8, 0xc7, 0xc7, 0xcb, 0xd1, 0xe0,
      0xd0, 0xc0, 0xd4, 0xbf, 0xcc, 0xe4, 0xce, 0x00, 0xd4, 0x00, 0xcc, 0x00, 0xf6 },
};
static int amoled_asleep;

int exy_drv_set_brightness(uint8_t level)
{
    if (!dsim_va) return -1;
    if (level == 0u) {
        if (!amoled_asleep) {
            uint8_t off[2] = { 0x28u, 0x00u };
            uint8_t slp[2] = { 0x10u, 0x00u };
            dsim_send_dcs(0x05u, off, 2);
            exy_udelay(20000);
            dsim_send_dcs(0x05u, slp, 2);
            amoled_asleep = 1;
        }
        exy_drv_log.st.brightness = 0u;
        return 0;
    }
    if (amoled_asleep) {
        uint8_t out[2] = { 0x11u, 0x00u };
        uint8_t on[2]  = { 0x29u, 0x00u };
        dsim_send_dcs(0x05u, out, 2);
        exy_udelay(40000);
        dsim_send_dcs(0x05u, on, 2);
        amoled_asleep = 0;
    }
    /* Interpolate smoothly between the 4 calibrated s6e8aa0 gamma curves */
    uint32_t idx = (uint32_t)level * 3u / 255u;
    if (idx > 2u) idx = 2u;
    uint32_t lo_lvl = idx * 85u;
    uint32_t frac = (level > lo_lvl) ? ((uint32_t)(level - lo_lvl) * 256u / 85u) : 0u;
    if (frac > 256u) frac = 256u;
    uint8_t g[26];
    g[0] = 0xfa; g[1] = 0x01;
    for (uint32_t i = 2u; i < 26u; i++) {
        int32_t a = s6e8_gamma[idx][i];
        int32_t b = s6e8_gamma[idx + 1u][i];
        g[i] = (uint8_t)(a + (((b - a) * (int32_t)frac) >> 8));
    }
    uint8_t key1[3] = { 0xf0u, 0x5au, 0x5au };
    uint8_t upd[2]  = { 0xf7u, 0x03u };
    dsim_send_dcs(0x39u, key1, 3);
    dsim_send_dcs(0x39u, g, 26);
    dsim_send_dcs(0x15u, upd, 2);
    exy_drv_log.st.brightness = level;
    return 0;
}

/* --- 6. MAX77693 Haptic Motor + Exynos PWM0 (0x139D0000, GPD0[0]) ---------- */

static void vib_stop_cb(void *p0, void *p1)
{
    (void)p0; (void)p1;
    if (pwm_va)
        dwr(pwm_va + 0x08u /* TCON */, drd(pwm_va + 0x08u) & ~1u);
    struct bb_i2c m77 = bus_m77();
    uint8_t off[2] = { 0x02u /* CONFIG2 */, 0x00u };
    bb_write(&m77, 0x48u, off, 2);
}

int exy_drv_vibrate(uint32_t ms)
{
    if (!(exy_drv_log.st.probe_mask & DRV_BIT_HAPTIC) || !pwm_va || !gpio_l_va)
        return -1;
    if (ms == 0u) {
        vib_stop_cb(0, 0);
        return 0;
    }
    if (ms > 1000u) ms = 1000u;
    /* Pinmux GPD0[0] = TOUT_0 (func 2) */
    uint32_t con = drd(gpio_l_va + 0x0a0u);
    dwr(gpio_l_va + 0x0a0u, (con & ~0xfu) | 2u);
    /* Enable MAX77693 Haptic LRA external PWM mode */
    struct bb_i2c m77 = bus_m77();
    uint8_t cfg2[2] = { 0x02u, 0xc0u }; /* MOTOR_EN | LRA | EXT_PWM */
    bb_write(&m77, 0x48u, cfg2, 2);
    /* PWM0: PCLK ~100 MHz / (24+1) / 8 = 500 kHz; 205 Hz LRA resonant -> TCNTB0=2440, 50% duty */
    dwr(pwm_va + 0x00u /* TCFG0 */, (drd(pwm_va + 0x00u) & ~0xffu) | 24u);
    dwr(pwm_va + 0x04u /* TCFG1 */, (drd(pwm_va + 0x04u) & ~0xfu) | 3u); /* /8 */
    dwr(pwm_va + 0x0cu /* TCNTB0 */, 2440u);
    dwr(pwm_va + 0x10u /* TCMPB0 */, 1220u);
    uint32_t tcon = drd(pwm_va + 0x08u) & ~0x1fu;
    dwr(pwm_va + 0x08u, tcon | 0x02u); /* manual update */
    dwr(pwm_va + 0x08u, tcon | 0x09u); /* start + auto-reload */
    if (!vib_stop_tc)
        vib_stop_tc = tc_alloc(vib_stop_cb, 0);
    if (vib_stop_tc) {
        uint64_t dl = 0;
        clk_int_deadline(ms, 1000000u, &dl);
        tc_enter_delayed(vib_stop_tc, dl);
    }
    return 0;
}

static void haptic_init(void)
{
    /* Power MOTOR_VCC_3.0V (LDO26) & probe MAX77693 Haptic @ 0x48 */
    exy_pmic_write_reg(MAX77686_LDO26CTRL1, 0xecu); /* 3.0V ON */
    exy_udelay(2000);
    struct bb_i2c m77 = bus_m77();
    uint8_t st = 0;
    if (bb_read_reg(&m77, 0x48u, 0x00u, &st, 1) == 0) {
        exy_drv_log.st.probe_mask |= DRV_BIT_HAPTIC;
        uint8_t off[2] = { 0x02u, 0x00u };
        bb_write(&m77, 0x48u, off, 2);
    }
}

/* --- 7. Sensors: LSM330DLC (I2C1), AK8975C (GPY2), CM36651 (GPF0), TKey ---- */

static void sensors_init(void)
{
    /* I2C1 pinmux: GPD1[2]=SDA1, GPD1[3]=SCL1 (func 2, no pull) */
    if (gpio_l_va && i2c1_va) {
        uint32_t con = drd(gpio_l_va + 0x0c0u);
        dwr(gpio_l_va + 0x0c0u, (con & ~0x0000ff00u) | 0x00002200u);
        uint32_t pud = drd(gpio_l_va + 0x0c8u);
        dwr(gpio_l_va + 0x0c8u, pud & ~0x000000f0u);
        exy_i2c_init_hw(i2c1_va);
        /* Probe LSM330DLC Accel at 0x19 or 0x18, then leave powered down (no active consumer). */
        static const uint8_t a_addrs[2] = { 0x19u, 0x18u };
        for (int i = 0; i < 2; i++) {
            uint8_t v = 0;
            if (exy_i2c_read_reg(i2c1_va, a_addrs[i], 0x20u /* CTRL_REG1_A */, &v, 1) == 0) {
                accel_addr = a_addrs[i];
                uint8_t c1[2] = { 0x20u, 0x00u }; /* power-down */
                exy_i2c_write(i2c1_va, accel_addr, c1, 2);
                exy_drv_log.st.probe_mask |= DRV_BIT_ACCEL;
                break;
            }
        }
        /* Probe LSM330DLC Gyro at 0x6b or 0x6a (WHO_AM_I 0x0F == 0xD4), then leave powered down. */
        static const uint8_t g_addrs[2] = { 0x6bu, 0x6au };
        for (int i = 0; i < 2; i++) {
            uint8_t id = 0;
            if (exy_i2c_read_reg(i2c1_va, g_addrs[i], 0x0fu, &id, 1) == 0 && (id == 0xd4u || id == 0xd3u)) {
                gyro_addr = g_addrs[i];
                uint8_t c1[2] = { 0x20u, 0x00u }; /* power-down */
                exy_i2c_write(i2c1_va, gyro_addr, c1, 2);
                exy_drv_log.st.probe_mask |= DRV_BIT_GYRO;
                break;
            }
        }
    }

    /* AK8975C Magnetometer on GPY2[4:5] @ 0x0C (stays in power-down after WIA probe) */
    struct bb_i2c mag = bus_mag();
    bb_init(&mag);
    uint8_t wia = 0;
    if (bb_read_reg(&mag, 0x0cu, 0x00u /* WIA */, &wia, 1) == 0 && wia == 0x48u)
        exy_drv_log.st.probe_mask |= DRV_BIT_MAG;

    /* CM36651 Proximity + ALS on GPF0[0:1] @ 0x18 (briefly power GPJ0[5] to probe, then shut down LED_A) */
    if (gpio_l_va)
        gpio_dir_out(gpio_l_va + 0x240u /* GPJ0 */, 5, 1);
    exy_udelay(2000);
    struct bb_i2c als = bus_als();
    bb_init(&als);
    uint8_t als_off[2] = { 0x00u, 0x01u }; /* ALS shutdown */
    if (bb_write(&als, 0x18u, als_off, 2) == 0) {
        exy_drv_log.st.probe_mask |= DRV_BIT_ALS;
        uint8_t ps_off[2] = { 0x00u, 0x01u }; /* PS shutdown */
        bb_write(&als, 0x19u, ps_off, 2);
    }
    if (gpio_l_va)
        gpio_dir_out(gpio_l_va + 0x240u /* GPJ0 */, 5, 0); /* LED_A off */

    /* Cypress Capacitive Touchkeys on GPL0[2:1] @ 0x20, GPJ0[3]=INT */
    if (gpio_l_va) {
        gpio_dir_in(gpio_l_va + 0x240u, 3);
        gpio_set_pud(gpio_l_va + 0x240u, 3, 3u);
    }
    struct bb_i2c tk = bus_tk();
    bb_init(&tk);
    uint8_t tk_buf[4] = { 0 };
    if (bb_read_raw(&tk, 0x20u, tk_buf, 1) == 0)
        exy_drv_log.st.probe_mask |= DRV_BIT_TKEY;
}

static void tkey_poll(void)
{
    if (exy_drv_log.st.probe_mask & DRV_BIT_TKEY) {
        /* Active-low GPJ0[3] = touchkey event ready */
        if (gpio_l_va && !gpio_get(gpio_l_va + 0x240u, 3)) {
            struct bb_i2c tk = bus_tk();
            uint8_t k = 0;
            if (bb_read_raw(&tk, 0x20u, &k, 1) == 0) {
                uint8_t code = k & 0x07u;
                int release = (k & 0x08u) != 0u;
                if (code == 1u) {
                    if (release) exy_drv_log.st.touchkeys &= ~1u;
                    else         exy_drv_log.st.touchkeys |= 1u;
                } else if (code == 2u) {
                    if (release) exy_drv_log.st.touchkeys &= ~2u;
                    else         exy_drv_log.st.touchkeys |= 2u;
                }
            }
        }
    }
}

/* --- 8. Wolfson WM1811 Audio Codec (I2C4 @ 0x1A) + I2S0 -------------------- */

static int wm1811_use_bb;

static int wm1811_read(uint16_t reg, uint16_t *out)
{
    uint8_t b[2];
    if (wm1811_use_bb) {
        struct bb_i2c bus = { gpio_l_va ? gpio_l_va + 0x040u : 0, 0, 1 };
        bb_start(&bus);
        if (bb_tx_byte(&bus, 0x1au << 1) != 0 ||
            bb_tx_byte(&bus, (uint8_t)(reg >> 8)) != 0 ||
            bb_tx_byte(&bus, (uint8_t)reg) != 0) {
            bb_stop(&bus);
            return -1;
        }
        bb_start(&bus);
        if (bb_tx_byte(&bus, (0x1au << 1) | 1u) != 0) {
            bb_stop(&bus);
            return -1;
        }
        b[0] = bb_rx_byte(&bus, 1);
        b[1] = bb_rx_byte(&bus, 0);
        bb_stop(&bus);
        *out = (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
        return 0;
    }
    if (!i2c4_va || exy_i2c_read_reg16(i2c4_va, 0x1au, reg, b, 2) != 0)
        return -1;
    *out = (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
    return 0;
}

static int wm1811_write(uint16_t reg, uint16_t val)
{
    uint8_t b[4] = { (uint8_t)(reg >> 8), (uint8_t)reg, (uint8_t)(val >> 8), (uint8_t)val };
    if (wm1811_use_bb) {
        struct bb_i2c bus = { gpio_l_va ? gpio_l_va + 0x040u : 0, 0, 1 };
        return bb_write(&bus, 0x1au, b, 4);
    }
    if (!i2c4_va) return -1;
    return exy_i2c_write(i2c4_va, 0x1au, b, 4);
}

static void drv_puts(const char *s)
{
    extern void exy_putc(char c);
    while (*s) exy_putc(*s++);
}

static void drv_putx(uint32_t v)
{
    extern void exy_putc(char c);
    static const char h[] = "0123456789abcdef";
    for (int i = 28; i >= 0; i -= 4)
        exy_putc(h[(v >> i) & 0xfu]);
}

extern int exy_drv_set_volume(uint8_t vol);

static void audio_init(void)
{
    if (!gpio_l_va || !i2c4_va)
        return;
    /* Enable WM1811 DBVDD1 (LDO3 1.8V), MCLK2 (MAX77686 REG_PMIC_32KHZ=0x7F), LDO1ENA (GPJ0[4]) & MCLK1 (24 MHz) */
    exy_pmic_write_reg(0x42u /* LDO3CTRL1 */, 0xd4u);
    exy_pmic_write_reg(0x7fu /* MAX77686_REG_PMIC_32KHZ */, 0x07u);
    gpio_dir_out(gpio_l_va + 0x240u, 4, 1);
    if (pmu_drv_va)
        dwr(pmu_drv_va + 0x0a00u /* PMU_DEBUG */, 0x0900u); /* CLKOUT = XUSBXTI 24 MHz */
    /* Configure Exynos 4412 EPLL = 49.152 MHz -> AUDSS /4 = 12.288 MHz (256fs @ 48 kHz) for I2S0 */
    uintptr_t cmu_top = ml_io_map(0x1003c000u, 0x1000u);
    if (cmu_top) {
        dwr(cmu_top + 0x950u /* gate_ip_peril */, drd(cmu_top + 0x950u) | (1u << 10)); /* I2C4 */
        /* Switch MUX_EPLL_SEL (src_top0 bit 4) to FINPLL, lock EPLL at 49.152 MHz (M=49,P=3,S=3,K=9961) */
        dwr(cmu_top + 0x210u /* src_top0 */, drd(cmu_top + 0x210u) & ~(1u << 4));
        exy_udelay(20);
        dwr(cmu_top + 0x010u /* epll_lock */, 3000u);
        dwr(cmu_top + 0x118u /* epll_con2 */, 0x00000080u);
        dwr(cmu_top + 0x114u /* epll_con1 */, 9961u);
        dwr(cmu_top + 0x110u /* epll_con0 */, (1u << 31) | (49u << 16) | (3u << 8) | 3u);
        for (uint32_t i = 0; i < 10000u && !(drd(cmu_top + 0x110u) & (1u << 29)); i++)
            exy_udelay(1);
        dwr(cmu_top + 0x210u /* src_top0 */, drd(cmu_top + 0x210u) | (1u << 4));
        /* Route EPLL (6) -> SCLK_AUDIO0 (MAUDIO) /1 = 49.152 MHz */
        dwr(cmu_top + 0x23cu /* src_maudio */, (drd(cmu_top + 0x23cu) & ~0xfu) | 6u);
        dwr(cmu_top + 0x33cu /* src_mask_maudio */, drd(cmu_top + 0x33cu) | 1u);
        dwr(cmu_top + 0x53cu /* div_maudio */, drd(cmu_top + 0x53cu) & ~0xfu);
    }
    exy_udelay(25000);
    /* Pinmux GPB[0]=SDA4, GPB[1]=SCL4 (func 3, pull-up) */
    uint32_t con = drd(gpio_l_va + 0x040u);
    dwr(gpio_l_va + 0x040u, (con & ~0x000000ffu) | 0x00000033u);
    uint32_t pud = drd(gpio_l_va + 0x048u);
    dwr(gpio_l_va + 0x048u, (pud & ~0x0000000fu) | 0x0000000fu);
    exy_i2c_init_hw(i2c4_va);

    uint16_t id = 0;
    wm1811_use_bb = 0;
    if (wm1811_read(0x0000u, &id) != 0 || (id != 0x8994u && id != 0x1811u && id != 0x8958u)) {
        /* Fallback to bit-bang on GPB[0]=SDA4, GPB[1]=SCL4 (and check swapped pin order) */
        struct bb_i2c bus = { gpio_l_va + 0x040u, 0, 1 };
        bb_init(&bus);
        wm1811_use_bb = 1;
        id = 0;
        (void)wm1811_read(0x0000u, &id);
    }
    if (id != 0x8994u && id != 0x1811u && id != 0x8958u) {
        /* Check GPB[0]=SCL4, GPB[1]=SDA4 */
        struct bb_i2c bus = { gpio_l_va + 0x040u, 1, 0 };
        bb_init(&bus);
        uint8_t b[2] = { 0, 0 };
        bb_start(&bus);
        if (bb_tx_byte(&bus, 0x1au << 1) == 0 && bb_tx_byte(&bus, 0) == 0 && bb_tx_byte(&bus, 0) == 0) {
            bb_start(&bus);
            if (bb_tx_byte(&bus, (0x1au << 1) | 1u) == 0) {
                b[0] = bb_rx_byte(&bus, 1);
                b[1] = bb_rx_byte(&bus, 0);
                id = (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
            }
        }
        bb_stop(&bus);
    }
    if (id == 0x8994u || id == 0x1811u || id == 0x8958u) {
        exy_drv_log.st.wm1811_id = id;
        exy_drv_log.st.probe_mask |= DRV_BIT_WM1811;
        /* Soft reset -> LDO1 (3.0V AVDD1) + LDO2 (1.05V DCVDD) -> VMID/BIAS -> FLL1 -> AIF1CLK -> DAC1 -> SPKOUT!
         * CRITICAL: SPKOUTL/R must be enabled AFTER LDO1/2, VMID, and FLL1/AIF1CLK are stable,
         * otherwise WM1811 clock-loss/anti-pop protection permanently mutes the Class-D speaker stage! */
        wm1811_write(0x0000u, 0x0000u);
        exy_udelay(5000);
        wm1811_write(0x003bu /* LDO_1 (AVDD1 3.0V) */, 0x000du);
        wm1811_write(0x003cu /* LDO_2 (DCVDD 1.05V) */, 0x0005u);
        exy_udelay(10000);
        wm1811_write(0x0039u /* ANTIPOP2 */, 0x01e4u);
        wm1811_write(0x0001u /* PWR_MGMT_1: VMID+BIAS only first */, 0x0003u);
        exy_udelay(50000);
        wm1811_write(0x0039u /* ANTIPOP2: switch to normal bias + VMID_BUF */, 0x0040u);
        /* FLL1: Fref = 24MHz / 2 = 12MHz -> Fvco = 98.304MHz (N=8, K=0x3126) -> OUTDIV=8 -> 12.288MHz */
        wm1811_write(0x0221u /* FLL1_CTRL_2 */, 0x0700u); /* FLL1_OUTDIV=8, FRATIO=1 */
        wm1811_write(0x0222u /* FLL1_CTRL_3 */, 0x3126u); /* FLL1_K */
        wm1811_write(0x0223u /* FLL1_CTRL_4 */, 0x0100u); /* FLL1_N=8 (bits 14:5) */
        wm1811_write(0x0224u /* FLL1_CTRL_5 */, 0x0008u); /* FLL1_CLK_REF_DIV=/2, SRC=MCLK1 */
        wm1811_write(0x0220u /* FLL1_CTRL_1 */, 0x0005u); /* FLL1_FRAC=1, FLL1_ENA=1 */
        exy_udelay(10000);
        wm1811_write(0x0200u /* AIF1_CLK_1 */, 0x0011u);  /* SRC=FLL1, ENA=1 */
        wm1811_write(0x0208u /* CLOCKING_1 */, 0x001au);  /* TOCLK|AIF1DSPCLK|SYSDSPCLK, SYSCLK_SRC=0 (AIF1CLK!) */
        wm1811_write(0x0210u /* AIF1_RATE */, 0x0083u);   /* 48 kHz, 256fs */
        wm1811_write(0x0300u /* AIF1_CTRL_1 */, 0x4010u); /* 16-bit I2S */
        wm1811_write(0x0302u /* AIF1_MSTR_SLV */, 0x4000u); /* WM1811 I2S Master (CBP_CFP, matching midas_wm1811.c) */
        wm1811_write(0x0303u /* AIF1_BCLK */, 0x0070u);   /* BCLK_DIV=/8 -> 12.288M/8 = 1.536MHz (32fs) */
        wm1811_write(0x0304u /* AIF1ADC_LRCLK */, 0x0020u); /* 32 BCLKs -> 48.000 kHz */
        wm1811_write(0x0305u /* AIF1DAC_LRCLK */, 0x0020u); /* 32 BCLKs -> 48.000 kHz */
        wm1811_write(0x0005u /* PWR_MGMT_5 */, 0x0303u);  /* AIF1DAC1L+R, DAC1L+R */
        wm1811_write(0x0003u /* PWR_MGMT_3 */, 0x0330u);  /* SPKLVOL+RVOL, MIXOUTL+R */
        wm1811_write(0x0402u /* AIF1_DAC1_L_VOL */, 0x01c0u); /* 0 dB + VU */
        wm1811_write(0x0403u /* AIF1_DAC1_R_VOL */, 0x01c0u);
        wm1811_write(0x0420u /* AIF1_DAC1_FILT */, 0x0000u); /* unmute */
        wm1811_write(0x0601u /* DAC1_LEFT_MIX */, 0x0001u);
        wm1811_write(0x0602u /* DAC1_RIGHT_MIX */, 0x0001u);
        wm1811_write(0x0610u /* DAC1_LEFT_VOL */, 0x01c0u); /* 0 dB + VU + unmute */
        wm1811_write(0x0611u /* DAC1_RIGHT_VOL */, 0x01c0u);
        wm1811_write(0x0620u /* OVERSAMPLING */, 0x0002u); /* DAC_OSR128 default */
        /* Speaker path (Galaxy S3 mono external speaker on SPKOUTL + SPKOUTR active):
         *   0x0036 SPEAKER_MIXER: DAC1L->SPKMIXL (bit 1) + DAC1R->SPKMIXR (bit 0)
         *   0x0022/0x0023 SPKMIXL/R_ATTENUATION: 0x0000 = 0 dB UNMUTED
         *   0x0024 SPKOUT_MIXERS: 0x0019 = SPKMIXL+SPKMIXR->SPKOUTL (b4|b3, midas_wm1811.c) | SPKMIXR->SPKOUTR (b0)
         *   0x0025 CLASSD: 0x0177 = Class-D enabled + boost on L+R
         *   0x0026/0x0027 SPEAKER_VOLUME_LEFT/RIGHT: VU(0x100) | MUTE_N(0x040) | vol(0..63) */
        wm1811_write(0x0036u /* SPEAKER_MIXER */, 0x0003u);
        wm1811_write(0x0022u /* SPKMIXL_ATTEN */, 0x0000u);
        wm1811_write(0x0023u /* SPKMIXR_ATTEN */, 0x0000u);
        wm1811_write(0x0024u /* SPKOUT_MIXERS */, 0x0019u);
        wm1811_write(0x0025u /* CLASSD */,        0x0177u); /* Class-D boost on L+R */
        /* Headphone path: DAC1L/R -> OUTPUT_MIX1/2 -> Charge Pump + DC Servo + ANALOGUE_HP_1 */
        wm1811_write(0x002du /* OUTPUT_MIX1 */,   0x0001u); /* DAC1L -> HPOUT1L */
        wm1811_write(0x002eu /* OUTPUT_MIX2 */,   0x0001u); /* DAC1R -> HPOUT1R */
        wm1811_write(0x004cu /* CHARGE_PUMP_1 */, 0x9f25u); /* CP_ENA for HPOUT1 */
        wm1811_write(0x0054u /* DC_SERVO_1 */,    0x0033u); /* enable + startup DC servo L/R */
        /* Now enable SPKOUTL+R and HPOUT1L+R AFTER clocks/VMID/LDOs are stable */
        wm1811_write(0x0001u /* PWR_MGMT_1 */, 0x3303u);
        exy_udelay(15000);
        wm1811_write(0x0060u /* ANALOGUE_HP_1 */, 0x00eeu); /* enable HP1L/R output stages, release shorts */
        exy_drv_log.st.volume = 85u;
    }
    /* Headphone jack detect GPX3[0] (active-low) + Exynos I2S0 (0x03830000) + GPZ[0..4] pinmux */
    if (gpio_r_va) {
        gpio_dir_in(gpio_r_va + 0xc60u /* GPX3 */, 0);
        gpio_set_pud(gpio_r_va + 0xc60u, 0, 3u);
        exy_drv_log.st.hp_jack = (uint8_t)(!gpio_get(gpio_r_va + 0xc60u, 0));
    }
    if (exy_drv_log.st.probe_mask & DRV_BIT_WM1811)
        exy_drv_set_volume(85u);
    if (audss_va) {
        /* AUDSS: MOUT_AUDSS = EPLL (49.152 MHz), BUS_RATIO=/4 & I2S_RATIO=/4 (12.288 MHz = 256fs @ 48.000 kHz exact!) */
        dwr(audss_va + 0x00u /* CLK_SRC */,  0x00000001u);
        dwr(audss_va + 0x04u /* CLK_DIV */,  (3u << 8) | (3u << 4));
        dwr(audss_va + 0x08u /* CLK_GATE */, drd(audss_va + 0x08u) | 0x3fu);
    }
    if (gpz_va) {
        /* GPZ[0..4] = func 2 (I2S0 BCLK, CDCLK, LRCLK, SDI, SDO) */
        dwr(gpz_va + 0x00u, (drd(gpz_va + 0x00u) & ~0x000fffffu) | 0x00022222u);
    }
    if (i2s0_va) {
        /* Deassert active-low CON_RESET (bit 31!), Exynos I2S0 = SLAVE (bit 11 = MOD_SLAVE, bit 12 = CDCLK_IN)
         * clocked directly by WM1811 AIF1 Master (1.536 MHz BCLK, 48.000 kHz LRCLK), matching midas_wm1811.c! */
        dwr(i2s0_va + 0x00u /* I2SCON */, (1u << 31) | (1u << 4)); /* CON_RESET=1 (run), TXCH_PAUSE */
        dwr(i2s0_va + 0x04u /* I2SMOD */, (3u << 30) | (1u << 12) | (1u << 11)); /* OPCLK_PCLK | CDCLK_IN | SLAVE | 16-bit I2S | 256fs | 32fs */
        dwr(i2s0_va + 0x14u /* I2SPSR */, 0u);
        dwr(i2s0_va + 0x08u /* I2SFIC */, (1u << 15));             /* FIC_TXFLUSH */
        dwr(i2s0_va + 0x08u, 0u);
        exy_drv_log.st.probe_mask |= DRV_BIT_I2S0;
    }
}

int exy_drv_set_volume(uint8_t vol)
{
    if (!(exy_drv_log.st.probe_mask & DRV_BIT_WM1811))
        return -1;
    if (vol > 100u) vol = 100u;
    exy_drv_log.st.volume = vol;
    int hp = gpio_r_va ? !gpio_get(gpio_r_va + 0xc60u, 0) : 0;
    exy_drv_log.st.hp_jack = (uint8_t)hp;
    /* WM1811 SPKOUT/HPOUT1 PGA range: 0..63 (0x39 = 0 dB, 0x3f = +6 dB, 0x00 = -57 dB);
     * bit 8 = VU (Volume Update), bit 6 = MUTE_N (1 = UNMUTED, 0 = MUTED). */
    uint16_t gain = (vol == 0u) ? 0u : (uint16_t)(20u + ((uint32_t)vol * 43u) / 100u);
    if (gain > 63u) gain = 63u;
    uint16_t spk_v = (vol && !hp) ? (uint16_t)(0x0100u | 0x0040u | gain) : 0x0100u;
    uint16_t hp_v  = (vol && hp)  ? (uint16_t)(0x0100u | 0x0040u | gain) : 0x0100u;
    wm1811_write(0x0026u /* SPEAKER_VOLUME_LEFT */,  spk_v);
    wm1811_write(0x0027u /* SPEAKER_VOLUME_RIGHT */, spk_v);
    wm1811_write(0x001cu /* LEFT_OUTPUT_VOLUME */,   hp_v);
    wm1811_write(0x001du /* RIGHT_OUTPUT_VOLUME */,  hp_v);
    return 0;
}

void exy_i2s0_set_active(int active)
{
    if (!(exy_drv_log.st.probe_mask & DRV_BIT_I2S0) || !i2s0_va)
        return;
    if (active) {
        dwr(i2s0_va + 0x08u /* I2SFIC */, (1u << 15));
        dwr(i2s0_va + 0x08u, 0u);
        dwr(i2s0_va + 0x00u /* I2SCON */, (1u << 31) | 1u); /* CON_RESET=1 | CON_ACTIVE */
    } else {
        dwr(i2s0_va + 0x00u, (1u << 31) | (1u << 4)); /* CON_RESET=1 | TXCH_PAUSE */
    }
}

/* Non-blocking push of interleaved stereo 16-bit PCM frames (32-bit word = L | (R<<16)) into I2S0 TX FIFO. */
uint32_t exy_i2s0_push_frames(const uint32_t *frames, uint32_t count)
{
    if (!(exy_drv_log.st.probe_mask & DRV_BIT_I2S0) || !i2s0_va || !frames)
        return 0u;
    if ((drd(i2s0_va + 0x00u) & ((1u << 31) | 1u)) != ((1u << 31) | 1u))
        dwr(i2s0_va + 0x00u, (1u << 31) | 1u);
    uint32_t pushed = 0u;
    while (pushed < count && !(drd(i2s0_va + 0x00u) & (1u << 8))) {
        dwr(i2s0_va + 0x10u /* I2STXD */, frames[pushed++]);
    }
    return pushed;
}

uint32_t exy_i2s0_tx_count(void)
{
    if (!(exy_drv_log.st.probe_mask & DRV_BIT_I2S0) || !i2s0_va)
        return 0u;
    uint32_t c = drd(i2s0_va + 0x08u /* I2SFIC */) & 0x7fu;
    return (c > 64u) ? 64u : c;
}

int exy_drv_audio_tone(uint16_t hz, uint16_t ms)
{
    (void)hz; (void)ms;
    return 0; /* muted during Wi-Fi bring-up per user request; I2S0/IOAudio2Device unchanged */
    if (!(exy_drv_log.st.probe_mask & DRV_BIT_I2S0) || !i2s0_va)
        return -1;
    if (hz < 100u) hz = 880u;
    if (hz > 8000u) hz = 8000u;
    if (ms > 500u) ms = 500u;
    if (!ms) return 0;
    /* Stream 48 kHz 16-bit stereo triangle wave through I2S0 TX FIFO (keep CON_RESET bit 31 = 1!) */
    uint32_t total = ((uint32_t)ms * 48u);
    uint32_t period = 48000u / (uint32_t)hz;
    if (period < 4u) period = 4u;
    uint32_t half = period >> 1;
    dwr(i2s0_va + 0x00u, (1u << 31) | (1u << 4));
    dwr(i2s0_va + 0x08u /* I2SFIC */, (1u << 15));
    dwr(i2s0_va + 0x08u, 0u);
    /* Play two clear tones (hz, then 1.33*hz) at max speaker gain so user can hear easily */
    exy_drv_set_volume(100u);
    uint32_t t0 = exy_mct_lo(), pushed = 0u;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t cur_hz = (pass == 0) ? (uint32_t)hz : ((uint32_t)hz * 4u) / 3u;
        uint32_t period = 48000u / cur_hz;
        if (period < 4u) period = 4u;
        uint32_t half = period >> 1;
        dwr(i2s0_va + 0x00u, (1u << 31) | (1u << 4));
        dwr(i2s0_va + 0x08u /* I2SFIC */, (1u << 15));
        dwr(i2s0_va + 0x08u, 0u);
        for (uint32_t i = 0; i < total; i++) {
            uint32_t pos = i % period;
            int32_t tri = (pos < half) ? (int32_t)((pos * 28000u) / half) - 14000
                                       : 14000 - (int32_t)(((pos - half) * 28000u) / half);
            uint32_t sample = ((uint32_t)(uint16_t)(int16_t)tri) | ((uint32_t)(uint16_t)(int16_t)tri << 16);
            if (i == 16u)
                dwr(i2s0_va + 0x00u /* I2SCON */, (1u << 31) | 1u);
            uint32_t wait = 0;
            while ((drd(i2s0_va + 0x00u) & (1u << 8)) && ++wait < 4000u)
                exy_udelay(1);
            if (wait >= 4000u) break;
            dwr(i2s0_va + 0x10u /* I2STXD */, sample);
            pushed++;
        }
        dwr(i2s0_va + 0x00u, (1u << 31) | (1u << 4));
        if (pass == 0) exy_udelay(60000);
    }
    uint32_t dt_us = (exy_mct_lo() - t0) / 24u;
    (void)period; (void)half;
    uint16_t irq2 = 0, p1 = 0, s26 = 0, c1 = 0, rst = 0, s24 = 0;
    (void)wm1811_read(0x0731u, &irq2); /* INTERRUPT_STATUS_2: bit 5 = FLL1_LOCK */
    (void)wm1811_read(0x0001u, &p1);
    (void)wm1811_read(0x0026u, &s26);
    (void)wm1811_read(0x0208u, &c1);
    (void)wm1811_read(0x0212u, &rst); /* Rate Status (SR_ERROR) */
    (void)wm1811_read(0x0024u, &s24);
    drv_puts("[TONE] n="); drv_putx(pushed);
    drv_puts(" us="); drv_putx(dt_us);
    drv_puts(" wm="); drv_putx(((uint32_t)irq2 << 16) | s26);
    drv_puts(" p1="); drv_putx(((uint32_t)s24 << 16) | p1);
    drv_puts(" clk="); drv_putx(((uint32_t)c1 << 16) | rst);
    drv_puts("\n");
    return 0;
}

/* --- 8b. Skyworks AAT1290 Camera Flash / Torch (GPJ1[0..2]) ---------------- */

static void flash_init(void)
{
    if (!gpio_l_va)
        return;
    uintptr_t gpj1 = gpio_l_va + 0x260u;
    gpio_dir_out(gpj1, 0, 0); /* host mode (0 = host, 1 = ISP) */
    gpio_dir_out(gpj1, 1, 0); /* FLEN = 0 */
    gpio_dir_out(gpj1, 2, 0); /* ENSET = 0 (off) */
    exy_drv_log.st.torch_lvl = 0u;
    exy_drv_log.st.probe_mask |= DRV_BIT_FLASH;
}

int exy_drv_set_torch(uint8_t level)
{
    if (!(exy_drv_log.st.probe_mask & DRV_BIT_FLASH) || !gpio_l_va)
        return -1;
    uintptr_t gpj1 = gpio_l_va + 0x260u;
    if (level > 16u) level = 16u;
    gpio_dir_out(gpj1, 0, 0); /* host control */
    gpio_dir_out(gpj1, 1, 0); /* FLEN off (movie/torch mode only) */
    gpio_dir_out(gpj1, 2, 0); /* reset pulse train (t_OFF > 500 us) */
    exy_udelay(600);
    if (level > 0u) {
        /* AAT1290 1-wire ENSET: 1 pulse = max movie current, 16 pulses = min */
        uint32_t pulses = 17u - (uint32_t)level;
        for (uint32_t i = 0; i < pulses; i++) {
            gpio_dir_out(gpj1, 2, 1);
            exy_udelay(2);
            if (i + 1u < pulses) {
                gpio_dir_out(gpj1, 2, 0);
                exy_udelay(2);
            }
        }
    }
    exy_drv_log.st.torch_lvl = level;
    return 0;
}

/* --- 8c. Exynos 4412 TMU (CPU Die Temp) & ADC (AP Thermistor) -------------- */

static void tmu_adc_init(void)
{
    if (tmu_va) {
        /* Unmask TMU APB clock in CMU_RIGHTBUS gate_ip_perir (0x10038960 bit 17) */
        uintptr_t cmu_perir = ml_io_map(0x10038000u, 0x1000u);
        if (cmu_perir)
            dwr(cmu_perir + 0x960u, drd(cmu_perir + 0x960u) | (1u << 17));
        exy_udelay(100);
        uint32_t orig_ctrl = drd(tmu_va + 0x20u /* TMU_CONTROL */);
        /* Reload efuse TRIMINFO unconditionally (Exynos 4412 TRIMINFO_CON=1) */
        dwr(tmu_va + 0x14u /* TRIMINFO_CON */, 1u);
        exy_udelay(200);
        uint32_t trim = drd(tmu_va + 0x00u /* TRIMINFO */);
        tmu_trim25 = (uint8_t)(trim & 0xffu);
        if (tmu_trim25 < 40u || tmu_trim25 > 100u)
            tmu_trim25 = 55u; /* Exynos 4412 nominal efuse fallback */
        /* Exynos 4412 TMU_CONTROL (per U-Boot exynos-tmu.c): 0x10608802 (VREF=16, MUX=6,
         * TRIP_MODE=4 [15:13], BUF_SLOPE=8, bit1=1), then set CORE_EN=1 (0x10608803). */
        dwr(tmu_va + 0x50u /* THRESHOLD_TEMP_RISE */, 0xffffffffu);
        dwr(tmu_va + 0x70u /* INTEN */, 0u);
        dwr(tmu_va + 0x78u /* INTCLEAR */, 0x01110111u);
        dwr(tmu_va + 0x20u /* TMU_CONTROL */, 0x10608802u);
        exy_udelay(200);
        dwr(tmu_va + 0x20u /* TMU_CONTROL */, 0x10608803u);
        uint32_t raw = 0u;
        for (uint32_t i = 0; i < 50u; i++) {
            exy_udelay(1000);
            raw = drd(tmu_va + 0x40u /* CURRENT_TEMP */) & 0xffu;
            if (raw >= 25u && raw < 255u)
                break;
        }
        uint32_t st = drd(tmu_va + 0x28u /* TMU_STATUS */);
        int32_t tc = (int32_t)raw - (int32_t)tmu_trim25 + 25;
        if (raw >= 25u && raw < 255u && tc >= 0 && tc <= 125) {
            exy_drv_log.st.cpu_temp_c = (int16_t)tc;
            exy_drv_log.st.probe_mask |= DRV_BIT_TMU;
        }
        drv_puts("[TMU] orig="); drv_putx(orig_ctrl);
        drv_puts(" st="); drv_putx(st);
        drv_puts(" trim="); drv_putx(trim);
        drv_puts(" raw="); drv_putx(raw);
        drv_puts(" tc="); drv_putx((uint32_t)exy_drv_log.st.cpu_temp_c);
        drv_puts("\n");
    }
    if (adc_va) {
        /* Exynos ADC ch1 (AP thermistor): 12-bit, prescaler=65, start conversion */
        dwr(adc_va + 0x1cu /* ADCMUX */, 1u);
        dwr(adc_va + 0x00u /* ADCCON */, (1u << 16) | (1u << 14) | (65u << 6) | 1u);
        for (uint32_t i = 0; i < 2000u; i++) {
            if (drd(adc_va + 0x00u) & (1u << 15)) {
                exy_drv_log.st.adc_ap = (uint16_t)(drd(adc_va + 0x0cu /* ADCDAT0 */) & 0xfffu);
                exy_drv_log.st.probe_mask |= DRV_BIT_ADC;
                break;
            }
            exy_udelay(5);
        }
    }
}

static void tmu_adc_poll(void)
{
    if (tmu_va) {
        uint32_t raw = drd(tmu_va + 0x40u) & 0xffu;
        if (raw >= 25u && raw < 255u) {
            int32_t tc = (int32_t)raw - (int32_t)tmu_trim25 + 25;
            if (tc >= 0 && tc <= 125) {
                exy_drv_log.st.cpu_temp_c = (int16_t)tc;
                exy_drv_log.st.probe_mask |= DRV_BIT_TMU;
            }
        }
    }
    if ((exy_drv_log.st.probe_mask & DRV_BIT_ADC) && adc_va) {
        dwr(adc_va + 0x1cu, 1u);
        dwr(adc_va + 0x00u, (1u << 16) | (1u << 14) | (65u << 6) | 1u);
        for (uint32_t i = 0; i < 400u; i++) {
            if (drd(adc_va + 0x00u) & (1u << 15)) {
                exy_drv_log.st.adc_ap = (uint16_t)(drd(adc_va + 0x0cu) & 0xfffu);
                break;
            }
            exy_udelay(2);
        }
    }
}

/* --- 9. Storage / Connectivity Drivers (SDHCI2, BCM4334 Wi-Fi/BT, DWC2) ---- */

static void wifi_bt_probe(void)
{
    /* BCM4334 Wi-Fi on SDHCI3 (0x12540000): ungate SDMMC3 clock, release WLAN_EN (GPJ0[0]=1) + GPK3[0..6] */
    if (gpio_l_va && gpio_r_va && sd3_va) {
        uintptr_t cmu_fsys = ml_io_map(0x1003c000u, 0x1000u);
        if (cmu_fsys) {
            /* Enable SDMMC3 bus clock (gate_ip_fsys bit 8) + SCLK_MMC3 = MPLL(800MHz)/8/2 = 50 MHz (div_fsys1 bits 31:16) */
            dwr(cmu_fsys + 0x940u /* gate_ip_fsys */, drd(cmu_fsys + 0x940u) | (1u << 8));
            dwr(cmu_fsys + 0x240u /* src_fsys */, (drd(cmu_fsys + 0x240u) & ~(0xfu << 12)) | (6u << 12));
            dwr(cmu_fsys + 0x340u /* src_mask_fsys */, drd(cmu_fsys + 0x340u) | (1u << 12));
            dwr(cmu_fsys + 0x544u /* div_fsys1 */, (drd(cmu_fsys + 0x544u) & 0x0000ffffu) | (1u << 24) | (7u << 16));
        }
        /* Reset-pulse WLAN_EN (GPJ0[0]: low 10ms -> high 120ms for BCM4334 POR + 32kHz LPO) */
        gpio_dir_out(gpio_l_va + 0x240u /* GPJ0 */, 0, 0);
        exy_udelay(10000);
        gpio_dir_out(gpio_l_va + 0x240u /* GPJ0 */, 0, 1); /* WLAN_EN high */
        /* GPK3[0..6] = func 2 (SD3_CLK, SD3_CMD, SD3_CD, SD3_DATA0..3) + pull-up on CMD/DATA */
        dwr(gpio_r_va + 0x0a0u, (drd(gpio_r_va + 0x0a0u) & ~0x0fffffffu) | 0x02222222u);
        dwr(gpio_r_va + 0x0a8u, (drd(gpio_r_va + 0x0a8u) & ~0x00003fffu) | 0x00003ffcu); /* pull-up pins 1..6 */
        exy_udelay(120000);
        uint32_t cap = drd(sd3_va + 0x40u);
        exy_drv_log.st.sd3_present = drd(sd3_va + 0x24u);
        if (cap != 0u && cap != ~0u) {
            exy_drv_log.st.probe_mask |= DRV_BIT_WIFI;
            extern int exy_wifi_sdio_init(void);
            extern uint32_t exy_wifi_get_ocr(void);
            (void)exy_wifi_sdio_init();
            exy_drv_log.st.sd3_ocr = exy_wifi_get_ocr();
        }
    }

    /* BCM4334 Bluetooth on UART0 (0x13800000): BT_EN (GPL0[6]=1) + BT_WAKE (GPX3[1]=1) + GPA0[0..3] */
    if (gpio_l_va && gpio_r_va && uart0_va) {
        dwr(gpio_l_va + 0x000u /* GPA0 */, (drd(gpio_l_va + 0x000u) & ~0x0000ffffu) | 0x00002222u);
        gpio_dir_out(gpio_r_va + 0x0c0u /* GPL0 */, 6, 0);
        exy_udelay(2000);
        gpio_dir_out(gpio_r_va + 0x0c0u /* GPL0 */, 6, 1); /* BT_EN high */
        gpio_dir_out(gpio_r_va + 0xc60u /* GPX3 */, 1, 1); /* BT_WAKE high */
        exy_udelay(30000);
        /* Configure UART0 115200 8N1 using U-Boot's UART2 baud ratio (or 100MHz default) */
        uint32_t ubrdiv = uart2_va ? (drd(uart2_va + 0x28u) & 0xffffu) : 53u;
        uint32_t ufrac  = uart2_va ? (drd(uart2_va + 0x2cu) & 0xffffu) : 4u;
        if (!ubrdiv || ubrdiv > 1000u) { ubrdiv = 53u; ufrac = 4u; }
        dwr(uart0_va + 0x00u /* ULCON */, 0x03u); /* 8N1 */
        dwr(uart0_va + 0x04u /* UCON */,  0x05u); /* TX/RX polling */
        dwr(uart0_va + 0x08u /* UFCON */, 0x07u); /* FIFO enable + reset */
        dwr(uart0_va + 0x0cu /* UMCON */, 0x10u); /* AFC auto flow control */
        dwr(uart0_va + 0x28u /* UBRDIV */, ubrdiv);
        dwr(uart0_va + 0x2cu /* UFRACVAL */, ufrac);
        exy_udelay(5000);
        /* Drain RX (bounded so floating/noisy UART0 RX never hangs boot) + send HCI_Reset (01 03 0C 00) */
        for (uint32_t d = 0u; d < 64u && (drd(uart0_va + 0x10u /* UTRSTAT */) & 1u); d++)
            (void)drd(uart0_va + 0x24u);
        static const uint8_t hci_rst[4] = { 0x01u, 0x03u, 0x0cu, 0x00u };
        for (int i = 0; i < 4; i++) {
            for (uint32_t w = 0; w < 2000u && !(drd(uart0_va + 0x10u) & 2u); w++) exy_udelay(5);
            dwr(uart0_va + 0x20u /* UTXH */, hci_rst[i]);
        }
        uint8_t rx[8] = { 0 };
        int got = 0;
        for (uint32_t t = 0; t < 4000u && got < 7; t++) {
            if (drd(uart0_va + 0x10u) & 1u)
                rx[got++] = (uint8_t)(drd(uart0_va + 0x24u /* URXH */) & 0xffu);
            else
                exy_udelay(10);
        }
        if (got >= 4 && rx[0] == 0x04u && rx[1] == 0x0eu) {
            exy_drv_log.st.bt_hci_ok = 1u;
            exy_drv_log.st.probe_mask |= DRV_BIT_BT;
        } else if (drd(uart0_va + 0x00u) == 0x03u) {
            exy_drv_log.st.probe_mask |= DRV_BIT_BT;
        }
        /* Power down unused BT radio after probe to save heat/battery */
        gpio_dir_out(gpio_r_va + 0xc60u /* GPX3 */, 1, 0); /* BT_WAKE low */
        gpio_dir_out(gpio_r_va + 0x0c0u /* GPL0 */, 6, 0); /* BT_EN low */
    }
}

/* --- Minimal DWC2 USB 2.0 EP0 Remote-Fastboot Gadget (VID 18d1, PID 4ee8) -- */
#define USBPHY_PA         0x125b0000u
#define k_iosleep_drv     ((void (*)(uint32_t))0x80328fd1)
#define k_thread_drv      ((int  (*)(void (*)(void *, int), void *, void **))0x80042c15)

static void dwc2_ep0_arm_out(void)
{
    dwr(otg_va + 0xb08u /* DOEPINT0 */, 0xffu);
    dwr(otg_va + 0xb10u /* DOEPTSIZ0 */, (3u << 29) | (1u << 19) | 64u);
    dwr(otg_va + 0xb00u /* DOEPCTL0 */,  (1u << 31) | (1u << 26)); /* EPEna | CNAK */
}

static void dwc2_ep0_tx(const uint8_t *buf, uint32_t len)
{
    if (len > 64u) len = 64u;
    if (drd(otg_va + 0x900u /* DIEPCTL0 */) & (1u << 31)) {
        dwr(otg_va + 0x900u, (1u << 30) | (1u << 27)); /* EPDis | SNAK */
        for (uint32_t i = 0; i < 200u && !(drd(otg_va + 0x908u) & (1u << 1)); i++)
            exy_udelay(1);
        dwr(otg_va + 0x10u /* GRSTCTL */, (1u << 5)); /* TxFFlsh FIFO 0 */
        for (uint32_t i = 0; i < 200u && (drd(otg_va + 0x10u) & (1u << 5)); i++)
            exy_udelay(1);
    }
    dwr(otg_va + 0x908u /* DIEPINT0 */, 0xffu);
    dwr(otg_va + 0x910u /* DIEPTSIZ0 */, (1u << 19) | len);
    dwr(otg_va + 0x900u /* DIEPCTL0 */,  (1u << 31) | (1u << 26)); /* EPEna | CNAK, MPS=64 */
    uint32_t words = (len + 3u) >> 2;
    for (uint32_t w = 0; w < words; w++) {
        uint32_t v = 0u;
        for (uint32_t b = 0; b < 4u && (w * 4u + b) < len; b++)
            v |= ((uint32_t)buf[w * 4u + b]) << (b * 8u);
        dwr(otg_va + 0x1000u /* EP0 FIFO */, v);
    }
}

static void dwc2_handle_setup(const uint8_t s[8])
{
    static const uint8_t desc_dev[18] = {
        18, 1, 0x00, 0x02, 0xff, 0x00, 0x00, 64,
        0xd1, 0x18, 0xe8, 0x4e, 0x00, 0x01, 1, 2, 0, 1
    };
    static const uint8_t desc_cfg[18] = {
        9, 2, 18, 0, 1, 1, 0, 0xc0, 250,
        9, 4, 0, 0, 0, 0xff, 0x42, 0x01, 0
    };
    static const uint8_t desc_str0[4] = { 4, 3, 0x09, 0x04 };
    static const uint8_t desc_str1[14] = { 14, 3, 'E',0,'x',0,'y',0,'X',0,'N',0,'U',0 };
    static const uint8_t zero2[2] = { 0, 0 };

    uint8_t bm = s[0], req = s[1];
    uint16_t val = (uint16_t)s[2] | ((uint16_t)s[3] << 8);
    uint16_t idx = (uint16_t)s[4] | ((uint16_t)s[5] << 8);
    uint16_t len = (uint16_t)s[6] | ((uint16_t)s[7] << 8);

    if (bm == 0x80u && req == 0x06u) { /* GET_DESCRIPTOR */
        const uint8_t *p = 0; uint32_t plen = 0u;
        uint8_t dtype = (uint8_t)(val >> 8), didx = (uint8_t)(val & 0xffu);
        if (dtype == 1u) { p = desc_dev; plen = sizeof(desc_dev); }
        else if (dtype == 2u) { p = desc_cfg; plen = sizeof(desc_cfg); }
        else if (dtype == 3u) {
            p = (didx == 0u) ? desc_str0 : desc_str1;
            plen = (didx == 0u) ? sizeof(desc_str0) : sizeof(desc_str1);
        }
        if (p) {
            if (len < plen) plen = len;
            dwc2_ep0_tx(p, plen);
            dwc2_ep0_arm_out();
            return;
        }
        dwr(otg_va + 0x900u, drd(otg_va + 0x900u) | (1u << 21)); /* STALL */
        dwc2_ep0_arm_out();
        return;
    }
    if (bm == 0x00u && req == 0x05u) { /* SET_ADDRESS */
        dwr(otg_va + 0x800u /* DCFG */, (drd(otg_va + 0x800u) & ~(0x7fu << 4)) | ((uint32_t)(val & 0x7fu) << 4));
        dwc2_ep0_tx(0, 0u);
        dwc2_ep0_arm_out();
        return;
    }
    if (bm == 0x00u && req == 0x09u) { /* SET_CONFIGURATION */
        dwc2_ep0_tx(0, 0u);
        dwc2_ep0_arm_out();
        return;
    }
    if ((bm & 0x80u) && req == 0x00u) { /* GET_STATUS */
        dwc2_ep0_tx(zero2, (len < 2u) ? len : 2u);
        dwc2_ep0_arm_out();
        return;
    }
    if (bm == 0x40u && req == 0xfcu) { /* Vendor synthetic button press (val=bitmask) */
        extern void exy_synth_btn_inject(uint32_t mask);
        exy_synth_btn_inject(val);
        dwc2_ep0_tx(0, 0u);
        dwc2_ep0_arm_out();
        return;
    }
    if (bm == 0x40u && req == 0xfdu) { /* Vendor synthetic touch (val=x|(down<<15), idx=y) */
        extern void exy_synth_touch_inject(uint16_t x, uint16_t y, uint32_t down);
        exy_synth_touch_inject(val & 0x7fffu, idx, (val >> 15) & 1u);
        dwc2_ep0_tx(0, 0u);
        dwc2_ep0_arm_out();
        return;
    }
    if (bm == 0x40u && req == 0xfbu && val == 0x1337u) { /* Vendor Remote Fastboot! */
        dwc2_ep0_tx(0, 0u);
        exy_udelay(30000);
        exy_clean_line(&exy_drv_log);
        exy_clean_line(&exy_drv_log.st.cpu_temp_c);
        extern void exy_reboot_fastboot(void);
        exy_reboot_fastboot();
        return;
    }
    dwr(otg_va + 0x900u, drd(otg_va + 0x900u) | (1u << 21)); /* STALL unknown */
    dwc2_ep0_arm_out();
}

static void exy_usb_poll_thread(void *param, int wait_res)
{
    (void)param; (void)wait_res;
    uint8_t setup[8] = { 0 };
    int have_setup = 0;
    uint32_t idle = 0u, setup_age = 0u;
    for (;;) {
        extern volatile uint32_t exy_usb_hb;
        exy_usb_hb++;
        uint32_t gint = drd(otg_va + 0x14u /* GINTSTS */);
        int busy = 0;
        if (gint & (1u << 12)) { /* USBRst */
            have_setup = 0;
            setup_age = 0u;
            dwr(otg_va + 0x800u, drd(otg_va + 0x800u) & ~(0x7fu << 4));
            dwr(otg_va + 0x908u /* DIEPINT0 */, 0xffu);
            dwr(otg_va + 0xb08u /* DOEPINT0 */, 0xffu);
            dwr(otg_va + 0x14u, (1u << 12));
            dwc2_ep0_arm_out();
            exy_drv_log.st.otg_resets++;
            busy = 1;
        }
        if (gint & (1u << 13)) { /* EnumDone */
            have_setup = 0;
            setup_age = 0u;
            dwr(otg_va + 0x900u, drd(otg_va + 0x900u) & ~3u); /* MPS = 64B */
            dwr(otg_va + 0x14u, (1u << 13));
            dwc2_ep0_arm_out();
            busy = 1;
        }
        for (uint32_t rx_lim = 0; rx_lim < 32u && (drd(otg_va + 0x14u) & (1u << 4)); rx_lim++) { /* RXFLvl */
            busy = 1;
            uint32_t st = drd(otg_va + 0x20u /* GRXSTSP */);
            uint32_t bcnt = (st >> 4) & 0x7ffu;
            uint32_t psts = (st >> 17) & 0xfu;
            if (psts == 6u && bcnt == 8u) { /* SETUP packet */
                uint32_t w0 = drd(otg_va + 0x1000u);
                uint32_t w1 = drd(otg_va + 0x1000u);
                setup[0] = (uint8_t)w0;       setup[1] = (uint8_t)(w0 >> 8);
                setup[2] = (uint8_t)(w0 >> 16); setup[3] = (uint8_t)(w0 >> 24);
                setup[4] = (uint8_t)w1;       setup[5] = (uint8_t)(w1 >> 8);
                setup[6] = (uint8_t)(w1 >> 16); setup[7] = (uint8_t)(w1 >> 24);
                have_setup = 1;
                setup_age = 0u;
            } else if (psts == 4u && have_setup) { /* SETUP stage complete */
                have_setup = 0;
                setup_age = 0u;
                dwc2_handle_setup(setup);
            } else {
                if (bcnt > 0u) {
                    uint32_t words = (bcnt + 3u) >> 2;
                    while (words--) (void)drd(otg_va + 0x1000u);
                }
                if (psts == 3u /* OUT_COMP */ || psts == 4u)
                    dwc2_ep0_arm_out();
            }
        }
        if (have_setup && ++setup_age > 10u) {
            have_setup = 0;
            setup_age = 0u;
            dwc2_ep0_arm_out();
        }
        /* Keep EP0 OUT armed when idle so future remote-fastboot SETUP packets are never NAKed. */
        if (!have_setup && !(drd(otg_va + 0xb00u) & (1u << 31)))
            dwc2_ep0_arm_out();
        if (busy) {
            idle = 0u;
            k_iosleep_drv(1u);
        } else {
            if (idle < 200u) idle++;
            k_iosleep_drv(idle >= 200u ? 100u : (idle > 50u ? 20u : 2u));
        }
    }
}

static void exy_usb_gadget_init(void)
{
    if (otg_gadget_started || !otg_va)
        return;
    /* 1. Ungate Exynos 4412 USBOTG/USBHOST bus clocks */
    uintptr_t cmu_fsys = ml_io_map(0x1003c000u, 0x1000u);
    if (cmu_fsys)
        dwr(cmu_fsys + 0x940u, drd(cmu_fsys + 0x940u) | (1u << 12) | (1u << 13));
    /* 2. Power on VUOTG_3.0V (LDO12), VHSIC_1.0V (LDO15: 25mV step -> 0xc8=1.0V), VHSIC_1.8V (LDO16) + MAX77693 SAFEOUT1 & MUIC USB path */
    exy_pmic_write_reg(0x4bu /* LDO12 */, 0xecu);
    exy_pmic_write_reg(0x4eu /* LDO15 */, 0xc8u);
    exy_pmic_write_reg(0x4fu /* LDO16 */, 0xd4u);
    struct bb_i2c m77 = bus_m77();
    uint8_t so = 0u;
    if (bb_read_reg(&m77, 0x66u, 0xc6u, &so, 1) == 0) {
        uint8_t b[2] = { 0xc6u, (uint8_t)(so | (1u << 6)) };
        (void)bb_write(&m77, 0x66u, b, 2);
    }
    uint8_t muic_usb[2] = { 0x0cu /* CONTROL1 */, 0x09u /* DN1DP2 */ };
    (void)bb_write(&m77, 0x25u, muic_usb, 2);
    /* Wait for LDO12/15/16 + SAFEOUT1 rails to settle on cold standalone boot */
    exy_udelay(5000);
    /* 3. Enable Exynos 4412 USB OTG PHY0 (24 MHz) */
    if (pmu_drv_va)
        dwr(pmu_drv_va + 0x704u /* USB_PHY_CTRL */, drd(pmu_drv_va + 0x704u) | 1u);
    uintptr_t phy = ml_io_map(USBPHY_PA, 0x1000u);
    if (phy) {
        dwr(phy + 0x00u /* PHYPWR */, drd(phy + 0x00u) & ~((1u << 5) | (1u << 4) | (1u << 3) | 1u));
        dwr(phy + 0x04u /* PHYCLK */, (drd(phy + 0x04u) & ~((1u << 4) | (1u << 3) | 7u)) | 5u);
        dwr(phy + 0x08u /* RSTCON */, (drd(phy + 0x08u) & ~6u) | 1u);
        exy_udelay(20);
        dwr(phy + 0x08u, drd(phy + 0x08u) & ~7u);
        exy_udelay(1000);
    }
    /* 4. Configure Synopsys DWC2 in Slave (PIO) HS Device mode + soft-connect */
    uint32_t id = 0u;
    for (int t = 0; t < 10; t++) {
        id = drd(otg_va + 0x40u /* GSNPSID */);
        if ((id >> 16) == 0x4f54u)
            break;
        exy_udelay(1000);
    }
    exy_drv_log.st.otg_id = id;
    if ((id >> 16) != 0x4f54u)
        return;
    exy_drv_log.st.probe_mask |= DRV_BIT_OTG;
    dwr(otg_va + 0x10u /* GRSTCTL */, 1u);
    for (uint32_t i = 0; i < 10000u && (drd(otg_va + 0x10u) & 1u); i++) exy_udelay(1);
    exy_udelay(1000);
    /* Match U-Boot dwc2_udc_otg.c: ForceDev | NPTxRewind | TRDT=5 | 16-bit UTMI+ | ToutCal=7 */
    dwr(otg_va + 0x0cu /* GUSBCFG */, (1u << 30) | (1u << 14) | (5u << 10) | (1u << 3) | 7u);
    dwr(otg_va + 0x08u /* GAHBCFG */, 0u); /* Slave PIO mode */
    dwr(otg_va + 0x804u /* DCTL */, drd(otg_va + 0x804u) | (1u << 1)); /* Soft disconnect first */
    exy_udelay(30000);
    dwr(otg_va + 0x800u /* DCFG */, (1u << 18)); /* EP_MISS_CNT=1 | High Speed 2.0 */
    dwr(otg_va + 0x818u /* DAINTMSK */, (1u << 16) | 1u);
    dwr(otg_va + 0x810u /* DIEPMSK */,  0x0du);
    dwr(otg_va + 0x814u /* DOEPMSK */,  0x0du);
    dwr(otg_va + 0x24u /* GRXFSIZ */, 256u);
    dwr(otg_va + 0x28u /* GNPTXFSIZ */, (64u << 16) | 256u);
    dwr(otg_va + 0x10u, (0x10u << 6) | (1u << 5) | (1u << 4)); /* Flush TX+RX FIFOs */
    for (uint32_t i = 0; i < 1000u && (drd(otg_va + 0x10u) & 0x30u); i++) exy_udelay(1);
    dwr(otg_va + 0x18u /* GINTMSK */, (1u << 4) | (1u << 12) | (1u << 13));
    dwr(otg_va + 0x14u /* GINTSTS */, 0xffffffffu);
    dwr(otg_va + 0x908u /* DIEPINT0 */, 0xffu);
    dwr(otg_va + 0xb08u /* DOEPINT0 */, 0xffu);
    dwc2_ep0_arm_out();
    dwr(otg_va + 0x804u /* DCTL */, drd(otg_va + 0x804u) & ~(1u << 1)); /* Clear SftDiscon -> enumerate 18d1:4ee8! */

    void *th = 0;
    if (k_thread_drv(exy_usb_poll_thread, 0, &th) == 0)
        otg_gadget_started = 1;
}

void exy_drv_otg_check(void)
{
    /* Handled asynchronously by exy_usb_poll_thread (18d1:4ee8 remote fastboot). */
}

static void bus_probes_init(void)
{
    /* microSD: enable VTF_2.8V (LDO21) and read GPX3[4] CD (active-low) + SDHCI2 PRESENT_STATE */
    exy_pmic_write_reg(MAX77686_LDO21CTRL1, 0xe8u);
    if (gpio_r_va) {
        gpio_dir_in(gpio_r_va + 0xc60u /* GPX3 */, 4);
        gpio_set_pud(gpio_r_va + 0xc60u, 4, 3u);
        exy_drv_log.st.sd_present = (uint8_t)(!gpio_get(gpio_r_va + 0xc60u, 4));
    }
    if (sd2_va) {
        uint32_t cap = drd(sd2_va + 0x40u /* CAPABILITIES */);
        if (cap != 0u && cap != ~0u)
            exy_drv_log.st.probe_mask |= DRV_BIT_SD2;
    }
    wifi_bt_probe();
    exy_usb_gadget_init();
}

/* --- Public Entry Points (called from gpu.c) ------------------------------- */

void exy_drivers_init(void *parent_service)
{
    if (drv_inited)
        return;
    drv_inited = 1;
    exy_drv_log.magic = 0x64727673u; /* "drvs" */
    exy_drv_log.init_done = 0x101u;
    exy_clean_line(&exy_drv_log);
    exy_drv_log.st.brightness = 180u;
    exy_drv_log.st.vpll_con0 = mali_vpll_con0;

    dsim_va    = ml_io_map(DSIM0_PA, 0x1000);
    i2c1_va    = ml_io_map(I2C1_PA, 0x1000);
    i2c4_va    = ml_io_map(I2C4_PA, 0x1000);
    pwm_va     = ml_io_map(PWM_PA, 0x1000);
    audss_va   = ml_io_map(AUDSS_PA, 0x1000);
    i2s0_va    = ml_io_map(I2S0_PA, 0x1000);
    gpz_va     = ml_io_map(GPZ_PA, 0x1000);
    pmu_drv_va = ml_io_map(PMU_PA, 0x1000);
    tmu_va     = ml_io_map(TMU_PA, 0x1000);
    adc_va     = ml_io_map(ADC_PA, 0x1000);
    uart0_va   = ml_io_map(UART0_PA, 0x1000);
    uart2_va   = ml_io_map(UART2_PA, 0x1000);
    sd2_va     = ml_io_map(SDHCI2_PA, 0x1000);
    sd3_va     = ml_io_map(SDHCI3_PA, 0x1000);
    otg_va     = ml_io_map(OTG_PA, 0x2000);

    exy_drv_log.init_done = 0x102u;
    exy_clean_line(&exy_drv_log);

    if (dsim_va && (drd(dsim_va + 0x00u) & (1u << 31))) /* DSIM PLL_STABLE */
        exy_drv_log.st.probe_mask |= DRV_BIT_DSIM;

    /* 1. Turn off U-Boot's blinding blue LED immediately */
    led_init();
    exy_drv_log.init_done = 0x103u;
    exy_clean_line(&exy_drv_log);

    /* 2. Init FG & MAX77693 bit-bang buses + native Apple IOPMPowerSource */
    struct bb_i2c fg = bus_fg(); bb_init(&fg);
    struct bb_i2c m77 = bus_m77(); bb_init(&m77);
    exy_drv_log.init_done = 0x104u;
    exy_clean_line(&exy_drv_log);
    if (parent_service) {
        pm_power_src = pm_ps_create();
        exy_drv_log.init_done = 0x105u;
        exy_clean_line(&exy_drv_log);
        if (pm_power_src) {
            if (ioservice_attach(pm_power_src, parent_service)) {
                exy_drv_log.st.probe_mask |= DRV_BIT_PMPS;
                exy_drv_log.init_done = 0x106u;
                exy_clean_line(&exy_drv_log);
                exy_drv_power_sync();
                exy_drv_log.init_done = 0x107u;
                exy_clean_line(&exy_drv_log);
                ioservice_register(pm_power_src, 0);
                exy_drv_log.init_done = 0x108u;
                exy_clean_line(&exy_drv_log);
                pm_poll_tc = tc_alloc(pm_poll_tick, 0);
                if (pm_poll_tc) {
                    uint64_t dl = 0;
                    clk_int_deadline(2000u, 1000000u, &dl);
                    tc_enter_delayed(pm_poll_tc, dl);
                }
            }
        }
    }
    if (!pm_power_src)
        exy_drv_power_sync();

    /* 3. Haptic, Sensors, Audio Codec+I2S0, Flash/Torch, TMU/ADC, Wi-Fi/BT/OTG/SD, RTC */
    exy_drv_log.init_done = 0x109u; exy_clean_line(&exy_drv_log);
    haptic_init();
    exy_drv_log.init_done = 0x10au; exy_clean_line(&exy_drv_log);
    sensors_init();
    exy_drv_log.init_done = 0x10cu; exy_clean_line(&exy_drv_log);
    extern void exy_audio_register(void *parent_service);
    extern void exy_wifi_set_parent(void *parent_service);
    audio_init();
    exy_audio_register(parent_service);
    exy_wifi_set_parent(parent_service);
    flash_init();
    tmu_adc_init();
    exy_drv_log.init_done = 0x10du; exy_clean_line(&exy_drv_log);
    bus_probes_init();
    exy_drv_log.init_done = 0x10eu; exy_clean_line(&exy_drv_log);
    (void)exy_rtc_read_epoch();

    exy_drv_log.init_done = 1u;
    exy_clean_line(&exy_drv_log);
    exy_clean_line(&exy_drv_log.st.accel[0]);
    exy_clean_line(&exy_drv_log.st.vpll_con0);
    exy_clean_line(&exy_drv_log.st.cpu_temp_c);
}

void exy_drv_poll(struct exy_drv_status *out)
{
    tkey_poll();
    (void)exy_rtc_read_epoch();
    exy_drv_log.st.vpll_con0 = mali_vpll_con0;
    if (gpio_r_va) {
        exy_drv_log.st.sd_present = (uint8_t)(!gpio_get(gpio_r_va + 0xc60u, 4));
        exy_drv_log.st.hp_jack = (uint8_t)(!gpio_get(gpio_r_va + 0xc60u, 0));
    }
    if (out) {
        const uint8_t *src = (const uint8_t *)&exy_drv_log.st;
        uint8_t *dst = (uint8_t *)out;
        for (uint32_t i = 0; i < (uint32_t)sizeof(exy_drv_log.st); i++)
            dst[i] = src[i];
    }
}

uint32_t exy_drv_poll_sz(void *out, uint32_t max_sz)
{
    exy_drv_poll(0);
    uint32_t n = (uint32_t)sizeof(exy_drv_log.st);
    if (max_sz < n) n = max_sz;
    const uint8_t *src = (const uint8_t *)&exy_drv_log.st;
    uint8_t *dst = (uint8_t *)out;
    for (uint32_t i = 0; i < n; i++)
        dst[i] = src[i];
    return n;
}

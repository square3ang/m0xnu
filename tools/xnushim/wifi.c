/*
 * Broadcom BCM4334 SDIO Wi-Fi driver for Galaxy S3 (Exynos 4412 SDHCI3 @ 0x12540000)
 * bridged into XNU BSD networking (en0 via ifnet_allocate / ifnet_attach / ether_*).
 *
 * Firmware (brcmfmac4334-sdio.bin) and GT-I9300 NVRAM are streamed from userspace
 * via IMGSGXSharedUserClient selector 22 (MALI_CMD_WIFI_OP), keeping DRV_AREA compact.
 */
#include <stdint.h>
#include "wifi_wpa.h"

typedef uintptr_t (*ml_io_map_t)(uintptr_t pa, uint32_t size);
#define ml_io_map ((ml_io_map_t)0x800c0bf5)

/* XNU BSD networking KPIs (kernelcache.n94.macho, Thumb) */
#define k_ifnet_allocate      ((int (*)(const void *, void **))0x80141a89)
#define k_ifnet_attach        ((int (*)(void *, const void *))0x80127be5)
#define k_ifnet_input         ((int (*)(void *, void *, const void *))0x80125755)
#define k_ifnet_set_flags     ((int (*)(void *, uint16_t, uint16_t))0x801421b5)
#define k_ifnet_set_mtu       ((int (*)(void *, uint32_t))0x80142c95)
#define k_ifnet_set_addrlen   ((int (*)(void *, uint8_t))0x80142cbd)
#define k_ifnet_set_hdrlen    ((int (*)(void *, uint8_t))0x80142cd9)
#define k_ifnet_set_baudrate  ((int (*)(void *, uint64_t))0x80142d11)
#define k_ifnet_set_lladdr    ((int (*)(void *, const void *, uint32_t))0x80143e95)
#define k_ether_add_proto     ((int (*)(void *, uint32_t, const void *, uint32_t))0x8013050d)
#define k_ether_del_proto     ((int (*)(void *, uint32_t))0x80130499)
#define k_ether_demux         ((int (*)(void *, void *, char *, uint32_t *))0x801307a1)
#define k_ether_frameout      ((int (*)(void *, void **, const void *, const char *, const char *, uint32_t *, uint32_t *))0x80130a19)
#define k_ether_ioctl         ((int (*)(void *, uint32_t, void *))0x80130b4d)
#define k_ether_check_multi   ((int (*)(void *, const void *))0x80130b29)
#define k_mbuf_allocpacket    ((int (*)(int, uint32_t, unsigned int *, void **))0x802bb109)
#define k_mbuf_copydata       ((int (*)(void *, uint32_t, uint32_t, void *))0x802baaad)
#define k_mbuf_freem          ((void (*)(void *))0x802ba901)
#define k_mbuf_data           ((void *(*)(void *))0x802ba009)
#define k_mbuf_pkthdr_len     ((uint32_t (*)(void *))0x802bac1d)
#define k_mbuf_pkthdr_setlen  ((void (*)(void *, uint32_t))0x802bac21)
#define k_mbuf_pkthdr_setrcvif ((void (*)(void *, void *))0x802bac31)
#define k_mbuf_pkthdr_setheader ((void (*)(void *, void *))0x802bac3d)
#define k_mbuf_adj            ((void (*)(void *, int))0x802baa09)
#define k_mbuf_setlen         ((void (*)(void *, uint32_t))0x802bab4d)
#define k_bpf_attach          ((int (*)(void *, uint32_t, uint32_t, void *, void *))0x80117499)
#define k_iosleep             ((void (*)(uint32_t))0x80328fd1)
#define k_thread_start        ((int (*)(void (*)(void *, int), void *, void **))0x80042c15)
#define k_copyin              ((int (*)(uint32_t, void *, uint32_t))0x800c86b4)
#define k_copyout             ((int (*)(const void *, uint32_t, uint32_t))0x800c87a4)
#define k_kev_post_msg        ((int (*)(void *))0x80293531)
#define k_osmeta_alloc        ((void *(*)(const char *))0x8030656d)
#define k_osnum_u64           ((void *(*)(uint64_t, uint32_t))0x80316e25)
#define k_osstr_cstr          ((void *(*)(const char *))0x8031a9a1)
#define k_osdata_bytes        ((void *(*)(const void *, uint32_t))0x803097ed)
#define k_iosvc_init          ((int (*)(void *, void *))0x8033240d)
#define k_iosvc_attach        ((int (*)(void *, void *))0x803326dd)
#define k_iosvc_reg           ((void (*)(void *, uint32_t))0x80333001)
#define k_ioreg_set_prop      ((int (*)(void *, const char *, void *))0x8032e26d)
#define k_ioreg_set_bool      ((int (*)(void *, const char *, int))0x8032e2dd)
#define k_oskext_load         ((int (*)(const char *))0x8031f819)

extern uintptr_t mct;
extern uintptr_t gpio_l_va;
extern uintptr_t gpio_r_va;

#define SDHCI3_PA       0x12540000u
#define BCM4334_RAM_SZ  0x00080000u /* 512 KiB internal SRAM */
#define BCM4334_AI_CC   0x18000000u /* ChipCommon core */
#define BCM4334_AI_ARM  0x18003000u /* ARM Cortex-M3 core */
#define BCM4334_AI_WRAP 0x18103000u /* ARM CM3 AI wrapper */
#define BCM4334_SDIO_C  0x18002000u /* SDIO device core */
#define BCM4334_F1_WIN  0x1000au    /* SBSDIO_FUNC1_SBADDRLOW */
#define BCM4334_F1_CSR  0x1000eu    /* SBSDIO_FUNC1_CHIPCLKCSR */

/* SDIO CCCR registers (Function 0) */
#define CCCR_IOEx       0x02u
#define CCCR_IORx       0x03u
#define CCCR_IENx       0x04u
#define CCCR_BUSIF      0x07u
#define CCCR_SPEED      0x13u
#define FBR_BLKSIZE(f)  (((uint32_t)(f) * 0x100u) + 0x10u)

/* SDPCM channels */
#define SDPCM_CH_CTRL   0u
#define SDPCM_CH_EVENT  1u
#define SDPCM_CH_DATA   2u

/* Userspace <-> kernel WIFI_OP command IDs (selector 22) */
#define WIFI_OP_STATUS      0u
#define WIFI_OP_INIT        1u
#define WIFI_OP_FW_WRITE    2u
#define WIFI_OP_NVRAM_BOOT  3u
#define WIFI_OP_CDC_CMD     4u

struct exy_wifi_status {
    uint32_t probed;
    uint32_t fw_loaded;
    uint32_t f2_ready;
    uint32_t link_up;
    uint32_t chip_id;
    uint32_t ocr;
    uint32_t rca;
    uint32_t tx_pkts;
    uint32_t rx_pkts;
    uint32_t en0_attached;
    uint8_t  mac[6];
    uint8_t  pad[2];
};

struct exy_wifi_req {
    uint32_t op;
    uint32_t arg0; /* offset or ioctl cmd */
    uint32_t arg1; /* length or flags (bit 0 = set) */
    uint32_t arg2;
    uint8_t  data[1024];
};

struct exy_wifi_rsp {
    int32_t  err;
    uint32_t out_len;
    uint8_t  data[1024];
};

static uintptr_t w_sd3;
static struct exy_wifi_status w_st;
static uint32_t w_cur_win = 0xffffffffu;
static uint8_t  w_tx_seq;
static uint16_t w_cdc_id;
static void *w_en0;
static int w_rx_started;
static volatile int w_busy;

static uint8_t w_tx_buf[2056] __attribute__((aligned(4)));
static uint8_t w_rx_buf[2056] __attribute__((aligned(4)));
static uint8_t w_cdc_pkt[2048] __attribute__((aligned(4)));
static uint8_t w_ctl_rsp[2048];
static volatile uint32_t w_ctl_rsp_len;
static volatile uint32_t w_ctl_status;
static volatile uint16_t w_ctl_wait_id;
static volatile int      w_ctl_got;
static void *w_parent_svc;
static void *w_iokit_ctl;
static void *w_iokit_if;
static uint32_t w_power = 1u;
static uint8_t  w_cur_ssid[32];
static uint32_t w_cur_ssid_len;
static uint8_t  w_cur_bssid[6];
static uint32_t w_cur_chan = 1u;
static int32_t  w_cur_rssi = -55;
static uint32_t w_auth_lower = 1u;
static uint32_t w_auth_upper = 1u;
static uint32_t w_assoc_result = 0u; /* APPLE80211_IOC_ASSOCIATE_RESULT: 1=success, 9=failed */

#define WIFI_MAX_APS 16u
struct wifi_ap_entry {
    uint8_t  bssid[6];
    uint8_t  ssid_len;
    uint8_t  channel;
    uint8_t  ssid[32];
    int16_t  rssi;
    int8_t   noise;
    uint8_t  pad;
    uint16_t cap;
    uint16_t beacon_int;
    uint16_t ie_len;
    uint8_t  ies[512];
};
static struct wifi_ap_entry w_aps[WIFI_MAX_APS];
static uint32_t w_ap_cnt;
static uint32_t w_ap_idx;
static volatile uint32_t w_scan_pending;
static volatile uint32_t w_assoc_pending;
static uint8_t  w_assoc_buf[512];
static uint32_t w_assoc_len;

/* Host-side IEEE 802.11i WPA2-PSK 4-way handshake state (BCM4334 6.10 lacks WLC_SUP) */
static volatile int      w_wpa_active;
static volatile int      w_wpa_done;
static volatile int      w_assoc_fail;
static uint8_t           w_wpa_pmk[32];
static uint8_t           w_wpa_ptk[48];
static uint8_t           w_wpa_snonce[32];
static uint8_t           w_wpa_rsnie[32];
static uint32_t          w_wpa_rsnie_len;
static uint8_t           w_wpa_eapol_rx[512];
static volatile uint32_t w_wpa_eapol_len;
static uint32_t          w_wpa_nonce_ctr;

static inline uint32_t wrd(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void     wwr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static void w_udelay(uint32_t us)
{
    if (!mct) return;
    uint32_t t0 = *(volatile uint32_t *)(mct + 0x100u);
    while (*(volatile uint32_t *)(mct + 0x100u) - t0 < us * 24u)
        ;
}

static void w_lock(void)
{
    while (__sync_lock_test_and_set(&w_busy, 1))
        w_udelay(20);
}

static void w_unlock(void)
{
    __sync_lock_release(&w_busy);
}

/* --- Exynos 4412 SDHCI3 low-level CMD / CMD52 / CMD53 ---------------------- */

static int sd3_cmd(uint16_t cmd_reg, uint32_t arg, uint32_t *resp0)
{
    for (uint32_t i = 0; i < 4000u; i++) {
        if (!(wrd(w_sd3 + 0x24u) & 3u)) break;
        w_udelay(5);
    }
    wwr(w_sd3 + 0x30u, 0xffffffffu);
    wwr(w_sd3 + 0x08u, arg);
    wwr(w_sd3 + 0x0cu, ((uint32_t)cmd_reg) << 16);
    for (uint32_t i = 0; i < 6000u; i++) {
        uint32_t st = wrd(w_sd3 + 0x30u);
        if (st & (1u << 15)) {
            wwr(w_sd3 + 0x30u, st);
            return -1;
        }
        if (st & 1u) {
            wwr(w_sd3 + 0x30u, 1u);
            if (resp0) *resp0 = wrd(w_sd3 + 0x10u);
            return 0;
        }
        w_udelay(5);
    }
    return -1;
}

static int sdio_cmd52(uint32_t func, uint32_t addr, uint8_t *val, int write)
{
    uint32_t arg = ((write ? 1u : 0u) << 31) | ((func & 7u) << 28) |
                   ((addr & 0x1ffffu) << 9) | (write ? (uint32_t)(*val) : 0u);
    uint32_t r = 0;
    /* CMD52: R5 response (48-bit + CRC + index = 0x1a) */
    if (sd3_cmd((52u << 8) | 0x1au, arg, &r) != 0)
        return -1;
    if (!write && val)
        *val = (uint8_t)(r & 0xffu);
    return 0;
}

static int sdio_cmd53_chunk(uint32_t func, uint32_t addr, uint8_t *buf, uint32_t len,
                            int write, int incr, int blk_mode, uint32_t blk_sz)
{
    uint32_t count = blk_mode ? (len / blk_sz) : (len & 0x1ffu);
    uint32_t arg = ((write ? 1u : 0u) << 31) | ((func & 7u) << 28) |
                   ((blk_mode ? 1u : 0u) << 27) | ((incr ? 1u : 0u) << 26) |
                   ((addr & 0x1ffffu) << 9) | (count & 0x1ffu);
    uint32_t bsz = blk_mode ? blk_sz : len;
    uint32_t bcnt = blk_mode ? count : 1u;

    for (uint32_t i = 0; i < 4000u; i++) {
        if (!(wrd(w_sd3 + 0x24u) & 3u)) break;
        w_udelay(5);
    }
    wwr(w_sd3 + 0x30u, 0xffffffffu);
    wwr(w_sd3 + 0x04u, (bcnt << 16) | (bsz & 0xfffu));
    wwr(w_sd3 + 0x08u, arg);
    uint16_t trn = (write ? 0u : (1u << 4)) | (blk_mode ? ((1u << 5) | (1u << 1)) : 0u);
    /* CMD53: data present (bit 5 = 0x20) + R5 (0x1a) = 0x3a */
    wwr(w_sd3 + 0x0cu, (((53u << 8) | 0x3au) << 16) | trn);

    for (uint32_t i = 0; i < 6000u; i++) {
        uint32_t st = wrd(w_sd3 + 0x30u);
        if (st & (1u << 15)) { wwr(w_sd3 + 0x30u, st); return -1; }
        if (st & 1u) { wwr(w_sd3 + 0x30u, 1u); break; }
        w_udelay(5);
    }

    uint32_t rem = len;
    uint32_t off = 0u;
    uint32_t want_bit = write ? (1u << 4) : (1u << 5); /* BUF_WR_RDY / BUF_RD_RDY */
    while (rem > 0u) {
        uint32_t waited = 0u;
        for (; waited < 10000u; waited++) {
            uint32_t st = wrd(w_sd3 + 0x30u);
            if (st & (1u << 15)) { wwr(w_sd3 + 0x30u, st); return -2; }
            if (st & want_bit) {
                wwr(w_sd3 + 0x30u, want_bit);
                break;
            }
            w_udelay(2);
        }
        if (waited >= 10000u) return -3;
        uint32_t chunk = (rem > bsz) ? bsz : rem;
        uint32_t words = (chunk + 3u) >> 2;
        if (write) {
            for (uint32_t w = 0; w < words; w++) {
                uint32_t v = 0u;
                for (uint32_t b = 0; b < 4u && off + b < len; b++)
                    v |= ((uint32_t)buf[off + b]) << (b * 8u);
                wwr(w_sd3 + 0x20u, v);
                off += 4u;
            }
        } else {
            for (uint32_t w = 0; w < words; w++) {
                uint32_t v = wrd(w_sd3 + 0x20u);
                for (uint32_t b = 0; b < 4u && off + b < len; b++)
                    buf[off + b] = (uint8_t)(v >> (b * 8u));
                off += 4u;
            }
        }
        rem -= chunk;
    }

    for (uint32_t i = 0; i < 10000u; i++) {
        uint32_t st = wrd(w_sd3 + 0x30u);
        if (st & (1u << 15)) { wwr(w_sd3 + 0x30u, st); return -(int)((st >> 16) | 0x4000u); }
        if (st & 2u) { wwr(w_sd3 + 0x30u, 2u); return 0; } /* XFER_COMPLETE */
        w_udelay(2);
    }
    return -5;
}

static int sdio_f1_set_win(uint32_t addr)
{
    uint32_t win = addr & ~0x7fffu;
    if (win == w_cur_win)
        return 0;
    uint8_t b0 = (uint8_t)((win >> 8) & 0xffu);
    uint8_t b1 = (uint8_t)((win >> 16) & 0xffu);
    uint8_t b2 = (uint8_t)((win >> 24) & 0xffu);
    if (sdio_cmd52(1u, BCM4334_F1_WIN + 0u, &b0, 1) != 0) return -1;
    if (sdio_cmd52(1u, BCM4334_F1_WIN + 1u, &b1, 1) != 0) return -1;
    if (sdio_cmd52(1u, BCM4334_F1_WIN + 2u, &b2, 1) != 0) return -1;
    w_cur_win = win;
    return 0;
}

static int bcm_bp_read32(uint32_t addr, uint32_t *out)
{
    if (sdio_f1_set_win(addr) != 0) return -1;
    uint32_t off = (addr & 0x7fffu) | 0x8000u; /* SBSDIO_SB_ACCESS_2_4B_FLAG */
    uint8_t b[4] = { 0 };
    if (sdio_cmd53_chunk(1u, off, b, 4u, 0, 1, 0, 4u) != 0) return -1;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 0;
}

static int bcm_bp_write32(uint32_t addr, uint32_t val)
{
    if (sdio_f1_set_win(addr) != 0) return -1;
    uint32_t off = (addr & 0x7fffu) | 0x8000u;
    uint8_t b[4] = { (uint8_t)val, (uint8_t)(val >> 8), (uint8_t)(val >> 16), (uint8_t)(val >> 24) };
    return sdio_cmd53_chunk(1u, off, b, 4u, 1, 1, 0, 4u);
}

static int bcm_bp_write_mem(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t pos = 0u;
    while (pos < len) {
        uint32_t cur = addr + pos;
        if (sdio_f1_set_win(cur) != 0) return -1;
        uint32_t win_rem = 0x8000u - (cur & 0x7fffu);
        uint32_t n = len - pos;
        if (n > win_rem) n = win_rem;
        /* Use 64-byte CMD53 byte-mode transfers WITH SBSDIO_SB_ACCESS_2_4B_FLAG (0x8000, matching brcmf_sdiod_ramrw). */
        if (n > 64u) n = 64u;
        uint32_t off = (cur & 0x7fffu) | 0x8000u;
        int rc = sdio_cmd53_chunk(1u, off, (uint8_t *)(data + pos), n, 1, 1, 0, n);
        if (rc != 0)
            return rc;
        pos += n;
    }
    return 0;
}

static uint32_t w_arm_wrap = BCM4334_AI_WRAP;
static uint32_t w_soc_wrap = 0x18104000u;
static uint32_t w_d11_wrap = 0x18101000u;
static uint32_t w_sdio_base = BCM4334_SDIO_C;

static void w_puts(const char *s)
{
    extern void exy_putc(char c);
    while (*s) exy_putc(*s++);
}

static void w_putx(uint32_t v)
{
    extern void exy_putc(char c);
    static const char h[] = "0123456789abcdef";
    for (int i = 28; i >= 0; i -= 4)
        exy_putc(h[(v >> i) & 0xfu]);
}

static void bcm_core_disable(uint32_t wrap, uint32_t prereset, uint32_t reset)
{
    if (!wrap) return;
    uint32_t v = 0u;
    if (bcm_bp_read32(wrap + 0x800u, &v) == 0 && (v & 1u))
        return;
    (void)bcm_bp_write32(wrap + 0x408u /* IOCTRL */, prereset | 0x03u /* FGC|CLOCK */);
    (void)bcm_bp_read32(wrap + 0x408u, &v);
    w_udelay(20);
    (void)bcm_bp_write32(wrap + 0x800u /* RESETCTRL */, 1u);
    w_udelay(20);
    (void)bcm_bp_write32(wrap + 0x408u, reset | 0x01u /* CLOCK */);
    (void)bcm_bp_read32(wrap + 0x408u, &v);
}

static void bcm_core_reset(uint32_t wrap, uint32_t prereset, uint32_t reset, uint32_t postreset)
{
    if (!wrap) return;
    uint32_t v = 0u;
    bcm_core_disable(wrap, prereset, reset);
    (void)bcm_bp_write32(wrap + 0x408u, reset | 0x03u /* FGC|CLOCK */);
    (void)bcm_bp_read32(wrap + 0x408u, &v);
    (void)bcm_bp_write32(wrap + 0x800u, 0u);
    w_udelay(20);
    (void)bcm_bp_write32(wrap + 0x408u, postreset | 0x01u /* CLOCK */);
    (void)bcm_bp_read32(wrap + 0x408u, &v);
}

/* Scan Broadcom AXI EROM (ChipCommon + 0xfc) to locate exact ARMCM3/SOCRAM/D11/SDIO cores & wrappers. */
static void bcm_scan_erom(void)
{
    uint32_t ep = 0u;
    if (bcm_bp_read32(BCM4334_AI_CC + 0xfcu, &ep) != 0 || ep < 0x18000000u) {
        w_puts("[WIFI-EROM] bad ep="); w_putx(ep); w_puts("\n");
        return;
    }
    w_puts("[WIFI-EROM-RAW]");
    for (uint32_t k = 0; k < 32u; k++) {
        uint32_t rw = 0u;
        if (bcm_bp_read32(ep + k * 4u, &rw) != 0) break;
        w_puts(" "); w_putx(rw);
    }
    w_puts("\n[WIFI-EROM] ep="); w_putx(ep); w_puts(":");
    for (int iter = 0; iter < 128; iter++) {
        uint32_t cia = 0u;
        if (bcm_bp_read32(ep, &cia) != 0) break;
        ep += 4u;
        if ((cia & 0x0fu) == 0x0fu) break; /* EROM_END */
        if ((cia & 0x07u) != 0x01u) continue;
        uint32_t cid = (cia >> 8) & 0xfffu;
        uint32_t cib = 0u;
        if (bcm_bp_read32(ep, &cib) != 0) break;
        ep += 4u;
        uint32_t base = 0u, wrap = 0u;
        for (int j = 0; j < 24; j++) {
            uint32_t ad = 0u;
            if (bcm_bp_read32(ep, &ad) != 0) break;
            if ((ad & 0x07u) != 0x05u) {
                if ((ad & 0x0fu) == 0x0fu || (ad & 0x07u) == 0x01u) break;
                ep += 4u;
                continue;
            }
            ep += 4u;
            uint32_t addr = ad & 0xfffff000u;
            uint32_t atype = (ad >> 4) & 0x03u;
            if (ad & 0x08u) ep += 4u;
            if ((ad & 0xc0u) == 0xc0u) {
                uint32_t sz = 0u;
                (void)bcm_bp_read32(ep, &sz);
                ep += 4u;
                if (sz & 0x08u) ep += 4u;
            }
            if (atype == 0u && !base) base = addr;
            else if ((atype == 2u || atype == 3u) && !wrap) wrap = addr;
        }
        w_puts(" "); w_putx((cid << 20) | ((wrap >> 12) & 0xffu) | (((base >> 12) & 0xffu) << 8));
        if (cid == 0x82au && wrap) w_arm_wrap = wrap;
        else if (cid == 0x80eu && wrap) w_soc_wrap = wrap;
        else if (cid == 0x812u && wrap) w_d11_wrap = wrap;
        else if (cid == 0x829u && base) w_sdio_base = base;
    }
    w_puts("\n[WIFI-EROM] arm="); w_putx(w_arm_wrap);
    w_puts(" soc="); w_putx(w_soc_wrap);
    w_puts(" d11="); w_putx(w_d11_wrap);
    w_puts(" sd="); w_putx(w_sdio_base);
    w_puts("\n");
}

/* --- BCM4334 SDIO Enumeration & Core Bring-up ------------------------------ */

uint32_t exy_wifi_get_ocr(void)
{
    return w_st.chip_id ? ((w_st.chip_id << 16) | (w_st.ocr & 0xffffu)) : w_st.ocr;
}

int exy_wifi_sdio_init(void)
{
    if (!w_sd3)
        w_sd3 = ml_io_map(SDHCI3_PA, 0x1000u);
    if (!w_sd3)
        return -1;
    w_cur_win = 0xffffffffu;

    /* Match U-Boot s5p_sdhci.c exact Exynos4412 CONTROL4/CONTROL2/CONTROL3 (no inverted ENFBCLKTX!) */
    wwr(w_sd3 + 0x2cu, (wrd(w_sd3 + 0x2cu) & 0x00ffffffu) | (1u << 24));
    for (uint32_t i = 0; i < 1000u && (wrd(w_sd3 + 0x2cu) & (1u << 24)); i++) w_udelay(10);
    wwr(w_sd3 + 0x8cu /* CONTROL4 */, (3u << 16)); /* 9mA SDCLK pad drive */
    wwr(w_sd3 + 0x80u /* CONTROL2 */, (1u << 31) | (1u << 30) | (1u << 14) | (1u << 8) | (2u << 4));
    wwr(w_sd3 + 0x84u /* CONTROL3 */, (1u << 15) | (1u << 7)); /* FCSEL1 | FCSEL0 (basic delay + 2ns) */
    wwr(w_sd3 + 0x28u, (0x0fu << 8)); /* 3.3V + Bus Power */
    wwr(w_sd3 + 0x2cu, (0x80u << 8) | 0x0001u); /* /256 + IntClkEn */
    for (uint32_t i = 0; i < 1000u && !(wrd(w_sd3 + 0x2cu) & 2u); i++) w_udelay(10);
    wwr(w_sd3 + 0x2cu, wrd(w_sd3 + 0x2cu) | 4u); /* SDClkEn */
    wwr(w_sd3 + 0x34u, 0x01ff00ffu); /* NORINTSTSEN */
    w_udelay(2000);

    (void)sd3_cmd((0u << 8) | 0x00u, 0u, 0); /* CMD0 */
    w_udelay(1000);
    uint32_t ocr = 0u;
    if (sd3_cmd((5u << 8) | 0x02u, 0u, &ocr) != 0 || !ocr)
        return -2;
    /* Negotiate 3.3V OCR window */
    for (int tries = 0; tries < 50; tries++) {
        if (sd3_cmd((5u << 8) | 0x02u, ocr & 0x00ff8000u, &ocr) == 0 && (ocr & (1u << 31)))
            break;
        w_udelay(10000);
    }
    w_st.ocr = ocr;
    if (!(ocr & (1u << 31)))
        return -3;

    /* CMD3 (get RCA) + CMD7 (select card) */
    uint32_t r3 = 0u;
    if (sd3_cmd((3u << 8) | 0x1au, 0u, &r3) != 0)
        return -4;
    w_st.rca = r3 >> 16;
    if (sd3_cmd((7u << 8) | 0x1bu, w_st.rca << 16, 0) != 0)
        return -5;

    /* Switch to 4-bit SDIO bus + 25 MHz clock */
    uint8_t v = 0x82u; /* CD_DISABLE | 4-bit */
    if (sdio_cmd52(0u, CCCR_BUSIF, &v, 1) == 0)
        wwr(w_sd3 + 0x28u, (wrd(w_sd3 + 0x28u) & ~0xffu) | 0x02u); /* HOSTCTL 4-bit */
    /* Raise SDCLK to ~12.5 MHz (/4 of 50 MHz SCLK_MMC3) for rock-solid zero-CRC-error SDIO PIO */
    wwr(w_sd3 + 0x2cu, wrd(w_sd3 + 0x2cu) & ~4u);
    wwr(w_sd3 + 0x2cu, (0x02u << 8) | 0x0001u);
    for (uint32_t i = 0; i < 1000u && !(wrd(w_sd3 + 0x2cu) & 2u); i++) w_udelay(10);
    wwr(w_sd3 + 0x2cu, wrd(w_sd3 + 0x2cu) | 4u);

    /* Enable Function 1 (backplane) & set F1=64B, F2=512B block sizes */
    v = 0x02u;
    if (sdio_cmd52(0u, CCCR_IOEx, &v, 1) != 0)
        return -6;
    for (int i = 0; i < 100; i++) {
        v = 0u;
        if (sdio_cmd52(0u, CCCR_IORx, &v, 0) == 0 && (v & 0x02u))
            break;
        w_udelay(2000);
    }
    uint8_t b64[2] = { 64u, 0u }, b512[2] = { 0u, 2u };
    (void)sdio_cmd52(0u, FBR_BLKSIZE(1) + 0u, &b64[0], 1);
    (void)sdio_cmd52(0u, FBR_BLKSIZE(1) + 1u, &b64[1], 1);
    (void)sdio_cmd52(0u, FBR_BLKSIZE(2) + 0u, &b512[0], 1);
    (void)sdio_cmd52(0u, FBR_BLKSIZE(2) + 1u, &b512[1], 1);

    /* Request ALP clock on BCM4334 so backplane RAM & ChipCommon are accessible */
    v = 0x08u; /* SBSDIO_ALP_AVAIL_REQ */
    (void)sdio_cmd52(1u, BCM4334_F1_CSR, &v, 1);
    for (int i = 0; i < 100; i++) {
        v = 0u;
        if (sdio_cmd52(1u, BCM4334_F1_CSR, &v, 0) == 0 && (v & 0x40u))
            break;
        w_udelay(1000);
    }

    uint32_t cid = 0u, soc_ci = 0u;
    if (bcm_bp_read32(BCM4334_AI_CC, &cid) == 0)
        w_st.chip_id = cid & 0xffffu;
    /* Scan AXI EROM, hold ARMCM3 & D11 in reset, and reset/enable SOCRAM before SRAM firmware upload */
    bcm_scan_erom();
    bcm_core_disable(w_arm_wrap, 0u, 0u);
    bcm_core_reset(w_d11_wrap, 0x0cu, 0x04u, 0x04u);
    bcm_core_reset(w_soc_wrap, 0u, 0u, 0u);
    (void)bcm_bp_read32(0x18004000u /* SOCRAM coreinfo */, &soc_ci);
    w_puts("[WIFI-CHIP] cid="); w_putx(cid);
    w_puts(" soc_ci="); w_putx(soc_ci);
    w_puts("\n");
    w_st.probed = 1u;
    return 0;
}

/* --- SDPCM / BDC / CDC Framing over SDIO Function 2 ------------------------ */

static int sdpcm_tx_frame(uint8_t chan, const uint8_t *payload, uint32_t plen)
{
    uint32_t hdr_len = 12u;
    uint32_t total = hdr_len + plen;
    uint32_t padded = (total + 3u) & ~3u;
    if (padded > sizeof(w_tx_buf))
        return -1;
    w_tx_buf[0] = (uint8_t)(total & 0xffu);
    w_tx_buf[1] = (uint8_t)((total >> 8) & 0xffu);
    w_tx_buf[2] = (uint8_t)(~w_tx_buf[0]);
    w_tx_buf[3] = (uint8_t)(~w_tx_buf[1]);
    w_tx_buf[4] = w_tx_seq++;
    w_tx_buf[5] = chan & 0x0fu;
    w_tx_buf[6] = 0u;
    w_tx_buf[7] = (uint8_t)hdr_len;
    w_tx_buf[8] = 0u; w_tx_buf[9] = 0u; w_tx_buf[10] = 0u; w_tx_buf[11] = 0u;
    for (uint32_t i = 0; i < plen; i++)
        w_tx_buf[hdr_len + i] = payload[i];
    for (uint32_t i = total; i < padded; i++)
        w_tx_buf[i] = 0u;

    /* Send on Function 2 (FIFO addr 0x8000, non-incrementing) */
    if (padded >= 512u) {
        uint32_t blks = (padded + 511u) & ~511u;
        if (blks > sizeof(w_tx_buf)) blks = sizeof(w_tx_buf);
        for (uint32_t i = padded; i < blks; i++) w_tx_buf[i] = 0u;
        return sdio_cmd53_chunk(2u, 0x8000u, w_tx_buf, blks, 1, 0, 1, 512u);
    }
    return sdio_cmd53_chunk(2u, 0x8000u, w_tx_buf, padded, 1, 0, 0, padded);
}

static void wifi_deliver_rx_eth(const uint8_t *eth, uint32_t len)
{
    if (!w_en0 || len < 14u || len > 1520u)
        return;
    void *m = 0;
    unsigned int maxseg = 1u;
    if (k_mbuf_allocpacket(1 /* MBUF_DONTWAIT */, len, &maxseg, &m) != 0 || !m)
        return;
    uint8_t *dst = (uint8_t *)k_mbuf_data(m);
    if (!dst) {
        k_mbuf_freem(m);
        return;
    }
    for (uint32_t i = 0; i < len; i++)
        dst[i] = eth[i];
    k_mbuf_setlen(m, len);
    k_mbuf_pkthdr_setlen(m, len);
    k_mbuf_pkthdr_setheader(m, dst);
    k_mbuf_adj(m, 14);
    k_mbuf_pkthdr_setrcvif(m, w_en0);
    if (k_ifnet_input(w_en0, m, 0) != 0)
        k_mbuf_freem(m);
    else
        w_st.rx_pkts++;
}

static int sdpcm_poll_rx_once(void)
{
    uint8_t hdr[4] = { 0 };
    if (sdio_cmd53_chunk(2u, 0x8000u, hdr, 4u, 0, 0, 0, 4u) != 0)
        return 0;
    uint16_t len = (uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8);
    uint16_t chk = (uint16_t)hdr[2] | ((uint16_t)hdr[3] << 8);
    if (len < 12u || len > sizeof(w_rx_buf) || (uint16_t)(len ^ chk) != 0xffffu)
        return 0;
    w_rx_buf[0] = hdr[0]; w_rx_buf[1] = hdr[1]; w_rx_buf[2] = hdr[2]; w_rx_buf[3] = hdr[3];
    uint32_t rem = ((uint32_t)len - 4u + 3u) & ~3u;
    uint32_t rd_len = (rem > 512u) ? ((rem + 511u) & ~511u) : rem;
    if (rd_len + 4u > sizeof(w_rx_buf))
        return 0;
    int blk = (rd_len >= 512u) ? 1 : 0;
    if (sdio_cmd53_chunk(2u, 0x8000u, w_rx_buf + 4u, rd_len, 0, 0, blk, blk ? 512u : rd_len) != 0)
        return 0;

    uint8_t chan = w_rx_buf[5] & 0x0fu;
    uint8_t doff = w_rx_buf[7];
    if (doff < 12u || doff > len)
        return 1;
    uint8_t *pl = w_rx_buf + doff;
    uint32_t plen = (uint32_t)len - doff;

    if (chan == SDPCM_CH_CTRL && plen >= 16u) {
        uint16_t rid = (uint16_t)((((uint32_t)pl[8] | ((uint32_t)pl[9] << 8) |
                       ((uint32_t)pl[10] << 16) | ((uint32_t)pl[11] << 24)) >> 16) & 0xffffu);
        if (rid == w_ctl_wait_id) {
            w_ctl_status = (uint32_t)pl[12] | ((uint32_t)pl[13] << 8) |
                           ((uint32_t)pl[14] << 16) | ((uint32_t)pl[15] << 24);
            uint32_t cpy = plen - 16u;
            if (cpy > sizeof(w_ctl_rsp)) cpy = sizeof(w_ctl_rsp);
            for (uint32_t i = 0; i < cpy; i++) w_ctl_rsp[i] = pl[16u + i];
            w_ctl_rsp_len = cpy;
            w_ctl_got = 1;
        }
    } else if (chan == SDPCM_CH_DATA && plen > 4u) {
        /* BDC header: 4 bytes + (data_offset * 4) */
        uint32_t bdc_off = 4u + ((uint32_t)pl[3] * 4u);
        if (bdc_off < plen) {
            const uint8_t *eth = pl + bdc_off;
            uint32_t elen = plen - bdc_off;
            if (w_wpa_active && elen >= 14u && eth[12] == 0x88u && eth[13] == 0x8eu) {
                if (!w_wpa_eapol_len && elen <= sizeof(w_wpa_eapol_rx)) {
                    for (uint32_t i = 0u; i < elen; i++) w_wpa_eapol_rx[i] = eth[i];
                    w_wpa_eapol_len = elen;
                }
            } else {
                wifi_deliver_rx_eth(eth, elen);
            }
        }
    } else if (chan == SDPCM_CH_EVENT && plen > 4u) {
        /* Broadcom async event: eth(14) + bcmeth_hdr(10) + wl_event_msg_t(be) */
        uint32_t bdc_off = 4u + ((uint32_t)pl[3] * 4u);
        if (bdc_off + 48u <= plen) {
            const uint8_t *ev = pl + bdc_off;
            if (ev[12] == 0x88u && ev[13] == 0x6cu) {
                uint16_t eflags = ((uint16_t)ev[26] << 8) | ev[27];
                uint32_t etype  = ((uint32_t)ev[28] << 24) | ((uint32_t)ev[29] << 16) |
                                  ((uint32_t)ev[30] << 8)  | ev[31];
                uint32_t estatus = ((uint32_t)ev[32] << 24) | ((uint32_t)ev[33] << 16) |
                                   ((uint32_t)ev[34] << 8)  | ev[35];
                uint32_t ereason = ((uint32_t)ev[36] << 24) | ((uint32_t)ev[37] << 16) |
                                   ((uint32_t)ev[38] << 8)  | ev[39];
                if (w_assoc_result == 0xffffu) {
                    w_puts("[WIFI-EV] t="); w_putx(etype);
                    w_puts(" st="); w_putx(estatus);
                    w_puts(" r="); w_putx(ereason);
                    w_puts(" f="); w_putx(eflags); w_puts("\n");
                }
                if (etype == 16u /* WLC_E_LINK */) {
                    if ((eflags & 1u) && w_cur_ssid_len > 0u && (!w_wpa_active || w_wpa_done))
                        w_st.link_up = 1u;
                    else if (!(eflags & 1u))
                        w_st.link_up = 0u;
                } else if (etype == 0u /* WLC_E_SET_SSID */) {
                    if (estatus == 0u && !w_wpa_active)
                        w_st.link_up = 1u;
                    else if (estatus != 0u)
                        w_assoc_fail = 1;
                } else if (etype == 6u /* DEAUTH_IND */ || etype == 12u /* DISASSOC_IND */) {
                    w_st.link_up = 0u;
                }
            }
        }
    }
    return 1;
}

static int bcm_cdc_cmd(uint32_t cmd, int is_set, uint8_t *buf, uint32_t len, uint32_t *out_len)
{
    if (!w_st.f2_ready || len > 2000u)
        return -1;
    w_lock();
    uint16_t id = ++w_cdc_id;
    uint32_t flags = (is_set ? 2u : 0u) | ((uint32_t)id << 16);
    w_cdc_pkt[0]  = (uint8_t)cmd;        w_cdc_pkt[1]  = (uint8_t)(cmd >> 8);
    w_cdc_pkt[2]  = (uint8_t)(cmd >> 16); w_cdc_pkt[3]  = (uint8_t)(cmd >> 24);
    w_cdc_pkt[4]  = (uint8_t)len;        w_cdc_pkt[5]  = (uint8_t)(len >> 8);
    w_cdc_pkt[6]  = (uint8_t)(len >> 16); w_cdc_pkt[7]  = (uint8_t)(len >> 24);
    w_cdc_pkt[8]  = (uint8_t)flags;      w_cdc_pkt[9]  = (uint8_t)(flags >> 8);
    w_cdc_pkt[10] = (uint8_t)(flags >> 16); w_cdc_pkt[11] = (uint8_t)(flags >> 24);
    w_cdc_pkt[12] = 0u; w_cdc_pkt[13] = 0u; w_cdc_pkt[14] = 0u; w_cdc_pkt[15] = 0u;
    for (uint32_t i = 0; i < len; i++)
        w_cdc_pkt[16u + i] = buf[i];

    w_ctl_wait_id = id;
    w_ctl_got = 0;
    int rc = sdpcm_tx_frame(SDPCM_CH_CTRL, w_cdc_pkt, 16u + len);
    if (rc == 0) {
        for (int tries = 0; tries < 250 && !w_ctl_got; tries++) {
            (void)sdpcm_poll_rx_once();
            if (!w_ctl_got) w_udelay(2000);
        }
    }
    int got = w_ctl_got;
    uint32_t rlen = w_ctl_rsp_len;
    if (got && out_len) {
        uint32_t cpy = (rlen < *out_len) ? rlen : *out_len;
        for (uint32_t i = 0; i < cpy; i++) buf[i] = w_ctl_rsp[i];
        *out_len = cpy;
    }
    w_unlock();
    return (rc == 0 && got) ? 0 : -2;
}

/* --- XNU BSD en0 Ethernet Interface ---------------------------------------- */

static int wifi_en0_output(void *ifp, void *m)
{
    (void)ifp;
    if (!m) return 0;
    uint32_t len = k_mbuf_pkthdr_len(m);
    if (!w_st.f2_ready || len < 14u || len > 1520u) {
        k_mbuf_freem(m);
        return 0;
    }
    uint8_t bdc[1536];
    bdc[0] = 0x20u; /* BDC_PROTO_VER = 2 */
    bdc[1] = 0u; bdc[2] = 0u; bdc[3] = 0u;
    k_mbuf_copydata(m, 0u, len, bdc + 4u);
    k_mbuf_freem(m);

    w_lock();
    if (sdpcm_tx_frame(SDPCM_CH_DATA, bdc, 4u + len) == 0)
        w_st.tx_pkts++;
    w_unlock();
    return 0;
}

/* --- IOKit IO80211Interface + Apple80211 ioctl / Kernel Event Bridge ------- */

static void w_os_rel(void *obj)
{
    if (!obj) return;
    void (*rel)(void *) = *(void (**)(void *))(*(uintptr_t *)obj + 0x14u);
    rel(obj);
}

static void w_set_u32(void *obj, const char *k, uint32_t v)
{
    void *n = k_osnum_u64((uint64_t)v, 32u);
    if (n) { k_ioreg_set_prop(obj, k, n); w_os_rel(n); }
}

static void w_set_str(void *obj, const char *k, const char *s)
{
    void *o = k_osstr_cstr(s);
    if (o) { k_ioreg_set_prop(obj, k, o); w_os_rel(o); }
}

void exy_wifi_set_parent(void *parent_service)
{
    w_parent_svc = parent_service;
}

static void wifi_post_80211_event(uint32_t code, const void *extra, uint32_t extra_len)
{
    struct {
        uint32_t vendor_code;
        uint32_t kev_class;
        uint32_t kev_subclass;
        uint32_t event_code;
        struct { uint32_t len; const void *ptr; } dv[5];
    } msg;
    for (uint32_t i = 0; i < sizeof(msg); i++) ((uint8_t *)&msg)[i] = 0u;
    static const char ifn[16] = "en0";
    msg.vendor_code  = 1u; /* KEV_VENDOR_APPLE */
    msg.kev_class    = 6u; /* KEV_IEEE80211_CLASS */
    msg.kev_subclass = 1u; /* KEV_IEEE80211_SUBCLASS */
    msg.event_code   = code;
    msg.dv[0].len    = 16u;
    msg.dv[0].ptr    = ifn;
    if (extra && extra_len) {
        msg.dv[1].len = extra_len;
        msg.dv[1].ptr = extra;
    }
    (void)k_kev_post_msg(&msg);
}

static uint32_t w_ionet_exp[16];
static uint32_t w_ctl_vtable[220];

static void wifi_register_iokit(void)
{
    if (w_iokit_if || !w_parent_svc)
        return;
    int krc = k_oskext_load("com.apple.iokit.IO80211Family");
    void *parent = w_parent_svc;
    void *ctl = k_osmeta_alloc("IOService");
    if (ctl) {
        if (k_iosvc_init(ctl, 0)) {
            /* Override getMetaClass (+0x1c) -> IO80211Controller::getMetaClass so
             * wifid's IOObjectConformsTo(parent, "IO80211Controller") succeeds
             * while keeping all other virtual methods safely on IOService. */
            const uint32_t *svt = *(const uint32_t **)ctl;
            for (uint32_t i = 0; i < 220u; i++) w_ctl_vtable[i] = svt[i];
            w_ctl_vtable[7] = 0x809cac09u; /* IO80211Controller::getMetaClass */
            *(uint32_t **)ctl = w_ctl_vtable;
            if (k_iosvc_attach(ctl, w_parent_svc)) {
                w_set_str(ctl, "IOModel", "BCM4334");
                w_set_str(ctl, "IOVendor", "Broadcom");
                k_iosvc_reg(ctl, 0u);
                w_iokit_ctl = ctl;
                parent = ctl;
            } else {
                w_os_rel(ctl);
            }
        } else {
            w_os_rel(ctl);
        }
    }
    void *ifc = k_osmeta_alloc("IO80211Interface");
    int inited = 0, att = 0;
    if (ifc) {
        inited = k_iosvc_init(ifc, 0);
        if (inited) {
            /* Populate IONetworkInterface::_ifp (+0x54) and _reserved (+0x98) so
             * IONetworkInterface::serializeProperties() reads live en0 state safely. */
            w_ionet_exp[1] = 6u;       /* +0x04: if_type = IFT_ETHER */
            w_ionet_exp[2] = 1500u;    /* +0x08: mtu */
            w_ionet_exp[3] = 0x8843u;  /* +0x0c: flags */
            w_ionet_exp[5] = 6u;       /* +0x14: addrlen */
            w_ionet_exp[6] = 14u;      /* +0x18: hdrlen */
            *(void **)((uint8_t *)ifc + 0x54u) = w_en0;
            *(void **)((uint8_t *)ifc + 0x98u) = w_ionet_exp;

            w_set_str(ifc, "BSD Name", "en0");
            w_set_str(ifc, "IOInterfaceNamePrefix", "en");
            w_set_u32(ifc, "IOInterfaceUnit", 0u);
            w_set_u32(ifc, "IOInterfaceType", 6u);
            w_set_u32(ifc, "IOMediaAddressLength", 6u);
            w_set_u32(ifc, "IOMaxTransferUnit", 1500u);
            void *md = k_osdata_bytes(w_st.mac, 6u);
            if (md) { k_ioreg_set_prop(ifc, "IOMACAddress", md); w_os_rel(md); }
            k_ioreg_set_bool(ifc, "IOPrimaryInterface", 1);
            k_ioreg_set_bool(ifc, "IOBuiltin", 1);
            k_ioreg_set_bool(ifc, "IO80211InterfaceWiFiPrimary", 1);
            att = k_iosvc_attach(ifc, parent);
            if (att) {
                k_iosvc_reg(ifc, 0u);
                w_iokit_if = ifc;
            } else {
                w_os_rel(ifc);
            }
        } else {
            w_os_rel(ifc);
        }
    }
    w_puts("[WIFI-IOKIT] krc="); w_putx((uint32_t)krc);
    w_puts(" ctl="); w_putx((uint32_t)(uintptr_t)w_iokit_ctl);
    w_puts(" ifc="); w_putx((uint32_t)(uintptr_t)ifc);
    w_puts(" init="); w_putx((uint32_t)inited);
    w_puts(" att="); w_putx((uint32_t)att);
    w_puts("\n");
}

static uint32_t wifi_do_scan(int post_event)
{
    if (!w_st.f2_ready || !w_power)
        return 0u;
    uint8_t sp[88];
    for (unsigned i = 0; i < sizeof(sp); i++) sp[i] = 0u;
    for (unsigned i = 36u; i < 42u; i++) sp[i] = 0xffu;
    sp[42] = 2u; /* DOT11_BSSTYPE_ANY */
    sp[43] = 0u; /* active */
    sp[44] = 2u; /* nprobes */
    sp[48] = 45u; /* active_time ms */
    sp[52] = 90u; /* passive_time ms */
    for (unsigned i = 56u; i < 60u; i++) sp[i] = 0xffu;
    sp[60] = 11u; /* 2.4 GHz ch 1..11 (US NVRAM rejects ch 12-13) */
    for (unsigned c = 0u; c < 11u; c++) {
        sp[64u + c * 2u] = (uint8_t)(c + 1u);
        sp[65u + c * 2u] = 0x10u;
    }
    uint32_t sl = sizeof(sp);
    if (bcm_cdc_cmd(50u /* WLC_SCAN */, 1, sp, sizeof(sp), &sl) != 0)
        return 0u;

    static uint8_t sres[2000];
    uint32_t srl = 0u, cnt = 0u;
    for (int pass = 0; pass < 3; pass++) {
        k_iosleep(pass == 0 ? 550u : 350u);
        for (unsigned i = 0; i < sizeof(sres); i++) sres[i] = 0u;
        sres[0] = (uint8_t)(sizeof(sres) & 0xffu);
        sres[1] = (uint8_t)(sizeof(sres) >> 8);
        srl = sizeof(sres);
        if (bcm_cdc_cmd(51u /* WLC_SCAN_RESULTS */, 0, sres, sizeof(sres), &srl) == 0 && srl > 12u) {
            cnt = (uint32_t)sres[8] | ((uint32_t)sres[9] << 8) |
                  ((uint32_t)sres[10] << 16) | ((uint32_t)sres[11] << 24);
            if (cnt > 0u) break;
        }
    }
    w_ap_cnt = 0u;
    w_ap_idx = 0u;
    uint32_t pos = 12u;
    for (uint32_t k = 0u; k < cnt && w_ap_cnt < WIFI_MAX_APS && pos + 84u <= srl; k++) {
        uint32_t blen = (uint32_t)sres[pos + 4u] | ((uint32_t)sres[pos + 5u] << 8) |
                        ((uint32_t)sres[pos + 6u] << 16) | ((uint32_t)sres[pos + 7u] << 24);
        if (blen < 84u || pos + blen > srl) break;
        struct wifi_ap_entry *ap = &w_aps[w_ap_cnt];
        for (int i = 0; i < 6; i++) ap->bssid[i] = sres[pos + 8u + i];
        ap->beacon_int = (uint16_t)sres[pos + 14u] | ((uint16_t)sres[pos + 15u] << 8);
        ap->cap        = (uint16_t)sres[pos + 16u] | ((uint16_t)sres[pos + 17u] << 8);
        ap->ssid_len   = sres[pos + 18u];
        if (ap->ssid_len > 32u) ap->ssid_len = 32u;
        for (int i = 0; i < 32; i++) ap->ssid[i] = (i < ap->ssid_len) ? sres[pos + 19u + i] : 0u;
        uint16_t chsp = (uint16_t)sres[pos + 72u] | ((uint16_t)sres[pos + 73u] << 8);
        ap->channel = (uint8_t)(chsp & 0xffu);
        if (ap->channel == 0u || ap->channel > 14u) ap->channel = 1u;
        ap->rssi  = (int16_t)((uint16_t)sres[pos + 78u] | ((uint16_t)sres[pos + 79u] << 8));
        ap->noise = (int8_t)sres[pos + 80u];
        if (ap->noise == 0) ap->noise = -90;
        ap->ie_len = 0u;
        if (blen >= 124u) {
            uint16_t ieo = (uint16_t)sres[pos + 116u] | ((uint16_t)sres[pos + 117u] << 8);
            uint32_t iel = (uint32_t)sres[pos + 120u] | ((uint32_t)sres[pos + 121u] << 8) |
                           ((uint32_t)sres[pos + 122u] << 16) | ((uint32_t)sres[pos + 123u] << 24);
            if (ieo >= 84u && ieo < blen && iel > 0u) {
                if (ieo + iel > blen) iel = blen - ieo;
                if (iel > sizeof(ap->ies)) iel = sizeof(ap->ies);
                /* Trim on whole 802.11 IE TLV boundaries so wifid's IE parser never drops the AP. */
                uint32_t valid_ie = 0u;
                while (valid_ie + 2u <= iel) {
                    uint32_t elen = 2u + (uint32_t)sres[pos + ieo + valid_ie + 1u];
                    if (valid_ie + elen > iel) break;
                    valid_ie += elen;
                }
                for (uint32_t j = 0u; j < valid_ie; j++) ap->ies[j] = sres[pos + ieo + j];
                ap->ie_len = (uint16_t)valid_ie;
            }
        }
        w_ap_cnt++;
        pos += (blen + 3u) & ~3u;
    }
    w_puts("[WIFI-SCAN] aps="); w_putx(w_ap_cnt);
    for (uint32_t k = 0u; k < w_ap_cnt && k < 8u; k++) {
        extern void exy_putc(char);
        w_puts(" [");
        for (uint8_t j = 0u; j < w_aps[k].ssid_len; j++) {
            char c = (char)w_aps[k].ssid[j];
            exy_putc((c >= 0x20 && c < 0x7f) ? c : '?');
        }
        int r = -w_aps[k].rssi;
        if (r < 0) r = 0;
        if (r > 99) r = 99;
        exy_putc(':');
        exy_putc((w_aps[k].cap & 0x10u) ? 'W' : 'O');
        exy_putc('-');
        exy_putc((char)('0' + (r / 10)));
        exy_putc((char)('0' + (r % 10)));
        w_puts("]");
    }
    w_puts("\n");
    if (post_event)
        wifi_post_80211_event(10u /* APPLE80211_M_SCAN_DONE */, 0, 0u);
    return w_ap_cnt;
}

static void wifi_post_dl_event(uint32_t code)
{
    struct {
        uint32_t if_family;
        uint32_t if_unit;
        char     if_name[16];
    } ned;
    for (uint32_t i = 0; i < sizeof(ned); i++) ((uint8_t *)&ned)[i] = 0u;
    ned.if_family = 2u; /* IFNET_FAMILY_ETHERNET */
    ned.if_unit   = 0u;
    ned.if_name[0] = 'e'; ned.if_name[1] = 'n';
    struct {
        uint32_t vendor_code;
        uint32_t kev_class;
        uint32_t kev_subclass;
        uint32_t event_code;
        struct { uint32_t len; const void *ptr; } dv[5];
    } msg;
    for (uint32_t i = 0; i < sizeof(msg); i++) ((uint8_t *)&msg)[i] = 0u;
    msg.vendor_code  = 1u; /* KEV_VENDOR_APPLE */
    msg.kev_class    = 1u; /* KEV_NETWORK_CLASS */
    msg.kev_subclass = 2u; /* KEV_DL_SUBCLASS */
    msg.event_code   = code; /* 13=KEV_DL_LINK_ON, 12=KEV_DL_LINK_OFF */
    msg.dv[0].len    = sizeof(ned);
    msg.dv[0].ptr    = &ned;
    (void)k_kev_post_msg(&msg);
}

static int wifi_fill_scan_result(uint32_t uptr, const struct wifi_ap_entry *ap)
{
    uint8_t sr[144];
    if (k_copyin(uptr, sr, 144u) != 0)
        return 14;
    uint16_t max_ie = (uint16_t)sr[0x8au] | ((uint16_t)sr[0x8bu] << 8);
    uint32_t ie_ptr = *(uint32_t *)(sr + 0x8cu);
    /* apple80211_scan_result as wifid's Apple80211 reads it (wifid 0x9f0c6..0x9f886) */
    int16_t noise = ap->noise ? ap->noise : -90;
    int16_t rssi = ap->rssi ? ap->rssi : -55;
    for (unsigned i = 0; i < 144u; i++) sr[i] = 0u;
    *(uint32_t *)(sr + 0x00u) = 1u;
    *(uint32_t *)(sr + 0x04u) = 1u;
    *(uint32_t *)(sr + 0x08u) = ap->channel ? ap->channel : 1u;
    *(uint32_t *)(sr + 0x0cu) = 0x0au; /* 2G|20MHz */
    *(int16_t  *)(sr + 0x10u) = 1;     /* nonzero: noise/snr below are valid */
    *(int16_t  *)(sr + 0x12u) = noise;
    *(int16_t  *)(sr + 0x14u) = (int16_t)(rssi - noise);
    *(int16_t  *)(sr + 0x16u) = rssi;
    *(uint16_t *)(sr + 0x18u) = ap->beacon_int ? ap->beacon_int : 100u;
    *(uint16_t *)(sr + 0x1au) = ap->cap;
    for (int i = 0; i < 6; i++) sr[0x1cu + i] = ap->bssid[i];
    sr[0x22u] = 4u;
    *(uint32_t *)(sr + 0x24u) = 1u;
    *(uint32_t *)(sr + 0x28u) = 11u;
    *(uint32_t *)(sr + 0x2cu) = 24u;
    *(uint32_t *)(sr + 0x30u) = 54u;
    sr[0x60u] = ap->ssid_len;
    for (int i = 0; i < 32; i++) sr[0x61u + i] = ap->ssid[i];
    /* Copy only core + WPA/RSN IEs so wifid's strict vendor/HT/VHT/Country IE parsers (0xa5348)
     * never reject an AP with -3900 and drop it from the scan result array. */
    uint8_t fie[512];
    uint16_t iel = 0u;
    uint16_t lim = ap->ie_len;
    if (lim > sizeof(fie)) lim = sizeof(fie);
    uint16_t pos = 0u;
    while (pos + 2u <= lim) {
        uint8_t id = ap->ies[pos];
        uint16_t elen = (uint16_t)(2u + ap->ies[pos + 1u]);
        if (pos + elen > lim) break;
        int keep = (id == 0u || id == 1u || id == 3u || id == 48u || id == 50u);
        if (id == 221u && elen >= 6u &&
            ap->ies[pos + 2u] == 0x00u && ap->ies[pos + 3u] == 0x50u &&
            ap->ies[pos + 4u] == 0xf2u && ap->ies[pos + 5u] == 0x01u)
            keep = 1; /* WPA1 OUI */
        if (keep && (uint32_t)iel + elen <= max_ie && (uint32_t)iel + elen <= sizeof(fie)) {
            for (uint16_t j = 0u; j < elen; j++) fie[iel + j] = ap->ies[pos + j];
            iel = (uint16_t)(iel + elen);
        }
        pos = (uint16_t)(pos + elen);
    }
    if (ie_ptr && iel) (void)k_copyout(fie, ie_ptr, iel);
    sr[0x8au] = (uint8_t)iel;
    sr[0x8bu] = (uint8_t)(iel >> 8);
    *(uint32_t *)(sr + 0x8cu) = ie_ptr;
    return k_copyout(sr, uptr, 144u);
}

/* Install pairwise (PTK, ea=BSSID) or group (GTK, ea=0) key into BCM4334 via WLC_SET_KEY (45) / wsec_key. */
static int wifi_install_key(uint32_t idx, uint32_t algo, uint32_t flags,
                            const uint8_t *key, uint32_t klen, const uint8_t ea[6])
{
    uint8_t wk[164];
    for (unsigned i = 0; i < sizeof(wk); i++) wk[i] = 0u;
    if (klen > 32u) klen = 32u;
    *(uint32_t *)(wk + 0u)   = idx;
    *(uint32_t *)(wk + 4u)   = klen;
    for (uint32_t i = 0u; i < klen; i++) wk[8u + i] = key[i];
    *(uint32_t *)(wk + 112u) = algo;  /* 4=AES_CCM, 2=TKIP */
    *(uint32_t *)(wk + 116u) = flags; /* 2=WL_PRIMARY_KEY */
    if (ea)
        for (int i = 0; i < 6; i++) wk[156u + i] = ea[i];
    uint32_t l = sizeof(wk);
    int rc = bcm_cdc_cmd(45u /* WLC_SET_KEY */, 1, wk, sizeof(wk), &l);
    uint32_t st = w_ctl_status;
    if (rc != 0 || st != 0u) {
        static const char k_wk[] = "wsec_key";
        uint8_t iv[9 + 164];
        for (unsigned i = 0; i < 9u; i++) iv[i] = (uint8_t)k_wk[i];
        for (unsigned i = 0; i < 164u; i++) iv[9u + i] = wk[i];
        uint32_t ivl = sizeof(iv);
        rc = bcm_cdc_cmd(263u /* WLC_SET_VAR */, 1, iv, sizeof(iv), &ivl);
        st = (st << 16) | (w_ctl_status & 0xffffu);
    }
    w_puts("[WIFI-KEY] i="); w_putx(idx); w_puts(" a="); w_putx(algo);
    w_puts(" l="); w_putx(klen); w_puts(" st="); w_putx(st); w_puts("\n");
    return (rc == 0 && (st & 0xffffu) == 0u) ? 0 : -1;
}

static void wifi_send_eapol(const uint8_t dst[6], const uint8_t *eapol, uint32_t elen)
{
    if (elen > 400u) return;
    uint8_t bdc[4 + 14 + 400];
    bdc[0] = 0x20u; bdc[1] = 0u; bdc[2] = 0u; bdc[3] = 0u;
    for (int i = 0; i < 6; i++) bdc[4 + i] = dst[i];
    for (int i = 0; i < 6; i++) bdc[10 + i] = w_st.mac[i];
    bdc[16] = 0x88u; bdc[17] = 0x8eu;
    for (uint32_t i = 0u; i < elen; i++) bdc[18u + i] = eapol[i];
    w_lock();
    (void)sdpcm_tx_frame(SDPCM_CH_DATA, bdc, 18u + elen);
    w_unlock();
}

static void wifi_process_eapol(void)
{
    uint32_t elen = w_wpa_eapol_len;
    if (!elen) return;
    uint8_t pkt[512];
    if (elen > sizeof(pkt)) elen = sizeof(pkt);
    for (uint32_t i = 0u; i < elen; i++) pkt[i] = w_wpa_eapol_rx[i];
    w_wpa_eapol_len = 0u;

    if (!w_wpa_active || elen < 14u + 99u)
        return;
    const uint8_t *src = pkt + 6;
    const uint8_t *ep  = pkt + 14; /* EAPOL header: ver(1), type(1), len(2), desc_type(1), key_info(2)... */
    uint32_t ep_avail = elen - 14u;
    if (ep[1] != 3u /* EAPOL-Key */)
        return;
    uint16_t body_len = ((uint16_t)ep[2] << 8) | ep[3];
    uint32_t ep_len = 4u + (uint32_t)body_len;
    if (ep_len > ep_avail || ep_len < 99u)
        return;
    uint8_t desc_type = ep[4];
    uint16_t kinfo = ((uint16_t)ep[5] << 8) | ep[6];
    uint16_t kd_len = ((uint16_t)ep[97] << 8) | ep[98];
    w_puts("[WIFI-EAPOL] dt="); w_putx(desc_type); w_puts(" ki="); w_putx(kinfo);
    w_puts(" kd="); w_putx(kd_len); w_puts("\n");
    if (99u + (uint32_t)kd_len > ep_len)
        return;
    uint16_t kver = kinfo & 0x0007u;
    int is_pw  = (kinfo & 0x0008u) != 0u;
    int is_ack = (kinfo & 0x0080u) != 0u;
    int is_mic = (kinfo & 0x0100u) != 0u;
    int is_sec = (kinfo & 0x0200u) != 0u;
    int is_enc = (kinfo & 0x1000u) != 0u;

    /* 1. Pairwise Message 1/4: Pairwise | Ack | !MIC */
    if (is_pw && is_ack && !is_mic) {
        /* Generate fresh SNonce via SHA-1(mct || mac || counter || anonce) */
        struct wpa_sha1_ctx sc;
        uint8_t seed[48], d0[20], d1[20];
        uint32_t t = mct ? *(volatile uint32_t *)(mct + 0x100u) : 0x12345678u;
        uint32_t ctr = ++w_wpa_nonce_ctr;
        for (int i = 0; i < 4; i++) seed[i] = (uint8_t)(t >> (i * 8));
        for (int i = 0; i < 4; i++) seed[4 + i] = (uint8_t)(ctr >> (i * 8));
        for (int i = 0; i < 6; i++) seed[8 + i] = w_st.mac[i];
        for (int i = 0; i < 32; i++) seed[14 + i] = ep[17 + i];
        seed[46] = 0u; seed[47] = 1u;
        wpa_sha1_init(&sc); wpa_sha1_update(&sc, seed, 47u); wpa_sha1_final(&sc, d0);
        seed[46] = 1u;
        wpa_sha1_init(&sc); wpa_sha1_update(&sc, seed, 48u); wpa_sha1_final(&sc, d1);
        for (int i = 0; i < 20; i++) w_wpa_snonce[i] = d0[i];
        for (int i = 0; i < 12; i++) w_wpa_snonce[20 + i] = d1[i];

        wpa_derive_ptk(w_wpa_pmk, src, w_st.mac, ep + 17, w_wpa_snonce, w_wpa_ptk);

        /* Sync exact RSN IE that BCM4334 transmitted in 802.11 AssocReq so AP's M2 check matches byte-for-byte */
        {
            static const char k_arie[] = "assoc_req_ies";
            uint8_t ieb[256];
            for (unsigned i = 0; i < sizeof(ieb); i++) ieb[i] = (i < sizeof(k_arie)) ? (uint8_t)k_arie[i] : 0u;
            uint32_t iel = sizeof(ieb);
            if (bcm_cdc_cmd(262u /* WLC_GET_VAR */, 0, ieb, sizeof(ieb), &iel) == 0 && w_ctl_status == 0u) {
                uint32_t p = 0u;
                while (p + 2u <= iel) {
                    uint8_t tag = ieb[p], tl = ieb[p + 1u];
                    if (p + 2u + tl > iel) break;
                    if (tag == 0x30u && (uint32_t)tl + 2u <= sizeof(w_wpa_rsnie)) {
                        w_wpa_rsnie_len = (uint32_t)tl + 2u;
                        for (uint32_t i = 0u; i < w_wpa_rsnie_len; i++) w_wpa_rsnie[i] = ieb[p + i];
                        break;
                    }
                    p += 2u + tl;
                }
            }
        }

        /* Build EAPOL-Key Message 2/4 */
        uint32_t m2_len = 99u + w_wpa_rsnie_len;
        uint8_t m2[160];
        for (unsigned i = 0; i < sizeof(m2); i++) m2[i] = 0u;
        m2[0] = 1u; m2[1] = 3u;
        m2[2] = (uint8_t)((m2_len - 4u) >> 8); m2[3] = (uint8_t)(m2_len - 4u);
        m2[4] = desc_type;
        uint16_t m2_ki = kver | 0x0108u; /* Pairwise | MIC */
        m2[5] = (uint8_t)(m2_ki >> 8); m2[6] = (uint8_t)m2_ki;
        for (int i = 0; i < 8; i++) m2[9 + i] = ep[9 + i];       /* Replay Counter */
        for (int i = 0; i < 32; i++) m2[17 + i] = w_wpa_snonce[i]; /* SNonce */
        m2[97] = (uint8_t)(w_wpa_rsnie_len >> 8); m2[98] = (uint8_t)w_wpa_rsnie_len;
        for (uint32_t i = 0u; i < w_wpa_rsnie_len; i++) m2[99u + i] = w_wpa_rsnie[i];
        uint8_t mic[20];
        wpa_hmac_sha1(w_wpa_ptk, 16u, m2, m2_len, mic);
        for (int i = 0; i < 16; i++) m2[81 + i] = mic[i];
        wifi_send_eapol(src, m2, m2_len);
        w_puts("[WIFI-WPA] M1->M2 ver="); w_putx(kver); w_puts(" ie="); w_putx(w_wpa_rsnie_len); w_puts("\n");
        return;
    }

    /* 2. Pairwise Message 3/4: Pairwise | Ack | MIC */
    if (is_pw && is_ack && is_mic) {
        /* Verify MIC over M3 */
        uint8_t chk[512], mic[20];
        for (uint32_t i = 0u; i < ep_len; i++) chk[i] = ep[i];
        for (int i = 0; i < 16; i++) chk[81 + i] = 0u;
        wpa_hmac_sha1(w_wpa_ptk, 16u, chk, ep_len, mic);
        int bad = 0;
        for (int i = 0; i < 16; i++) if (mic[i] != ep[81 + i]) bad = 1;
        if (bad) {
            w_puts("[WIFI-WPA] M3_BADMIC\n");
            return;
        }
        /* Unwrap GTK from Key Data (RFC 3394 AES Key Unwrap with KEK = ptk[16..31]) */
        uint8_t gtk[32];
        uint32_t gtk_len = 0u, gtk_id = 0u;
        if (is_enc && kd_len >= 16u && kd_len <= 320u) {
            uint8_t plain[320];
            if (wpa_aes_unwrap(w_wpa_ptk + 16, ep + 99, kd_len, plain) == 0) {
                uint32_t plen = (uint32_t)kd_len - 8u;
                uint32_t p = 0u;
                while (p + 2u <= plen) {
                    uint8_t tag = plain[p], tl = plain[p + 1u];
                    if (tag == 0xddu && tl == 0u) break;
                    if (p + 2u + tl > plen) break;
                    if (tag == 0xddu && tl >= 8u &&
                        plain[p + 2u] == 0x00u && plain[p + 3u] == 0x0fu &&
                        plain[p + 4u] == 0xacu && plain[p + 5u] == 0x01u) {
                        gtk_id = plain[p + 6u] & 0x03u;
                        gtk_len = (uint32_t)tl - 6u;
                        if (gtk_len > 32u) gtk_len = 32u;
                        for (uint32_t i = 0u; i < gtk_len; i++) gtk[i] = plain[p + 8u + i];
                    }
                    p += 2u + tl;
                }
            }
        }
        /* Send EAPOL-Key Message 4/4 BEFORE installing PTK so M4 is sent in the clear */
        uint8_t m4[99];
        for (unsigned i = 0; i < sizeof(m4); i++) m4[i] = 0u;
        m4[0] = 1u; m4[1] = 3u; m4[2] = 0u; m4[3] = 95u;
        m4[4] = desc_type;
        uint16_t m4_ki = kver | 0x0308u; /* Pairwise | MIC | Secure */
        m4[5] = (uint8_t)(m4_ki >> 8); m4[6] = (uint8_t)m4_ki;
        for (int i = 0; i < 8; i++) m4[9 + i] = ep[9 + i]; /* Replay Counter */
        wpa_hmac_sha1(w_wpa_ptk, 16u, m4, 99u, mic);
        for (int i = 0; i < 16; i++) m4[81 + i] = mic[i];
        wifi_send_eapol(src, m4, 99u);
        k_iosleep(20u);

        /* Install PTK (TK = ptk[32..47], AES-CCM) and GTK */
        for (int i = 0; i < 6; i++) w_cur_bssid[i] = src[i];
        (void)wifi_install_key(0u, 4u /* AES_CCM */, 2u /* WL_PRIMARY_KEY */, w_wpa_ptk + 32, 16u, src);
        if (gtk_len >= 16u) {
            uint8_t z6[6] = { 0 };
            uint32_t galgo = (gtk_len == 16u) ? 4u /* AES_CCM */ : 2u /* TKIP */;
            (void)wifi_install_key(gtk_id, galgo, 2u, gtk, gtk_len, z6);
        }
        w_wpa_done = 1;
        w_st.link_up = 1u;
        w_puts("[WIFI-WPA] M3->M4 OK gtk="); w_putx((gtk_id << 8) | gtk_len); w_puts("\n");
        return;
    }

    /* 3. Group Key Handshake Message 1/2 (periodic GTK rekey): !Pairwise | Ack | MIC | Secure */
    if (!is_pw && is_ack && is_mic && is_sec && w_wpa_done) {
        uint8_t chk[512], mic[20];
        for (uint32_t i = 0u; i < ep_len; i++) chk[i] = ep[i];
        for (int i = 0; i < 16; i++) chk[81 + i] = 0u;
        wpa_hmac_sha1(w_wpa_ptk, 16u, chk, ep_len, mic);
        for (int i = 0; i < 16; i++) if (mic[i] != ep[81 + i]) return;
        if (is_enc && kd_len >= 16u && kd_len <= 320u) {
            uint8_t plain[320];
            if (wpa_aes_unwrap(w_wpa_ptk + 16, ep + 99, kd_len, plain) == 0) {
                uint32_t plen = (uint32_t)kd_len - 8u, p = 0u;
                while (p + 2u <= plen) {
                    uint8_t tag = plain[p], tl = plain[p + 1u];
                    if (tag == 0xddu && tl == 0u) break;
                    if (p + 2u + tl > plen) break;
                    if (tag == 0xddu && tl >= 8u &&
                        plain[p + 2u] == 0x00u && plain[p + 3u] == 0x0fu &&
                        plain[p + 4u] == 0xacu && plain[p + 5u] == 0x01u) {
                        uint32_t gid = plain[p + 6u] & 0x03u;
                        uint32_t gl = (uint32_t)tl - 6u;
                        if (gl >= 16u && gl <= 32u) {
                            uint8_t z6[6] = { 0 };
                            (void)wifi_install_key(gid, (gl == 16u) ? 4u : 2u, 2u, plain + p + 8u, gl, z6);
                            w_puts("[WIFI-WPA] GTK_REKEY "); w_putx(gid); w_puts("\n");
                        }
                    }
                    p += 2u + tl;
                }
            }
        }
        uint8_t gm2[99];
        for (unsigned i = 0; i < sizeof(gm2); i++) gm2[i] = 0u;
        gm2[0] = 1u; gm2[1] = 3u; gm2[2] = 0u; gm2[3] = 95u;
        gm2[4] = desc_type;
        uint16_t gki = kver | 0x0300u; /* Group | MIC | Secure */
        gm2[5] = (uint8_t)(gki >> 8); gm2[6] = (uint8_t)gki;
        for (int i = 0; i < 8; i++) gm2[9 + i] = ep[9 + i];
        wpa_hmac_sha1(w_wpa_ptk, 16u, gm2, 99u, mic);
        for (int i = 0; i < 16; i++) gm2[81 + i] = mic[i];
        wifi_send_eapol(src, gm2, 99u);
    }
}

static int wifi_do_associate(const uint8_t *ad, uint32_t ad_len)
{
    if (!w_st.f2_ready || ad_len < 56u)
        return 22; /* EINVAL */
    uint16_t auth_lo = (uint16_t)ad[6] | ((uint16_t)ad[7] << 8);
    uint16_t auth_hi = (uint16_t)ad[8] | ((uint16_t)ad[9] << 8);
    uint32_t slen = (uint32_t)ad[12] | ((uint32_t)ad[13] << 8) |
                    ((uint32_t)ad[14] << 16) | ((uint32_t)ad[15] << 24);
    if (slen > 32u) slen = 32u;
    w_cur_ssid_len = slen;
    for (uint32_t i = 0; i < 32u; i++) w_cur_ssid[i] = (i < slen) ? ad[16u + i] : 0u;
    for (uint32_t i = 0; i < 6u; i++) w_cur_bssid[i] = ad[48u + i];
    w_auth_lower = auth_lo;
    w_auth_upper = auth_hi;
    w_st.link_up = 0u;
    w_assoc_result = 0xffffu; /* wifid 0xa9820 in-progress sentinel */
    w_wpa_active = 0;
    w_wpa_done = 0;
    w_assoc_fail = 0;
    w_wpa_eapol_len = 0u;

    /* Sync channel/RSSI and AP RSN group cipher from last scan cache if matched */
    uint8_t grp_cipher[4] = { 0x00u, 0x0fu, 0xacu, 0x04u }; /* default AES-CCMP */
    for (uint32_t i = 0u; i < w_ap_cnt; i++) {
        if (w_aps[i].ssid_len == slen) {
            int eq = 1;
            for (uint32_t j = 0u; j < slen; j++)
                if (w_aps[i].ssid[j] != w_cur_ssid[j]) { eq = 0; break; }
            if (eq) {
                w_cur_chan = w_aps[i].channel;
                w_cur_rssi = w_aps[i].rssi;
                uint16_t pos = 0u;
                while (pos + 2u <= w_aps[i].ie_len) {
                    uint8_t id = w_aps[i].ies[pos], tl = w_aps[i].ies[pos + 1u];
                    if (pos + 2u + tl > w_aps[i].ie_len) break;
                    if (id == 48u && tl >= 6u) {
                        for (int k = 0; k < 4; k++) grp_cipher[k] = w_aps[i].ies[pos + 4u + k];
                        break;
                    }
                    pos = (uint16_t)(pos + 2u + tl);
                }
                break;
            }
        }
    }

    /* Configure BCM4334 auth/security (Open or host-handled WPA/WPA2-PSK via EAPOL + WLC_SET_KEY) */
    uint32_t wsec = 0u, wpa_auth = 0u;
    if (auth_hi & 0x818u) { wsec = 6u /* TKIP | AES */; wpa_auth = 0x80u /* WPA2_PSK */; }
    else if (auth_hi & 0x06u) { wsec = 6u; wpa_auth = 0x04u /* WPA_PSK */; }

    uint32_t klen = 0u;
    if (wpa_auth && ad_len >= 104u) {
        klen = (uint32_t)ad[60] | ((uint32_t)ad[61] << 8) |
               ((uint32_t)ad[62] << 16) | ((uint32_t)ad[63] << 24);
        if (klen == 32u) {
            for (uint32_t i = 0u; i < 32u; i++) w_wpa_pmk[i] = ad[72u + i];
            /* Build standard 22-byte WPA2-PSK CCMP RSN IE matching AP's group cipher */
            static const uint8_t rsn_tail[14] = {
                0x01,0x00, 0x00,0x0f,0xac,0x04, /* 1 pairwise: CCMP */
                0x01,0x00, 0x00,0x0f,0xac,0x02, /* 1 AKM: PSK */
                0x00,0x00                       /* RSN caps */
            };
            w_wpa_rsnie[0] = 0x30u; w_wpa_rsnie[1] = 20u;
            w_wpa_rsnie[2] = 0x01u; w_wpa_rsnie[3] = 0x00u;
            for (int i = 0; i < 4; i++) w_wpa_rsnie[4 + i] = grp_cipher[i];
            for (int i = 0; i < 14; i++) w_wpa_rsnie[8 + i] = rsn_tail[i];
            w_wpa_rsnie_len = 22u;
            w_wpa_active = 1;
        }
    }

    uint8_t b4[4];
    uint32_t l4 = 4u;
    b4[0] = 1u; b4[1] = 0u; b4[2] = 0u; b4[3] = 0u;
    (void)bcm_cdc_cmd(20u /* WLC_SET_INFRA */, 1, b4, 4u, &l4);
    b4[0] = (auth_lo == 2u) ? 1u : 0u; b4[1] = 0u; b4[2] = 0u; b4[3] = 0u; l4 = 4u;
    (void)bcm_cdc_cmd(22u /* WLC_SET_AUTH */, 1, b4, 4u, &l4);
    b4[0] = (uint8_t)wsec; b4[1] = 0u; b4[2] = 0u; b4[3] = 0u; l4 = 4u;
    (void)bcm_cdc_cmd(134u /* WLC_SET_WSEC */, 1, b4, 4u, &l4);
    uint32_t wsec_st = w_ctl_status;
    b4[0] = (uint8_t)wpa_auth; b4[1] = 0u; b4[2] = 0u; b4[3] = 0u; l4 = 4u;
    (void)bcm_cdc_cmd(165u /* WLC_SET_WPA_AUTH */, 1, b4, 4u, &l4);
    uint32_t wpa_st = w_ctl_status;
    {
        static const char k_sw[] = "sup_wpa";
        uint8_t sw[16];
        for (unsigned i = 0; i < sizeof(sw); i++) sw[i] = (i < sizeof(k_sw)) ? (uint8_t)k_sw[i] : 0u;
        uint32_t swl = sizeof(k_sw) + 4u;
        (void)bcm_cdc_cmd(263u /* WLC_SET_VAR */, 1, sw, swl, &swl);
    }
    {
        static const char k_wr[] = "wsec_restrict";
        uint8_t wr[20];
        for (unsigned i = 0; i < sizeof(wr); i++) wr[i] = (i < sizeof(k_wr)) ? (uint8_t)k_wr[i] : 0u;
        wr[sizeof(k_wr)] = (uint8_t)(wpa_auth ? 1u : 0u);
        uint32_t wrl = sizeof(k_wr) + 4u;
        (void)bcm_cdc_cmd(263u /* WLC_SET_VAR */, 1, wr, wrl, &wrl);
    }
    if (wsec_st || wpa_st) {
        w_puts("[WIFI-SEC] ws="); w_putx(wsec_st); w_puts(" wa="); w_putx(wpa_st); w_puts("\n");
    }

    uint8_t ssid_req[36];
    for (unsigned i = 0; i < sizeof(ssid_req); i++) ssid_req[i] = 0u;
    ssid_req[0] = (uint8_t)slen;
    for (uint32_t i = 0; i < slen; i++) ssid_req[4u + i] = w_cur_ssid[i];
    uint32_t srl = sizeof(ssid_req);
    int rc = bcm_cdc_cmd(26u /* WLC_SET_SSID */, 1, ssid_req, sizeof(ssid_req), &srl);
    uint32_t ssid_st = w_ctl_status;

    /* Wait up to 6s while actively pumping SDPCM RX for EAPOL 4-way handshake (or BSSID for Open AP). */
    int linked = 0;
    if (rc == 0) {
        for (int tries = 0; tries < 600; tries++) {
            w_lock();
            for (int r = 0; r < 8 && sdpcm_poll_rx_once(); r++)
                ;
            w_unlock();
            if (w_wpa_eapol_len)
                wifi_process_eapol();
            if (w_assoc_fail)
                break;
            if (w_wpa_active) {
                if (w_wpa_done) {
                    linked = 1;
                    break;
                }
            } else if (w_st.link_up || (tries % 15) == 14) {
                uint8_t bss[6] = { 0 };
                uint32_t bl = 6u;
                if (bcm_cdc_cmd(23u /* WLC_GET_BSSID */, 0, bss, 6u, &bl) == 0 && w_ctl_status == 0u) {
                    if (bss[0] | bss[1] | bss[2] | bss[3] | bss[4] | bss[5]) {
                        for (int i = 0; i < 6; i++) w_cur_bssid[i] = bss[i];
                        linked = 1;
                        break;
                    }
                }
            }
            k_iosleep(10u);
        }
    }
    uint32_t kver = (ad_len >= 60u) ? ((uint32_t)ad[56] | ((uint32_t)ad[57] << 8) |
                                       ((uint32_t)ad[58] << 16) | ((uint32_t)ad[59] << 24)) : 0u;
    uint32_t kcip = (ad_len >= 68u) ? ((uint32_t)ad[64] | ((uint32_t)ad[65] << 8) |
                                       ((uint32_t)ad[66] << 16) | ((uint32_t)ad[67] << 24)) : 0u;
    w_puts("[WIFI-ASSOC] rc="); w_putx((uint32_t)rc);
    w_puts(" st="); w_putx(ssid_st);
    w_puts(" wpa="); w_putx((uint32_t)((w_wpa_active << 4) | w_wpa_done));
    w_puts(" link="); w_putx((uint32_t)linked);
    w_puts(" auth="); w_putx(((uint32_t)auth_hi << 16) | auth_lo);
    w_puts(" kv="); w_putx((kver << 16) | (kcip & 0xffffu));
    w_puts(" kl="); w_putx(klen);
    w_puts(" len="); w_putx(ad_len);
    w_puts(" ssid=");
    for (uint32_t i = 0; i < slen; i++) {
        extern void exy_putc(char);
        exy_putc((w_cur_ssid[i] >= 0x20 && w_cur_ssid[i] < 0x7f) ? (char)w_cur_ssid[i] : '?');
    }
    w_puts("\n");
    if (linked) {
        w_st.link_up = 1u;
        w_assoc_result = 1u; /* wifid 0x1137e0 table[1] -> 0 (success) */
        wifi_post_80211_event(9u /* APPLE80211_M_ASSOC_DONE */, 0, 0u);
        wifi_post_80211_event(3u /* APPLE80211_M_BSSID_CHANGED */, 0, 0u);
        wifi_post_80211_event(4u /* APPLE80211_M_LINK_CHANGED */, 0, 0u);
        wifi_post_dl_event(13u /* KEV_DL_LINK_ON */);
        return 0;
    }
    w_wpa_active = 0;
    w_assoc_result = 9u; /* -3905 auth/assoc failed */
    wifi_post_80211_event(9u /* APPLE80211_M_ASSOC_DONE */, 0, 0u);
    return 5; /* EIO */
}

struct xnu_apple80211req {
    char     ifname[16];
    int32_t  req_type;
    int32_t  req_val;
    uint32_t req_len;
    uint32_t req_data;
};

static int wifi_handle_a80211(struct xnu_apple80211req *req, int is_set)
{
    int32_t t = req->req_type;
    int rc = 0;
    uint8_t tmp[512];
    for (unsigned i = 0; i < sizeof(tmp); i++) tmp[i] = 0u;

    if (!is_set) {
        switch (t) {
        case 1: /* APPLE80211_IOC_SSID */
            if (!w_st.link_up || !w_cur_ssid_len) { rc = 6 /* ENXIO */; break; }
            if (req->req_len > w_cur_ssid_len) req->req_len = w_cur_ssid_len;
            if (req->req_data && req->req_len) rc = k_copyout(w_cur_ssid, req->req_data, req->req_len);
            break;
        case 2: /* APPLE80211_IOC_AUTH_TYPE */
            *(uint32_t *)(tmp + 0) = 1u;
            *(uint32_t *)(tmp + 4) = w_auth_lower;
            *(uint32_t *)(tmp + 8) = w_auth_upper;
            if (req->req_data && req->req_len >= 12u) rc = k_copyout(tmp, req->req_data, 12u);
            break;
        case 4: /* APPLE80211_IOC_CHANNEL */
            *(uint32_t *)(tmp + 0) = 1u;
            *(uint32_t *)(tmp + 4) = 1u;
            *(uint32_t *)(tmp + 8) = w_cur_chan;
            *(uint32_t *)(tmp + 12) = 0x0au; /* 2G | 20MHz */
            if (req->req_data && req->req_len >= 16u) rc = k_copyout(tmp, req->req_data, 16u);
            break;
        case 5: /* APPLE80211_IOC_POWERSAVE */
        case 18: /* APPLE80211_IOC_INT_MIT */
        case 32: /* APPLE80211_IOC_TX_ANTENNA */
        case 37: /* APPLE80211_IOC_ANTENNA_DIVERSITY */
        case 50: /* APPLE80211_IOC_ASSOC_STATUS */
        case 54: /* APPLE80211_IOC_NUM_RADIOS / WOW */
            req->req_val = (t == 54) ? 1 : 0;
            break;
        case 6: /* APPLE80211_IOC_PROTMODE */
            *(uint32_t *)(tmp + 0) = 1u;
            if (req->req_data && req->req_len >= 12u) rc = k_copyout(tmp, req->req_data, 12u);
            break;
        case 7: /* APPLE80211_IOC_TXPOWER */
            *(uint32_t *)(tmp + 0) = 1u;
            *(uint32_t *)(tmp + 4) = 15u;
            *(uint32_t *)(tmp + 8) = 1u; /* dBm */
            if (req->req_data && req->req_len >= 12u) rc = k_copyout(tmp, req->req_data, 12u);
            break;
        case 8: /* APPLE80211_IOC_RATE */
            *(uint32_t *)(tmp + 0) = 1u;
            *(uint32_t *)(tmp + 4) = 1u;
            *(uint32_t *)(tmp + 8) = 54u;
            if (req->req_data && req->req_len >= 12u)
                rc = k_copyout(tmp, req->req_data, (req->req_len < 24u) ? req->req_len : 24u);
            break;
        case 9: /* APPLE80211_IOC_BSSID */
            if (!w_st.link_up) { rc = 57 /* ENOTCONN */; break; }
            if (req->req_data && req->req_len >= 6u) rc = k_copyout(w_cur_bssid, req->req_data, 6u);
            break;
        case 11: /* APPLE80211_IOC_SCAN_RESULT */
            if (w_ap_idx >= w_ap_cnt) {
                rc = 5; /* EIO: exact end-of-scan-results sentinel checked by wifid */
                break;
            }
            if (!req->req_data || req->req_len < 144u) { rc = 22; break; }
            rc = wifi_fill_scan_result(req->req_data, &w_aps[w_ap_idx++]);
            break;
        case 12: /* APPLE80211_IOC_CARD_CAPABILITIES: wifid 0x9b85e reads 8 raw cap bytes into ref+0x2c (no version hdr) */
            tmp[0] = 0x6fu; /* WEP|TKIP|AES|AES_CCM|IBSS|PMGT */
            tmp[1] = 0xe7u; /* TXPMGT|SHSLOT|SHPREAMBLE|WPA1|WPA2|WPA */
            tmp[2] = 0x0fu; /* BURST|WME|SHORT_GI */
            if (req->req_data && req->req_len)
                rc = k_copyout(tmp, req->req_data, (req->req_len < 224u) ? req->req_len : 224u);
            break;
        case 13: /* APPLE80211_IOC_STATE */
            req->req_val = w_st.link_up ? 4 : 0; /* 4=RUN, 0=INIT */
            break;
        case 14: /* APPLE80211_IOC_PHY_MODE */
            *(uint32_t *)(tmp + 0) = 1u;
            *(uint32_t *)(tmp + 4) = 0x1eu; /* A|B|G|N */
            *(uint32_t *)(tmp + 8) = 0x14u; /* G|N */
            if (req->req_data && req->req_len >= 12u) rc = k_copyout(tmp, req->req_data, 12u);
            break;
        case 15: /* APPLE80211_IOC_OP_MODE */
            req->req_val = 1; /* STA */
            break;
        case 16: /* APPLE80211_IOC_RSSI */
        case 17: /* APPLE80211_IOC_NOISE */
            *(uint32_t *)(tmp + 0) = 1u;
            *(uint32_t *)(tmp + 4) = 1u;
            *(uint32_t *)(tmp + 8) = 1u; /* dBm */
            *(int32_t  *)(tmp + 12) = (t == 16) ? w_cur_rssi : -90;
            *(int32_t  *)(tmp + 28) = (t == 16) ? w_cur_rssi : -90;
            if (req->req_data && req->req_len)
                rc = k_copyout(tmp, req->req_data, (req->req_len < 52u) ? req->req_len : 52u);
            break;
        case 19: /* APPLE80211_IOC_POWER */
            *(uint32_t *)(tmp + 0) = 1u;
            *(uint32_t *)(tmp + 4) = 1u;
            *(uint32_t *)(tmp + 8) = w_power;
            if (req->req_data && req->req_len)
                rc = k_copyout(tmp, req->req_data, (req->req_len < 24u) ? req->req_len : 24u);
            break;
        case 21: /* APPLE80211_IOC_ASSOCIATE_RESULT */
            req->req_val = (int32_t)w_assoc_result;
            break;
        case 27: /* APPLE80211_IOC_SUPPORTED_CHANNELS */
            *(uint32_t *)(tmp + 0) = 1u;
            *(uint32_t *)(tmp + 4) = 13u;
            for (uint32_t c = 0u; c < 13u; c++) {
                *(uint32_t *)(tmp + 8u + c * 12u + 0u) = 1u;
                *(uint32_t *)(tmp + 8u + c * 12u + 4u) = c + 1u;
                *(uint32_t *)(tmp + 8u + c * 12u + 8u) = 0x0au;
            }
            if (req->req_data && req->req_len)
                rc = k_copyout(tmp, req->req_data, (req->req_len < 164u) ? req->req_len : 164u);
            break;
        case 28: /* APPLE80211_IOC_LOCALE */
            req->req_val = 1; /* FCC */
            break;
        case 29: /* APPLE80211_IOC_DEAUTH */
            *(uint32_t *)(tmp + 0) = 1u; /* version */
            *(uint32_t *)(tmp + 4) = 0u; /* reason */
            if (req->req_data && req->req_len)
                rc = k_copyout(tmp, req->req_data, (req->req_len < 16u) ? req->req_len : 16u);
            break;
        case 39: /* APPLE80211_IOC_RATE_SET */
            *(uint32_t *)(tmp + 0) = 1u;
            *(uint32_t *)(tmp + 4) = 4u;
            *(uint32_t *)(tmp + 8) = 1u;  *(uint32_t *)(tmp + 12) = 11u;
            *(uint32_t *)(tmp + 16) = 24u; *(uint32_t *)(tmp + 20) = 54u;
            if (req->req_data && req->req_len)
                rc = k_copyout(tmp, req->req_data, (req->req_len < 68u) ? req->req_len : 68u);
            break;
        case 43: /* APPLE80211_IOC_DRIVER_VERSION */
        case 44: /* APPLE80211_IOC_HARDWARE_VERSION */
            {
                const char *vs = (t == 43) ? "ExyBCM4334 6.10" : "BCM4334B1 SDIO";
                uint32_t vl = 0u;
                while (vs[vl]) { tmp[8u + vl] = (uint8_t)vs[vl]; vl++; }
                *(uint32_t *)(tmp + 0) = 1u;
                *(uint32_t *)(tmp + 4) = vl;
                if (req->req_data && req->req_len)
                    rc = k_copyout(tmp, req->req_data, (req->req_len < 8u + vl + 1u) ? req->req_len : (8u + vl + 1u));
            }
            break;
        case 51: /* APPLE80211_IOC_COUNTRY_CODE */
            tmp[0] = 'K'; tmp[1] = 'R'; tmp[2] = 0u;
            if (req->req_data && req->req_len)
                rc = k_copyout(tmp, req->req_data, (req->req_len < 3u) ? req->req_len : 3u);
            break;
        case 57: /* APPLE80211_IOC_GUARD_INTERVAL */
            req->req_val = 0;
            break;
        case 103: /* APPLE80211_IOC_CURRENT_NETWORK (0x67, 144B scan_result) */
            if (!w_st.link_up || !w_cur_ssid_len) { rc = 57 /* ENOTCONN */; break; }
            if (!req->req_data || req->req_len < 144u) { rc = 22; break; }
            {
                const struct wifi_ap_entry *hit = 0;
                for (uint32_t i = 0u; i < w_ap_cnt; i++) {
                    if (w_aps[i].ssid_len == w_cur_ssid_len) {
                        int eq = 1;
                        for (uint32_t j = 0u; j < w_cur_ssid_len; j++)
                            if (w_aps[i].ssid[j] != w_cur_ssid[j]) { eq = 0; break; }
                        if (eq) { hit = &w_aps[i]; break; }
                    }
                }
                if (hit) {
                    rc = wifi_fill_scan_result(req->req_data, hit);
                } else {
                    struct wifi_ap_entry cur;
                    for (uint32_t i = 0; i < sizeof(cur); i++) ((uint8_t *)&cur)[i] = 0u;
                    for (int i = 0; i < 6; i++) cur.bssid[i] = w_cur_bssid[i];
                    cur.ssid_len = (uint8_t)w_cur_ssid_len;
                    for (uint32_t i = 0; i < w_cur_ssid_len; i++) cur.ssid[i] = w_cur_ssid[i];
                    cur.channel = (uint8_t)w_cur_chan;
                    cur.rssi = (int16_t)w_cur_rssi;
                    cur.noise = -90;
                    cur.beacon_int = 100u;
                    cur.cap = 0x0411u;
                    rc = wifi_fill_scan_result(req->req_data, &cur);
                }
            }
            break;
        case 96: /* APPLE80211_IOC_VIRTUAL_IF_ROLE: must fail so wifid keeps en0 as primary! */
            rc = 45; /* EOPNOTSUPP */
            break;
        case 106: /* 0x6a: APPLE80211_IOC_RSSI_BOUNDS (ver, min, max, threshold) */
            if (req->req_data && req->req_len >= 16u) {
                int32_t rb[4];
                rb[0] = 1;
                rb[1] = -95;
                rb[2] = -35;
                rb[3] = -85;
                rc = k_copyout(rb, req->req_data, 16u);
            } else {
                rc = 22;
            }
            break;
        default:
            rc = 45; /* EOPNOTSUPP */
            break;
        }
    } else {
        switch (t) {
        case 19: /* APPLE80211_IOC_POWER */
            if (req->req_data && req->req_len >= 12u && k_copyin(req->req_data, tmp, 12u) == 0) {
                uint32_t np = *(uint32_t *)(tmp + 8);
                w_power = np ? 1u : 0u;
                uint8_t d4[4] = { 0 };
                uint32_t dl = 4u;
                (void)bcm_cdc_cmd(w_power ? 2u /* WLC_UP */ : 3u /* WLC_DOWN */, 1, d4, 0u, &dl);
                if (!w_power && w_st.link_up) {
                    w_st.link_up = 0u;
                    w_cur_ssid_len = 0u;
                    wifi_post_80211_event(4u /* APPLE80211_M_LINK_CHANGED */, 0, 0u);
                    wifi_post_dl_event(12u /* KEV_DL_LINK_OFF */);
                }
                wifi_post_80211_event(1u /* APPLE80211_M_POWER_CHANGED */, 0, 0u);
            }
            break;
        case 10: /* APPLE80211_IOC_SCAN_REQ */
        case 86: /* APPLE80211_IOC_SCAN_REQ_MULTIPLE */
            /* Run scan asynchronously in wifi_rx_thread so ioctl returns before SCAN_DONE posts. */
            w_scan_pending = 1u;
            break;
        case 20: /* APPLE80211_IOC_ASSOCIATE */
            if (req->req_data && req->req_len >= 56u) {
                uint32_t cpy = (req->req_len < sizeof(w_assoc_buf)) ? req->req_len : sizeof(w_assoc_buf);
                if (k_copyin(req->req_data, w_assoc_buf, cpy) == 0) {
                    w_assoc_len = cpy;
                    w_assoc_result = 0xffffu; /* wifid 0xa9820 in-progress sentinel */
                    w_assoc_pending = 1u; /* run async in wifi_rx_thread after ioctl returns */
                    rc = 0;
                } else {
                    rc = 14;
                }
            } else {
                rc = 22;
            }
            break;
        case 22: /* APPLE80211_IOC_DISASSOCIATE */
            {
                uint32_t was_up = w_st.link_up;
                uint8_t d4[4];
                d4[0] = 0u; d4[1] = 0u; d4[2] = 0u; d4[3] = 0u;
                uint32_t dl = 4u;
                (void)bcm_cdc_cmd(52u /* WLC_DISASSOC */, 1, d4, 0u, &dl);
                w_st.link_up = 0u;
                w_assoc_result = 0u;
                w_wpa_active = 0;
                w_wpa_done = 0;
                w_cur_ssid_len = 0u;
                if (was_up) {
                    wifi_post_80211_event(4u /* APPLE80211_M_LINK_CHANGED */, 0, 0u);
                    wifi_post_dl_event(12u /* KEV_DL_LINK_OFF */);
                }
            }
            break;
        case 5: /* POWERSAVE */
        case 18: /* INT_MIT */
        case 51: /* COUNTRY_CODE */
            rc = 0;
            break;
        default:
            rc = 45; /* EOPNOTSUPP */
            break;
        }
    }
    return rc;
}

static int wifi_en0_ioctl(void *ifp, uint32_t cmd, void *data)
{
    if (cmd == 0xc0286938u /* SIOCGIFMEDIA */ && data) {
        uint8_t *m = (uint8_t *)data;
        *(uint32_t *)(m + 0x10u) = 0x80u; /* IFM_IEEE80211 */
        *(uint32_t *)(m + 0x14u) = 0u;
        *(uint32_t *)(m + 0x18u) = 1u | (w_st.link_up ? 2u : 0u); /* IFM_AVALID | IFM_ACTIVE */
        *(uint32_t *)(m + 0x1cu) = 0x80u;
        uint32_t ucnt = *(uint32_t *)(m + 0x20u);
        uint32_t uptr = *(uint32_t *)(m + 0x24u);
        *(uint32_t *)(m + 0x20u) = 1u;
        if (ucnt >= 1u && uptr) {
            uint32_t mw = 0x80u;
            (void)k_copyout(&mw, uptr, 4u);
        }
        return 0;
    }
    if ((cmd == 0xc02069c9u /* SIOCGA80211 */ || cmd == 0x802069c8u /* SIOCSA80211 */) && data)
        return wifi_handle_a80211((struct xnu_apple80211req *)data, (cmd == 0x802069c8u) ? 1 : 0);
    return k_ether_ioctl(ifp, cmd, data);
}

static void wifi_en0_detach(void *ifp) { (void)ifp; }

static void wifi_rx_thread(void *param, int wait_res)
{
    (void)param; (void)wait_res;
    for (;;) {
        if (!w_st.f2_ready) {
            k_iosleep(100u);
            continue;
        }
        if (w_assoc_pending) {
            w_assoc_pending = 0u;
            k_iosleep(30u); /* let wifid arm its async ASSOC_DONE waiter after ioctl(20) returns */
            (void)wifi_do_associate(w_assoc_buf, w_assoc_len);
            continue;
        }
        if (w_scan_pending) {
            w_scan_pending = 0u;
            k_iosleep(30u); /* let wifid arm its RunLoop/KEV scan waiter after ioctl(10) returns */
            (void)wifi_do_scan(1);
            continue;
        }
        int got = 0;
        w_lock();
        got = sdpcm_poll_rx_once();
        w_unlock();
        if (w_wpa_eapol_len)
            wifi_process_eapol();
        if (!got)
            k_iosleep(w_st.link_up ? 5u : 20u);
    }
}

/* Matches XNU struct ifnet_init_params (ARM32) */
struct xnu_ifnet_init_params {
    const void *uniqueid;
    uint32_t    uniqueid_len;
    const char *name;
    uint32_t    unit;
    uint32_t    family;
    uint32_t    type;
    void       *output;
    void       *demux;
    void       *add_proto;
    void       *del_proto;
    void       *check_multi;
    void       *framer;
    void       *softc;
    void       *ioctl;
    void       *set_bpf_tap;
    void       *detach;
    void       *event;
    const void *broadcast_addr;
    uint32_t    broadcast_len;
};

static void wifi_attach_en0(void)
{
    if (w_en0) return;
    static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    static const char uid[] = "exy-bcm4334-en0";
    struct xnu_ifnet_init_params p;
    for (uint32_t i = 0; i < sizeof(p); i++) ((uint8_t *)&p)[i] = 0u;
    p.uniqueid       = uid;
    p.uniqueid_len   = sizeof(uid) - 1u;
    p.name           = "en";
    p.unit           = 0u;
    p.family         = 2u;   /* IFNET_FAMILY_ETHERNET */
    p.type           = 6u;   /* IFT_ETHER */
    p.output         = (void *)wifi_en0_output;
    p.demux          = (void *)k_ether_demux;
    p.add_proto      = (void *)k_ether_add_proto;
    p.del_proto      = (void *)k_ether_del_proto;
    p.check_multi    = (void *)k_ether_check_multi;
    p.framer         = (void *)k_ether_frameout;
    p.softc          = &w_st;
    p.ioctl          = (void *)wifi_en0_ioctl;
    p.detach         = (void *)wifi_en0_detach;
    p.broadcast_len  = 6u;
    p.broadcast_addr = bcast;

    void *ifp = 0;
    int arc = k_ifnet_allocate(&p, &ifp);
    w_puts("[WIFI-EN0] alloc="); w_putx((uint32_t)arc);
    w_puts(" ifp="); w_putx((uint32_t)(uintptr_t)ifp);
    if (arc != 0 || !ifp) {
        w_puts("\n");
        return;
    }
    k_ifnet_set_mtu(ifp, 1500u);
    k_ifnet_set_addrlen(ifp, 6u);
    k_ifnet_set_hdrlen(ifp, 14u);
    k_ifnet_set_baudrate(ifp, 54000000ull);
    k_ifnet_set_flags(ifp, 0x8843u /* UP|BROADCAST|RUNNING|SIMPLEX|MULTICAST */, 0xffffu);

    /* Minimal sockaddr_dl for ifnet_attach (sdl_nlen=0, MAC at sdl_data[0..5]) */
    uint8_t sdl[20];
    for (int i = 0; i < 20; i++) sdl[i] = 0u;
    sdl[0] = 20u;  /* sdl_len */
    sdl[1] = 18u;  /* AF_LINK */
    sdl[4] = 6u;   /* IFT_ETHER */
    sdl[5] = 0u;   /* sdl_nlen = 0 (XNU fills if_xname) */
    sdl[6] = 6u;   /* sdl_alen = 6 */
    for (int i = 0; i < 6; i++) sdl[8 + i] = w_st.mac[i];

    int trc = k_ifnet_attach(ifp, sdl);
    w_puts(" att="); w_putx((uint32_t)trc);
    w_puts("\n");
    if (trc == 0) {
        w_en0 = ifp;
        w_st.en0_attached = 1u;
        (void)k_ifnet_set_lladdr(ifp, w_st.mac, 6u);
        (void)k_bpf_attach(ifp, 1u /* DLT_EN10MB */, 14u, 0, 0);
        if (!w_rx_started) {
            void *th = 0;
            if (k_thread_start(wifi_rx_thread, 0, &th) == 0)
                w_rx_started = 1;
        }
        wifi_register_iokit();
    }
}

/* --- Userspace Control Entry Point (IMGSGXSharedUserClient sel 22) --------- */

int exy_wifi_handle_op(const void *in_buf, uint32_t in_len, void *out_buf, uint32_t *out_len)
{
    if (!in_buf || in_len < 16u || !out_buf || !out_len || *out_len < 8u)
        return -1;
    const struct exy_wifi_req *req = (const struct exy_wifi_req *)in_buf;
    struct exy_wifi_rsp *rsp = (struct exy_wifi_rsp *)out_buf;
    rsp->err = 0;
    rsp->out_len = 0u;

    if (req->op == WIFI_OP_STATUS) {
        uint32_t sz = sizeof(w_st);
        const uint8_t *s = (const uint8_t *)&w_st;
        for (uint32_t i = 0; i < sz; i++) rsp->data[i] = s[i];
        rsp->out_len = sz;
        *out_len = 8u + sz;
        return 0;
    }
    if (req->op == WIFI_OP_INIT) {
        w_lock();
        rsp->err = exy_wifi_sdio_init();
        w_unlock();
        *out_len = 8u;
        return 0;
    }
    if (req->op == WIFI_OP_FW_WRITE) {
        uint32_t off = req->arg0;
        uint32_t n   = req->arg1;
        if (!w_st.probed || n > sizeof(req->data) || off + n > BCM4334_RAM_SZ) {
            rsp->err = -10;
            *out_len = 8u;
            return 0;
        }
        w_lock();
        rsp->err = bcm_bp_write_mem(off, req->data, n);
        w_unlock();
        *out_len = 8u;
        return 0;
    }
    if (req->op == WIFI_OP_NVRAM_BOOT) {
        /* Real Samsung Exynos 4412 (midas/m0/n7100) Murata BCM4334B1 NVRAM (devid=0x4380, swctrlmap_2g/5g).
         * Embedded here so kernel-only flashes pick it up without reflashing the 1.5 GiB iOS rootfs. */
        static const char k_midas_nvram[] =
            "manfid=0x2d0\0"
            "prodid=0x05c1\0"
            "vendid=0x14e4\0"
            "devid=0x4380\0"
            "boardtype=0x05de\0"
            "boardrev=0x1200\0"
            "boardnum=22\0"
            "macaddr=3c:a1:0d:0a:82:e1\0"
            "sromrev=3\0"
            "boardflags=0x10081a01\0"
            "xtalfreq=37400\0"
            "nocrc=1\0"
            "ag0=252\0"
            "aa2g=1\0"
            "aa5g=1\0"
            "ccode=ALL\0"
            "regrev=0\0"
            "pa0itssit=0x20\0"
            "pa0b0=0x15a3\0"
            "pa0b1=0xfd30\0"
            "pa0b2=0xff17\0"
            "tssifloor2g=24\0"
            "extpagain2g=2\0"
            "extpagain5g=2\0"
            "cckPwrOffset=8\0"
            "rssismf2g=0xf\0"
            "rssismc2g=0x9\0"
            "rssisav2g=0x1\0"
            "rssismf5g=0x9\0"
            "rssismc5g=0x9\0"
            "rssisav5g=0x1\0"
            "pa1lob0=0x1532\0"
            "pa1lob1=0xfd5e\0"
            "pa1lob2=0xff3e\0"
            "tssifloor5gl=26\0"
            "pa1b0=0x1542\0"
            "pa1b1=0xfd67\0"
            "pa1b2=0xff51\0"
            "tssifloor5gm=26\0"
            "pa1hib0=0x1544\0"
            "pa1hib1=0xfd4c\0"
            "pa1hib2=0xff42\0"
            "tssifloor5gh=26\0"
            "PwrOffset40mhz2g=4\0"
            "PwrOffset40mhz5g=-10\0"
            "rxpo5g=0\0"
            "maxp2ga0=0x52\0"
            "ofdm2gpo=0xaaaaaaa2\0"
            "mcs2gpo0=0xdddd\0"
            "mcs2gpo1=0xdddd\0"
            "mcs2gpo2=0xdddd\0"
            "mcs2gpo3=0xdddd\0"
            "maxp5ga0=0x50\0"
            "maxp5gla0=0x49\0"
            "maxp5gha0=0x54\0"
            "ofdm5gpo=0x44444441\0"
            "ofdm5glpo=0x66666663\0"
            "ofdm5ghpo=0x55555552\0"
            "mcs5gpo0=0x6666\0"
            "mcs5gpo1=0x6666\0"
            "mcs5glpo0=0x8888\0"
            "mcs5glpo1=0x8888\0"
            "mcs5ghpo0=0x6666\0"
            "mcs5ghpo1=0x6666\0"
            "mcs5gpo2=0x6666\0"
            "mcs5gpo3=0x6666\0"
            "mcs5glpo2=0x8888\0"
            "mcs5glpo3=0x8888\0"
            "mcs5ghpo2=0x6666\0"
            "mcs5ghpo3=0x6666\0"
            "pagc2g=0x10\0"
            "swctrlmap_2g=0x00010001,0x00420002,0x00020002,0x044202,0x1ff\0"
            "swctrlmap_5g=0x00080008,0x00300010,0x00100010,0x044202,0x238\0"
            "elna_off_gain_idx_2g=27\0"
            "triso2g=4\0"
            "triso5g=5\0"
            "muxenab=0x10\0"
            "dacrate2xen=1\0"
            "txalpfbyp=1\0"
            "txalpfpu=1\0"
            "dacpu.fab.4=1\0"
            "aci_detect_en_2g=1\0"
            "gain_settle_dly_2g=4\0"
            "gain_settle_dly_5g=4\0"
            "noise_cal_po_2g=-1\0"
            "noise_cal_po_40_2g=-1\0"
            "noise_cal_high_gain_2g=73\0"
            "noise_cal_nf_substract_val_2g=346\0"
            "noise_cal_po_5g=-1\0"
            "noise_cal_po_40_5g=-1\0"
            "noise_cal_high_gain_5g=73\0";
        if (!w_st.probed) {
            rsp->err = -11;
            *out_len = 8u;
            return 0;
        }
        uint32_t n = (uint32_t)sizeof(k_midas_nvram);
        uint32_t padded = (n + 3u) & ~3u;
        uint32_t words = padded >> 2;
        uint32_t token = ((~words & 0xffffu) << 16) | (words & 0xffffu);
        uint32_t nv_addr = BCM4334_RAM_SZ - 4u - padded;

        w_lock();
        int rc = bcm_bp_write_mem(nv_addr, (const uint8_t *)k_midas_nvram, n);
        if (rc == 0 && padded > n) {
            uint8_t zpad[4] = { 0, 0, 0, 0 };
            rc = bcm_bp_write_mem(nv_addr + n, zpad, padded - n);
        }
        if (rc == 0)
            rc = bcm_bp_write32(BCM4334_RAM_SZ - 4u, token);
        if (rc == 0) {
            uint32_t rb4 = 0u, rbt = 0u, rbt2 = 0u, arm_r = 0u, sd_i = 0u;
            (void)bcm_bp_read32(0x00000004u, &rb4);
            (void)bcm_bp_read32(BCM4334_RAM_SZ - 4u, &rbt);
            /* Keep F2 OFF during ARM boot so firmware sees a clean 0->1 F2 enable edge after init */
            uint8_t zero = 0u, f1_only = 0x02u;
            (void)sdio_cmd52(0u, CCCR_IOEx, &f1_only, 1);
            (void)sdio_cmd52(1u, 0x1000fu /* SBSDIO_FUNC1_SDIOPULLUP */, &zero, 1);
            (void)bcm_bp_write32(w_sdio_base + 0x48u /* tosbmailboxdata */, 4u << 16);
            (void)bcm_bp_write32(w_sdio_base + 0x20u /* INTSTATUS */, 0xffffffffu);
            bcm_core_reset(w_arm_wrap, 0u, 0u, 0u);
            /* Wait until firmware overwrites top-of-RAM token with sdpcm_shared ptr, then finish reclaim */
            for (int i = 0; i < 60; i++) {
                rbt2 = rbt;
                (void)bcm_bp_read32(BCM4334_RAM_SZ - 4u, &rbt2);
                if (rbt2 != rbt && rbt2 >= 0x1000u && rbt2 < BCM4334_RAM_SZ)
                    break;
                w_udelay(5000);
            }
            w_udelay(150000);
            /* Request HT clock (SBSDIO_HT_AVAIL_REQ = 0x10), re-seed SDPCM v4, set F2 blksize & IEN */
            uint8_t csr = 0x10u;
            (void)sdio_cmd52(1u, BCM4334_F1_CSR, &csr, 1);
            for (int i = 0; i < 100; i++) {
                uint8_t s = 0u;
                if (sdio_cmd52(1u, BCM4334_F1_CSR, &s, 0) == 0 && (s & 0x80u)) break;
                w_udelay(2000);
            }
            (void)bcm_bp_write32(w_sdio_base + 0x48u /* tosbmailboxdata */, 4u << 16);
            uint8_t b512[2] = { 0u, 2u }, ien = 0x07u;
            (void)sdio_cmd52(0u, FBR_BLKSIZE(2) + 0u, &b512[0], 1);
            (void)sdio_cmd52(0u, FBR_BLKSIZE(2) + 1u, &b512[1], 1);
            (void)sdio_cmd52(0u, CCCR_IENx, &ien, 1);
            /* Clean 0->1 enable of SDIO Function 2 after firmware is in idle loop */
            uint8_t v = 0x06u; /* F1 + F2 */
            (void)sdio_cmd52(0u, CCCR_IOEx, &v, 1);
            uint8_t last_ior = 0u, last_csr = 0u, last_ioe = 0u;
            for (int tries = 0; tries < 200; tries++) {
                v = 0u;
                if (sdio_cmd52(0u, CCCR_IORx, &v, 0) == 0) {
                    last_ior = v;
                    if (v & 0x04u) {
                        w_st.f2_ready = 1u;
                        w_st.fw_loaded = 1u;
                        break;
                    }
                }
                w_udelay(5000);
            }
            if (w_st.f2_ready) {
                uint8_t wm = 8u;
                (void)sdio_cmd52(1u, 0x10008u /* SBSDIO_WATERMARK */, &wm, 1);
                (void)sdio_cmd52(1u, 0x1000fu /* SBSDIO_FUNC1_SDIOPULLUP */, &zero, 1);
                (void)bcm_bp_write32(w_sdio_base + 0x24u /* hostintmask */, 0x200000f0u);
            }
            (void)sdio_cmd52(0u, CCCR_IOEx, &last_ioe, 0);
            (void)sdio_cmd52(1u, BCM4334_F1_CSR, &last_csr, 0);
            (void)bcm_bp_read32(BCM4334_RAM_SZ - 4u, &rbt2);
            (void)bcm_bp_read32(w_arm_wrap + 0x800u, &arm_r);
            (void)bcm_bp_read32(w_sdio_base + 0x20u, &sd_i);
            w_puts("[WIFI-BOOT] rb4="); w_putx(rb4);
            w_puts(" tok="); w_putx(rbt);
            w_puts(" post="); w_putx(rbt2);
            w_puts(" ior="); w_putx(((uint32_t)last_ioe << 16) | ((uint32_t)last_csr << 8) | last_ior);
            w_puts(" arm="); w_putx(arm_r);
            w_puts(" sdi="); w_putx(sd_i);
            w_puts("\n");
            if (!w_st.f2_ready) {
                uint32_t cc = 0u, cs = 0u, im = 0u, fim = 0u;
                (void)bcm_bp_read32(w_sdio_base + 0x00u, &cc);
                (void)bcm_bp_read32(w_sdio_base + 0x04u, &cs);
                (void)bcm_bp_read32(w_sdio_base + 0x28u, &im);
                (void)bcm_bp_read32(w_sdio_base + 0x34u, &fim);
                w_puts("[WIFI-SD] cc="); w_putx(cc);
                w_puts(" cs="); w_putx(cs);
                w_puts(" im="); w_putx(im);
                w_puts(" fim="); w_putx(fim);
                w_puts("\n");
            }
            /* Dump sdpcm_shared + rte_console from BCM4334 SRAM */
            if (rbt2 >= 0x1000u && rbt2 < BCM4334_RAM_SZ - 32u) {
                uint32_t fl = 0u, tr = 0u, ax = 0u, af = 0u, al = 0u, con = 0u;
                (void)bcm_bp_read32(rbt2 + 0x00u, &fl);
                (void)bcm_bp_read32(rbt2 + 0x04u, &tr);
                (void)bcm_bp_read32(rbt2 + 0x08u, &ax);
                (void)bcm_bp_read32(rbt2 + 0x0cu, &af);
                (void)bcm_bp_read32(rbt2 + 0x10u, &al);
                (void)bcm_bp_read32(rbt2 + 0x14u, &con);
                w_puts("[WIFI-SH] fl="); w_putx(fl);
                w_puts(" tr="); w_putx(tr);
                w_puts(" ax="); w_putx(ax);
                w_puts(" af="); w_putx(af);
                w_puts(" al="); w_putx(al);
                w_puts(" con="); w_putx(con);
                w_puts("\n");
                if (con >= 0x1000u && con < BCM4334_RAM_SZ - 24u) {
                    uint32_t lbuf = 0u, lsz = 0u, lidx = 0u;
                    (void)bcm_bp_read32(con + 0x08u, &lbuf);
                    (void)bcm_bp_read32(con + 0x0cu, &lsz);
                    (void)bcm_bp_read32(con + 0x10u, &lidx);
                    w_puts("[WIFI-CON] buf="); w_putx(lbuf);
                    w_puts(" sz="); w_putx(lsz);
                    w_puts(" idx="); w_putx(lidx);
                    w_puts("\n");
                    if (lbuf >= 0x1000u && lbuf < BCM4334_RAM_SZ && lsz > 0u && lsz <= 0x4000u) {
                        uint32_t n = (lidx > 0u && lidx <= lsz) ? lidx : lsz;
                        if (n > 512u) n = 512u;
                        w_puts("[WIFI-LOG] ");
                        for (uint32_t off = 0u; off < n; off += 4u) {
                            uint32_t w = 0u;
                            if (bcm_bp_read32((lbuf + off) & ~3u, &w) != 0) break;
                            for (int b = 0; b < 4; b++) {
                                char c = (char)((w >> (b * 8)) & 0xffu);
                                if (c == '\n') { w_puts(" | "); }
                                else if (c >= 0x20 && c < 0x7f) {
                                    extern void exy_putc(char);
                                    exy_putc(c);
                                }
                            }
                        }
                        w_puts("\n");
                    }
                }
            }
            if (!w_st.f2_ready)
                rc = -12;
        }
        w_unlock();

        if (rc == 0) {
            /* Default GT-I9300 EFS MAC (overridden by firmware cur_etheraddr if returned) */
            w_st.mac[0] = 0x3cu; w_st.mac[1] = 0xa1u; w_st.mac[2] = 0x0du;
            w_st.mac[3] = 0x0au; w_st.mac[4] = 0x82u; w_st.mac[5] = 0xe1u;
            static const char k_eth_var[] = "cur_etheraddr";
            uint8_t q[32];
            for (unsigned i = 0; i < sizeof(q); i++)
                q[i] = (i < sizeof(k_eth_var)) ? (uint8_t)k_eth_var[i] : 0u;
            uint32_t ql = sizeof(q);
            int crc = bcm_cdc_cmd(262u /* WLC_GET_VAR */, 0, q, 20u, &ql);
            w_puts("[WIFI-CDC] rc="); w_putx((uint32_t)crc);
            w_puts(" ql="); w_putx(ql);
            if (crc == 0 && ql >= 6u) {
                for (int i = 0; i < 6; i++) w_st.mac[i] = q[i];
                w_puts(" mac=");
                for (int i = 0; i < 6; i++) w_putx(w_st.mac[i]);
            }
            w_puts("\n");
            /* Disable MPC & power-save, enable event_msgs, bring radio up, pre-populate scan cache */
            uint8_t mpc[8] = { 'm', 'p', 'c', 0u, 0u, 0u, 0u, 0u };
            uint32_t ml = 8u;
            (void)bcm_cdc_cmd(263u /* WLC_SET_VAR */, 1, mpc, 8u, &ml);
            uint8_t pm[4] = { 0u, 0u, 0u, 0u };
            uint32_t pml = 4u;
            (void)bcm_cdc_cmd(86u /* WLC_SET_PM */, 1, pm, 4u, &pml);
            {
                static const char k_ev[] = "event_msgs";
                uint8_t evm[32];
                for (unsigned i = 0; i < sizeof(evm); i++) evm[i] = 0u;
                for (unsigned i = 0; i < sizeof(k_ev); i++) evm[i] = (uint8_t)k_ev[i];
                for (unsigned i = 0; i < 8u; i++) evm[sizeof(k_ev) + i] = 0xffu; /* events 0..63 */
                uint32_t evl = sizeof(k_ev) + 16u;
                (void)bcm_cdc_cmd(263u /* WLC_SET_VAR */, 1, evm, evl, &evl);
            }
            uint8_t infra[4] = { 1u, 0u, 0u, 0u };
            uint32_t il = 4u;
            (void)bcm_cdc_cmd(20u /* WLC_SET_INFRA */, 1, infra, 4u, &il);
            uint8_t dummy[4] = { 0 };
            uint32_t dl = 4u;
            (void)bcm_cdc_cmd(2u /* WLC_UP */, 1, dummy, 0u, &dl);
            k_iosleep(100u);
            (void)wifi_do_scan(0);
            wifi_attach_en0();
        }
        rsp->err = rc;
        *out_len = 8u;
        return 0;
    }
    if (req->op == WIFI_OP_CDC_CMD) {
        uint32_t cmd = req->arg0;
        uint32_t n   = req->arg1;
        int is_set   = (req->arg2 & 1u) ? 1 : 0;
        if (n > sizeof(rsp->data)) n = sizeof(rsp->data);
        for (uint32_t i = 0; i < n; i++) rsp->data[i] = req->data[i];
        uint32_t rl = sizeof(rsp->data);
        rsp->err = bcm_cdc_cmd(cmd, is_set, rsp->data, n, &rl);
        rsp->out_len = (rsp->err == 0) ? rl : 0u;
        *out_len = 8u + rsp->out_len;
        return 0;
    }
    rsp->err = -99;
    *out_len = 8u;
    return 0;
}

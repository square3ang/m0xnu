/*
 * Broadcom BCM4334 SDIO Wi-Fi driver for Galaxy S3 (Exynos 4412 SDHCI3 @ 0x12540000)
 * bridged into XNU BSD networking (en0 via ifnet_allocate / ifnet_attach / ether_*).
 *
 * Firmware (brcmfmac4334-sdio.bin) and GT-I9300 NVRAM are streamed from userspace
 * via IMGSGXSharedUserClient selector 22 (MALI_CMD_WIFI_OP), keeping DRV_AREA compact.
 */
#include <stdint.h>

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
#define k_mbuf_setlen         ((void (*)(void *, uint32_t))0x802bab4d)
#define k_bpf_attach          ((int (*)(void *, uint32_t, uint32_t, void *, void *))0x80117499)
#define k_iosleep             ((void (*)(uint32_t))0x80328fd1)
#define k_thread_start        ((int (*)(void (*)(void *, int), void *, void **))0x80042c15)

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

static uint8_t w_tx_buf[1600] __attribute__((aligned(4)));
static uint8_t w_rx_buf[1600] __attribute__((aligned(4)));
static uint8_t w_ctl_rsp[1024];
static volatile uint32_t w_ctl_rsp_len;
static volatile uint16_t w_ctl_wait_id;
static volatile int      w_ctl_got;

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
        if (st & (1u << 15)) { wwr(w_sd3 + 0x30u, st); return -4; }
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
        if (n > 256u) n = 256u;
        uint32_t off = cur & 0x7fffu;
        if ((n & 63u) == 0u && n >= 64u) {
            if (sdio_cmd53_chunk(1u, off, (uint8_t *)(data + pos), n, 1, 1, 1, 64u) != 0)
                return -2;
        } else {
            if (sdio_cmd53_chunk(1u, off, (uint8_t *)(data + pos), n, 1, 1, 0, n) != 0)
                return -3;
        }
        pos += n;
    }
    return 0;
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

    /* Reset SDHCI3, select SCLK_MMC3 (CONTROL2 bits 5:4 = 2), 3.3V power, ~200 kHz init clock */
    wwr(w_sd3 + 0x2cu, (wrd(w_sd3 + 0x2cu) & 0x00ffffffu) | (1u << 24));
    for (uint32_t i = 0; i < 1000u && (wrd(w_sd3 + 0x2cu) & (1u << 24)); i++) w_udelay(10);
    wwr(w_sd3 + 0x80u /* CONTROL2 */, (1u << 30) | (1u << 15) | (1u << 14) | (2u << 4));
    wwr(w_sd3 + 0x84u /* CONTROL3 */, 0u);
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
    /* Raise SDCLK to ~25 MHz (/2 of 50 MHz SCLK_MMC3) */
    wwr(w_sd3 + 0x2cu, wrd(w_sd3 + 0x2cu) & ~4u);
    wwr(w_sd3 + 0x2cu, (0x01u << 8) | 0x0001u);
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

    uint32_t cid = 0u;
    if (bcm_bp_read32(BCM4334_AI_CC, &cid) == 0)
        w_st.chip_id = cid & 0xffffu;
    /* Hold ARM CM3 core in reset while firmware is uploaded to SRAM */
    (void)bcm_bp_write32(BCM4334_AI_WRAP + 0x800u /* AI_RESETCTRL */, 1u);
    (void)bcm_bp_write32(BCM4334_AI_WRAP + 0x408u /* AI_IOCTRL */, 0x03u);
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
    if (sdio_cmd53_chunk(2u, 0x8000u, w_rx_buf + 4u, rem, 0, 0, 0, rem) != 0)
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
            uint32_t cpy = plen - 16u;
            if (cpy > sizeof(w_ctl_rsp)) cpy = sizeof(w_ctl_rsp);
            for (uint32_t i = 0; i < cpy; i++) w_ctl_rsp[i] = pl[16u + i];
            w_ctl_rsp_len = cpy;
            w_ctl_got = 1;
        }
    } else if (chan == SDPCM_CH_DATA && plen > 4u) {
        /* BDC header: 4 bytes + (data_offset * 4) */
        uint32_t bdc_off = 4u + ((uint32_t)pl[3] * 4u);
        if (bdc_off < plen)
            wifi_deliver_rx_eth(pl + bdc_off, plen - bdc_off);
    } else if (chan == SDPCM_CH_EVENT && plen > 4u) {
        /* Broadcom async event (link up/down) */
        uint32_t bdc_off = 4u + ((uint32_t)pl[3] * 4u);
        if (bdc_off + 24u < plen) {
            const uint8_t *ev = pl + bdc_off;
            if (ev[12] == 0x88u && ev[13] == 0x6cu) /* BRCM ether type */
                w_st.link_up = 1u;
        }
    }
    return 1;
}

static int bcm_cdc_cmd(uint32_t cmd, int is_set, uint8_t *buf, uint32_t len, uint32_t *out_len)
{
    if (!w_st.f2_ready || len > 1000u)
        return -1;
    uint8_t pkt[1024];
    uint16_t id = ++w_cdc_id;
    uint32_t flags = (is_set ? 2u : 0u) | ((uint32_t)id << 16);
    pkt[0]  = (uint8_t)cmd;        pkt[1]  = (uint8_t)(cmd >> 8);
    pkt[2]  = (uint8_t)(cmd >> 16); pkt[3]  = (uint8_t)(cmd >> 24);
    pkt[4]  = (uint8_t)len;        pkt[5]  = (uint8_t)(len >> 8);
    pkt[6]  = (uint8_t)(len >> 16); pkt[7]  = (uint8_t)(len >> 24);
    pkt[8]  = (uint8_t)flags;      pkt[9]  = (uint8_t)(flags >> 8);
    pkt[10] = (uint8_t)(flags >> 16); pkt[11] = (uint8_t)(flags >> 24);
    pkt[12] = 0u; pkt[13] = 0u; pkt[14] = 0u; pkt[15] = 0u;
    for (uint32_t i = 0; i < len; i++)
        pkt[16u + i] = buf[i];

    w_lock();
    w_ctl_wait_id = id;
    w_ctl_got = 0;
    int rc = sdpcm_tx_frame(SDPCM_CH_CTRL, pkt, 16u + len);
    if (rc == 0) {
        for (int tries = 0; tries < 200 && !w_ctl_got; tries++) {
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

static int wifi_en0_ioctl(void *ifp, uint32_t cmd, void *data)
{
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
        int got = 0;
        w_lock();
        got = sdpcm_poll_rx_once();
        w_unlock();
        if (!got)
            k_iosleep(5u);
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
    uint32_t    broadcast_len;
    const void *broadcast_addr;
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
    if (k_ifnet_allocate(&p, &ifp) != 0 || !ifp)
        return;
    k_ifnet_set_mtu(ifp, 1500u);
    k_ifnet_set_addrlen(ifp, 6u);
    k_ifnet_set_hdrlen(ifp, 14u);
    k_ifnet_set_baudrate(ifp, 54000000ull);
    k_ifnet_set_flags(ifp, 0x8843u /* UP|BROADCAST|RUNNING|SIMPLEX|MULTICAST */, 0xffffu);

    /* Minimal sockaddr_dl for ifnet_attach */
    uint8_t sdl[20];
    for (int i = 0; i < 20; i++) sdl[i] = 0u;
    sdl[0] = 20u;  /* sdl_len */
    sdl[1] = 18u;  /* AF_LINK */
    sdl[4] = 6u;   /* IFT_ETHER */
    sdl[5] = 3u;   /* "en0" len */
    sdl[6] = 6u;   /* alen */
    sdl[8] = 'e'; sdl[9] = 'n'; sdl[10] = '0';
    for (int i = 0; i < 6; i++) sdl[11 + i] = w_st.mac[i];

    if (k_ifnet_attach(ifp, sdl) == 0) {
        w_en0 = ifp;
        w_st.en0_attached = 1u;
        (void)k_ifnet_set_lladdr(ifp, w_st.mac, 6u);
        (void)k_bpf_attach(ifp, 1u /* DLT_EN10MB */, 14u, 0, 0);
        if (!w_rx_started) {
            void *th = 0;
            if (k_thread_start(wifi_rx_thread, 0, &th) == 0)
                w_rx_started = 1;
        }
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
        /* Write null-separated NVRAM + 4-byte token to top of BCM4334 SRAM, then release ARM CM3 */
        uint32_t n = req->arg1;
        if (!w_st.probed || n == 0u || n > sizeof(req->data) - 4u) {
            rsp->err = -11;
            *out_len = 8u;
            return 0;
        }
        uint32_t padded = (n + 3u) & ~3u;
        uint32_t words = padded >> 2;
        uint32_t token = ((~words & 0xffffu) << 16) | (words & 0xffffu);
        uint32_t nv_addr = BCM4334_RAM_SZ - 4u - padded;

        w_lock();
        int rc = bcm_bp_write_mem(nv_addr, req->data, padded);
        if (rc == 0)
            rc = bcm_bp_write32(BCM4334_RAM_SZ - 4u, token);
        if (rc == 0) {
            /* Release BCM4334 ARM Cortex-M3 from reset */
            (void)bcm_bp_write32(BCM4334_SDIO_C + 0x20u /* INTSTATUS */, 0xffffffffu);
            (void)bcm_bp_write32(BCM4334_AI_WRAP + 0x408u, 0x01u);
            (void)bcm_bp_write32(BCM4334_AI_WRAP + 0x800u, 0x00u);
            w_udelay(50000);
            /* Enable SDIO Function 2 and wait for F2 Ready */
            uint8_t v = 0x06u; /* F1 + F2 */
            (void)sdio_cmd52(0u, CCCR_IOEx, &v, 1);
            for (int tries = 0; tries < 200; tries++) {
                v = 0u;
                if (sdio_cmd52(0u, CCCR_IORx, &v, 0) == 0 && (v & 0x04u)) {
                    w_st.f2_ready = 1u;
                    w_st.fw_loaded = 1u;
                    break;
                }
                w_udelay(5000);
            }
            if (!w_st.f2_ready)
                rc = -12;
        }
        w_unlock();

        if (rc == 0) {
            /* Default GT-I9300 EFS MAC (overridden by firmware cur_etheraddr if returned) */
            w_st.mac[0] = 0x02u; w_st.mac[1] = 0x00u; w_st.mac[2] = 0x00u;
            w_st.mac[3] = 0x43u; w_st.mac[4] = 0x34u; w_st.mac[5] = 0x00u;
            static const char k_eth_var[] = "cur_etheraddr";
            uint8_t q[32];
            for (unsigned i = 0; i < sizeof(q); i++)
                q[i] = (i < sizeof(k_eth_var)) ? (uint8_t)k_eth_var[i] : 0u;
            uint32_t ql = sizeof(q);
            if (bcm_cdc_cmd(262u /* WLC_GET_VAR */, 0, q, 20u, &ql) == 0 && ql >= 6u) {
                for (int i = 0; i < 6; i++) w_st.mac[i] = q[i];
            }
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

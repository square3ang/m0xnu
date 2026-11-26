/*
 * Freestanding IEEE 802.11i WPA2-PSK crypto primitives for tools/xnushim/wifi.c:
 *   - SHA-1 & HMAC-SHA1 (FIPS 180-4 / RFC 2104)
 *   - IEEE 802.11i PRF-384 (PTK derivation: KCK[16] || KEK[16] || TK[16])
 *   - AES-128 block decrypt & RFC 3394 AES Key Unwrap (GTK extraction from M3)
 */
#ifndef EXY_WIFI_WPA_H
#define EXY_WIFI_WPA_H

#include <stdint.h>

static inline uint32_t wpa_rol32(uint32_t v, unsigned n)
{
    return (v << n) | (v >> (32u - n));
}

static inline uint32_t wpa_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static inline void wpa_put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/* --- SHA-1 ----------------------------------------------------------------- */

struct wpa_sha1_ctx {
    uint32_t h[5];
    uint64_t total;
    uint8_t  buf[64];
    uint32_t idx;
};

static void wpa_sha1_block(uint32_t h[5], const uint8_t b[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = wpa_be32(b + i * 4);
    for (int i = 16; i < 80; i++)
        w[i] = wpa_rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1u);
    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (bb & c) | ((~bb) & d); k = 0x5a827999u; }
        else if (i < 40) { f = bb ^ c ^ d;             k = 0x6ed9eba1u; }
        else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8f1bbcdcu; }
        else             { f = bb ^ c ^ d;             k = 0xca62c1d6u; }
        uint32_t t = wpa_rol32(a, 5u) + f + e + k + w[i];
        e = d; d = c; c = wpa_rol32(bb, 30u); bb = a; a = t;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
}

static void wpa_sha1_init(struct wpa_sha1_ctx *c)
{
    c->h[0] = 0x67452301u; c->h[1] = 0xefcdab89u; c->h[2] = 0x98badcfeu;
    c->h[3] = 0x10325476u; c->h[4] = 0xc3d2e1f0u;
    c->total = 0ull; c->idx = 0u;
}

static void wpa_sha1_update(struct wpa_sha1_ctx *c, const uint8_t *d, uint32_t len)
{
    c->total += len;
    for (uint32_t i = 0; i < len; i++) {
        c->buf[c->idx++] = d[i];
        if (c->idx == 64u) {
            wpa_sha1_block(c->h, c->buf);
            c->idx = 0u;
        }
    }
}

static void wpa_sha1_final(struct wpa_sha1_ctx *c, uint8_t out[20])
{
    uint64_t bits = c->total * 8ull;
    uint8_t pad = 0x80u;
    wpa_sha1_update(c, &pad, 1u);
    pad = 0u;
    while (c->idx != 56u) wpa_sha1_update(c, &pad, 1u);
    uint8_t lb[8];
    wpa_put_be32(lb, (uint32_t)(bits >> 32));
    wpa_put_be32(lb + 4, (uint32_t)bits);
    wpa_sha1_update(c, lb, 8u);
    for (int i = 0; i < 5; i++) wpa_put_be32(out + i * 4, c->h[i]);
}

static void wpa_hmac_sha1(const uint8_t *key, uint32_t klen,
                          const uint8_t *msg, uint32_t mlen, uint8_t out[20])
{
    uint8_t kblk[64], ipad[64], opad[64], inner[20];
    for (int i = 0; i < 64; i++) kblk[i] = 0u;
    if (klen > 64u) {
        struct wpa_sha1_ctx kc;
        wpa_sha1_init(&kc);
        wpa_sha1_update(&kc, key, klen);
        wpa_sha1_final(&kc, kblk);
    } else {
        for (uint32_t i = 0; i < klen; i++) kblk[i] = key[i];
    }
    for (int i = 0; i < 64; i++) { ipad[i] = kblk[i] ^ 0x36u; opad[i] = kblk[i] ^ 0x5cu; }
    struct wpa_sha1_ctx ctx;
    wpa_sha1_init(&ctx);
    wpa_sha1_update(&ctx, ipad, 64u);
    wpa_sha1_update(&ctx, msg, mlen);
    wpa_sha1_final(&ctx, inner);
    wpa_sha1_init(&ctx);
    wpa_sha1_update(&ctx, opad, 64u);
    wpa_sha1_update(&ctx, inner, 20u);
    wpa_sha1_final(&ctx, out);
}

/* IEEE 802.11i PRF-384 (48B PTK = KCK[16] || KEK[16] || TK[16]) */
static void wpa_derive_ptk(const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6],
                           const uint8_t anonce[32], const uint8_t snonce[32], uint8_t ptk[48])
{
    static const char label[] = "Pairwise key expansion";
    uint8_t data[23 + 76 + 1]; /* label(22) + '\0'(1) + 2*6 + 2*32 + ctr(1) = 100B */
    uint32_t p = 0u;
    for (int i = 0; i < 23; i++) data[p++] = (uint8_t)label[i];
    int cmp_mac = 0;
    for (int i = 0; i < 6; i++) if (aa[i] != spa[i]) { cmp_mac = (aa[i] < spa[i]) ? -1 : 1; break; }
    const uint8_t *m1 = (cmp_mac <= 0) ? aa : spa;
    const uint8_t *m2 = (cmp_mac <= 0) ? spa : aa;
    for (int i = 0; i < 6; i++) data[p++] = m1[i];
    for (int i = 0; i < 6; i++) data[p++] = m2[i];
    int cmp_n = 0;
    for (int i = 0; i < 32; i++) if (anonce[i] != snonce[i]) { cmp_n = (anonce[i] < snonce[i]) ? -1 : 1; break; }
    const uint8_t *n1 = (cmp_n <= 0) ? anonce : snonce;
    const uint8_t *n2 = (cmp_n <= 0) ? snonce : anonce;
    for (int i = 0; i < 32; i++) data[p++] = n1[i];
    for (int i = 0; i < 32; i++) data[p++] = n2[i];
    uint8_t dig[20];
    for (uint8_t ctr = 0u; ctr < 3u; ctr++) {
        data[p] = ctr;
        wpa_hmac_sha1(pmk, 32u, data, p + 1u, dig);
        for (uint32_t i = 0; i < 20u && (uint32_t)ctr * 20u + i < 48u; i++)
            ptk[(uint32_t)ctr * 20u + i] = dig[i];
    }
}

/* --- AES-128 Decrypt + RFC 3394 AES Key Unwrap ----------------------------- */

static const uint8_t wpa_aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t wpa_aes_isbox[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

static inline uint8_t wpa_gf_mul(uint8_t a, uint8_t b)
{
    uint8_t p = 0u;
    for (int i = 0; i < 8; i++) {
        if (b & 1u) p ^= a;
        uint8_t hi = a & 0x80u;
        a <<= 1;
        if (hi) a ^= 0x1bu;
        b >>= 1;
    }
    return p;
}

static void wpa_aes128_expand(const uint8_t key[16], uint8_t rk[176])
{
    static const uint8_t rcon[10] = { 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36 };
    for (int i = 0; i < 16; i++) rk[i] = key[i];
    for (int r = 0; r < 10; r++) {
        uint8_t *prev = rk + r * 16;
        uint8_t *cur  = prev + 16;
        uint8_t t[4] = { wpa_aes_sbox[prev[13]] ^ rcon[r], wpa_aes_sbox[prev[14]],
                         wpa_aes_sbox[prev[15]],           wpa_aes_sbox[prev[12]] };
        for (int i = 0; i < 4; i++) cur[i] = prev[i] ^ t[i];
        for (int i = 4; i < 16; i++) cur[i] = prev[i] ^ cur[i - 4];
    }
}

static void wpa_aes128_decrypt_block(const uint8_t rk[176], uint8_t s[16])
{
    for (int i = 0; i < 16; i++) s[i] ^= rk[160 + i];
    for (int r = 9; r >= 0; r--) {
        /* InvShiftRows */
        uint8_t t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
        t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
        t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;
        /* InvSubBytes + AddRoundKey */
        for (int i = 0; i < 16; i++) s[i] = wpa_aes_isbox[s[i]] ^ rk[r * 16 + i];
        if (r > 0) {
            /* InvMixColumns */
            for (int c = 0; c < 4; c++) {
                uint8_t *col = s + c * 4;
                uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = wpa_gf_mul(a0, 14) ^ wpa_gf_mul(a1, 11) ^ wpa_gf_mul(a2, 13) ^ wpa_gf_mul(a3, 9);
                col[1] = wpa_gf_mul(a0, 9)  ^ wpa_gf_mul(a1, 14) ^ wpa_gf_mul(a2, 11) ^ wpa_gf_mul(a3, 13);
                col[2] = wpa_gf_mul(a0, 13) ^ wpa_gf_mul(a1, 9)  ^ wpa_gf_mul(a2, 14) ^ wpa_gf_mul(a3, 11);
                col[3] = wpa_gf_mul(a0, 11) ^ wpa_gf_mul(a1, 13) ^ wpa_gf_mul(a2, 9)  ^ wpa_gf_mul(a3, 14);
            }
        }
    }
}

/* RFC 3394 AES Key Unwrap: cipher_len must be 8*(n+1) with n >= 1; outputs plain_len = cipher_len - 8. */
static int wpa_aes_unwrap(const uint8_t kek[16], const uint8_t *cipher, uint32_t clen, uint8_t *plain)
{
    if (clen < 16u || (clen & 7u) != 0u)
        return -1;
    uint32_t n = (clen / 8u) - 1u;
    uint8_t rk[176], a[8], b[16];
    wpa_aes128_expand(kek, rk);
    for (int i = 0; i < 8; i++) a[i] = cipher[i];
    for (uint32_t i = 0; i < n * 8u; i++) plain[i] = cipher[8u + i];
    for (int j = 5; j >= 0; j--) {
        for (int i = (int)n; i >= 1; i--) {
            uint32_t t = (uint32_t)n * (uint32_t)j + (uint32_t)i;
            b[0] = a[0]; b[1] = a[1]; b[2] = a[2]; b[3] = a[3];
            b[4] = a[4] ^ (uint8_t)(t >> 24);
            b[5] = a[5] ^ (uint8_t)(t >> 16);
            b[6] = a[6] ^ (uint8_t)(t >> 8);
            b[7] = a[7] ^ (uint8_t)t;
            for (int k = 0; k < 8; k++) b[8 + k] = plain[(uint32_t)(i - 1) * 8u + (uint32_t)k];
            wpa_aes128_decrypt_block(rk, b);
            for (int k = 0; k < 8; k++) a[k] = b[k];
            for (int k = 0; k < 8; k++) plain[(uint32_t)(i - 1) * 8u + (uint32_t)k] = b[8 + k];
        }
    }
    for (int i = 0; i < 8; i++)
        if (a[i] != 0xa6u) return -1;
    return 0;
}

#endif /* EXY_WIFI_WPA_H */

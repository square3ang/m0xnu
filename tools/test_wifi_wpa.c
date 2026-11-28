/*
 * Host unit tests for tools/xnushim/wifi_wpa.h crypto primitives
 * (SHA-1, HMAC-SHA1, IEEE 802.11i PRF-384, AES-128 decrypt, RFC 3394 Key Unwrap)
 * plus overflow-safe scan-result TLV & EAPOL replay-counter edge cases.
 *
 * Build & run: cc -O2 -Wall -Wextra -Werror tools/test_wifi_wpa.c -o /tmp/test_wifi_wpa && /tmp/test_wifi_wpa
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "xnushim/wifi_wpa.h"

static void check_eq(const uint8_t *got, const uint8_t *exp, size_t n, const char *name)
{
    if (memcmp(got, exp, n) != 0) {
        fprintf(stderr, "FAIL %s\n  got:", name);
        for (size_t i = 0; i < n; i++) fprintf(stderr, " %02x", got[i]);
        fprintf(stderr, "\n  exp:");
        for (size_t i = 0; i < n; i++) fprintf(stderr, " %02x", exp[i]);
        fprintf(stderr, "\n");
        assert(0);
    }
}

static void test_sha1(void)
{
    struct wpa_sha1_ctx c;
    uint8_t out[20];

    /* FIPS 180-4 empty string */
    static const uint8_t exp_empty[20] = {
        0xda,0x39,0xa3,0xee,0x5e,0x6b,0x4b,0x0d,0x32,0x55,
        0xbf,0xef,0x95,0x60,0x18,0x90,0xaf,0xd8,0x07,0x09
    };
    wpa_sha1_init(&c);
    wpa_sha1_final(&c, out);
    check_eq(out, exp_empty, 20, "sha1_empty");

    /* FIPS 180-4 "abc" */
    static const uint8_t exp_abc[20] = {
        0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,
        0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d
    };
    wpa_sha1_init(&c);
    wpa_sha1_update(&c, (const uint8_t *)"abc", 3u);
    wpa_sha1_final(&c, out);
    check_eq(out, exp_abc, 20, "sha1_abc");

    /* FIPS 180-4 2-block (56-byte boundary) */
    const char *m2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const uint8_t exp_m2[20] = {
        0x84,0x98,0x3e,0x44,0x1c,0x3b,0xd2,0x6e,0xba,0xae,
        0x4a,0xa1,0xf9,0x51,0x29,0xe5,0xe5,0x46,0x70,0xf1
    };
    wpa_sha1_init(&c);
    wpa_sha1_update(&c, (const uint8_t *)m2, (uint32_t)strlen(m2));
    wpa_sha1_final(&c, out);
    check_eq(out, exp_m2, 20, "sha1_2block");
}

static void test_hmac_sha1(void)
{
    uint8_t out[20];

    /* RFC 2202 Test Case 1 */
    uint8_t k1[20];
    memset(k1, 0x0bu, sizeof(k1));
    static const uint8_t exp1[20] = {
        0xb6,0x17,0x31,0x86,0x55,0x05,0x72,0x64,0xe2,0x8b,
        0xc0,0xb6,0xfb,0x37,0x8c,0x8e,0xf1,0x46,0xbe,0x00
    };
    wpa_hmac_sha1(k1, 20u, (const uint8_t *)"Hi There", 8u, out);
    check_eq(out, exp1, 20, "hmac_rfc2202_1");

    /* RFC 2202 Test Case 2 */
    static const uint8_t exp2[20] = {
        0xef,0xfc,0xdf,0x6a,0xe5,0xeb,0x2f,0xa2,0xd2,0x74,
        0x16,0xd5,0xf1,0x84,0xdf,0x9c,0x25,0x9a,0x7c,0x79
    };
    wpa_hmac_sha1((const uint8_t *)"Jefe", 4u,
                  (const uint8_t *)"what do ya want for nothing?", 28u, out);
    check_eq(out, exp2, 20, "hmac_rfc2202_2");

    /* RFC 2202 Test Case 6 (>64-byte key hashed first) */
    uint8_t k6[80];
    memset(k6, 0xaau, sizeof(k6));
    const char *d6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    static const uint8_t exp6[20] = {
        0xaa,0x4a,0xe5,0xe1,0x52,0x72,0xd0,0x0e,0x95,0x70,
        0x56,0x37,0xce,0x8a,0x3b,0x55,0xed,0x40,0x21,0x12
    };
    wpa_hmac_sha1(k6, 80u, (const uint8_t *)d6, (uint32_t)strlen(d6), out);
    check_eq(out, exp6, 20, "hmac_rfc2202_6");
}

static void test_prf384_ptk(void)
{
    /* IEEE 802.11i-2004 Annex H.4.3 / WPA2-PSK test vector */
    static const uint8_t pmk[32] = {
        0x0d,0xc0,0xd6,0xeb,0x90,0x55,0x5e,0xd6,0x41,0x97,0x56,0xb9,0xa1,0x5e,0xc3,0xe3,
        0x20,0x9b,0x63,0xdf,0x70,0x7d,0xd5,0x08,0xd1,0x45,0x81,0xf8,0x98,0x27,0x21,0xaf
    };
    static const uint8_t aa[6]  = { 0xa0,0xa1,0xa1,0xa3,0xa4,0xa5 };
    static const uint8_t spa[6] = { 0xb0,0xb1,0xb2,0xb3,0xb4,0xb5 };
    static const uint8_t anonce[32] = {
        0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,
        0xf6,0xf7,0xf8,0xf9,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b
    };
    static const uint8_t snonce[32] = {
        0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,
        0xd6,0xd7,0xd8,0xd9,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b
    };
    uint8_t ptk1[48], ptk2[48];
    wpa_derive_ptk(pmk, aa, spa, anonce, snonce, ptk1);
    /* Symmetry: swapping AA/SPA or ANonce/SNonce must yield identical PTK */
    wpa_derive_ptk(pmk, spa, aa, snonce, anonce, ptk2);
    check_eq(ptk1, ptk2, 48, "ptk_symmetry");

    /* Verify against direct HMAC-SHA1 over min/max canonical concatenation */
    uint8_t msg[100];
    memcpy(msg, "Pairwise key expansion\0", 23);
    memcpy(msg + 23, aa, 6);
    memcpy(msg + 29, spa, 6);
    memcpy(msg + 35, snonce, 32); /* snonce[0]=0xc0 < anonce[0]=0xe0 */
    memcpy(msg + 67, anonce, 32);
    uint8_t ref[48], dig[20];
    msg[99] = 0; wpa_hmac_sha1(pmk, 32u, msg, 100u, dig); memcpy(ref + 0, dig, 20);
    msg[99] = 1; wpa_hmac_sha1(pmk, 32u, msg, 100u, dig); memcpy(ref + 20, dig, 20);
    msg[99] = 2; wpa_hmac_sha1(pmk, 32u, msg, 100u, dig); memcpy(ref + 40, dig, 8);
    check_eq(ptk1, ref, 48, "ptk_prf384_exact");
}

static void test_aes_and_unwrap(void)
{
    /* FIPS 197 Appendix B AES-128 decrypt vector */
    static const uint8_t key[16] = {
        0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c
    };
    static const uint8_t ct[16] = {
        0x39,0x25,0x84,0x1d,0x02,0xdc,0x09,0xfb,0xdc,0x11,0x85,0x97,0x19,0x6a,0x0b,0x32
    };
    static const uint8_t pt_exp[16] = {
        0x32,0x43,0xf6,0xa8,0x88,0x5a,0x30,0x8d,0x31,0x31,0x98,0xa2,0xe0,0x37,0x07,0x34
    };
    uint8_t rk[176], blk[16];
    wpa_aes128_expand(key, rk);
    memcpy(blk, ct, 16);
    wpa_aes128_decrypt_block(rk, blk);
    check_eq(blk, pt_exp, 16, "aes128_fips197_decrypt");

    /* RFC 3394 Section 4.1: Wrap 128 bits of Key Data with a 128-bit KEK */
    static const uint8_t kek[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
    };
    static const uint8_t wrapped[24] = {
        0x1f,0xa6,0x8b,0x0a,0x81,0x12,0xb4,0x47,0xae,0xf3,0x4b,0xd8,
        0xfb,0x5a,0x7b,0x82,0x9d,0x3e,0x86,0x23,0x71,0xd2,0xcf,0xe5
    };
    static const uint8_t plain_exp[16] = {
        0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff
    };
    uint8_t plain[24];
    assert(wpa_aes_unwrap(kek, wrapped, 24u, plain) == 0);
    check_eq(plain, plain_exp, 16, "rfc3394_unwrap_128");

    /* Tampered ciphertext must fail IV check */
    uint8_t bad[24];
    memcpy(bad, wrapped, 24);
    bad[12] ^= 0x01u;
    assert(wpa_aes_unwrap(kek, bad, 24u, plain) == -1);

    /* Invalid lengths must be rejected */
    assert(wpa_aes_unwrap(kek, wrapped, 0u, plain) == -1);
    assert(wpa_aes_unwrap(kek, wrapped, 8u, plain) == -1);
    assert(wpa_aes_unwrap(kek, wrapped, 20u, plain) == -1);
}

static int replay_gt(const uint8_t a[8], const uint8_t b[8])
{
    for (int i = 0; i < 8; i++)
        if (a[i] != b[i]) return a[i] > b[i];
    return 0;
}

static uint32_t parse_scan_ies_safe(const uint8_t *sres, uint32_t srl, uint32_t pos, uint32_t blen)
{
    if (pos > srl || srl - pos < 84u || blen < 84u || blen > srl - pos)
        return 0u;
    if (blen < 124u)
        return 0u;
    uint16_t ieo = (uint16_t)sres[pos + 116u] | ((uint16_t)sres[pos + 117u] << 8);
    uint32_t iel = (uint32_t)sres[pos + 120u] | ((uint32_t)sres[pos + 121u] << 8) |
                   ((uint32_t)sres[pos + 122u] << 16) | ((uint32_t)sres[pos + 123u] << 24);
    if (ieo < 84u || ieo >= blen || iel == 0u)
        return 0u;
    if (iel > blen - ieo) iel = blen - ieo;
    uint32_t valid = 0u;
    while (valid + 2u <= iel) {
        uint32_t elen = 2u + (uint32_t)sres[pos + ieo + valid + 1u];
        if (elen > iel - valid) break;
        valid += elen;
    }
    return valid;
}

static void test_eapol_and_tlv_bounds(void)
{
    static const uint8_t r0[8] = { 0,0,0,0,0,0,0,1 };
    static const uint8_t r1[8] = { 0,0,0,0,0,0,0,2 };
    static const uint8_t r2[8] = { 0,0,0,0,0,0,1,0 };
    assert(!replay_gt(r0, r0));
    assert(replay_gt(r1, r0));
    assert(!replay_gt(r0, r1));
    assert(replay_gt(r2, r1));

    /* Malformed/overflowing scan record lengths must never pass bounds checks */
    uint8_t buf[200];
    memset(buf, 0, sizeof(buf));
    /* Overflow blen = 0xffffff80u with pos = 12, srl = 200 */
    assert(parse_scan_ies_safe(buf, sizeof(buf), 12u, 0xffffff80u) == 0u);
    /* Overflow iel = 0xfffffff0u inside valid blen = 140 */
    buf[12 + 116] = 124; buf[12 + 117] = 0; /* ieo = 124 */
    buf[12 + 120] = 0xf0; buf[12 + 121] = 0xff; buf[12 + 122] = 0xff; buf[12 + 123] = 0xff;
    /* Put one valid 6-byte IE at 12+124 and one truncated IE after it */
    buf[12 + 124] = 48; buf[12 + 125] = 4; /* 6 bytes */
    buf[12 + 130] = 221; buf[12 + 131] = 50; /* truncated past blen=140 (only 10B left) */
    assert(parse_scan_ies_safe(buf, sizeof(buf), 12u, 140u) == 6u);
}

int main(void)
{
    test_sha1();
    test_hmac_sha1();
    test_prf384_ptk();
    test_aes_and_unwrap();
    test_eapol_and_tlv_bounds();
    puts("test_wifi_wpa: PASS");
    return 0;
}

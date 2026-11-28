/*
 * Host unit test for PVRTC-1 (4bpp & 2bpp mode 0/1) decoder and drm_mali BO pool allocator.
 * Build & run on host:
 *   gcc -O2 -Wall -Wextra -Werror gpu/ios-gl/test_pvrtc_pool.c gpu/ios-gl/pvrtc.c -o /tmp/test_pvrtc_pool && /tmp/test_pvrtc_pool
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int pvrtc_decode_rgba(const uint8_t *src, size_t src_sz, uint32_t w, uint32_t h,
                             int is_2bpp, int force_opaque, uint8_t *dst);
extern int pvrtc_decode_rgb565(const uint8_t *src, size_t src_sz, uint32_t w, uint32_t h,
                               int is_2bpp, uint16_t *dst);

static void put_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

/* Pack opaque color A (RGB554) and opaque color B (RGB555) with mode bit. */
static uint32_t pack_col_opaque(uint32_t ra, uint32_t ga, uint32_t ba4,
                                uint32_t rb, uint32_t gb, uint32_t bb5, uint32_t mode)
{
    uint32_t wa = 0x8000u | ((ra & 31u) << 10) | ((ga & 31u) << 5) | ((ba4 & 15u) << 1) | (mode & 1u);
    uint32_t wb = 0x8000u | ((rb & 31u) << 10) | ((gb & 31u) << 5) | (bb5 & 31u);
    return wa | (wb << 16);
}

static void test_pvrtc_4bpp(void)
{
    /* 8x8 (2x2 blocks) solid black A, solid white B, mode 0, all modulation = 3 (100% B = 255). */
    uint8_t src[4 * 8];
    uint32_t col = pack_col_opaque(0, 0, 0, 31, 31, 31, 0);
    for (int i = 0; i < 4; i++) {
        put_u32_le(src + i * 8, 0xffffffffu);
        put_u32_le(src + i * 8 + 4, col);
    }
    uint8_t rgba[8 * 8 * 4];
    uint16_t rgb565[8 * 8];
    assert(pvrtc_decode_rgba(src, sizeof(src), 8, 8, 0, 0, rgba) == 0);
    assert(pvrtc_decode_rgb565(src, sizeof(src), 8, 8, 0, rgb565) == 0);
    for (int i = 0; i < 64; i++) {
        assert(rgba[i * 4 + 0] == 255 && rgba[i * 4 + 1] == 255 && rgba[i * 4 + 2] == 255 && rgba[i * 4 + 3] == 255);
        assert(rgb565[i] == 0xffffu);
    }

    /* Mode 1 punch-through alpha (m == 2 -> alpha == 0). */
    col = pack_col_opaque(0, 0, 0, 31, 31, 31, 1);
    for (int i = 0; i < 4; i++) {
        put_u32_le(src + i * 8, 0xaaaaaaaau); /* all 2-bit fields == 2 */
        put_u32_le(src + i * 8 + 4, col);
    }
    assert(pvrtc_decode_rgba(src, sizeof(src), 8, 8, 0, 0, rgba) == 0);
    for (int i = 0; i < 64; i++) {
        assert(rgba[i * 4 + 3] == 0);
        assert(rgba[i * 4 + 0] == 127);
    }
    /* force_opaque overrides punch-through alpha. */
    assert(pvrtc_decode_rgba(src, sizeof(src), 8, 8, 0, 1, rgba) == 0);
    for (int i = 0; i < 64; i++)
        assert(rgba[i * 4 + 3] == 255);
}

static void test_pvrtc_2bpp_mode0_and_mode1(void)
{
    /* 16x8 (2x2 blocks of 8x4), black A -> white B. */
    uint8_t src[4 * 8];
    uint8_t rgba[16 * 8 * 4];

    /* Mode 0: 1 bit per pixel, set only row ly=1 (bits 8..15) to 1 (white), others 0 (black). */
    uint32_t col0 = pack_col_opaque(0, 0, 0, 31, 31, 31, 0);
    for (int i = 0; i < 4; i++) {
        put_u32_le(src + i * 8, 0x0000ff00u);
        put_u32_le(src + i * 8 + 4, col0);
    }
    assert(pvrtc_decode_rgba(src, sizeof(src), 16, 8, 1, 0, rgba) == 0);
    for (int y = 0; y < 8; y++) {
        uint8_t expect = ((y & 3) == 1) ? 255u : 0u;
        for (int x = 0; x < 16; x++)
            assert(rgba[(y * 16 + x) * 4 + 0] == expect);
    }

    /* Mode 1 regression test: odd rows ly=1,3 pack 2-bit modulation at bitpos = ly*8 + (lx & ~1):
     *   ly=1: lx=1,3,5,7 -> bits 8..9, 10..11, 12..13, 14..15
     *   ly=3: lx=1,3,5,7 -> bits 24..25, 26..27, 28..29, 30..31
     * Set all odd-row even-(lx+ly) pixels to m=3 (white=255) and all even-row pixels to m=0 (black=0).
     * With the old bug (ly*8 + lx), bit 8/24 were skipped and bit 15/31 overflowed, corrupting rows 1, 2, 3! */
    uint32_t col1 = pack_col_opaque(0, 0, 0, 31, 31, 31, 1);
    uint32_t mod1 = 0xff00ff00u; /* bits 8..15 and 24..31 = 1 (m=3); bit 0 = 0 (4-neighbor avg) */
    for (int i = 0; i < 4; i++) {
        put_u32_le(src + i * 8, mod1);
        put_u32_le(src + i * 8 + 4, col1);
    }
    assert(pvrtc_decode_rgba(src, sizeof(src), 16, 8, 1, 0, rgba) == 0);
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 16; x++) {
            uint8_t r = rgba[(y * 16 + x) * 4 + 0];
            if (((x + y) & 1) == 0) {
                /* Even (x+y): direct 2-bit modulation: odd rows -> 255, even rows -> 0. */
                uint8_t expect = (y & 1) ? 255u : 0u;
                assert(r == expect);
            } else {
                /* Odd (x+y): 4-neighbor average of two 0s and two 8s -> weight 4 -> 127. */
                assert(r == 127u);
            }
        }
    }

    /* Mode 1 bit 0 = 1 (H/V mode) + bit 20 = 1 (vertical) + bit 21 = 1:
     * pixel (lx=4, ly=2) must read bit 21 -> weight 8 (255), while bit 20 selects vertical interpolation. */
    uint32_t mod_v = (1u << 0) | (1u << 20) | (1u << 21);
    for (int i = 0; i < 4; i++) {
        put_u32_le(src + i * 8, mod_v);
        put_u32_le(src + i * 8 + 4, col1);
    }
    assert(pvrtc_decode_rgba(src, sizeof(src), 16, 8, 1, 0, rgba) == 0);
    assert(rgba[(2 * 16 + 4) * 4 + 0] == 255u);
    /* Neighbor (lx=4, ly=3) has (4+3) odd, vertical avg of (4,2)=8 and (4,4)=0 -> weight 4 -> 127. */
    assert(rgba[(3 * 16 + 4) * 4 + 0] == 127u);
}

static void test_pvrtc_bounds_and_heap_fallback(void)
{
    uint8_t dummy[32] = {0};
    uint8_t out[64 * 4];
    /* Invalid & overflow dimensions must fail cleanly, never hang or read OOB. */
    assert(pvrtc_decode_rgba(NULL, 32, 8, 8, 0, 0, out) == -1);
    assert(pvrtc_decode_rgba(dummy, 32, 0, 8, 0, 0, out) == -1);
    assert(pvrtc_decode_rgba(dummy, 32, 8, 0, 0, 0, out) == -1);
    assert(pvrtc_decode_rgba(dummy, 31, 8, 8, 0, 0, out) == -1);
    assert(pvrtc_decode_rgba(dummy, 32, 4097, 8, 0, 0, out) == -1);
    assert(pvrtc_decode_rgba(dummy, 32, 0x80000000u, 8, 0, 0, out) == -1);
    assert(pvrtc_decode_rgba(dummy, 32, 65536, 65536, 0, 0, out) == -1);

    /* Non-block-multiple width 9x8 (4bpp): needs 4x2 = 8 blocks = 64 bytes; 32 bytes must be rejected! */
    assert(pvrtc_decode_rgba(dummy, 32, 9, 8, 0, 0, out) == -1);
    uint8_t src9[64] = {0};
    uint8_t out9[9 * 8 * 4];
    assert(pvrtc_decode_rgba(src9, sizeof(src9), 9, 8, 0, 1, out9) == 0);

    /* Large texture (>128 blocks wide -> 1024x8 4bpp = 256x2 blocks) exercises heap fallback cleanly. */
    size_t big_src_sz = 256u * 2u * 8u;
    uint8_t *big_src = calloc(1, big_src_sz);
    uint8_t *big_out = malloc(1024u * 8u * 4u);
    assert(big_src && big_out);
    assert(pvrtc_decode_rgba(big_src, big_src_sz, 1024, 8, 0, 1, big_out) == 0);
    assert(big_out[0] == 0 && big_out[3] == 255);
    free(big_src);
    free(big_out);
}

/* --- Mirror of drm_mali.c pool allocator & overflow guards --- */
#define TEST_MAX_BOS 64
struct test_ext { uint32_t off, size; };
static struct test_ext t_exts[TEST_MAX_BOS];
static int t_num_exts = 0;
static uint32_t t_work_size = 0x100000u;
static uint32_t t_next_off = 0u;

static int t_pool_alloc(uint32_t req_sz, uint32_t *out_off)
{
    if (req_sz > t_work_size)
        return -ENOMEM;
    uint32_t sz = (req_sz + 0xfffu) & ~0xfffu;
    if (!sz) sz = 0x1000u;
    if (sz > t_work_size)
        return -ENOMEM;
    int best = -1;
    for (int i = 0; i < t_num_exts; i++) {
        if (t_exts[i].size >= sz && (best < 0 || t_exts[i].size < t_exts[best].size))
            best = i;
    }
    if (best >= 0) {
        *out_off = t_exts[best].off;
        t_exts[best].off += sz;
        t_exts[best].size -= sz;
        if (t_exts[best].size == 0)
            t_exts[best] = t_exts[--t_num_exts];
        return 0;
    }
    if (sz > t_work_size || t_next_off > t_work_size - sz)
        return -ENOMEM;
    *out_off = t_next_off;
    t_next_off += sz;
    return 0;
}

static void t_pool_free(uint32_t off, uint32_t sz)
{
    if (!sz) return;
    for (int i = 0; i < t_num_exts; i++) {
        if (t_exts[i].off + t_exts[i].size == off) {
            off = t_exts[i].off;
            sz += t_exts[i].size;
            t_exts[i] = t_exts[--t_num_exts];
            i = -1;
        } else if (off + sz == t_exts[i].off) {
            sz += t_exts[i].size;
            t_exts[i] = t_exts[--t_num_exts];
            i = -1;
        }
    }
    if (off + sz == t_next_off) {
        t_next_off = off;
        return;
    }
    if (t_num_exts < TEST_MAX_BOS) {
        t_exts[t_num_exts].off = off;
        t_exts[t_num_exts].size = sz;
        t_num_exts++;
    }
}

static void test_pool_allocator(void)
{
    uint32_t a, b, c, d, o;
    /* Overflow / wrap rejection */
    assert(t_pool_alloc(0xfffff800u, &o) == -ENOMEM);
    assert(t_pool_alloc(0xffffffffu, &o) == -ENOMEM);
    assert(t_pool_alloc(t_work_size + 1u, &o) == -ENOMEM);

    /* Allocate 4 blocks, free middle two in reverse order -> must coalesce into 1 extent. */
    assert(t_pool_alloc(0x10000u, &a) == 0 && a == 0x00000u);
    assert(t_pool_alloc(0x20000u, &b) == 0 && b == 0x10000u);
    assert(t_pool_alloc(0x30000u, &c) == 0 && c == 0x30000u);
    assert(t_pool_alloc(0x10000u, &d) == 0 && d == 0x60000u);
    t_pool_free(c, 0x30000u);
    assert(t_num_exts == 1);
    t_pool_free(b, 0x20000u);
    assert(t_num_exts == 1 && t_exts[0].off == 0x10000u && t_exts[0].size == 0x50000u);

    /* Freeing top block d must coalesce with hole [0x10000..0x60000) and reclaim next_off -> 0x10000. */
    t_pool_free(d, 0x10000u);
    assert(t_num_exts == 0 && t_next_off == 0x10000u);
    t_pool_free(a, 0x10000u);
    assert(t_num_exts == 0 && t_next_off == 0u);

    /* Best-fit selection & exact full-pool allocation */
    assert(t_pool_alloc(t_work_size, &o) == 0 && o == 0u);
    assert(t_pool_alloc(0x1000u, &o) == -ENOMEM);
    t_pool_free(0u, t_work_size);
    assert(t_num_exts == 0 && t_next_off == 0u);
}

int main(void)
{
    test_pvrtc_4bpp();
    test_pvrtc_2bpp_mode0_and_mode1();
    test_pvrtc_bounds_and_heap_fallback();
    test_pool_allocator();
    printf("test_pvrtc_pool: ALL PASS\n");
    return 0;
}

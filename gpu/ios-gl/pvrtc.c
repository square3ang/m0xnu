/*
 * Fast exact Imagination Technologies PVRTC-1 (4BPP & 2BPP, RGB & RGBA) decoder.
 * Decodes GL_COMPRESSED_RGB_PVRTC_* directly to RGB565 (half VRAM/bandwidth) and
 * GL_COMPRESSED_RGBA_PVRTC_* to RGBA8888 for Mali-400 (Lima).
 */
#include <stdint.h>
#include <stddef.h>
extern void *malloc(size_t);
extern void free(void *);

struct pvrtc_blk {
    uint8_t a[4];
    uint8_t b[4];
    uint32_t mod;
    uint8_t mode;
};

struct pvrtc_vcol {
    uint16_t a[4];
    uint16_t b[4];
};

static inline uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void build_twiddle_luts(uint32_t bw, uint32_t bh, uint32_t *tx, uint32_t *ty)
{
    uint32_t min_d = bw < bh ? bw : bh;
    uint32_t mask = min_d - 1u;
    uint32_t min_bits = 0u;
    for (uint32_t bit = 1u; bit < min_d; bit <<= 1u)
        min_bits++;

    for (uint32_t x = 0; x < bw; x++) {
        uint32_t v = 0u, sh = 1u;
        for (uint32_t bit = 1u; bit < min_d; bit <<= 1u, sh += 2u) {
            if (x & bit) v |= (1u << sh);
        }
        if (bw > bh)
            v |= (x & ~mask) << min_bits;
        tx[x] = v * 8u;
    }
    for (uint32_t y = 0; y < bh; y++) {
        uint32_t v = 0u, sh = 0u;
        for (uint32_t bit = 1u; bit < min_d; bit <<= 1u, sh += 2u) {
            if (y & bit) v |= (1u << sh);
        }
        if (bh > bw)
            v |= (y & ~mask) << min_bits;
        ty[y] = v * 8u;
    }
}

static inline void unpack_blk(const uint8_t *p, struct pvrtc_blk *out)
{
    uint32_t mod = read_u32_le(p);
    uint32_t col = read_u32_le(p + 4u);
    uint32_t wa = col & 0xffffu;
    uint32_t wb = col >> 16;

    out->mod = mod;
    out->mode = (uint8_t)(col & 1u);

    if (wa & 0x8000u) {
        uint32_t r = (wa >> 10) & 0x1fu;
        uint32_t g = (wa >> 5)  & 0x1fu;
        uint32_t b = (wa >> 1)  & 0x0fu;
        out->a[0] = (uint8_t)((r << 3) | (r >> 2));
        out->a[1] = (uint8_t)((g << 3) | (g >> 2));
        out->a[2] = (uint8_t)((b << 4) | b);
        out->a[3] = 255u;
    } else {
        uint32_t a = (wa >> 12) & 0x07u;
        uint32_t r = (wa >> 8)  & 0x0fu;
        uint32_t g = (wa >> 4)  & 0x0fu;
        uint32_t b = (wa >> 1)  & 0x07u;
        out->a[0] = (uint8_t)((r << 4) | r);
        out->a[1] = (uint8_t)((g << 4) | g);
        out->a[2] = (uint8_t)((b << 5) | (b << 2) | (b >> 1));
        out->a[3] = (uint8_t)((a << 5) | (a << 2) | (a >> 1));
    }

    if (wb & 0x8000u) {
        uint32_t r = (wb >> 10) & 0x1fu;
        uint32_t g = (wb >> 5)  & 0x1fu;
        uint32_t b = wb & 0x1fu;
        out->b[0] = (uint8_t)((r << 3) | (r >> 2));
        out->b[1] = (uint8_t)((g << 3) | (g >> 2));
        out->b[2] = (uint8_t)((b << 3) | (b >> 2));
        out->b[3] = 255u;
    } else {
        uint32_t a = (wb >> 12) & 0x07u;
        uint32_t r = (wb >> 8)  & 0x0fu;
        uint32_t g = (wb >> 4)  & 0x0fu;
        uint32_t b = wb & 0x0fu;
        out->b[0] = (uint8_t)((r << 4) | r);
        out->b[1] = (uint8_t)((g << 4) | g);
        out->b[2] = (uint8_t)((b << 4) | b);
        out->b[3] = (uint8_t)((a << 5) | (a << 2) | (a >> 1));
    }
}

static void load_row(const uint8_t *src, uint32_t bw, const uint32_t *tx, uint32_t ty_off,
                     struct pvrtc_blk *dst_row)
{
    for (uint32_t bx = 0; bx < bw; bx++)
        unpack_blk(src + (size_t)(tx[bx] | ty_off), &dst_row[bx]);
}

static inline int mod_val_2bpp_lut(const uint8_t *src, const uint32_t *tx, const uint32_t *ty,
                                   uint32_t bw, uint32_t bh, int x, int y)
{
    uint32_t wx = (uint32_t)x & ((bw << 3) - 1u);
    uint32_t wy = (uint32_t)y & ((bh << 2) - 1u);
    uint32_t bx = wx >> 3, by = wy >> 2;
    uint32_t lx = wx & 7u, ly = wy & 3u;
    const uint8_t *blk = src + (size_t)(tx[bx] | ty[by]);
    uint32_t mod = read_u32_le(blk);
    uint32_t col = read_u32_le(blk + 4u);
    if (!(col & 1u))
        return ((mod >> (ly * 8u + lx)) & 1u) ? 8 : 0;
    if (((lx + ly) & 1u) == 0u) {
        uint32_t bitpos = ly * 8u + (lx & ~1u);
        uint32_t m;
        if (bitpos == 0u)
            m = ((mod & 2u) >> 1) * 3u;
        else if (bitpos == 20u && (mod & 1u))
            m = ((mod >> 21) & 1u) * 3u;
        else
            m = (mod >> bitpos) & 3u;
        static const int lut[4] = { 0, 3, 5, 8 };
        return lut[m];
    }
    return -1;
}

static int pvrtc_decode_impl(const uint8_t *src, size_t src_sz, uint32_t w, uint32_t h,
                             int is_2bpp, int force_opaque, uint8_t *dst_rgba, uint16_t *dst_565)
{
    if (!src || (!dst_rgba && !dst_565) || !w || !h || w > 4096u || h > 4096u)
        return -1;
    uint32_t blk_w = is_2bpp ? 8u : 4u;
    uint32_t bw = (w + blk_w - 1u) / blk_w; if (bw < 2u) bw = 2u;
    uint32_t bh = (h + 3u) / 4u;            if (bh < 2u) bh = 2u;
    uint32_t pbw = 2u; while (pbw < bw) pbw <<= 1u;
    uint32_t pbh = 2u; while (pbh < bh) pbh <<= 1u;
    bw = pbw; bh = pbh;
    size_t need = (size_t)bw * (size_t)bh * 8u;
    if (src_sz < need)
        return -1;

    /* Scratch buffers: stack for <=128 blocks (~7 KiB, up to 512x512 4bpp), heap fallback for larger. */
    uint32_t st_tx[128], st_ty[128];
    struct pvrtc_blk st_r0[128], st_r1[128];
    struct pvrtc_vcol st_vc[128];

    uint32_t *tx = (bw <= 128u) ? st_tx : (uint32_t *)malloc(bw * sizeof(uint32_t));
    uint32_t *ty = (bh <= 128u) ? st_ty : (uint32_t *)malloc(bh * sizeof(uint32_t));
    struct pvrtc_blk *row0 = (bw <= 128u) ? st_r0 : (struct pvrtc_blk *)malloc(bw * sizeof(*row0));
    struct pvrtc_blk *row1 = (bw <= 128u) ? st_r1 : (struct pvrtc_blk *)malloc(bw * sizeof(*row1));
    struct pvrtc_vcol *vc  = (bw <= 128u) ? st_vc : (struct pvrtc_vcol *)malloc(bw * sizeof(*vc));
    if (!tx || !ty || !row0 || !row1 || !vc) {
        if (tx && tx != st_tx) free(tx);
        if (ty && ty != st_ty) free(ty);
        if (row0 && row0 != st_r0) free(row0);
        if (row1 && row1 != st_r1) free(row1);
        if (vc && vc != st_vc) free(vc);
        return -1;
    }

    build_twiddle_luts(bw, bh, tx, ty);

    static const uint8_t w4_m0[4] = { 0, 3, 5, 8 };
    static const uint8_t w4_m1[4] = { 0, 4, 4, 8 };
    uint32_t bw_mask = bw - 1u;
    uint32_t bh_mask = bh - 1u;
    uint32_t cached_by0 = ~0u, cached_by1 = ~0u;
    int shift = is_2bpp ? 5 : 4;

    for (uint32_t y = 0; y < h; y++) {
        int cy = (int)y - 2;
        uint32_t by0 = (uint32_t)(cy >> 2) & bh_mask;
        uint32_t by1 = (by0 + 1u) & bh_mask;
        uint32_t fy  = (uint32_t)(cy & 3);
        uint32_t ify = 4u - fy;

        if (by0 != cached_by0) {
            if (by0 == cached_by1) {
                struct pvrtc_blk *tmp = row0; row0 = row1; row1 = tmp;
                cached_by0 = by0;
                load_row(src, bw, tx, ty[by1], row1);
                cached_by1 = by1;
            } else {
                load_row(src, bw, tx, ty[by0], row0);
                load_row(src, bw, tx, ty[by1], row1);
                cached_by0 = by0;
                cached_by1 = by1;
            }
        }

        /* Pre-interpolate vertically once per block on this scanline. */
        if (dst_565) {
            for (uint32_t bx = 0; bx < bw; bx++) {
                const struct pvrtc_blk *p0 = &row0[bx], *p1 = &row1[bx];
                vc[bx].a[0] = (uint16_t)(p0->a[0] * ify + p1->a[0] * fy);
                vc[bx].a[1] = (uint16_t)(p0->a[1] * ify + p1->a[1] * fy);
                vc[bx].a[2] = (uint16_t)(p0->a[2] * ify + p1->a[2] * fy);
                vc[bx].b[0] = (uint16_t)(p0->b[0] * ify + p1->b[0] * fy);
                vc[bx].b[1] = (uint16_t)(p0->b[1] * ify + p1->b[1] * fy);
                vc[bx].b[2] = (uint16_t)(p0->b[2] * ify + p1->b[2] * fy);
            }
        } else {
            for (uint32_t bx = 0; bx < bw; bx++) {
                const struct pvrtc_blk *p0 = &row0[bx], *p1 = &row1[bx];
                vc[bx].a[0] = (uint16_t)(p0->a[0] * ify + p1->a[0] * fy);
                vc[bx].a[1] = (uint16_t)(p0->a[1] * ify + p1->a[1] * fy);
                vc[bx].a[2] = (uint16_t)(p0->a[2] * ify + p1->a[2] * fy);
                vc[bx].a[3] = (uint16_t)(p0->a[3] * ify + p1->a[3] * fy);
                vc[bx].b[0] = (uint16_t)(p0->b[0] * ify + p1->b[0] * fy);
                vc[bx].b[1] = (uint16_t)(p0->b[1] * ify + p1->b[1] * fy);
                vc[bx].b[2] = (uint16_t)(p0->b[2] * ify + p1->b[2] * fy);
                vc[bx].b[3] = (uint16_t)(p0->b[3] * ify + p1->b[3] * fy);
            }
        }

        const struct pvrtc_blk *mod_row = ((y & 3u) < 2u) ? row1 : row0;
        uint32_t mod_ysh = (y & 3u) << 3; /* 4bpp: 4 pixels * 2 bits = 8 bits per local row */
        uint8_t *out_rgba = dst_rgba ? (dst_rgba + (size_t)y * (size_t)w * 4u) : NULL;
        uint16_t *out_565 = dst_565  ? (dst_565  + (size_t)y * (size_t)w)      : NULL;

        if (!is_2bpp) {
            for (uint32_t x = 0; x < w; x++) {
                int cx = (int)x - 2;
                uint32_t bx0 = (uint32_t)(cx >> 2) & bw_mask;
                uint32_t bx1 = (bx0 + 1u) & bw_mask;
                uint32_t fx  = (uint32_t)(cx & 3);
                uint32_t ifx = 4u - fx;

                const struct pvrtc_vcol *v0 = &vc[bx0], *v1 = &vc[bx1];
                const struct pvrtc_blk *mb = &mod_row[(x >> 2) & bw_mask];
                uint32_t m = (mb->mod >> (mod_ysh + ((x & 3u) << 1))) & 3u;
                uint32_t weight, punch = 0u;
                if (mb->mode) {
                    weight = w4_m1[m];
                    if (m == 2u) punch = 1u;
                } else {
                    weight = w4_m0[m];
                }
                uint32_t iw = 8u - weight;

                uint32_t r = (((v0->a[0] * ifx + v1->a[0] * fx) >> 4) * iw +
                              ((v0->b[0] * ifx + v1->b[0] * fx) >> 4) * weight) >> 3;
                uint32_t g = (((v0->a[1] * ifx + v1->a[1] * fx) >> 4) * iw +
                              ((v0->b[1] * ifx + v1->b[1] * fx) >> 4) * weight) >> 3;
                uint32_t b = (((v0->a[2] * ifx + v1->a[2] * fx) >> 4) * iw +
                              ((v0->b[2] * ifx + v1->b[2] * fx) >> 4) * weight) >> 3;
                if (out_565) {
                    out_565[x] = (uint16_t)(((r & 0xf8u) << 8) | ((g & 0xfcu) << 3) | (b >> 3));
                } else {
                    uint8_t *px = out_rgba + x * 4u;
                    px[0] = (uint8_t)r;
                    px[1] = (uint8_t)g;
                    px[2] = (uint8_t)b;
                    if (force_opaque) {
                        px[3] = 255u;
                    } else if (punch) {
                        px[3] = 0u;
                    } else {
                        uint32_t a = (((v0->a[3] * ifx + v1->a[3] * fx) >> 4) * iw +
                                      ((v0->b[3] * ifx + v1->b[3] * fx) >> 4) * weight) >> 3;
                        px[3] = (uint8_t)a;
                    }
                }
            }
        } else {
            for (uint32_t x = 0; x < w; x++) {
                int cx = (int)x - 4;
                uint32_t bx0 = (uint32_t)(cx >> 3) & bw_mask;
                uint32_t bx1 = (bx0 + 1u) & bw_mask;
                uint32_t fx  = (uint32_t)(cx & 7);
                uint32_t ifx = 8u - fx;

                const struct pvrtc_vcol *v0 = &vc[bx0], *v1 = &vc[bx1];
                int weight = mod_val_2bpp_lut(src, tx, ty, bw, bh, (int)x, (int)y);
                if (weight < 0) {
                    const struct pvrtc_blk *mb = &mod_row[(x >> 3) & bw_mask];
                    int l = mod_val_2bpp_lut(src, tx, ty, bw, bh, (int)x - 1, (int)y);
                    int r = mod_val_2bpp_lut(src, tx, ty, bw, bh, (int)x + 1, (int)y);
                    int u = mod_val_2bpp_lut(src, tx, ty, bw, bh, (int)x, (int)y - 1);
                    int d = mod_val_2bpp_lut(src, tx, ty, bw, bh, (int)x, (int)y + 1);
                    if (mb->mod & 1u)
                        weight = (mb->mod & (1u << 20)) ? ((u + d + 1) >> 1) : ((l + r + 1) >> 1);
                    else
                        weight = (l + r + u + d + 2) >> 2;
                }
                uint32_t iw = (uint32_t)(8 - weight);
                uint32_t wt = (uint32_t)weight;
                uint32_t r = (((v0->a[0] * ifx + v1->a[0] * fx) >> shift) * iw +
                              ((v0->b[0] * ifx + v1->b[0] * fx) >> shift) * wt) >> 3;
                uint32_t g = (((v0->a[1] * ifx + v1->a[1] * fx) >> shift) * iw +
                              ((v0->b[1] * ifx + v1->b[1] * fx) >> shift) * wt) >> 3;
                uint32_t b = (((v0->a[2] * ifx + v1->a[2] * fx) >> shift) * iw +
                              ((v0->b[2] * ifx + v1->b[2] * fx) >> shift) * wt) >> 3;
                if (out_565) {
                    out_565[x] = (uint16_t)(((r & 0xf8u) << 8) | ((g & 0xfcu) << 3) | (b >> 3));
                } else {
                    uint8_t *px = out_rgba + x * 4u;
                    px[0] = (uint8_t)r;
                    px[1] = (uint8_t)g;
                    px[2] = (uint8_t)b;
                    if (force_opaque) {
                        px[3] = 255u;
                    } else {
                        uint32_t a = (((v0->a[3] * ifx + v1->a[3] * fx) >> shift) * iw +
                                      ((v0->b[3] * ifx + v1->b[3] * fx) >> shift) * wt) >> 3;
                        px[3] = (uint8_t)a;
                    }
                }
            }
        }
    }

    if (tx != st_tx) free(tx);
    if (ty != st_ty) free(ty);
    if (row0 != st_r0 && row0 != st_r1) free(row0);
    if (row1 != st_r0 && row1 != st_r1) free(row1);
    if (vc != st_vc) free(vc);
    return 0;
}

int pvrtc_decode_rgba(const uint8_t *src, size_t src_sz, uint32_t w, uint32_t h,
                      int is_2bpp, int force_opaque, uint8_t *dst)
{
    return pvrtc_decode_impl(src, src_sz, w, h, is_2bpp, force_opaque, dst, NULL);
}

int pvrtc_decode_rgb565(const uint8_t *src, size_t src_sz, uint32_t w, uint32_t h,
                        int is_2bpp, uint16_t *dst)
{
    return pvrtc_decode_impl(src, src_sz, w, h, is_2bpp, 1, NULL, dst);
}

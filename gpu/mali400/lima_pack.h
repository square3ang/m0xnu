/*
 * Copyright 2016 Intel Corporation
 * Copyright 2016 Broadcom
 * Copyright 2020 Collabora, Ltd.
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2025 Lima Project
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <inttypes.h>
#include <stdio.h>
#include "util/bitpack_helpers.h"
#include "util/half_float.h"
#define FILE_TYPE FILE
#define CONSTANT_ const
#define GLOBAL_

#define __gen_unpack_float(x, y, z) uif(__gen_unpack_uint(x, y, z))
#define __gen_unpack_half(x, y, z)                                             \
   _mesa_half_to_float(__gen_unpack_uint(x, y, z))

static inline uint64_t
__gen_unpack_uint(CONSTANT_ uint32_t *restrict cl, uint32_t start, uint32_t end)
{
   uint64_t val = 0;
   const int width = end - start + 1;
   const uint64_t mask =
      (width == 64) ? ~((uint64_t)0) : ((uint64_t)1 << width) - 1;

   for (unsigned word = start / 32; word < (end / 32) + 1; word++) {
      val |= ((uint64_t)cl[word]) << ((word - start / 32) * 32);
   }

   return (val >> (start % 32)) & mask;
}

static inline uint64_t
__gen_pack_unorm16(float f, uint32_t start, uint32_t end)
{
   return util_bitpack_uint(float_to_ushort(f), start, end);
}

static inline float
__gen_unpack_unorm16(CONSTANT_ uint32_t *restrict cl, uint32_t start, uint32_t end)
{
   return ushort_to_float(__gen_unpack_uint(cl, start, end));
}

static inline uint64_t
__gen_pack_unorm8(float f, uint32_t start, uint32_t end)
{
   return util_bitpack_uint(float_to_ubyte(f), start, end);
}

static inline float
__gen_unpack_unorm8(CONSTANT_ uint32_t *restrict cl, uint32_t start, uint32_t end)
{
   return ubyte_to_float(__gen_unpack_uint(cl, start, end));
}

static inline uint64_t
__gen_unpack_sint(CONSTANT_ uint32_t *restrict cl, uint32_t start, uint32_t end)
{
   int size = end - start + 1;
   int64_t val = __gen_unpack_uint(cl, start, end);

   return util_sign_extend(val, size);
}

static inline float
__gen_unpack_ulod(const uint32_t *restrict cl, uint32_t start, uint32_t end)
{
   uint32_t u = __gen_unpack_uint(cl, start, end);

   return ((float)u) / 16.0;
}

static inline float
__gen_unpack_slod(const uint32_t *restrict cl, uint32_t start, uint32_t end)
{
   int32_t u = __gen_unpack_sint(cl, start, end);

   return ((float)u) / 16.0;
}

static inline uint64_t
__gen_to_groups(uint32_t value, uint32_t group_size, uint32_t length)
{
   /* Zero is not representable, clamp to minimum */
   if (value == 0)
      return 1;

   /* Round up to the nearest number of groups */
   uint32_t groups = DIV_ROUND_UP(value, group_size);

   /* The 0 encoding means "all" */
   if (groups == ((uint64_t)1) << length)
      return 0;

   /* Otherwise it's encoded as the identity */
   assert(groups < (1u << length) && "out of bounds");
   assert(groups >= 1 && "exhaustive");
   return groups;
}

static inline uint64_t
__gen_from_groups(uint32_t value, uint32_t group_size, uint32_t length)
{
   return group_size * (value ? value : (1 << length));
}

#define lima_pack(dst, T, name)                                                \
   for (struct LIMA_##T name = {LIMA_##T##_header},                            \
                       *_loop_count = (GLOBAL_ void *)((uintptr_t)0);          \
        (uintptr_t)_loop_count < 1; (                                          \
           {                                                                   \
              LIMA_##T##_pack((GLOBAL_ uint32_t *)(dst), &name);               \
              _loop_count = (GLOBAL_ void *)(((uintptr_t)_loop_count) + 1);    \
           }))

#define lima_unpack(fp, src, T, name)                                          \
   struct LIMA_##T name;                                                       \
   LIMA_##T##_unpack(fp, (CONSTANT_ uint8_t *)(src), &name)

#define lima_print(fp, T, var, indent) LIMA_##T##_print(fp, &(var), indent)

static inline void
lima_merge_helper(uint32_t *dst, const uint32_t *src, size_t bytes)
{
   assert((bytes & 3) == 0);

   for (unsigned i = 0; i < (bytes / 4); ++i)
      dst[i] |= src[i];
}

#define lima_merge(packed1, packed2, type)                                      \
   lima_merge_helper((packed1).opaque, (packed2).opaque, LIMA_##type##_LENGTH)

#if defined(NDEBUG)
#define lima_genxml_validate_bounds(a, b, c)
#define lima_genxml_validate_mask(a, b, c, d, e) true
#define lima_genxml_validate_exact(a, b, c, d)   true
#else
static inline void
lima_genxml_validate_bounds(const char *name, uint64_t value, uint64_t bound)
{
   if (unlikely(value >= bound)) {
      fprintf(stderr, "%s out-of-bounds, got 0x%" PRIx64 ", max %" PRIx64 "\n",
              name, value, bound);

      UNREACHABLE("Out-of-bounds pack");
   }
}

static inline bool
lima_genxml_validate_mask(FILE *fp, const char *name, const void *cl_,
                         uint32_t index, uint32_t bad_mask)
{
   const uint32_t *cl = (const uint32_t *)cl_;
   uint32_t bad = cl[index] & bad_mask;

   if (bad && fp != NULL) {
      fprintf(
         fp,
         "XXX: Unknown field of %s unpacked at word %u got %X, bad mask %X\n",
         name, index, cl[index], bad);
   }

   return bad == 0;
}

static bool
lima_genxml_validate_exact(FILE *fp, const char *name, uint64_t value,
                          uint64_t exact)
{
   if (value != exact && fp != NULL) {
      fprintf(fp, "XXX: Expected %s to equal %" PRIx64 " but got %" PRIx64 "\n",
              name, value, exact);
   }

   return value == exact;
}

#endif

/* Everything after this is autogenerated from XML. Do not hand edit. */

enum lima_texel_format {
   LIMA_TEXEL_FORMAT_L8 = 9,
   LIMA_TEXEL_FORMAT_A8 = 10,
   LIMA_TEXEL_FORMAT_I8 = 11,
   LIMA_TEXEL_FORMAT_BGR_565 = 14,
   LIMA_TEXEL_FORMAT_BGRA_5551 = 15,
   LIMA_TEXEL_FORMAT_BGRA_4444 = 16,
   LIMA_TEXEL_FORMAT_L8A8 = 17,
   LIMA_TEXEL_FORMAT_L16 = 18,
   LIMA_TEXEL_FORMAT_A16 = 19,
   LIMA_TEXEL_FORMAT_I16 = 20,
   LIMA_TEXEL_FORMAT_RGB_888 = 21,
   LIMA_TEXEL_FORMAT_RGBA_8888 = 22,
   LIMA_TEXEL_FORMAT_RGBX_8888 = 23,
   LIMA_TEXEL_FORMAT_ETC1_RGB8 = 32,
   LIMA_TEXEL_FORMAT_L16_FLOAT = 34,
   LIMA_TEXEL_FORMAT_A16_FLOAT = 35,
   LIMA_TEXEL_FORMAT_I16_FLOAT = 36,
   LIMA_TEXEL_FORMAT_L16A16_FLOAT = 37,
   LIMA_TEXEL_FORMAT_R16G16B16A16_FLOAT = 38,
   LIMA_TEXEL_FORMAT_Z24X8 = 44,
   LIMA_TEXEL_FORMAT_R16G16B16_FLOAT = 47,
   LIMA_TEXEL_FORMAT_Z24S8_RLD = 50,
};

static inline const char *
lima_texel_format_as_str(enum lima_texel_format imm)
{
    switch (imm) {
    case LIMA_TEXEL_FORMAT_L8: return "L8";
    case LIMA_TEXEL_FORMAT_A8: return "A8";
    case LIMA_TEXEL_FORMAT_I8: return "I8";
    case LIMA_TEXEL_FORMAT_BGR_565: return "BGR_565";
    case LIMA_TEXEL_FORMAT_BGRA_5551: return "BGRA_5551";
    case LIMA_TEXEL_FORMAT_BGRA_4444: return "BGRA_4444";
    case LIMA_TEXEL_FORMAT_L8A8: return "L8A8";
    case LIMA_TEXEL_FORMAT_L16: return "L16";
    case LIMA_TEXEL_FORMAT_A16: return "A16";
    case LIMA_TEXEL_FORMAT_I16: return "I16";
    case LIMA_TEXEL_FORMAT_RGB_888: return "RGB_888";
    case LIMA_TEXEL_FORMAT_RGBA_8888: return "RGBA_8888";
    case LIMA_TEXEL_FORMAT_RGBX_8888: return "RGBX_8888";
    case LIMA_TEXEL_FORMAT_ETC1_RGB8: return "ETC1_RGB8";
    case LIMA_TEXEL_FORMAT_L16_FLOAT: return "L16_FLOAT";
    case LIMA_TEXEL_FORMAT_A16_FLOAT: return "A16_FLOAT";
    case LIMA_TEXEL_FORMAT_I16_FLOAT: return "I16_FLOAT";
    case LIMA_TEXEL_FORMAT_L16A16_FLOAT: return "L16A16_FLOAT";
    case LIMA_TEXEL_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    case LIMA_TEXEL_FORMAT_Z24X8: return "Z24X8";
    case LIMA_TEXEL_FORMAT_R16G16B16_FLOAT: return "R16G16B16_FLOAT";
    case LIMA_TEXEL_FORMAT_Z24S8_RLD: return "Z24S8_RLD";
    default: return NULL;
    }
}
enum lima_texture_range {
   LIMA_TEXTURE_RANGE_UNORM = 0,
   LIMA_TEXTURE_RANGE_SNORM_HALF = 1,
   LIMA_TEXTURE_RANGE_SNORM = 2,
   LIMA_TEXTURE_RANGE_UNORM_INVERTED = 3,
};

static inline const char *
lima_texture_range_as_str(enum lima_texture_range imm)
{
    switch (imm) {
    case LIMA_TEXTURE_RANGE_UNORM: return "UNORM";
    case LIMA_TEXTURE_RANGE_SNORM_HALF: return "SNORM HALF";
    case LIMA_TEXTURE_RANGE_SNORM: return "SNORM";
    case LIMA_TEXTURE_RANGE_UNORM_INVERTED: return "UNORM INVERTED";
    default: return NULL;
    }
}
enum lima_sampler_dimension {
   LIMA_SAMPLER_DIMENSION_1D = 0,
   LIMA_SAMPLER_DIMENSION_2D = 1,
   LIMA_SAMPLER_DIMENSION_3D = 2,
};

static inline const char *
lima_sampler_dimension_as_str(enum lima_sampler_dimension imm)
{
    switch (imm) {
    case LIMA_SAMPLER_DIMENSION_1D: return "1D";
    case LIMA_SAMPLER_DIMENSION_2D: return "2D";
    case LIMA_SAMPLER_DIMENSION_3D: return "3D";
    default: return NULL;
    }
}
enum lima_mipfilter {
   LIMA_MIPFILTER_NEAREST = 0,
   LIMA_MIPFILTER_LINEAR = 3,
};

static inline const char *
lima_mipfilter_as_str(enum lima_mipfilter imm)
{
    switch (imm) {
    case LIMA_MIPFILTER_NEAREST: return "Nearest";
    case LIMA_MIPFILTER_LINEAR: return "Linear";
    default: return NULL;
    }
}
enum lima_tex_wrap {
   LIMA_TEX_WRAP_REPEAT = 0,
   LIMA_TEX_WRAP_CLAMP_TO_EDGE = 1,
   LIMA_TEX_WRAP_CLAMP = 2,
   LIMA_TEX_WRAP_CLAMP_TO_BORDER = 3,
   LIMA_TEX_WRAP_MIRROR_REPEAT = 4,
   LIMA_TEX_WRAP_MIRROR_CLAMP_TO_EDGE = 5,
   LIMA_TEX_WRAP_MIRROR_CLAMP = 6,
   LIMA_TEX_WRAP_MIRROR_CLAMP_TO_BORDER = 7,
};

static inline const char *
lima_tex_wrap_as_str(enum lima_tex_wrap imm)
{
    switch (imm) {
    case LIMA_TEX_WRAP_REPEAT: return "Repeat";
    case LIMA_TEX_WRAP_CLAMP_TO_EDGE: return "Clamp To Edge";
    case LIMA_TEX_WRAP_CLAMP: return "Clamp";
    case LIMA_TEX_WRAP_CLAMP_TO_BORDER: return "Clamp To Border";
    case LIMA_TEX_WRAP_MIRROR_REPEAT: return "Mirror Repeat";
    case LIMA_TEX_WRAP_MIRROR_CLAMP_TO_EDGE: return "Mirror Clamp To Edge";
    case LIMA_TEX_WRAP_MIRROR_CLAMP: return "Mirror Clamp";
    case LIMA_TEX_WRAP_MIRROR_CLAMP_TO_BORDER: return "Mirror Clamp To Border";
    default: return NULL;
    }
}
enum lima_texture_layout {
   LIMA_TEXTURE_LAYOUT_LINEAR = 0,
   LIMA_TEXTURE_LAYOUT_TILED = 3,
};

static inline const char *
lima_texture_layout_as_str(enum lima_texture_layout imm)
{
    switch (imm) {
    case LIMA_TEXTURE_LAYOUT_LINEAR: return "Linear";
    case LIMA_TEXTURE_LAYOUT_TILED: return "Tiled";
    default: return NULL;
    }
}
enum lima_polygon_tile_size {
   LIMA_POLYGON_TILE_SIZE_1X1 = 0,
   LIMA_POLYGON_TILE_SIZE_2X2 = 1,
   LIMA_POLYGON_TILE_SIZE_4X4 = 2,
};

static inline const char *
lima_polygon_tile_size_as_str(enum lima_polygon_tile_size imm)
{
    switch (imm) {
    case LIMA_POLYGON_TILE_SIZE_1X1: return "1x1";
    case LIMA_POLYGON_TILE_SIZE_2X2: return "2x2";
    case LIMA_POLYGON_TILE_SIZE_4X4: return "4x4";
    default: return NULL;
    }
}
enum lima_blend_func {
   LIMA_BLEND_FUNC_SUBTRACT = 0,
   LIMA_BLEND_FUNC_REVERSE_SUBTRACT = 1,
   LIMA_BLEND_FUNC_ADD = 2,
   LIMA_BLEND_FUNC_LOGICOP = 3,
   LIMA_BLEND_FUNC_MIN = 4,
   LIMA_BLEND_FUNC_MAX = 5,
};

static inline const char *
lima_blend_func_as_str(enum lima_blend_func imm)
{
    switch (imm) {
    case LIMA_BLEND_FUNC_SUBTRACT: return "Subtract";
    case LIMA_BLEND_FUNC_REVERSE_SUBTRACT: return "Reverse Subtract";
    case LIMA_BLEND_FUNC_ADD: return "Add";
    case LIMA_BLEND_FUNC_LOGICOP: return "Logicop";
    case LIMA_BLEND_FUNC_MIN: return "Min";
    case LIMA_BLEND_FUNC_MAX: return "Max";
    default: return NULL;
    }
}
enum lima_blend_factor_color {
   LIMA_BLEND_FACTOR_COLOR_SRC_COLOR = 0,
   LIMA_BLEND_FACTOR_COLOR_DST_COLOR = 1,
   LIMA_BLEND_FACTOR_COLOR_CONST_COLOR = 2,
   LIMA_BLEND_FACTOR_COLOR_ZERO = 3,
   LIMA_BLEND_FACTOR_COLOR_SRC_ALPHA_SAT = 4,
   LIMA_BLEND_FACTOR_COLOR_SRC1_COLOR = 5,
   LIMA_BLEND_FACTOR_COLOR_INV_SRC_COLOR = 8,
   LIMA_BLEND_FACTOR_COLOR_INV_DST_COLOR = 9,
   LIMA_BLEND_FACTOR_COLOR_INV_CONST_COLOR = 10,
   LIMA_BLEND_FACTOR_COLOR_ONE = 11,
   LIMA_BLEND_FACTOR_COLOR_INV_SRC1_COLOR = 13,
   LIMA_BLEND_FACTOR_COLOR_SRC_ALPHA = 16,
   LIMA_BLEND_FACTOR_COLOR_DST_ALPHA = 17,
   LIMA_BLEND_FACTOR_COLOR_CONST_ALPHA = 18,
   LIMA_BLEND_FACTOR_COLOR_SRC1_ALPHA = 21,
   LIMA_BLEND_FACTOR_COLOR_INV_SRC_ALPHA = 24,
   LIMA_BLEND_FACTOR_COLOR_INV_DST_ALPHA = 25,
   LIMA_BLEND_FACTOR_COLOR_INV_CONST_ALPHA = 26,
   LIMA_BLEND_FACTOR_COLOR_INV_SRC1_ALPHA = 29,
};

static inline const char *
lima_blend_factor_color_as_str(enum lima_blend_factor_color imm)
{
    switch (imm) {
    case LIMA_BLEND_FACTOR_COLOR_SRC_COLOR: return "SRC Color";
    case LIMA_BLEND_FACTOR_COLOR_DST_COLOR: return "DST Color";
    case LIMA_BLEND_FACTOR_COLOR_CONST_COLOR: return "Const Color";
    case LIMA_BLEND_FACTOR_COLOR_ZERO: return "Zero";
    case LIMA_BLEND_FACTOR_COLOR_SRC_ALPHA_SAT: return "SRC Alpha SAT";
    case LIMA_BLEND_FACTOR_COLOR_SRC1_COLOR: return "SRC1 Color";
    case LIMA_BLEND_FACTOR_COLOR_INV_SRC_COLOR: return "INV SRC Color";
    case LIMA_BLEND_FACTOR_COLOR_INV_DST_COLOR: return "INV DST Color";
    case LIMA_BLEND_FACTOR_COLOR_INV_CONST_COLOR: return "INV Const Color";
    case LIMA_BLEND_FACTOR_COLOR_ONE: return "One";
    case LIMA_BLEND_FACTOR_COLOR_INV_SRC1_COLOR: return "INV SRC1 Color";
    case LIMA_BLEND_FACTOR_COLOR_SRC_ALPHA: return "SRC Alpha";
    case LIMA_BLEND_FACTOR_COLOR_DST_ALPHA: return "DST Alpha";
    case LIMA_BLEND_FACTOR_COLOR_CONST_ALPHA: return "Const Alpha";
    case LIMA_BLEND_FACTOR_COLOR_SRC1_ALPHA: return "SRC1 Alpha";
    case LIMA_BLEND_FACTOR_COLOR_INV_SRC_ALPHA: return "INV SRC Alpha";
    case LIMA_BLEND_FACTOR_COLOR_INV_DST_ALPHA: return "INV DST Alpha";
    case LIMA_BLEND_FACTOR_COLOR_INV_CONST_ALPHA: return "INV Const Alpha";
    case LIMA_BLEND_FACTOR_COLOR_INV_SRC1_ALPHA: return "INV SRC1 Alpha";
    default: return NULL;
    }
}
enum lima_blend_factor_alpha {
   LIMA_BLEND_FACTOR_ALPHA_SRC_ALPHA = 0,
   LIMA_BLEND_FACTOR_ALPHA_DST_ALPHA = 1,
   LIMA_BLEND_FACTOR_ALPHA_CONST_ALPHA = 2,
   LIMA_BLEND_FACTOR_ALPHA_ZERO = 3,
   LIMA_BLEND_FACTOR_ALPHA_SRC_ALPHA_SAT = 4,
   LIMA_BLEND_FACTOR_ALPHA_SRC1_ALPHA = 5,
   LIMA_BLEND_FACTOR_ALPHA_INV_SRC_ALPHA = 8,
   LIMA_BLEND_FACTOR_ALPHA_INV_DST_ALPHA = 9,
   LIMA_BLEND_FACTOR_ALPHA_INV_CONST_ALPHA = 10,
   LIMA_BLEND_FACTOR_ALPHA_ONE = 11,
   LIMA_BLEND_FACTOR_ALPHA_INV_SRC1_ALPHA = 13,
};

static inline const char *
lima_blend_factor_alpha_as_str(enum lima_blend_factor_alpha imm)
{
    switch (imm) {
    case LIMA_BLEND_FACTOR_ALPHA_SRC_ALPHA: return "SRC Alpha";
    case LIMA_BLEND_FACTOR_ALPHA_DST_ALPHA: return "DST Alpha";
    case LIMA_BLEND_FACTOR_ALPHA_CONST_ALPHA: return "Const Alpha";
    case LIMA_BLEND_FACTOR_ALPHA_ZERO: return "Zero";
    case LIMA_BLEND_FACTOR_ALPHA_SRC_ALPHA_SAT: return "SRC Alpha SAT";
    case LIMA_BLEND_FACTOR_ALPHA_SRC1_ALPHA: return "SRC1 Alpha";
    case LIMA_BLEND_FACTOR_ALPHA_INV_SRC_ALPHA: return "INV SRC Alpha";
    case LIMA_BLEND_FACTOR_ALPHA_INV_DST_ALPHA: return "INV DST Alpha";
    case LIMA_BLEND_FACTOR_ALPHA_INV_CONST_ALPHA: return "INV Const Alpha";
    case LIMA_BLEND_FACTOR_ALPHA_ONE: return "One";
    case LIMA_BLEND_FACTOR_ALPHA_INV_SRC1_ALPHA: return "INV SRC1 Alpha";
    default: return NULL;
    }
}
enum lima_logic_op {
   LIMA_LOGIC_OP_CLEAR = 0,
   LIMA_LOGIC_OP_NOR = 1,
   LIMA_LOGIC_OP_AND_INVERTED = 2,
   LIMA_LOGIC_OP_COPY_INVERTED = 3,
   LIMA_LOGIC_OP_AND_REVERSE = 4,
   LIMA_LOGIC_OP_INVERT = 5,
   LIMA_LOGIC_OP_XOR = 6,
   LIMA_LOGIC_OP_NAND = 7,
   LIMA_LOGIC_OP_AND = 8,
   LIMA_LOGIC_OP_EQUIV = 9,
   LIMA_LOGIC_OP_NOOP = 10,
   LIMA_LOGIC_OP_OR_INVERTED = 11,
   LIMA_LOGIC_OP_COPY = 12,
   LIMA_LOGIC_OP_OR_REVERSE = 13,
   LIMA_LOGIC_OP_OR = 14,
   LIMA_LOGIC_OP_SET = 15,
};

static inline const char *
lima_logic_op_as_str(enum lima_logic_op imm)
{
    switch (imm) {
    case LIMA_LOGIC_OP_CLEAR: return "CLEAR";
    case LIMA_LOGIC_OP_NOR: return "NOR";
    case LIMA_LOGIC_OP_AND_INVERTED: return "AND Inverted";
    case LIMA_LOGIC_OP_COPY_INVERTED: return "COPY Inverted";
    case LIMA_LOGIC_OP_AND_REVERSE: return "AND Reverse";
    case LIMA_LOGIC_OP_INVERT: return "INVERT";
    case LIMA_LOGIC_OP_XOR: return "XOR";
    case LIMA_LOGIC_OP_NAND: return "NAND";
    case LIMA_LOGIC_OP_AND: return "AND";
    case LIMA_LOGIC_OP_EQUIV: return "EQUIV";
    case LIMA_LOGIC_OP_NOOP: return "NOOP";
    case LIMA_LOGIC_OP_OR_INVERTED: return "OR Inverted";
    case LIMA_LOGIC_OP_COPY: return "COPY";
    case LIMA_LOGIC_OP_OR_REVERSE: return "OR Reverse";
    case LIMA_LOGIC_OP_OR: return "OR";
    case LIMA_LOGIC_OP_SET: return "SET";
    default: return NULL;
    }
}
enum lima_draw_mode {
   LIMA_DRAW_MODE_POINTS = 0,
   LIMA_DRAW_MODE_LINES = 1,
   LIMA_DRAW_MODE_TRIANGLES = 2,
};

static inline const char *
lima_draw_mode_as_str(enum lima_draw_mode imm)
{
    switch (imm) {
    case LIMA_DRAW_MODE_POINTS: return "Points";
    case LIMA_DRAW_MODE_LINES: return "Lines";
    case LIMA_DRAW_MODE_TRIANGLES: return "Triangles";
    default: return NULL;
    }
}
enum lima_varying_type {
   LIMA_VARYING_TYPE_VEC4_FP32 = 0,
   LIMA_VARYING_TYPE_VEC2_FP32 = 1,
   LIMA_VARYING_TYPE_VEC4_FP16 = 2,
   LIMA_VARYING_TYPE_VEC2_FP16 = 3,
   LIMA_VARYING_TYPE_VEC2_SNORM16 = 4,
   LIMA_VARYING_TYPE_VEC2_UNORM16 = 5,
   LIMA_VARYING_TYPE_VEC3_LOWP = 6,
   LIMA_VARYING_TYPE_VEC4_UNORM8 = 7,
};

static inline const char *
lima_varying_type_as_str(enum lima_varying_type imm)
{
    switch (imm) {
    case LIMA_VARYING_TYPE_VEC4_FP32: return "Vec4 FP32";
    case LIMA_VARYING_TYPE_VEC2_FP32: return "Vec2 FP32";
    case LIMA_VARYING_TYPE_VEC4_FP16: return "Vec4 FP16";
    case LIMA_VARYING_TYPE_VEC2_FP16: return "Vec2 FP16";
    case LIMA_VARYING_TYPE_VEC2_SNORM16: return "Vec2 SNORM16";
    case LIMA_VARYING_TYPE_VEC2_UNORM16: return "Vec2 UNORM16";
    case LIMA_VARYING_TYPE_VEC3_LOWP: return "Vec3 LOWP";
    case LIMA_VARYING_TYPE_VEC4_UNORM8: return "Vec4 UNORM8";
    default: return NULL;
    }
}
enum lima_stencil_op {
   LIMA_STENCIL_OP_KEEP = 0,
   LIMA_STENCIL_OP_REPLACE = 1,
   LIMA_STENCIL_OP_ZERO = 2,
   LIMA_STENCIL_OP_INVERT = 3,
   LIMA_STENCIL_OP_INCR_WRAP = 4,
   LIMA_STENCIL_OP_DECR_WRAP = 5,
   LIMA_STENCIL_OP_INCR = 6,
   LIMA_STENCIL_OP_DECR = 7,
};

static inline const char *
lima_stencil_op_as_str(enum lima_stencil_op imm)
{
    switch (imm) {
    case LIMA_STENCIL_OP_KEEP: return "Keep";
    case LIMA_STENCIL_OP_REPLACE: return "Replace";
    case LIMA_STENCIL_OP_ZERO: return "Zero";
    case LIMA_STENCIL_OP_INVERT: return "Invert";
    case LIMA_STENCIL_OP_INCR_WRAP: return "Incr Wrap";
    case LIMA_STENCIL_OP_DECR_WRAP: return "Decr Wrap";
    case LIMA_STENCIL_OP_INCR: return "Incr";
    case LIMA_STENCIL_OP_DECR: return "Decr";
    default: return NULL;
    }
}
enum lima_compare_func {
   LIMA_COMPARE_FUNC_NEVER = 0,
   LIMA_COMPARE_FUNC_LESS = 1,
   LIMA_COMPARE_FUNC_EQUAL = 2,
   LIMA_COMPARE_FUNC_LEQUAL = 3,
   LIMA_COMPARE_FUNC_GREATER = 4,
   LIMA_COMPARE_FUNC_NOTEQUAL = 5,
   LIMA_COMPARE_FUNC_GEQUAL = 6,
   LIMA_COMPARE_FUNC_ALWAYS = 7,
};

static inline const char *
lima_compare_func_as_str(enum lima_compare_func imm)
{
    switch (imm) {
    case LIMA_COMPARE_FUNC_NEVER: return "Never";
    case LIMA_COMPARE_FUNC_LESS: return "Less";
    case LIMA_COMPARE_FUNC_EQUAL: return "Equal";
    case LIMA_COMPARE_FUNC_LEQUAL: return "Lequal";
    case LIMA_COMPARE_FUNC_GREATER: return "Greater";
    case LIMA_COMPARE_FUNC_NOTEQUAL: return "Notequal";
    case LIMA_COMPARE_FUNC_GEQUAL: return "Gequal";
    case LIMA_COMPARE_FUNC_ALWAYS: return "Always";
    default: return NULL;
    }
}
struct LIMA_TEXTURE_DESCRIPTOR {
   enum lima_texel_format               texel_format;
   bool                                 reverse_component_order;
   bool                                 swap_rb;
   enum lima_texture_range              range;
   bool                                 has_signed_components;
   uint32_t                             unknown_0;
   uint32_t                             stride;
   uint32_t                             unknown_1;
   bool                                 unnorm_coords;
   bool                                 unknown_2;
   bool                                 cube_map;
   enum lima_sampler_dimension          sampler_dim;
   float                                min_lod;
   float                                max_lod;
   float                                lod_bias;
   uint32_t                             max_anisotropy;
   bool                                 has_stride;
   enum lima_mipfilter                  mipfilter;
   bool                                 min_img_filter_nearest;
   bool                                 mag_img_filter_nearest;
   enum lima_tex_wrap                   wrap_s;
   enum lima_tex_wrap                   wrap_t;
   enum lima_tex_wrap                   wrap_r;
   uint32_t                             width;
   uint32_t                             height;
   uint32_t                             depth;
   float                                border_red;
   float                                border_green;
   float                                border_blue;
   float                                border_alpha;
   uint32_t                             unknown_4;
   enum lima_texture_layout             layout;
   uint32_t                             unknown_5;
   uint32_t                             mip_0;
   uint32_t                             mip_1;
   uint32_t                             mip_2;
   uint32_t                             mip_3;
   uint32_t                             mip_4;
   uint32_t                             mip_5;
   uint32_t                             mip_6;
   uint32_t                             mip_7;
   uint32_t                             mip_8;
   uint32_t                             mip_9;
   uint32_t                             mip_10;
   uint32_t                             unknown_6;
};

#define LIMA_TEXTURE_DESCRIPTOR_header          \
   .max_anisotropy = 1

static inline void
LIMA_TEXTURE_DESCRIPTOR_pack(GLOBAL_ uint32_t * restrict cl,
                             const struct LIMA_TEXTURE_DESCRIPTOR * restrict values)
{
   assert((values->mip_0 & 0x3f) == 0);
   assert((values->mip_1 & 0x3f) == 0);
   assert((values->mip_2 & 0x3f) == 0);
   assert((values->mip_3 & 0x3f) == 0);
   assert((values->mip_4 & 0x3f) == 0);
   assert((values->mip_5 & 0x3f) == 0);
   assert((values->mip_6 & 0x3f) == 0);
   assert((values->mip_7 & 0x3f) == 0);
   assert((values->mip_8 & 0x3f) == 0);
   assert((values->mip_9 & 0x3f) == 0);
   assert((values->mip_10 & 0x3f) == 0);
   lima_genxml_validate_bounds("Texture Descriptor::texel_format", values->texel_format, 0x40ull);
   lima_genxml_validate_bounds("Texture Descriptor::reverse_component_order", values->reverse_component_order, 0x2ull);
   lima_genxml_validate_bounds("Texture Descriptor::swap_rb", values->swap_rb, 0x2ull);
   lima_genxml_validate_bounds("Texture Descriptor::range", values->range, 0x4ull);
   lima_genxml_validate_bounds("Texture Descriptor::has_signed_components", values->has_signed_components, 0x2ull);
   lima_genxml_validate_bounds("Texture Descriptor::unknown_0", values->unknown_0, 0x20ull);
   lima_genxml_validate_bounds("Texture Descriptor::stride", values->stride, 0x8000ull);
   lima_genxml_validate_bounds("Texture Descriptor::unknown_1", values->unknown_1, 0x100ull);
   cl[ 0] = util_bitpack_uint(values->texel_format, 0, 5) |
            util_bitpack_uint(values->reverse_component_order, 6, 6) |
            util_bitpack_uint(values->swap_rb, 7, 7) |
            util_bitpack_uint(values->range, 8, 9) |
            util_bitpack_uint(values->has_signed_components, 10, 10) |
            util_bitpack_uint(values->unknown_0, 11, 15) |
            util_bitpack_uint(values->stride, 16, 30) |
            util_bitpack_uint(values->unknown_1, 31, 38);
   lima_genxml_validate_bounds("Texture Descriptor::unknown_1", values->unknown_1, 0x100ull);
   lima_genxml_validate_bounds("Texture Descriptor::unnorm_coords", values->unnorm_coords, 0x2ull);
   lima_genxml_validate_bounds("Texture Descriptor::unknown_2", values->unknown_2, 0x2ull);
   lima_genxml_validate_bounds("Texture Descriptor::cube_map", values->cube_map, 0x2ull);
   lima_genxml_validate_bounds("Texture Descriptor::sampler_dim", values->sampler_dim, 0x4ull);
   cl[ 1] = util_bitpack_uint(values->unknown_1, 31, 38) >> 32 |
            util_bitpack_uint(values->unnorm_coords, 7, 7) |
            util_bitpack_uint(values->unknown_2, 8, 8) |
            util_bitpack_uint(values->cube_map, 9, 9) |
            util_bitpack_uint(values->sampler_dim, 10, 11) |
            util_bitpack_ufixed_clamp(values->min_lod, 12, 19, 4) |
            util_bitpack_ufixed_clamp(values->max_lod, 20, 27, 4) |
            util_bitpack_sfixed_clamp(values->lod_bias, 28, 36, 4);
   lima_genxml_validate_bounds("Texture Descriptor::max_anisotropy", util_logbase2(values->max_anisotropy), 0x8ull);
   lima_genxml_validate_bounds("Texture Descriptor::has_stride", values->has_stride, 0x2ull);
   lima_genxml_validate_bounds("Texture Descriptor::mipfilter", values->mipfilter, 0x4ull);
   lima_genxml_validate_bounds("Texture Descriptor::min_img_filter_nearest", values->min_img_filter_nearest, 0x2ull);
   lima_genxml_validate_bounds("Texture Descriptor::mag_img_filter_nearest", values->mag_img_filter_nearest, 0x2ull);
   lima_genxml_validate_bounds("Texture Descriptor::wrap_s", values->wrap_s, 0x8ull);
   lima_genxml_validate_bounds("Texture Descriptor::wrap_t", values->wrap_t, 0x8ull);
   lima_genxml_validate_bounds("Texture Descriptor::wrap_r", values->wrap_r, 0x8ull);
   lima_genxml_validate_bounds("Texture Descriptor::width", values->width, 0x2000ull);
   cl[ 2] = util_bitpack_sfixed_clamp(values->lod_bias, 28, 36, 4) >> 32 |
            util_bitpack_uint(util_logbase2(values->max_anisotropy), 5, 7) |
            util_bitpack_uint(values->has_stride, 8, 8) |
            util_bitpack_uint(values->mipfilter, 9, 10) |
            util_bitpack_uint(values->min_img_filter_nearest, 11, 11) |
            util_bitpack_uint(values->mag_img_filter_nearest, 12, 12) |
            util_bitpack_uint(values->wrap_s, 13, 15) |
            util_bitpack_uint(values->wrap_t, 16, 18) |
            util_bitpack_uint(values->wrap_r, 19, 21) |
            util_bitpack_uint(values->width, 22, 34);
   lima_genxml_validate_bounds("Texture Descriptor::width", values->width, 0x2000ull);
   lima_genxml_validate_bounds("Texture Descriptor::height", values->height, 0x2000ull);
   lima_genxml_validate_bounds("Texture Descriptor::depth", values->depth, 0x2000ull);
   cl[ 3] = util_bitpack_uint(values->width, 22, 34) >> 32 |
            util_bitpack_uint(values->height, 3, 15) |
            util_bitpack_uint(values->depth, 16, 28) |
            __gen_pack_unorm16(values->border_red, 29, 44);
   cl[ 4] = __gen_pack_unorm16(values->border_red, 29, 44) >> 32 |
            __gen_pack_unorm16(values->border_green, 13, 28) |
            __gen_pack_unorm16(values->border_blue, 29, 44);
   lima_genxml_validate_bounds("Texture Descriptor::unknown_4", values->unknown_4, 0x10000ull);
   cl[ 5] = __gen_pack_unorm16(values->border_blue, 29, 44) >> 32 |
            __gen_pack_unorm16(values->border_alpha, 13, 28) |
            util_bitpack_uint(values->unknown_4, 29, 44);
   lima_genxml_validate_bounds("Texture Descriptor::unknown_4", values->unknown_4, 0x10000ull);
   lima_genxml_validate_bounds("Texture Descriptor::layout", values->layout, 0x4ull);
   lima_genxml_validate_bounds("Texture Descriptor::unknown_5", values->unknown_5, 0x8000ull);
   lima_genxml_validate_bounds("Texture Descriptor::mip_0", values->mip_0, 0x100000000ull);
   cl[ 6] = util_bitpack_uint(values->unknown_4, 29, 44) >> 32 |
            util_bitpack_uint(values->layout, 13, 14) |
            util_bitpack_uint(values->unknown_5, 15, 29) |
            util_bitpack_uint(values->mip_0, 24, 55);
   lima_genxml_validate_bounds("Texture Descriptor::mip_0", values->mip_0, 0x100000000ull);
   lima_genxml_validate_bounds("Texture Descriptor::mip_1", values->mip_1, 0x100000000ull);
   cl[ 7] = util_bitpack_uint(values->mip_0, 24, 55) >> 32 |
            util_bitpack_uint(values->mip_1, 18, 49);
   lima_genxml_validate_bounds("Texture Descriptor::mip_1", values->mip_1, 0x100000000ull);
   lima_genxml_validate_bounds("Texture Descriptor::mip_2", values->mip_2, 0x100000000ull);
   cl[ 8] = util_bitpack_uint(values->mip_1, 18, 49) >> 32 |
            util_bitpack_uint(values->mip_2, 12, 43);
   lima_genxml_validate_bounds("Texture Descriptor::mip_2", values->mip_2, 0x100000000ull);
   lima_genxml_validate_bounds("Texture Descriptor::mip_3", values->mip_3, 0x100000000ull);
   cl[ 9] = util_bitpack_uint(values->mip_2, 12, 43) >> 32 |
            util_bitpack_uint(values->mip_3, 6, 37);
   lima_genxml_validate_bounds("Texture Descriptor::mip_3", values->mip_3, 0x100000000ull);
   lima_genxml_validate_bounds("Texture Descriptor::mip_4", values->mip_4, 0x100000000ull);
   cl[10] = util_bitpack_uint(values->mip_3, 6, 37) >> 32 |
            util_bitpack_uint(values->mip_4, 0, 31);
   lima_genxml_validate_bounds("Texture Descriptor::mip_5", values->mip_5 >> 6, 0x4000000ull);
   lima_genxml_validate_bounds("Texture Descriptor::mip_6", values->mip_6, 0x100000000ull);
   cl[11] = util_bitpack_uint(values->mip_5 >> 6, 0, 25) |
            util_bitpack_uint(values->mip_6, 20, 51);
   lima_genxml_validate_bounds("Texture Descriptor::mip_6", values->mip_6, 0x100000000ull);
   lima_genxml_validate_bounds("Texture Descriptor::mip_7", values->mip_7, 0x100000000ull);
   cl[12] = util_bitpack_uint(values->mip_6, 20, 51) >> 32 |
            util_bitpack_uint(values->mip_7, 14, 45);
   lima_genxml_validate_bounds("Texture Descriptor::mip_7", values->mip_7, 0x100000000ull);
   lima_genxml_validate_bounds("Texture Descriptor::mip_8", values->mip_8, 0x100000000ull);
   cl[13] = util_bitpack_uint(values->mip_7, 14, 45) >> 32 |
            util_bitpack_uint(values->mip_8, 8, 39);
   lima_genxml_validate_bounds("Texture Descriptor::mip_8", values->mip_8, 0x100000000ull);
   lima_genxml_validate_bounds("Texture Descriptor::mip_9", values->mip_9, 0x100000000ull);
   cl[14] = util_bitpack_uint(values->mip_8, 8, 39) >> 32 |
            util_bitpack_uint(values->mip_9, 2, 33);
   lima_genxml_validate_bounds("Texture Descriptor::mip_9", values->mip_9, 0x100000000ull);
   lima_genxml_validate_bounds("Texture Descriptor::mip_10", values->mip_10 >> 6, 0x4000000ull);
   lima_genxml_validate_bounds("Texture Descriptor::unknown_6", values->unknown_6, 0x10ull);
   cl[15] = util_bitpack_uint(values->mip_9, 2, 33) >> 32 |
            util_bitpack_uint(values->mip_10 >> 6, 2, 27) |
            util_bitpack_uint(values->unknown_6, 28, 31);
}

#define LIMA_TEXTURE_DESCRIPTOR_LENGTH 64
struct lima_texture_descriptor_packed { uint32_t opaque[16];};
static inline bool
LIMA_TEXTURE_DESCRIPTOR_unpack(FILE_TYPE *fp, CONSTANT_ uint8_t * restrict cl,
                               struct LIMA_TEXTURE_DESCRIPTOR * restrict values)
{
   values->texel_format = (enum lima_texel_format) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 0, 5);
   values->reverse_component_order = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 6, 6);
   values->swap_rb = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 7, 7);
   values->range = (enum lima_texture_range) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 8, 9);
   values->has_signed_components = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 10, 10);
   values->unknown_0 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 11, 15);
   values->stride = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 16, 30);
   values->unknown_1 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 31, 38);
   values->unnorm_coords = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 39, 39);
   values->unknown_2 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 40, 40);
   values->cube_map = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 41, 41);
   values->sampler_dim = (enum lima_sampler_dimension) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 42, 43);
   values->min_lod = __gen_unpack_ulod((CONSTANT_ uint32_t *) cl, 44, 51);
   values->max_lod = __gen_unpack_ulod((CONSTANT_ uint32_t *) cl, 52, 59);
   values->lod_bias = __gen_unpack_slod((CONSTANT_ uint32_t *) cl, 60, 68);
   values->max_anisotropy = 1 << __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 69, 71);
   values->has_stride = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 72, 72);
   values->mipfilter = (enum lima_mipfilter) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 73, 74);
   values->min_img_filter_nearest = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 75, 75);
   values->mag_img_filter_nearest = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 76, 76);
   values->wrap_s = (enum lima_tex_wrap) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 77, 79);
   values->wrap_t = (enum lima_tex_wrap) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 80, 82);
   values->wrap_r = (enum lima_tex_wrap) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 83, 85);
   values->width = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 86, 98);
   values->height = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 99, 111);
   values->depth = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 112, 124);
   values->border_red = __gen_unpack_unorm16((CONSTANT_ uint32_t *) cl, 125, 140);
   values->border_green = __gen_unpack_unorm16((CONSTANT_ uint32_t *) cl, 141, 156);
   values->border_blue = __gen_unpack_unorm16((CONSTANT_ uint32_t *) cl, 157, 172);
   values->border_alpha = __gen_unpack_unorm16((CONSTANT_ uint32_t *) cl, 173, 188);
   values->unknown_4 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 189, 204);
   values->layout = (enum lima_texture_layout) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 205, 206);
   values->unknown_5 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 207, 221);
   values->mip_0 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 222, 247) << 6;
   values->mip_1 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 248, 273) << 6;
   values->mip_2 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 274, 299) << 6;
   values->mip_3 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 300, 325) << 6;
   values->mip_4 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 326, 351) << 6;
   values->mip_5 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 352, 377) << 6;
   values->mip_6 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 378, 403) << 6;
   values->mip_7 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 404, 429) << 6;
   values->mip_8 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 430, 455) << 6;
   values->mip_9 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 456, 481) << 6;
   values->mip_10 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 482, 507) << 6;
   values->unknown_6 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 508, 511);
   return true;
}

static inline void
LIMA_TEXTURE_DESCRIPTOR_print(FILE *fp, const struct LIMA_TEXTURE_DESCRIPTOR * values, unsigned indent)
{
   if (lima_texel_format_as_str(values->texel_format))
     fprintf(fp, "%*sTexel Format: %s\n", indent, "", lima_texel_format_as_str(values->texel_format));
   else
     fprintf(fp, "%*sTexel Format: unknown %X (XXX)\n", indent, "", values->texel_format);
   fprintf(fp, "%*sReverse Component Order: %s\n", indent, "", values->reverse_component_order ? "true" : "false");
   fprintf(fp, "%*sSwap RB: %s\n", indent, "", values->swap_rb ? "true" : "false");
   if (lima_texture_range_as_str(values->range))
     fprintf(fp, "%*sRange: %s\n", indent, "", lima_texture_range_as_str(values->range));
   else
     fprintf(fp, "%*sRange: unknown %X (XXX)\n", indent, "", values->range);
   fprintf(fp, "%*sHas Signed Components: %s\n", indent, "", values->has_signed_components ? "true" : "false");
   fprintf(fp, "%*sUnknown 0: 0x%" PRIx32 "\n", indent, "", values->unknown_0);
   fprintf(fp, "%*sStride: %u\n", indent, "", values->stride);
   fprintf(fp, "%*sUnknown 1: 0x%" PRIx32 "\n", indent, "", values->unknown_1);
   fprintf(fp, "%*sUnnorm Coords: %s\n", indent, "", values->unnorm_coords ? "true" : "false");
   fprintf(fp, "%*sUnknown 2: %s\n", indent, "", values->unknown_2 ? "true" : "false");
   fprintf(fp, "%*sCube Map: %s\n", indent, "", values->cube_map ? "true" : "false");
   if (lima_sampler_dimension_as_str(values->sampler_dim))
     fprintf(fp, "%*sSampler Dim: %s\n", indent, "", lima_sampler_dimension_as_str(values->sampler_dim));
   else
     fprintf(fp, "%*sSampler Dim: unknown %X (XXX)\n", indent, "", values->sampler_dim);
   fprintf(fp, "%*sMin Lod: %f\n", indent, "", values->min_lod);
   fprintf(fp, "%*sMax Lod: %f\n", indent, "", values->max_lod);
   fprintf(fp, "%*sLod Bias: %f\n", indent, "", values->lod_bias);
   fprintf(fp, "%*sMax Anisotropy: %u\n", indent, "", values->max_anisotropy);
   fprintf(fp, "%*sHas Stride: %s\n", indent, "", values->has_stride ? "true" : "false");
   if (lima_mipfilter_as_str(values->mipfilter))
     fprintf(fp, "%*sMipfilter: %s\n", indent, "", lima_mipfilter_as_str(values->mipfilter));
   else
     fprintf(fp, "%*sMipfilter: unknown %X (XXX)\n", indent, "", values->mipfilter);
   fprintf(fp, "%*sMin Img Filter Nearest: %s\n", indent, "", values->min_img_filter_nearest ? "true" : "false");
   fprintf(fp, "%*sMag Img Filter Nearest: %s\n", indent, "", values->mag_img_filter_nearest ? "true" : "false");
   if (lima_tex_wrap_as_str(values->wrap_s))
     fprintf(fp, "%*sWrap S: %s\n", indent, "", lima_tex_wrap_as_str(values->wrap_s));
   else
     fprintf(fp, "%*sWrap S: unknown %X (XXX)\n", indent, "", values->wrap_s);
   if (lima_tex_wrap_as_str(values->wrap_t))
     fprintf(fp, "%*sWrap T: %s\n", indent, "", lima_tex_wrap_as_str(values->wrap_t));
   else
     fprintf(fp, "%*sWrap T: unknown %X (XXX)\n", indent, "", values->wrap_t);
   if (lima_tex_wrap_as_str(values->wrap_r))
     fprintf(fp, "%*sWrap R: %s\n", indent, "", lima_tex_wrap_as_str(values->wrap_r));
   else
     fprintf(fp, "%*sWrap R: unknown %X (XXX)\n", indent, "", values->wrap_r);
   fprintf(fp, "%*sWidth: %u\n", indent, "", values->width);
   fprintf(fp, "%*sHeight: %u\n", indent, "", values->height);
   fprintf(fp, "%*sDepth: %u\n", indent, "", values->depth);
   fprintf(fp, "%*sBorder Red: %f\n", indent, "", values->border_red);
   fprintf(fp, "%*sBorder Green: %f\n", indent, "", values->border_green);
   fprintf(fp, "%*sBorder Blue: %f\n", indent, "", values->border_blue);
   fprintf(fp, "%*sBorder Alpha: %f\n", indent, "", values->border_alpha);
   fprintf(fp, "%*sUnknown 4: 0x%" PRIx32 "\n", indent, "", values->unknown_4);
   if (lima_texture_layout_as_str(values->layout))
     fprintf(fp, "%*sLayout: %s\n", indent, "", lima_texture_layout_as_str(values->layout));
   else
     fprintf(fp, "%*sLayout: unknown %X (XXX)\n", indent, "", values->layout);
   fprintf(fp, "%*sUnknown 5: 0x%" PRIx32 "\n", indent, "", values->unknown_5);
   fprintf(fp, "%*sMip 0: 0x%" PRIx32 "\n", indent, "", values->mip_0);
   fprintf(fp, "%*sMip 1: 0x%" PRIx32 "\n", indent, "", values->mip_1);
   fprintf(fp, "%*sMip 2: 0x%" PRIx32 "\n", indent, "", values->mip_2);
   fprintf(fp, "%*sMip 3: 0x%" PRIx32 "\n", indent, "", values->mip_3);
   fprintf(fp, "%*sMip 4: 0x%" PRIx32 "\n", indent, "", values->mip_4);
   fprintf(fp, "%*sMip 5: 0x%" PRIx32 "\n", indent, "", values->mip_5);
   fprintf(fp, "%*sMip 6: 0x%" PRIx32 "\n", indent, "", values->mip_6);
   fprintf(fp, "%*sMip 7: 0x%" PRIx32 "\n", indent, "", values->mip_7);
   fprintf(fp, "%*sMip 8: 0x%" PRIx32 "\n", indent, "", values->mip_8);
   fprintf(fp, "%*sMip 9: 0x%" PRIx32 "\n", indent, "", values->mip_9);
   fprintf(fp, "%*sMip 10: 0x%" PRIx32 "\n", indent, "", values->mip_10);
   fprintf(fp, "%*sUnknown 6: 0x%" PRIx32 "\n", indent, "", values->unknown_6);
}
struct LIMA_CLEAR_VALUE_8BPC_COLOR {
   float                                red;
   float                                green;
   float                                blue;
   float                                alpha;
};

#define LIMA_CLEAR_VALUE_8BPC_COLOR_header 0

static inline void
LIMA_CLEAR_VALUE_8BPC_COLOR_pack(GLOBAL_ uint32_t * restrict cl,
                                 const struct LIMA_CLEAR_VALUE_8BPC_COLOR * restrict values)
{
   cl[ 0] = __gen_pack_unorm8(values->red, 0, 7) |
            __gen_pack_unorm8(values->green, 8, 15) |
            __gen_pack_unorm8(values->blue, 16, 23) |
            __gen_pack_unorm8(values->alpha, 24, 31);
}

#define LIMA_CLEAR_VALUE_8BPC_COLOR_LENGTH 4
struct lima_clear_value_8bpc_color_packed { uint32_t opaque[1];};
static inline bool
LIMA_CLEAR_VALUE_8BPC_COLOR_unpack(FILE_TYPE *fp, CONSTANT_ uint8_t * restrict cl,
                                   struct LIMA_CLEAR_VALUE_8BPC_COLOR * restrict values)
{
   values->red = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 0, 7);
   values->green = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 8, 15);
   values->blue = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 16, 23);
   values->alpha = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 24, 31);
   return true;
}

static inline void
LIMA_CLEAR_VALUE_8BPC_COLOR_print(FILE *fp, const struct LIMA_CLEAR_VALUE_8BPC_COLOR * values, unsigned indent)
{
   fprintf(fp, "%*sRed: %f\n", indent, "", values->red);
   fprintf(fp, "%*sGreen: %f\n", indent, "", values->green);
   fprintf(fp, "%*sBlue: %f\n", indent, "", values->blue);
   fprintf(fp, "%*sAlpha: %f\n", indent, "", values->alpha);
}
struct LIMA_CLEAR_VALUE_16BPC_COLOR {
   float                                red;
   float                                green;
   float                                blue;
   float                                alpha;
};

#define LIMA_CLEAR_VALUE_16BPC_COLOR_header 0

static inline void
LIMA_CLEAR_VALUE_16BPC_COLOR_pack(GLOBAL_ uint32_t * restrict cl,
                                  const struct LIMA_CLEAR_VALUE_16BPC_COLOR * restrict values)
{
   cl[ 0] = __gen_pack_unorm16(values->red, 0, 15) |
            __gen_pack_unorm16(values->green, 16, 31);
   cl[ 1] = __gen_pack_unorm16(values->blue, 0, 15) |
            __gen_pack_unorm16(values->alpha, 16, 31);
}

#define LIMA_CLEAR_VALUE_16BPC_COLOR_LENGTH 8
struct lima_clear_value_16bpc_color_packed { uint32_t opaque[2];};
static inline bool
LIMA_CLEAR_VALUE_16BPC_COLOR_unpack(FILE_TYPE *fp, CONSTANT_ uint8_t * restrict cl,
                                    struct LIMA_CLEAR_VALUE_16BPC_COLOR * restrict values)
{
   values->red = __gen_unpack_unorm16((CONSTANT_ uint32_t *) cl, 0, 15);
   values->green = __gen_unpack_unorm16((CONSTANT_ uint32_t *) cl, 16, 31);
   values->blue = __gen_unpack_unorm16((CONSTANT_ uint32_t *) cl, 32, 47);
   values->alpha = __gen_unpack_unorm16((CONSTANT_ uint32_t *) cl, 48, 63);
   return true;
}

static inline void
LIMA_CLEAR_VALUE_16BPC_COLOR_print(FILE *fp, const struct LIMA_CLEAR_VALUE_16BPC_COLOR * values, unsigned indent)
{
   fprintf(fp, "%*sRed: %f\n", indent, "", values->red);
   fprintf(fp, "%*sGreen: %f\n", indent, "", values->green);
   fprintf(fp, "%*sBlue: %f\n", indent, "", values->blue);
   fprintf(fp, "%*sAlpha: %f\n", indent, "", values->alpha);
}
struct LIMA_TILEBUFFER_CHANNEL_LAYOUT {
   uint32_t                             red;
   uint32_t                             green;
   uint32_t                             blue;
   uint32_t                             alpha;
};

#define LIMA_TILEBUFFER_CHANNEL_LAYOUT_header 0

static inline void
LIMA_TILEBUFFER_CHANNEL_LAYOUT_pack(GLOBAL_ uint32_t * restrict cl,
                                    const struct LIMA_TILEBUFFER_CHANNEL_LAYOUT * restrict values)
{
   lima_genxml_validate_bounds("Tilebuffer Channel Layout::red", values->red, 0x10ull);
   lima_genxml_validate_bounds("Tilebuffer Channel Layout::green", values->green, 0x10ull);
   lima_genxml_validate_bounds("Tilebuffer Channel Layout::blue", values->blue, 0x10ull);
   lima_genxml_validate_bounds("Tilebuffer Channel Layout::alpha", values->alpha, 0x10ull);
   cl[ 0] = util_bitpack_uint(values->red, 0, 3) |
            util_bitpack_uint(values->green, 4, 7) |
            util_bitpack_uint(values->blue, 8, 11) |
            util_bitpack_uint(values->alpha, 12, 15);
}

#define LIMA_TILEBUFFER_CHANNEL_LAYOUT_LENGTH 2
struct lima_tilebuffer_channel_layout_packed { uint32_t opaque[1];};
static inline bool
LIMA_TILEBUFFER_CHANNEL_LAYOUT_unpack(FILE_TYPE *fp, CONSTANT_ uint8_t * restrict cl,
                                      struct LIMA_TILEBUFFER_CHANNEL_LAYOUT * restrict values)
{
   values->red = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 0, 3);
   values->green = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 4, 7);
   values->blue = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 8, 11);
   values->alpha = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 12, 15);
   return true;
}

static inline void
LIMA_TILEBUFFER_CHANNEL_LAYOUT_print(FILE *fp, const struct LIMA_TILEBUFFER_CHANNEL_LAYOUT * values, unsigned indent)
{
   fprintf(fp, "%*sRed: %u\n", indent, "", values->red);
   fprintf(fp, "%*sGreen: %u\n", indent, "", values->green);
   fprintf(fp, "%*sBlue: %u\n", indent, "", values->blue);
   fprintf(fp, "%*sAlpha: %u\n", indent, "", values->alpha);
}
struct LIMA_PP_FRAME {
   uint32_t                             plbu_array_address;
   uint32_t                             render_address;
   uint32_t                             vertex_address;
   bool                                 fp16_tilebuffer;
   bool                                 early_z;
   bool                                 early_z_1_disable;
   bool                                 early_z_2_disable;
   bool                                 origin_lower_left;
   bool                                 quad_cover_summation;
   uint32_t                             clear_value_depth;
   uint32_t                             clear_value_stencil;
   struct LIMA_CLEAR_VALUE_16BPC_COLOR  clear_value_16bpc_color;
   struct LIMA_CLEAR_VALUE_8BPC_COLOR   clear_value_8bpc_color_0;
   struct LIMA_CLEAR_VALUE_8BPC_COLOR   clear_value_8bpc_color_1;
   struct LIMA_CLEAR_VALUE_8BPC_COLOR   clear_value_8bpc_color_2;
   struct LIMA_CLEAR_VALUE_8BPC_COLOR   clear_value_8bpc_color_3;
   uint32_t                             bounding_box_right;
   uint32_t                             bounding_box_left;
   uint32_t                             bounding_box_bottom;
   uint32_t                             fragment_stack_address;
   uint32_t                             fragment_stack_size;
   uint32_t                             fragment_stack_pointer_initial_value;
   uint32_t                             origin_x;
   uint32_t                             origin_y;
   uint32_t                             subpixel_specifier;
   uint32_t                             tiebreak_mode;
   uint32_t                             polygon_tile_amount_x;
   uint32_t                             polygon_tile_amount_y;
   enum lima_polygon_tile_size          polygon_tile_size;
   bool                                 scale_points_and_lines;
   bool                                 scale_dithering_matrix;
   bool                                 scale_fragcoord;
   bool                                 scale_derivatives;
   bool                                 flip_sprites;
   bool                                 flip_dithering_matrix;
   bool                                 flip_fragcoord;
   bool                                 flip_derivatives;
   uint32_t                             scale_factor_x;
   uint32_t                             scale_factor_y;
   struct LIMA_TILEBUFFER_CHANNEL_LAYOUT tilebuffer_channel_layout;
};

#define LIMA_PP_FRAME_header                    \
   .clear_value_16bpc_color = { LIMA_CLEAR_VALUE_16BPC_COLOR_header },  \
   .clear_value_8bpc_color_0 = { LIMA_CLEAR_VALUE_8BPC_COLOR_header },  \
   .clear_value_8bpc_color_1 = { LIMA_CLEAR_VALUE_8BPC_COLOR_header },  \
   .clear_value_8bpc_color_2 = { LIMA_CLEAR_VALUE_8BPC_COLOR_header },  \
   .clear_value_8bpc_color_3 = { LIMA_CLEAR_VALUE_8BPC_COLOR_header },  \
   .bounding_box_right = 1,  \
   .bounding_box_bottom = 1,  \
   .origin_y = 1,  \
   .tilebuffer_channel_layout = { LIMA_TILEBUFFER_CHANNEL_LAYOUT_header }

static inline void
LIMA_PP_FRAME_pack(GLOBAL_ uint32_t * restrict cl,
                   const struct LIMA_PP_FRAME * restrict values)
{
   assert((values->plbu_array_address & 0x7) == 0);
   assert((values->render_address & 0x3f) == 0);
   assert((values->vertex_address & 0x3f) == 0);
   assert(values->bounding_box_right >= 1);
   assert(values->bounding_box_bottom >= 1);
   assert((values->fragment_stack_address & 0x3f) == 0);
   assert(values->origin_y >= 1);
   lima_genxml_validate_bounds("PP Frame::plbu_array_address", values->plbu_array_address, 0x100000000ull);
   cl[ 0] = util_bitpack_uint(values->plbu_array_address, 0, 31);
   lima_genxml_validate_bounds("PP Frame::render_address", values->render_address, 0x100000000ull);
   cl[ 1] = util_bitpack_uint(values->render_address, 0, 31);
   lima_genxml_validate_bounds("PP Frame::vertex_address", values->vertex_address, 0x100000000ull);
   cl[ 2] = util_bitpack_uint(values->vertex_address, 0, 31);
   lima_genxml_validate_bounds("PP Frame::fp16_tilebuffer", values->fp16_tilebuffer, 0x2ull);
   lima_genxml_validate_bounds("PP Frame::early_z", values->early_z, 0x2ull);
   lima_genxml_validate_bounds("PP Frame::early_z_1_disable", values->early_z_1_disable, 0x2ull);
   lima_genxml_validate_bounds("PP Frame::early_z_2_disable", values->early_z_2_disable, 0x2ull);
   lima_genxml_validate_bounds("PP Frame::origin_lower_left", values->origin_lower_left, 0x2ull);
   lima_genxml_validate_bounds("PP Frame::quad_cover_summation", values->quad_cover_summation, 0x2ull);
   cl[ 3] = util_bitpack_uint(values->fp16_tilebuffer, 0, 0) |
            util_bitpack_uint(values->early_z, 1, 1) |
            util_bitpack_uint(values->early_z_1_disable, 3, 3) |
            util_bitpack_uint(values->early_z_2_disable, 4, 4) |
            util_bitpack_uint(values->origin_lower_left, 5, 5) |
            util_bitpack_uint(values->quad_cover_summation, 6, 6);
   lima_genxml_validate_bounds("PP Frame::clear_value_depth", values->clear_value_depth, 0x1000000ull);
   cl[ 4] = util_bitpack_uint(values->clear_value_depth, 0, 23);
   lima_genxml_validate_bounds("PP Frame::clear_value_stencil", values->clear_value_stencil, 0x100ull);
   cl[ 5] = util_bitpack_uint(values->clear_value_stencil, 0, 7);
   cl[ 6] = __gen_pack_unorm16(values->clear_value_16bpc_color.red, 0, 15) |
            __gen_pack_unorm16(values->clear_value_16bpc_color.green, 16, 31) |
            __gen_pack_unorm8(values->clear_value_8bpc_color_0.red, 0, 7) |
            __gen_pack_unorm8(values->clear_value_8bpc_color_0.green, 8, 15) |
            __gen_pack_unorm8(values->clear_value_8bpc_color_0.blue, 16, 23) |
            __gen_pack_unorm8(values->clear_value_8bpc_color_0.alpha, 24, 31);
   cl[ 7] = __gen_pack_unorm16(values->clear_value_16bpc_color.blue, 0, 15) |
            __gen_pack_unorm16(values->clear_value_16bpc_color.alpha, 16, 31) |
            __gen_pack_unorm8(values->clear_value_8bpc_color_1.red, 0, 7) |
            __gen_pack_unorm8(values->clear_value_8bpc_color_1.green, 8, 15) |
            __gen_pack_unorm8(values->clear_value_8bpc_color_1.blue, 16, 23) |
            __gen_pack_unorm8(values->clear_value_8bpc_color_1.alpha, 24, 31);
   cl[ 8] = __gen_pack_unorm8(values->clear_value_8bpc_color_2.red, 0, 7) |
            __gen_pack_unorm8(values->clear_value_8bpc_color_2.green, 8, 15) |
            __gen_pack_unorm8(values->clear_value_8bpc_color_2.blue, 16, 23) |
            __gen_pack_unorm8(values->clear_value_8bpc_color_2.alpha, 24, 31);
   cl[ 9] = __gen_pack_unorm8(values->clear_value_8bpc_color_3.red, 0, 7) |
            __gen_pack_unorm8(values->clear_value_8bpc_color_3.green, 8, 15) |
            __gen_pack_unorm8(values->clear_value_8bpc_color_3.blue, 16, 23) |
            __gen_pack_unorm8(values->clear_value_8bpc_color_3.alpha, 24, 31);
   lima_genxml_validate_bounds("PP Frame::bounding_box_right", values->bounding_box_right - 1, 0x4000ull);
   lima_genxml_validate_bounds("PP Frame::bounding_box_left", values->bounding_box_left, 0x10ull);
   cl[10] = util_bitpack_uint(values->bounding_box_right - 1, 0, 13) |
            util_bitpack_uint(values->bounding_box_left, 16, 19);
   lima_genxml_validate_bounds("PP Frame::bounding_box_bottom", values->bounding_box_bottom - 1, 0x4000ull);
   cl[11] = util_bitpack_uint(values->bounding_box_bottom - 1, 0, 13);
   lima_genxml_validate_bounds("PP Frame::fragment_stack_address", values->fragment_stack_address, 0x100000000ull);
   cl[12] = util_bitpack_uint(values->fragment_stack_address, 0, 31);
   lima_genxml_validate_bounds("PP Frame::fragment_stack_size", values->fragment_stack_size, 0x10000ull);
   lima_genxml_validate_bounds("PP Frame::fragment_stack_pointer_initial_value", values->fragment_stack_pointer_initial_value, 0x10000ull);
   cl[13] = util_bitpack_uint(values->fragment_stack_size, 0, 15) |
            util_bitpack_uint(values->fragment_stack_pointer_initial_value, 16, 31);
   cl[14] = 0;
   cl[15] = 0;
   lima_genxml_validate_bounds("PP Frame::origin_x", values->origin_x, 0x10000ull);
   cl[16] = util_bitpack_uint(values->origin_x, 0, 15);
   lima_genxml_validate_bounds("PP Frame::origin_y", values->origin_y - 1, 0x10000ull);
   cl[17] = util_bitpack_uint(values->origin_y - 1, 0, 15);
   lima_genxml_validate_bounds("PP Frame::subpixel_specifier", values->subpixel_specifier, 0x100ull);
   cl[18] = util_bitpack_uint(values->subpixel_specifier, 0, 7);
   lima_genxml_validate_bounds("PP Frame::tiebreak_mode", values->tiebreak_mode, 0x4ull);
   cl[19] = util_bitpack_uint(values->tiebreak_mode, 0, 1);
   lima_genxml_validate_bounds("PP Frame::polygon_tile_amount_x", values->polygon_tile_amount_x, 0x40ull);
   lima_genxml_validate_bounds("PP Frame::polygon_tile_amount_y", values->polygon_tile_amount_y, 0x40ull);
   lima_genxml_validate_bounds("PP Frame::polygon_tile_size", values->polygon_tile_size, 0x4ull);
   cl[20] = util_bitpack_uint(values->polygon_tile_amount_x, 0, 5) |
            util_bitpack_uint(values->polygon_tile_amount_y, 16, 21) |
            util_bitpack_uint(values->polygon_tile_size, 28, 29);
   lima_genxml_validate_bounds("PP Frame::scale_points_and_lines", values->scale_points_and_lines, 0x2ull);
   lima_genxml_validate_bounds("PP Frame::scale_dithering_matrix", values->scale_dithering_matrix, 0x2ull);
   lima_genxml_validate_bounds("PP Frame::scale_fragcoord", values->scale_fragcoord, 0x2ull);
   lima_genxml_validate_bounds("PP Frame::scale_derivatives", values->scale_derivatives, 0x2ull);
   lima_genxml_validate_bounds("PP Frame::flip_sprites", values->flip_sprites, 0x2ull);
   lima_genxml_validate_bounds("PP Frame::flip_dithering_matrix", values->flip_dithering_matrix, 0x2ull);
   lima_genxml_validate_bounds("PP Frame::flip_fragcoord", values->flip_fragcoord, 0x2ull);
   lima_genxml_validate_bounds("PP Frame::flip_derivatives", values->flip_derivatives, 0x2ull);
   lima_genxml_validate_bounds("PP Frame::scale_factor_x", values->scale_factor_x, 0x8ull);
   lima_genxml_validate_bounds("PP Frame::scale_factor_y", values->scale_factor_y, 0x8ull);
   cl[21] = util_bitpack_uint(values->scale_points_and_lines, 0, 0) |
            util_bitpack_uint(values->scale_dithering_matrix, 1, 1) |
            util_bitpack_uint(values->scale_fragcoord, 2, 2) |
            util_bitpack_uint(values->scale_derivatives, 3, 3) |
            util_bitpack_uint(values->flip_sprites, 8, 8) |
            util_bitpack_uint(values->flip_dithering_matrix, 9, 9) |
            util_bitpack_uint(values->flip_fragcoord, 10, 10) |
            util_bitpack_uint(values->flip_derivatives, 11, 11) |
            util_bitpack_uint(values->scale_factor_x, 16, 18) |
            util_bitpack_uint(values->scale_factor_y, 20, 22);
   lima_genxml_validate_bounds("PP Frame::red", values->tilebuffer_channel_layout.red, 0x10ull);
   lima_genxml_validate_bounds("PP Frame::green", values->tilebuffer_channel_layout.green, 0x10ull);
   lima_genxml_validate_bounds("PP Frame::blue", values->tilebuffer_channel_layout.blue, 0x10ull);
   lima_genxml_validate_bounds("PP Frame::alpha", values->tilebuffer_channel_layout.alpha, 0x10ull);
   cl[22] = util_bitpack_uint(values->tilebuffer_channel_layout.red, 0, 3) |
            util_bitpack_uint(values->tilebuffer_channel_layout.green, 4, 7) |
            util_bitpack_uint(values->tilebuffer_channel_layout.blue, 8, 11) |
            util_bitpack_uint(values->tilebuffer_channel_layout.alpha, 12, 15);
}

#define LIMA_PP_FRAME_LENGTH 92
struct lima_pp_frame_packed { uint32_t opaque[23];};
static inline bool
LIMA_PP_FRAME_unpack(FILE_TYPE *fp, CONSTANT_ uint8_t * restrict cl,
                     struct LIMA_PP_FRAME * restrict values)
{
   values->plbu_array_address = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 3, 31) << 3;
   values->render_address = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 38, 63) << 6;
   values->vertex_address = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 70, 95) << 6;
   values->fp16_tilebuffer = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 96, 96);
   values->early_z = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 97, 97);
   values->early_z_1_disable = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 99, 99);
   values->early_z_2_disable = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 100, 100);
   values->origin_lower_left = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 101, 101);
   values->quad_cover_summation = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 102, 102);
   values->clear_value_depth = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 128, 151);
   values->clear_value_stencil = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 160, 167);
   values->clear_value_16bpc_color.red = __gen_unpack_unorm16((CONSTANT_ uint32_t *) cl, 192, 207);
   values->clear_value_16bpc_color.green = __gen_unpack_unorm16((CONSTANT_ uint32_t *) cl, 208, 223);
   values->clear_value_16bpc_color.blue = __gen_unpack_unorm16((CONSTANT_ uint32_t *) cl, 224, 239);
   values->clear_value_16bpc_color.alpha = __gen_unpack_unorm16((CONSTANT_ uint32_t *) cl, 240, 255);
   values->clear_value_8bpc_color_0.red = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 192, 199);
   values->clear_value_8bpc_color_0.green = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 200, 207);
   values->clear_value_8bpc_color_0.blue = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 208, 215);
   values->clear_value_8bpc_color_0.alpha = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 216, 223);
   values->clear_value_8bpc_color_1.red = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 224, 231);
   values->clear_value_8bpc_color_1.green = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 232, 239);
   values->clear_value_8bpc_color_1.blue = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 240, 247);
   values->clear_value_8bpc_color_1.alpha = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 248, 255);
   values->clear_value_8bpc_color_2.red = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 256, 263);
   values->clear_value_8bpc_color_2.green = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 264, 271);
   values->clear_value_8bpc_color_2.blue = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 272, 279);
   values->clear_value_8bpc_color_2.alpha = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 280, 287);
   values->clear_value_8bpc_color_3.red = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 288, 295);
   values->clear_value_8bpc_color_3.green = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 296, 303);
   values->clear_value_8bpc_color_3.blue = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 304, 311);
   values->clear_value_8bpc_color_3.alpha = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 312, 319);
   values->bounding_box_right = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 320, 333) + 1;
   values->bounding_box_left = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 336, 339);
   values->bounding_box_bottom = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 352, 365) + 1;
   values->fragment_stack_address = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 390, 415) << 6;
   values->fragment_stack_size = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 416, 431);
   values->fragment_stack_pointer_initial_value = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 432, 447);
   values->origin_x = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 512, 527);
   values->origin_y = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 544, 559) + 1;
   values->subpixel_specifier = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 576, 583);
   values->tiebreak_mode = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 608, 609);
   values->polygon_tile_amount_x = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 640, 645);
   values->polygon_tile_amount_y = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 656, 661);
   values->polygon_tile_size = (enum lima_polygon_tile_size) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 668, 669);
   values->scale_points_and_lines = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 672, 672);
   values->scale_dithering_matrix = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 673, 673);
   values->scale_fragcoord = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 674, 674);
   values->scale_derivatives = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 675, 675);
   values->flip_sprites = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 680, 680);
   values->flip_dithering_matrix = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 681, 681);
   values->flip_fragcoord = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 682, 682);
   values->flip_derivatives = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 683, 683);
   values->scale_factor_x = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 688, 690);
   values->scale_factor_y = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 692, 694);
   values->tilebuffer_channel_layout.red = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 704, 707);
   values->tilebuffer_channel_layout.green = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 708, 711);
   values->tilebuffer_channel_layout.blue = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 712, 715);
   values->tilebuffer_channel_layout.alpha = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 716, 719);
   bool valid = true;
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 0, 0x7);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 1, 0x3f);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 2, 0x3f);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 3, 0xffffff84);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 4, 0xff000000);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 5, 0xffffff00);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 10, 0xfff0c000);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 11, 0xffffc000);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 12, 0x3f);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 14, 0xffffffff);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 15, 0xffffffff);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 16, 0xffff0000);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 17, 0xffff0000);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 18, 0xffffff00);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 19, 0xfffffffc);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 20, 0xcfc0ffc0);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 21, 0xff88f0f0);
   valid &= lima_genxml_validate_mask(fp, "PP Frame", cl, 22, 0xffff0000);
   return valid;
}

static inline void
LIMA_PP_FRAME_print(FILE *fp, const struct LIMA_PP_FRAME * values, unsigned indent)
{
   fprintf(fp, "%*sPLBU Array Address: %u\n", indent, "", values->plbu_array_address);
   fprintf(fp, "%*sRender Address: %u\n", indent, "", values->render_address);
   fprintf(fp, "%*sVertex Address: %u\n", indent, "", values->vertex_address);
   fprintf(fp, "%*sFP16 Tilebuffer: %s\n", indent, "", values->fp16_tilebuffer ? "true" : "false");
   fprintf(fp, "%*sEarly Z: %s\n", indent, "", values->early_z ? "true" : "false");
   fprintf(fp, "%*sEarly Z 1 Disable: %s\n", indent, "", values->early_z_1_disable ? "true" : "false");
   fprintf(fp, "%*sEarly Z 2 Disable: %s\n", indent, "", values->early_z_2_disable ? "true" : "false");
   fprintf(fp, "%*sOrigin Lower Left: %s\n", indent, "", values->origin_lower_left ? "true" : "false");
   fprintf(fp, "%*sQuad Cover Summation: %s\n", indent, "", values->quad_cover_summation ? "true" : "false");
   fprintf(fp, "%*sClear Value Depth: %u\n", indent, "", values->clear_value_depth);
   fprintf(fp, "%*sClear Value Stencil: %u\n", indent, "", values->clear_value_stencil);
   fprintf(fp, "%*sClear Value 16bpc Color:\n", indent, "");
   LIMA_CLEAR_VALUE_16BPC_COLOR_print(fp, &values->clear_value_16bpc_color, indent + 2);
   fprintf(fp, "%*sClear Value 8bpc Color 0:\n", indent, "");
   LIMA_CLEAR_VALUE_8BPC_COLOR_print(fp, &values->clear_value_8bpc_color_0, indent + 2);
   fprintf(fp, "%*sClear Value 8bpc Color 1:\n", indent, "");
   LIMA_CLEAR_VALUE_8BPC_COLOR_print(fp, &values->clear_value_8bpc_color_1, indent + 2);
   fprintf(fp, "%*sClear Value 8bpc Color 2:\n", indent, "");
   LIMA_CLEAR_VALUE_8BPC_COLOR_print(fp, &values->clear_value_8bpc_color_2, indent + 2);
   fprintf(fp, "%*sClear Value 8bpc Color 3:\n", indent, "");
   LIMA_CLEAR_VALUE_8BPC_COLOR_print(fp, &values->clear_value_8bpc_color_3, indent + 2);
   fprintf(fp, "%*sBounding Box Right: %u\n", indent, "", values->bounding_box_right);
   fprintf(fp, "%*sBounding Box Left: %u\n", indent, "", values->bounding_box_left);
   fprintf(fp, "%*sBounding Box Bottom: %u\n", indent, "", values->bounding_box_bottom);
   fprintf(fp, "%*sFragment Stack Address: 0x%" PRIx32 "\n", indent, "", values->fragment_stack_address);
   fprintf(fp, "%*sFragment Stack Size: %u\n", indent, "", values->fragment_stack_size);
   fprintf(fp, "%*sFragment Stack Pointer Initial Value: 0x%" PRIx32 "\n", indent, "", values->fragment_stack_pointer_initial_value);
   fprintf(fp, "%*sOrigin X: %u\n", indent, "", values->origin_x);
   fprintf(fp, "%*sOrigin Y: %u\n", indent, "", values->origin_y);
   fprintf(fp, "%*sSubpixel Specifier: %u\n", indent, "", values->subpixel_specifier);
   fprintf(fp, "%*sTiebreak Mode: %u\n", indent, "", values->tiebreak_mode);
   fprintf(fp, "%*sPolygon Tile Amount X: %u\n", indent, "", values->polygon_tile_amount_x);
   fprintf(fp, "%*sPolygon Tile Amount Y: %u\n", indent, "", values->polygon_tile_amount_y);
   if (lima_polygon_tile_size_as_str(values->polygon_tile_size))
     fprintf(fp, "%*sPolygon Tile Size: %s\n", indent, "", lima_polygon_tile_size_as_str(values->polygon_tile_size));
   else
     fprintf(fp, "%*sPolygon Tile Size: unknown %X (XXX)\n", indent, "", values->polygon_tile_size);
   fprintf(fp, "%*sScale Points and Lines: %s\n", indent, "", values->scale_points_and_lines ? "true" : "false");
   fprintf(fp, "%*sScale Dithering Matrix: %s\n", indent, "", values->scale_dithering_matrix ? "true" : "false");
   fprintf(fp, "%*sScale Fragcoord: %s\n", indent, "", values->scale_fragcoord ? "true" : "false");
   fprintf(fp, "%*sScale Derivatives: %s\n", indent, "", values->scale_derivatives ? "true" : "false");
   fprintf(fp, "%*sFlip Sprites: %s\n", indent, "", values->flip_sprites ? "true" : "false");
   fprintf(fp, "%*sFlip Dithering Matrix: %s\n", indent, "", values->flip_dithering_matrix ? "true" : "false");
   fprintf(fp, "%*sFlip Fragcoord: %s\n", indent, "", values->flip_fragcoord ? "true" : "false");
   fprintf(fp, "%*sFlip Derivatives: %s\n", indent, "", values->flip_derivatives ? "true" : "false");
   fprintf(fp, "%*sScale Factor X: %u\n", indent, "", values->scale_factor_x);
   fprintf(fp, "%*sScale Factor Y: %u\n", indent, "", values->scale_factor_y);
   fprintf(fp, "%*sTilebuffer Channel Layout:\n", indent, "");
   LIMA_TILEBUFFER_CHANNEL_LAYOUT_print(fp, &values->tilebuffer_channel_layout, indent + 2);
}
struct LIMA_STENCIL {
   enum lima_compare_func               compare_function;
   enum lima_stencil_op                 stencil_fail;
   enum lima_stencil_op                 depth_fail;
   enum lima_stencil_op                 depth_pass;
   uint32_t                             unknown;
   uint32_t                             reference_value;
   uint32_t                             mask;
};

#define LIMA_STENCIL_header 0

static inline void
LIMA_STENCIL_pack(GLOBAL_ uint32_t * restrict cl,
                  const struct LIMA_STENCIL * restrict values)
{
   lima_genxml_validate_bounds("Stencil::compare_function", values->compare_function, 0x8ull);
   lima_genxml_validate_bounds("Stencil::stencil_fail", values->stencil_fail, 0x8ull);
   lima_genxml_validate_bounds("Stencil::depth_fail", values->depth_fail, 0x8ull);
   lima_genxml_validate_bounds("Stencil::depth_pass", values->depth_pass, 0x8ull);
   lima_genxml_validate_bounds("Stencil::unknown", values->unknown, 0x10ull);
   lima_genxml_validate_bounds("Stencil::reference_value", values->reference_value, 0x100ull);
   lima_genxml_validate_bounds("Stencil::mask", values->mask, 0x100ull);
   cl[ 0] = util_bitpack_uint(values->compare_function, 0, 2) |
            util_bitpack_uint(values->stencil_fail, 3, 5) |
            util_bitpack_uint(values->depth_fail, 6, 8) |
            util_bitpack_uint(values->depth_pass, 9, 11) |
            util_bitpack_uint(values->unknown, 12, 15) |
            util_bitpack_uint(values->reference_value, 16, 23) |
            util_bitpack_uint(values->mask, 24, 31);
}

#define LIMA_STENCIL_LENGTH 4
struct lima_stencil_packed { uint32_t opaque[1];};
static inline bool
LIMA_STENCIL_unpack(FILE_TYPE *fp, CONSTANT_ uint8_t * restrict cl,
                    struct LIMA_STENCIL * restrict values)
{
   values->compare_function = (enum lima_compare_func) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 0, 2);
   values->stencil_fail = (enum lima_stencil_op) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 3, 5);
   values->depth_fail = (enum lima_stencil_op) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 6, 8);
   values->depth_pass = (enum lima_stencil_op) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 9, 11);
   values->unknown = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 12, 15);
   values->reference_value = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 16, 23);
   values->mask = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 24, 31);
   return true;
}

static inline void
LIMA_STENCIL_print(FILE *fp, const struct LIMA_STENCIL * values, unsigned indent)
{
   if (lima_compare_func_as_str(values->compare_function))
     fprintf(fp, "%*sCompare Function: %s\n", indent, "", lima_compare_func_as_str(values->compare_function));
   else
     fprintf(fp, "%*sCompare Function: unknown %X (XXX)\n", indent, "", values->compare_function);
   if (lima_stencil_op_as_str(values->stencil_fail))
     fprintf(fp, "%*sStencil Fail: %s\n", indent, "", lima_stencil_op_as_str(values->stencil_fail));
   else
     fprintf(fp, "%*sStencil Fail: unknown %X (XXX)\n", indent, "", values->stencil_fail);
   if (lima_stencil_op_as_str(values->depth_fail))
     fprintf(fp, "%*sDepth Fail: %s\n", indent, "", lima_stencil_op_as_str(values->depth_fail));
   else
     fprintf(fp, "%*sDepth Fail: unknown %X (XXX)\n", indent, "", values->depth_fail);
   if (lima_stencil_op_as_str(values->depth_pass))
     fprintf(fp, "%*sDepth Pass: %s\n", indent, "", lima_stencil_op_as_str(values->depth_pass));
   else
     fprintf(fp, "%*sDepth Pass: unknown %X (XXX)\n", indent, "", values->depth_pass);
   fprintf(fp, "%*sUnknown: 0x%" PRIx32 "\n", indent, "", values->unknown);
   fprintf(fp, "%*sReference Value: 0x%" PRIx32 "\n", indent, "", values->reference_value);
   fprintf(fp, "%*sMask: 0x%" PRIx32 "\n", indent, "", values->mask);
}
struct LIMA_RENDER_STATE {
   float                                blend_color_b;
   float                                blend_color_g;
   float                                blend_color_r;
   float                                blend_color_a;
   enum lima_blend_func                 blend_func_rgb;
   enum lima_blend_func                 blend_func_a;
   enum lima_logic_op                   logicop_rgb;
   enum lima_blend_factor_color         blend_factor_src_rgb;
   enum lima_blend_factor_color         blend_factor_dst_rgb;
   enum lima_logic_op                   logicop_a;
   enum lima_blend_factor_alpha         blend_factor_src_a;
   enum lima_blend_factor_alpha         blend_factor_dst_a;
   uint32_t                             unknown_0;
   uint32_t                             color_mask;
   bool                                 depth_test;
   enum lima_compare_func               depth_compare_func;
   bool                                 depth_test_near_clip_disable;
   bool                                 depth_test_far_clip_disable;
   uint32_t                             frag_depth_register;
   bool                                 shader_writes_depth_stencil;
   bool                                 shader_writes_depth;
   bool                                 shader_writes_stencil;
   uint32_t                             unknown_1;
   uint32_t                             depth_offset_scale;
   uint32_t                             depth_offset_units;
   float                                viewport_near;
   float                                viewport_far;
   struct LIMA_STENCIL                  stencil_front;
   struct LIMA_STENCIL                  stencil_back;
   uint32_t                             stencil_write_mask_front;
   uint32_t                             stencil_write_mask_back;
   float                                alpha_reference_value;
   uint32_t                             unknown_2;
   enum lima_compare_func               alpha_test_func;
   uint32_t                             msaa;
   bool                                 alpha_to_coverage;
   bool                                 alpha_to_one;
   bool                                 unknown_3;
   enum lima_draw_mode                  draw_mode;
   uint32_t                             sample_mask;
   uint32_t                             frag_color_register_0;
   uint32_t                             frag_color_register_1;
   uint32_t                             frag_color_register_2;
   uint32_t                             frag_color_register_3;
   uint32_t                             fs_first_instr_length;
   uint32_t                             shader_address;
   enum lima_varying_type               varying_type_0;
   enum lima_varying_type               varying_type_1;
   enum lima_varying_type               varying_type_2;
   enum lima_varying_type               varying_type_3;
   enum lima_varying_type               varying_type_4;
   enum lima_varying_type               varying_type_5;
   enum lima_varying_type               varying_type_6;
   enum lima_varying_type               varying_type_7;
   enum lima_varying_type               varying_type_8;
   enum lima_varying_type               varying_type_9;
   uint32_t                             varying_type_10_low;
   uint32_t                             uniform_count;
   uint32_t                             uniforms_address;
   uint32_t                             unknown_4;
   uint32_t                             textures_address;
   uint32_t                             varying_stride;
   bool                                 has_samplers;
   bool                                 unknown_5;
   bool                                 has_uniforms;
   bool                                 unknown_6;
   bool                                 early_z;
   uint32_t                             unknown_7;
   bool                                 pixel_kill;
   bool                                 unknown_8;
   uint32_t                             sampler_count;
   uint32_t                             frag_color_1_register;
   uint32_t                             unknown_9;
   bool                                 front_face_ccw;
   bool                                 dithering;
   uint32_t                             unknown_10;
   bool                                 uniform_bit;
   uint32_t                             unknown_11;
   uint32_t                             varying_type_10_high;
   enum lima_varying_type               varying_type_11;
   uint32_t                             varyings_address;
};

#define LIMA_RENDER_STATE_header                \
   .stencil_front = { LIMA_STENCIL_header },  \
   .stencil_back = { LIMA_STENCIL_header }

static inline void
LIMA_RENDER_STATE_pack(GLOBAL_ uint32_t * restrict cl,
                       const struct LIMA_RENDER_STATE * restrict values)
{
   assert((values->shader_address & 0x1f) == 0);
   assert((values->uniforms_address & 0xf) == 0);
   assert((values->textures_address & 0xf) == 0);
   assert((values->varying_stride & 0x7) == 0);
   assert((values->varyings_address & 0xf) == 0);
   cl[ 0] = __gen_pack_unorm8(values->blend_color_b, 0, 7) |
            __gen_pack_unorm8(values->blend_color_g, 16, 23);
   cl[ 1] = __gen_pack_unorm8(values->blend_color_r, 0, 7) |
            __gen_pack_unorm8(values->blend_color_a, 16, 23);
   lima_genxml_validate_bounds("Render State::blend_func_rgb", values->blend_func_rgb, 0x8ull);
   lima_genxml_validate_bounds("Render State::blend_func_a", values->blend_func_a, 0x8ull);
   lima_genxml_validate_bounds("Render State::logicop_rgb", values->logicop_rgb, 0x10ull);
   lima_genxml_validate_bounds("Render State::blend_factor_src_rgb", values->blend_factor_src_rgb, 0x20ull);
   lima_genxml_validate_bounds("Render State::blend_factor_dst_rgb", values->blend_factor_dst_rgb, 0x20ull);
   lima_genxml_validate_bounds("Render State::logicop_a", values->logicop_a, 0x10ull);
   lima_genxml_validate_bounds("Render State::blend_factor_src_a", values->blend_factor_src_a, 0x10ull);
   lima_genxml_validate_bounds("Render State::blend_factor_dst_a", values->blend_factor_dst_a, 0x10ull);
   lima_genxml_validate_bounds("Render State::unknown_0", values->unknown_0, 0x10ull);
   lima_genxml_validate_bounds("Render State::color_mask", values->color_mask, 0x10ull);
   cl[ 2] = util_bitpack_uint(values->blend_func_rgb, 0, 2) |
            util_bitpack_uint(values->blend_func_a, 3, 5) |
            util_bitpack_uint(values->logicop_rgb, 6, 9) |
            util_bitpack_uint(values->blend_factor_src_rgb, 6, 10) |
            util_bitpack_uint(values->blend_factor_dst_rgb, 11, 15) |
            util_bitpack_uint(values->logicop_a, 16, 19) |
            util_bitpack_uint(values->blend_factor_src_a, 16, 19) |
            util_bitpack_uint(values->blend_factor_dst_a, 20, 23) |
            util_bitpack_uint(values->unknown_0, 24, 27) |
            util_bitpack_uint(values->color_mask, 28, 31);
   lima_genxml_validate_bounds("Render State::depth_test", values->depth_test, 0x2ull);
   lima_genxml_validate_bounds("Render State::depth_compare_func", values->depth_compare_func, 0x8ull);
   lima_genxml_validate_bounds("Render State::depth_test_near_clip_disable", values->depth_test_near_clip_disable, 0x2ull);
   lima_genxml_validate_bounds("Render State::depth_test_far_clip_disable", values->depth_test_far_clip_disable, 0x2ull);
   lima_genxml_validate_bounds("Render State::frag_depth_register", values->frag_depth_register, 0x10ull);
   lima_genxml_validate_bounds("Render State::shader_writes_depth_stencil", values->shader_writes_depth_stencil, 0x2ull);
   lima_genxml_validate_bounds("Render State::shader_writes_depth", values->shader_writes_depth, 0x2ull);
   lima_genxml_validate_bounds("Render State::shader_writes_stencil", values->shader_writes_stencil, 0x2ull);
   lima_genxml_validate_bounds("Render State::unknown_1", values->unknown_1, 0x8ull);
   lima_genxml_validate_bounds("Render State::depth_offset_scale", values->depth_offset_scale, 0x100ull);
   lima_genxml_validate_bounds("Render State::depth_offset_units", values->depth_offset_units, 0x100ull);
   cl[ 3] = util_bitpack_uint(values->depth_test, 0, 0) |
            util_bitpack_uint(values->depth_compare_func, 1, 3) |
            util_bitpack_uint(values->depth_test_near_clip_disable, 4, 4) |
            util_bitpack_uint(values->depth_test_far_clip_disable, 5, 5) |
            util_bitpack_uint(values->frag_depth_register, 6, 9) |
            util_bitpack_uint(values->shader_writes_depth_stencil, 10, 10) |
            util_bitpack_uint(values->shader_writes_depth, 11, 11) |
            util_bitpack_uint(values->shader_writes_stencil, 12, 12) |
            util_bitpack_uint(values->unknown_1, 13, 15) |
            util_bitpack_uint(values->depth_offset_scale, 16, 23) |
            util_bitpack_uint(values->depth_offset_units, 24, 31);
   cl[ 4] = __gen_pack_unorm16(values->viewport_near, 0, 15) |
            __gen_pack_unorm16(values->viewport_far, 16, 31);
   lima_genxml_validate_bounds("Render State::compare_function", values->stencil_front.compare_function, 0x8ull);
   lima_genxml_validate_bounds("Render State::stencil_fail", values->stencil_front.stencil_fail, 0x8ull);
   lima_genxml_validate_bounds("Render State::depth_fail", values->stencil_front.depth_fail, 0x8ull);
   lima_genxml_validate_bounds("Render State::depth_pass", values->stencil_front.depth_pass, 0x8ull);
   lima_genxml_validate_bounds("Render State::unknown", values->stencil_front.unknown, 0x10ull);
   lima_genxml_validate_bounds("Render State::reference_value", values->stencil_front.reference_value, 0x100ull);
   lima_genxml_validate_bounds("Render State::mask", values->stencil_front.mask, 0x100ull);
   cl[ 5] = util_bitpack_uint(values->stencil_front.compare_function, 0, 2) |
            util_bitpack_uint(values->stencil_front.stencil_fail, 3, 5) |
            util_bitpack_uint(values->stencil_front.depth_fail, 6, 8) |
            util_bitpack_uint(values->stencil_front.depth_pass, 9, 11) |
            util_bitpack_uint(values->stencil_front.unknown, 12, 15) |
            util_bitpack_uint(values->stencil_front.reference_value, 16, 23) |
            util_bitpack_uint(values->stencil_front.mask, 24, 31);
   lima_genxml_validate_bounds("Render State::compare_function", values->stencil_back.compare_function, 0x8ull);
   lima_genxml_validate_bounds("Render State::stencil_fail", values->stencil_back.stencil_fail, 0x8ull);
   lima_genxml_validate_bounds("Render State::depth_fail", values->stencil_back.depth_fail, 0x8ull);
   lima_genxml_validate_bounds("Render State::depth_pass", values->stencil_back.depth_pass, 0x8ull);
   lima_genxml_validate_bounds("Render State::unknown", values->stencil_back.unknown, 0x10ull);
   lima_genxml_validate_bounds("Render State::reference_value", values->stencil_back.reference_value, 0x100ull);
   lima_genxml_validate_bounds("Render State::mask", values->stencil_back.mask, 0x100ull);
   cl[ 6] = util_bitpack_uint(values->stencil_back.compare_function, 0, 2) |
            util_bitpack_uint(values->stencil_back.stencil_fail, 3, 5) |
            util_bitpack_uint(values->stencil_back.depth_fail, 6, 8) |
            util_bitpack_uint(values->stencil_back.depth_pass, 9, 11) |
            util_bitpack_uint(values->stencil_back.unknown, 12, 15) |
            util_bitpack_uint(values->stencil_back.reference_value, 16, 23) |
            util_bitpack_uint(values->stencil_back.mask, 24, 31);
   lima_genxml_validate_bounds("Render State::stencil_write_mask_front", values->stencil_write_mask_front, 0x100ull);
   lima_genxml_validate_bounds("Render State::stencil_write_mask_back", values->stencil_write_mask_back, 0x100ull);
   lima_genxml_validate_bounds("Render State::unknown_2", values->unknown_2, 0x100ull);
   cl[ 7] = util_bitpack_uint(values->stencil_write_mask_front, 0, 7) |
            util_bitpack_uint(values->stencil_write_mask_back, 8, 15) |
            __gen_pack_unorm8(values->alpha_reference_value, 16, 23) |
            util_bitpack_uint(values->unknown_2, 24, 31);
   lima_genxml_validate_bounds("Render State::alpha_test_func", values->alpha_test_func, 0x8ull);
   lima_genxml_validate_bounds("Render State::msaa", values->msaa, 0x10ull);
   lima_genxml_validate_bounds("Render State::alpha_to_coverage", values->alpha_to_coverage, 0x2ull);
   lima_genxml_validate_bounds("Render State::alpha_to_one", values->alpha_to_one, 0x2ull);
   lima_genxml_validate_bounds("Render State::unknown_3", values->unknown_3, 0x2ull);
   lima_genxml_validate_bounds("Render State::draw_mode", values->draw_mode, 0x4ull);
   lima_genxml_validate_bounds("Render State::sample_mask", values->sample_mask, 0x10ull);
   lima_genxml_validate_bounds("Render State::frag_color_register_0", values->frag_color_register_0, 0x10ull);
   lima_genxml_validate_bounds("Render State::frag_color_register_1", values->frag_color_register_1, 0x10ull);
   lima_genxml_validate_bounds("Render State::frag_color_register_2", values->frag_color_register_2, 0x10ull);
   lima_genxml_validate_bounds("Render State::frag_color_register_3", values->frag_color_register_3, 0x10ull);
   cl[ 8] = util_bitpack_uint(values->alpha_test_func, 0, 2) |
            util_bitpack_uint(values->msaa, 3, 6) |
            util_bitpack_uint(values->alpha_to_coverage, 7, 7) |
            util_bitpack_uint(values->alpha_to_one, 8, 8) |
            util_bitpack_uint(values->unknown_3, 9, 9) |
            util_bitpack_uint(values->draw_mode, 10, 11) |
            util_bitpack_uint(values->sample_mask, 12, 15) |
            util_bitpack_uint(values->frag_color_register_0, 16, 19) |
            util_bitpack_uint(values->frag_color_register_1, 20, 23) |
            util_bitpack_uint(values->frag_color_register_2, 24, 27) |
            util_bitpack_uint(values->frag_color_register_3, 28, 31);
   lima_genxml_validate_bounds("Render State::fs_first_instr_length", values->fs_first_instr_length, 0x20ull);
   lima_genxml_validate_bounds("Render State::shader_address", values->shader_address, 0x100000000ull);
   cl[ 9] = util_bitpack_uint(values->fs_first_instr_length, 0, 4) |
            util_bitpack_uint(values->shader_address, 0, 31);
   lima_genxml_validate_bounds("Render State::varying_type_0", values->varying_type_0, 0x8ull);
   lima_genxml_validate_bounds("Render State::varying_type_1", values->varying_type_1, 0x8ull);
   lima_genxml_validate_bounds("Render State::varying_type_2", values->varying_type_2, 0x8ull);
   lima_genxml_validate_bounds("Render State::varying_type_3", values->varying_type_3, 0x8ull);
   lima_genxml_validate_bounds("Render State::varying_type_4", values->varying_type_4, 0x8ull);
   lima_genxml_validate_bounds("Render State::varying_type_5", values->varying_type_5, 0x8ull);
   lima_genxml_validate_bounds("Render State::varying_type_6", values->varying_type_6, 0x8ull);
   lima_genxml_validate_bounds("Render State::varying_type_7", values->varying_type_7, 0x8ull);
   lima_genxml_validate_bounds("Render State::varying_type_8", values->varying_type_8, 0x8ull);
   lima_genxml_validate_bounds("Render State::varying_type_9", values->varying_type_9, 0x8ull);
   lima_genxml_validate_bounds("Render State::varying_type_10_low", values->varying_type_10_low, 0x4ull);
   cl[10] = util_bitpack_uint(values->varying_type_0, 0, 2) |
            util_bitpack_uint(values->varying_type_1, 3, 5) |
            util_bitpack_uint(values->varying_type_2, 6, 8) |
            util_bitpack_uint(values->varying_type_3, 9, 11) |
            util_bitpack_uint(values->varying_type_4, 12, 14) |
            util_bitpack_uint(values->varying_type_5, 15, 17) |
            util_bitpack_uint(values->varying_type_6, 18, 20) |
            util_bitpack_uint(values->varying_type_7, 21, 23) |
            util_bitpack_uint(values->varying_type_8, 24, 26) |
            util_bitpack_uint(values->varying_type_9, 27, 29) |
            util_bitpack_uint(values->varying_type_10_low, 30, 31);
   lima_genxml_validate_bounds("Render State::uniform_count", values->uniform_count, 0x10ull);
   lima_genxml_validate_bounds("Render State::uniforms_address", values->uniforms_address, 0x100000000ull);
   cl[11] = util_bitpack_uint(values->uniform_count, 0, 3) |
            util_bitpack_uint(values->uniforms_address, 0, 31);
   lima_genxml_validate_bounds("Render State::unknown_4", values->unknown_4, 0x10ull);
   lima_genxml_validate_bounds("Render State::textures_address", values->textures_address, 0x100000000ull);
   cl[12] = util_bitpack_uint(values->unknown_4, 0, 3) |
            util_bitpack_uint(values->textures_address, 0, 31);
   lima_genxml_validate_bounds("Render State::varying_stride", values->varying_stride >> 3, 0x20ull);
   lima_genxml_validate_bounds("Render State::has_samplers", values->has_samplers, 0x2ull);
   lima_genxml_validate_bounds("Render State::unknown_5", values->unknown_5, 0x2ull);
   lima_genxml_validate_bounds("Render State::has_uniforms", values->has_uniforms, 0x2ull);
   lima_genxml_validate_bounds("Render State::unknown_6", values->unknown_6, 0x2ull);
   lima_genxml_validate_bounds("Render State::early_z", values->early_z, 0x2ull);
   lima_genxml_validate_bounds("Render State::unknown_7", values->unknown_7, 0x4ull);
   lima_genxml_validate_bounds("Render State::pixel_kill", values->pixel_kill, 0x2ull);
   lima_genxml_validate_bounds("Render State::unknown_8", values->unknown_8, 0x2ull);
   lima_genxml_validate_bounds("Render State::sampler_count", values->sampler_count, 0x4000ull);
   lima_genxml_validate_bounds("Render State::frag_color_1_register", values->frag_color_1_register, 0x10ull);
   cl[13] = util_bitpack_uint(values->varying_stride >> 3, 0, 4) |
            util_bitpack_uint(values->has_samplers, 5, 5) |
            util_bitpack_uint(values->unknown_5, 6, 6) |
            util_bitpack_uint(values->has_uniforms, 7, 7) |
            util_bitpack_uint(values->unknown_6, 8, 8) |
            util_bitpack_uint(values->early_z, 9, 9) |
            util_bitpack_uint(values->unknown_7, 10, 11) |
            util_bitpack_uint(values->pixel_kill, 12, 12) |
            util_bitpack_uint(values->unknown_8, 13, 13) |
            util_bitpack_uint(values->sampler_count, 14, 27) |
            util_bitpack_uint(values->frag_color_1_register, 28, 31);
   lima_genxml_validate_bounds("Render State::unknown_9", values->unknown_9, 0x1000ull);
   lima_genxml_validate_bounds("Render State::front_face_ccw", values->front_face_ccw, 0x2ull);
   lima_genxml_validate_bounds("Render State::dithering", values->dithering, 0x2ull);
   lima_genxml_validate_bounds("Render State::unknown_10", values->unknown_10, 0x4ull);
   lima_genxml_validate_bounds("Render State::uniform_bit", values->uniform_bit, 0x2ull);
   lima_genxml_validate_bounds("Render State::unknown_11", values->unknown_11, 0x8000ull);
   cl[14] = util_bitpack_uint(values->unknown_9, 0, 11) |
            util_bitpack_uint(values->front_face_ccw, 12, 12) |
            util_bitpack_uint(values->dithering, 13, 13) |
            util_bitpack_uint(values->unknown_10, 14, 15) |
            util_bitpack_uint(values->uniform_bit, 16, 16) |
            util_bitpack_uint(values->unknown_11, 17, 31);
   lima_genxml_validate_bounds("Render State::varying_type_10_high", values->varying_type_10_high, 0x2ull);
   lima_genxml_validate_bounds("Render State::varying_type_11", values->varying_type_11, 0x8ull);
   lima_genxml_validate_bounds("Render State::varyings_address", values->varyings_address, 0x100000000ull);
   cl[15] = util_bitpack_uint(values->varying_type_10_high, 0, 0) |
            util_bitpack_uint(values->varying_type_11, 1, 3) |
            util_bitpack_uint(values->varyings_address, 0, 31);
}

#define LIMA_RENDER_STATE_LENGTH 64
struct lima_render_state_packed { uint32_t opaque[16];};
static inline bool
LIMA_RENDER_STATE_unpack(FILE_TYPE *fp, CONSTANT_ uint8_t * restrict cl,
                         struct LIMA_RENDER_STATE * restrict values)
{
   values->blend_color_b = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 0, 7);
   values->blend_color_g = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 16, 23);
   values->blend_color_r = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 32, 39);
   values->blend_color_a = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 48, 55);
   values->blend_func_rgb = (enum lima_blend_func) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 64, 66);
   values->blend_func_a = (enum lima_blend_func) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 67, 69);
   values->logicop_rgb = (enum lima_logic_op) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 70, 73);
   values->blend_factor_src_rgb = (enum lima_blend_factor_color) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 70, 74);
   values->blend_factor_dst_rgb = (enum lima_blend_factor_color) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 75, 79);
   values->logicop_a = (enum lima_logic_op) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 80, 83);
   values->blend_factor_src_a = (enum lima_blend_factor_alpha) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 80, 83);
   values->blend_factor_dst_a = (enum lima_blend_factor_alpha) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 84, 87);
   values->unknown_0 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 88, 91);
   values->color_mask = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 92, 95);
   values->depth_test = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 96, 96);
   values->depth_compare_func = (enum lima_compare_func) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 97, 99);
   values->depth_test_near_clip_disable = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 100, 100);
   values->depth_test_far_clip_disable = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 101, 101);
   values->frag_depth_register = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 102, 105);
   values->shader_writes_depth_stencil = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 106, 106);
   values->shader_writes_depth = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 107, 107);
   values->shader_writes_stencil = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 108, 108);
   values->unknown_1 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 109, 111);
   values->depth_offset_scale = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 112, 119);
   values->depth_offset_units = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 120, 127);
   values->viewport_near = __gen_unpack_unorm16((CONSTANT_ uint32_t *) cl, 128, 143);
   values->viewport_far = __gen_unpack_unorm16((CONSTANT_ uint32_t *) cl, 144, 159);
   values->stencil_front.compare_function = (enum lima_compare_func) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 160, 162);
   values->stencil_front.stencil_fail = (enum lima_stencil_op) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 163, 165);
   values->stencil_front.depth_fail = (enum lima_stencil_op) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 166, 168);
   values->stencil_front.depth_pass = (enum lima_stencil_op) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 169, 171);
   values->stencil_front.unknown = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 172, 175);
   values->stencil_front.reference_value = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 176, 183);
   values->stencil_front.mask = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 184, 191);
   values->stencil_back.compare_function = (enum lima_compare_func) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 192, 194);
   values->stencil_back.stencil_fail = (enum lima_stencil_op) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 195, 197);
   values->stencil_back.depth_fail = (enum lima_stencil_op) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 198, 200);
   values->stencil_back.depth_pass = (enum lima_stencil_op) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 201, 203);
   values->stencil_back.unknown = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 204, 207);
   values->stencil_back.reference_value = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 208, 215);
   values->stencil_back.mask = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 216, 223);
   values->stencil_write_mask_front = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 224, 231);
   values->stencil_write_mask_back = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 232, 239);
   values->alpha_reference_value = __gen_unpack_unorm8((CONSTANT_ uint32_t *) cl, 240, 247);
   values->unknown_2 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 248, 255);
   values->alpha_test_func = (enum lima_compare_func) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 256, 258);
   values->msaa = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 259, 262);
   values->alpha_to_coverage = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 263, 263);
   values->alpha_to_one = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 264, 264);
   values->unknown_3 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 265, 265);
   values->draw_mode = (enum lima_draw_mode) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 266, 267);
   values->sample_mask = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 268, 271);
   values->frag_color_register_0 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 272, 275);
   values->frag_color_register_1 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 276, 279);
   values->frag_color_register_2 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 280, 283);
   values->frag_color_register_3 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 284, 287);
   values->fs_first_instr_length = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 288, 292);
   values->shader_address = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 293, 319) << 5;
   values->varying_type_0 = (enum lima_varying_type) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 320, 322);
   values->varying_type_1 = (enum lima_varying_type) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 323, 325);
   values->varying_type_2 = (enum lima_varying_type) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 326, 328);
   values->varying_type_3 = (enum lima_varying_type) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 329, 331);
   values->varying_type_4 = (enum lima_varying_type) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 332, 334);
   values->varying_type_5 = (enum lima_varying_type) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 335, 337);
   values->varying_type_6 = (enum lima_varying_type) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 338, 340);
   values->varying_type_7 = (enum lima_varying_type) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 341, 343);
   values->varying_type_8 = (enum lima_varying_type) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 344, 346);
   values->varying_type_9 = (enum lima_varying_type) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 347, 349);
   values->varying_type_10_low = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 350, 351);
   values->uniform_count = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 352, 355);
   values->uniforms_address = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 356, 383) << 4;
   values->unknown_4 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 384, 387);
   values->textures_address = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 388, 415) << 4;
   values->varying_stride = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 416, 420) << 3;
   values->has_samplers = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 421, 421);
   values->unknown_5 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 422, 422);
   values->has_uniforms = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 423, 423);
   values->unknown_6 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 424, 424);
   values->early_z = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 425, 425);
   values->unknown_7 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 426, 427);
   values->pixel_kill = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 428, 428);
   values->unknown_8 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 429, 429);
   values->sampler_count = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 430, 443);
   values->frag_color_1_register = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 444, 447);
   values->unknown_9 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 448, 459);
   values->front_face_ccw = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 460, 460);
   values->dithering = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 461, 461);
   values->unknown_10 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 462, 463);
   values->uniform_bit = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 464, 464);
   values->unknown_11 = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 465, 479);
   values->varying_type_10_high = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 480, 480);
   values->varying_type_11 = (enum lima_varying_type) __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 481, 483);
   values->varyings_address = __gen_unpack_uint((CONSTANT_ uint32_t *) cl, 484, 511) << 4;
   bool valid = true;
   valid &= lima_genxml_validate_mask(fp, "Render State", cl, 0, 0xff00ff00);
   valid &= lima_genxml_validate_mask(fp, "Render State", cl, 1, 0xff00ff00);
   return valid;
}

static inline void
LIMA_RENDER_STATE_print(FILE *fp, const struct LIMA_RENDER_STATE * values, unsigned indent)
{
   fprintf(fp, "%*sBlend Color B: %f\n", indent, "", values->blend_color_b);
   fprintf(fp, "%*sBlend Color G: %f\n", indent, "", values->blend_color_g);
   fprintf(fp, "%*sBlend Color R: %f\n", indent, "", values->blend_color_r);
   fprintf(fp, "%*sBlend Color A: %f\n", indent, "", values->blend_color_a);
   if (lima_blend_func_as_str(values->blend_func_rgb))
     fprintf(fp, "%*sBlend Func RGB: %s\n", indent, "", lima_blend_func_as_str(values->blend_func_rgb));
   else
     fprintf(fp, "%*sBlend Func RGB: unknown %X (XXX)\n", indent, "", values->blend_func_rgb);
   if (lima_blend_func_as_str(values->blend_func_a))
     fprintf(fp, "%*sBlend Func A: %s\n", indent, "", lima_blend_func_as_str(values->blend_func_a));
   else
     fprintf(fp, "%*sBlend Func A: unknown %X (XXX)\n", indent, "", values->blend_func_a);
   if (lima_logic_op_as_str(values->logicop_rgb))
     fprintf(fp, "%*sLogicop RGB: %s\n", indent, "", lima_logic_op_as_str(values->logicop_rgb));
   else
     fprintf(fp, "%*sLogicop RGB: unknown %X (XXX)\n", indent, "", values->logicop_rgb);
   if (lima_blend_factor_color_as_str(values->blend_factor_src_rgb))
     fprintf(fp, "%*sBlend Factor SRC RGB: %s\n", indent, "", lima_blend_factor_color_as_str(values->blend_factor_src_rgb));
   else
     fprintf(fp, "%*sBlend Factor SRC RGB: unknown %X (XXX)\n", indent, "", values->blend_factor_src_rgb);
   if (lima_blend_factor_color_as_str(values->blend_factor_dst_rgb))
     fprintf(fp, "%*sBlend Factor DST RGB: %s\n", indent, "", lima_blend_factor_color_as_str(values->blend_factor_dst_rgb));
   else
     fprintf(fp, "%*sBlend Factor DST RGB: unknown %X (XXX)\n", indent, "", values->blend_factor_dst_rgb);
   if (lima_logic_op_as_str(values->logicop_a))
     fprintf(fp, "%*sLogicop A: %s\n", indent, "", lima_logic_op_as_str(values->logicop_a));
   else
     fprintf(fp, "%*sLogicop A: unknown %X (XXX)\n", indent, "", values->logicop_a);
   if (lima_blend_factor_alpha_as_str(values->blend_factor_src_a))
     fprintf(fp, "%*sBlend Factor SRC A: %s\n", indent, "", lima_blend_factor_alpha_as_str(values->blend_factor_src_a));
   else
     fprintf(fp, "%*sBlend Factor SRC A: unknown %X (XXX)\n", indent, "", values->blend_factor_src_a);
   if (lima_blend_factor_alpha_as_str(values->blend_factor_dst_a))
     fprintf(fp, "%*sBlend Factor DST A: %s\n", indent, "", lima_blend_factor_alpha_as_str(values->blend_factor_dst_a));
   else
     fprintf(fp, "%*sBlend Factor DST A: unknown %X (XXX)\n", indent, "", values->blend_factor_dst_a);
   fprintf(fp, "%*sUnknown 0: 0x%" PRIx32 "\n", indent, "", values->unknown_0);
   fprintf(fp, "%*sColor Mask: 0x%" PRIx32 "\n", indent, "", values->color_mask);
   fprintf(fp, "%*sDepth Test: %s\n", indent, "", values->depth_test ? "true" : "false");
   if (lima_compare_func_as_str(values->depth_compare_func))
     fprintf(fp, "%*sDepth Compare Func: %s\n", indent, "", lima_compare_func_as_str(values->depth_compare_func));
   else
     fprintf(fp, "%*sDepth Compare Func: unknown %X (XXX)\n", indent, "", values->depth_compare_func);
   fprintf(fp, "%*sDepth Test Near Clip Disable: %s\n", indent, "", values->depth_test_near_clip_disable ? "true" : "false");
   fprintf(fp, "%*sDepth Test Far Clip Disable: %s\n", indent, "", values->depth_test_far_clip_disable ? "true" : "false");
   fprintf(fp, "%*sFrag Depth Register: 0x%" PRIx32 "\n", indent, "", values->frag_depth_register);
   fprintf(fp, "%*sShader Writes Depth Stencil: %s\n", indent, "", values->shader_writes_depth_stencil ? "true" : "false");
   fprintf(fp, "%*sShader Writes Depth: %s\n", indent, "", values->shader_writes_depth ? "true" : "false");
   fprintf(fp, "%*sShader Writes Stencil: %s\n", indent, "", values->shader_writes_stencil ? "true" : "false");
   fprintf(fp, "%*sUnknown 1: 0x%" PRIx32 "\n", indent, "", values->unknown_1);
   fprintf(fp, "%*sDepth Offset Scale: 0x%" PRIx32 "\n", indent, "", values->depth_offset_scale);
   fprintf(fp, "%*sDepth Offset Units: 0x%" PRIx32 "\n", indent, "", values->depth_offset_units);
   fprintf(fp, "%*sViewport Near: %f\n", indent, "", values->viewport_near);
   fprintf(fp, "%*sViewport Far: %f\n", indent, "", values->viewport_far);
   fprintf(fp, "%*sStencil Front:\n", indent, "");
   LIMA_STENCIL_print(fp, &values->stencil_front, indent + 2);
   fprintf(fp, "%*sStencil Back:\n", indent, "");
   LIMA_STENCIL_print(fp, &values->stencil_back, indent + 2);
   fprintf(fp, "%*sStencil Write Mask Front: 0x%" PRIx32 "\n", indent, "", values->stencil_write_mask_front);
   fprintf(fp, "%*sStencil Write Mask Back: 0x%" PRIx32 "\n", indent, "", values->stencil_write_mask_back);
   fprintf(fp, "%*sAlpha Reference Value: %f\n", indent, "", values->alpha_reference_value);
   fprintf(fp, "%*sUnknown 2: 0x%" PRIx32 "\n", indent, "", values->unknown_2);
   if (lima_compare_func_as_str(values->alpha_test_func))
     fprintf(fp, "%*sAlpha Test Func: %s\n", indent, "", lima_compare_func_as_str(values->alpha_test_func));
   else
     fprintf(fp, "%*sAlpha Test Func: unknown %X (XXX)\n", indent, "", values->alpha_test_func);
   fprintf(fp, "%*sMSAA: 0x%" PRIx32 "\n", indent, "", values->msaa);
   fprintf(fp, "%*sAlpha To Coverage: %s\n", indent, "", values->alpha_to_coverage ? "true" : "false");
   fprintf(fp, "%*sAlpha To One: %s\n", indent, "", values->alpha_to_one ? "true" : "false");
   fprintf(fp, "%*sUnknown 3: %s\n", indent, "", values->unknown_3 ? "true" : "false");
   if (lima_draw_mode_as_str(values->draw_mode))
     fprintf(fp, "%*sDraw mode: %s\n", indent, "", lima_draw_mode_as_str(values->draw_mode));
   else
     fprintf(fp, "%*sDraw mode: unknown %X (XXX)\n", indent, "", values->draw_mode);
   fprintf(fp, "%*sSample Mask: 0x%" PRIx32 "\n", indent, "", values->sample_mask);
   fprintf(fp, "%*sFrag Color Register 0: 0x%" PRIx32 "\n", indent, "", values->frag_color_register_0);
   fprintf(fp, "%*sFrag Color Register 1: 0x%" PRIx32 "\n", indent, "", values->frag_color_register_1);
   fprintf(fp, "%*sFrag Color Register 2: 0x%" PRIx32 "\n", indent, "", values->frag_color_register_2);
   fprintf(fp, "%*sFrag Color Register 3: 0x%" PRIx32 "\n", indent, "", values->frag_color_register_3);
   fprintf(fp, "%*sFS First Instr Length: %u\n", indent, "", values->fs_first_instr_length);
   fprintf(fp, "%*sShader Address: 0x%" PRIx32 "\n", indent, "", values->shader_address);
   if (lima_varying_type_as_str(values->varying_type_0))
     fprintf(fp, "%*sVarying Type 0: %s\n", indent, "", lima_varying_type_as_str(values->varying_type_0));
   else
     fprintf(fp, "%*sVarying Type 0: unknown %X (XXX)\n", indent, "", values->varying_type_0);
   if (lima_varying_type_as_str(values->varying_type_1))
     fprintf(fp, "%*sVarying Type 1: %s\n", indent, "", lima_varying_type_as_str(values->varying_type_1));
   else
     fprintf(fp, "%*sVarying Type 1: unknown %X (XXX)\n", indent, "", values->varying_type_1);
   if (lima_varying_type_as_str(values->varying_type_2))
     fprintf(fp, "%*sVarying Type 2: %s\n", indent, "", lima_varying_type_as_str(values->varying_type_2));
   else
     fprintf(fp, "%*sVarying Type 2: unknown %X (XXX)\n", indent, "", values->varying_type_2);
   if (lima_varying_type_as_str(values->varying_type_3))
     fprintf(fp, "%*sVarying Type 3: %s\n", indent, "", lima_varying_type_as_str(values->varying_type_3));
   else
     fprintf(fp, "%*sVarying Type 3: unknown %X (XXX)\n", indent, "", values->varying_type_3);
   if (lima_varying_type_as_str(values->varying_type_4))
     fprintf(fp, "%*sVarying Type 4: %s\n", indent, "", lima_varying_type_as_str(values->varying_type_4));
   else
     fprintf(fp, "%*sVarying Type 4: unknown %X (XXX)\n", indent, "", values->varying_type_4);
   if (lima_varying_type_as_str(values->varying_type_5))
     fprintf(fp, "%*sVarying Type 5: %s\n", indent, "", lima_varying_type_as_str(values->varying_type_5));
   else
     fprintf(fp, "%*sVarying Type 5: unknown %X (XXX)\n", indent, "", values->varying_type_5);
   if (lima_varying_type_as_str(values->varying_type_6))
     fprintf(fp, "%*sVarying Type 6: %s\n", indent, "", lima_varying_type_as_str(values->varying_type_6));
   else
     fprintf(fp, "%*sVarying Type 6: unknown %X (XXX)\n", indent, "", values->varying_type_6);
   if (lima_varying_type_as_str(values->varying_type_7))
     fprintf(fp, "%*sVarying Type 7: %s\n", indent, "", lima_varying_type_as_str(values->varying_type_7));
   else
     fprintf(fp, "%*sVarying Type 7: unknown %X (XXX)\n", indent, "", values->varying_type_7);
   if (lima_varying_type_as_str(values->varying_type_8))
     fprintf(fp, "%*sVarying Type 8: %s\n", indent, "", lima_varying_type_as_str(values->varying_type_8));
   else
     fprintf(fp, "%*sVarying Type 8: unknown %X (XXX)\n", indent, "", values->varying_type_8);
   if (lima_varying_type_as_str(values->varying_type_9))
     fprintf(fp, "%*sVarying Type 9: %s\n", indent, "", lima_varying_type_as_str(values->varying_type_9));
   else
     fprintf(fp, "%*sVarying Type 9: unknown %X (XXX)\n", indent, "", values->varying_type_9);
   fprintf(fp, "%*sVarying Type 10 Low: %u\n", indent, "", values->varying_type_10_low);
   fprintf(fp, "%*sUniform Count: 0x%" PRIx32 "\n", indent, "", values->uniform_count);
   fprintf(fp, "%*sUniforms Address: 0x%" PRIx32 "\n", indent, "", values->uniforms_address);
   fprintf(fp, "%*sUnknown 4: 0x%" PRIx32 "\n", indent, "", values->unknown_4);
   fprintf(fp, "%*sTextures Address: 0x%" PRIx32 "\n", indent, "", values->textures_address);
   fprintf(fp, "%*sVarying Stride: %u\n", indent, "", values->varying_stride);
   fprintf(fp, "%*sHas Samplers: %s\n", indent, "", values->has_samplers ? "true" : "false");
   fprintf(fp, "%*sUnknown 5: %s\n", indent, "", values->unknown_5 ? "true" : "false");
   fprintf(fp, "%*sHas Uniforms: %s\n", indent, "", values->has_uniforms ? "true" : "false");
   fprintf(fp, "%*sUnknown 6: %s\n", indent, "", values->unknown_6 ? "true" : "false");
   fprintf(fp, "%*sEarly Z: %s\n", indent, "", values->early_z ? "true" : "false");
   fprintf(fp, "%*sUnknown 7: 0x%" PRIx32 "\n", indent, "", values->unknown_7);
   fprintf(fp, "%*sPixel Kill: %s\n", indent, "", values->pixel_kill ? "true" : "false");
   fprintf(fp, "%*sUnknown 8: %s\n", indent, "", values->unknown_8 ? "true" : "false");
   fprintf(fp, "%*sSampler Count: %u\n", indent, "", values->sampler_count);
   fprintf(fp, "%*sFrag Color 1 Register: 0x%" PRIx32 "\n", indent, "", values->frag_color_1_register);
   fprintf(fp, "%*sUnknown 9: 0x%" PRIx32 "\n", indent, "", values->unknown_9);
   fprintf(fp, "%*sFront face CCW: %s\n", indent, "", values->front_face_ccw ? "true" : "false");
   fprintf(fp, "%*sDithering: %s\n", indent, "", values->dithering ? "true" : "false");
   fprintf(fp, "%*sUnknown 10: 0x%" PRIx32 "\n", indent, "", values->unknown_10);
   fprintf(fp, "%*sUniform Bit: %s\n", indent, "", values->uniform_bit ? "true" : "false");
   fprintf(fp, "%*sUnknown 11: 0x%" PRIx32 "\n", indent, "", values->unknown_11);
   fprintf(fp, "%*sVarying Type 10 High: %u\n", indent, "", values->varying_type_10_high);
   if (lima_varying_type_as_str(values->varying_type_11))
     fprintf(fp, "%*sVarying Type 11: %s\n", indent, "", lima_varying_type_as_str(values->varying_type_11));
   else
     fprintf(fp, "%*sVarying Type 11: unknown %X (XXX)\n", indent, "", values->varying_type_11);
   fprintf(fp, "%*sVaryings Address: 0x%" PRIx32 "\n", indent, "", values->varyings_address);
}

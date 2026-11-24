/*
 * Host check: mali_pp_clear_frame/mali_pp_stream against Mesa's generated packer.
 * gcc -std=gnu11 -DHAVE_PTHREAD -DHAVE_TIMESPEC_GET -DHAVE_STRUCT_TIMESPEC -DUTIL_ARCH_LITTLE_ENDIAN=1
 *   -DUTIL_ARCH_BIG_ENDIAN=0 -D_GNU_SOURCE -I. -I../mesa-lima/src -I../mesa-lima/include
 *   test_frame.c mali400.c -lm && ./a.out
 */
#include <assert.h>
#include <stdio.h>
#include "lima_pack.h"
#include "mali400.h"
#include "tri_job.h"

static int mapped(uint32_t va)
{
	unsigned i;

	for (i = 0; i < tri_job.nranges; i++)
		if (va - tri_job.ranges[i].va < tri_job.ranges[i].size)
			return 1;
	return 0;
}

/* tri_job fits mali_run_job: blobs inside ranges, pool and page tables big enough */
static void check_job(void)
{
	uint32_t pool = 0, dte[1024] = { 0 };
	unsigned i, k, pts = 0;

	for (i = 0; i < tri_job.nranges; i++) {
		const struct mali_job_range *r = &tri_job.ranges[i];

		assert(!(r->va & 0xfff) && !(r->size & 0xfff));
		pool += r->size;
		for (k = 0; k < r->size; k += 0x1000)
			dte[(r->va + k) >> 22] = 1;
	}
	dte[0x5f000000 >> 22] = dte[0x60000000 >> 22] = 1;	/* work area, fb */
	for (i = 0; i < 1024; i++)
		pts += dte[i];
	assert(pool <= MALI_MEM_SIZE - 0x20000 && pts <= 8);
	for (i = 0; i < tri_job.nblobs; i++)
		assert(mapped(tri_job.blobs[i].va) && mapped(tri_job.blobs[i].va + 4 * tri_job.blobs[i].n - 1));
	for (i = 0; i < 6; i++)
		assert(mapped(tri_job.gp[i] - (i & 1)));	/* end addresses are exclusive */
	assert(mapped(tri_job.pp[1]) && tri_job.num_pp == 4);
	for (i = 0; i < 4; i++)
		assert(mapped(tri_job.plbu_array[i]));
	printf("job: %u ranges, %#x bytes, %u page tables\n", tri_job.nranges, pool, pts);
}

int main(void)
{
	const struct mali_fb fb = { 0x60000000, 720, 1280, 2880 };
	uint32_t mine[MALI_PP_FRAME_REGS], wb[3 * MALI_PP_WB_REGS], ref[MALI_PP_FRAME_REGS] = { 0 };
	struct LIMA_CLEAR_VALUE_8BPC_COLOR c = { .red = 0x11 / 255.0, .green = 0x22 / 255.0,
						 .blue = 0x33 / 255.0, .alpha = 1.0 };
	static uint32_t s[2 * 45 * 80 + 2];
	unsigned i;

	mali_pp_clear_frame(mine, wb, &fb, 0x4000a000, 0x40009000, 0xff332211);
	lima_pack(ref, PP_FRAME, f) {
		f.plbu_array_address = 0x4000a000;
		f.render_address = 0x40009000;
		f.early_z = true;
		f.clear_value_depth = 0xffffff;
		f.clear_value_8bpc_color_0 = f.clear_value_8bpc_color_1 = c;
		f.clear_value_8bpc_color_2 = f.clear_value_8bpc_color_3 = c;
		f.origin_x = 1;
		f.bounding_box_right = fb.width;
		f.bounding_box_bottom = fb.height;
		f.origin_y = fb.height * 2;
		f.scale_fragcoord = f.scale_derivatives = true;
		f.flip_dithering_matrix = f.flip_fragcoord = f.flip_derivatives = true;
		f.subpixel_specifier = 0x77;
		f.tiebreak_mode = 1;
		f.tilebuffer_channel_layout.red = f.tilebuffer_channel_layout.green = 8;
		f.tilebuffer_channel_layout.blue = f.tilebuffer_channel_layout.alpha = 8;
	}
	for (i = 0; i < MALI_PP_FRAME_REGS; i++)
		if (mine[i] != ref[i])
			printf("frame[%u] mine %08x mesa %08x\n", i, mine[i], ref[i]);
	for (i = 0; i < MALI_PP_FRAME_REGS; i++)
		assert(mine[i] == ref[i]);
	assert(wb[0] == 2 && wb[1] == 0x60000000 && wb[2] == 3 && wb[5] == 360);

	assert(mali_pp_stream(s, 720, 1280) == sizeof(s));
	assert(s[1] == 0xb8000000 && s[3] == 0xb8000001 && s[2 * 45 + 1] == 0xb8000100);
	assert(s[2 * 45 * 80 - 1] == (0xb8000000 | 44 | 79 << 8) && s[2 * 45 * 80 + 1] == 0xbc000000);
	check_job();
	puts("ok");
	return 0;
}

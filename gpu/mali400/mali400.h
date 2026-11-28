/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Freestanding Mali-400 MP4 (Exynos 4412) hardware layer.
 * Register map and init sequences follow Linux lima (drivers/gpu/drm/lima),
 * PP frame layout follows Mesa lima (genxml/common.xml, lima_job.c).
 * No OS dependency: the host supplies MMIO bases, a work area and a delay.
 */
#ifndef MALI400_H
#define MALI400_H

#include <stdint.h>

#define MALI_PP_FRAME_REGS	23
#define MALI_PP_WB_REGS		12

struct mali_env {
	uintptr_t gpu;		/* 0x13000000 mapped */
	uintptr_t cmu;		/* 0x10030000 mapped */
	uintptr_t pmu;		/* 0x10020000 mapped (Exynos PMU) */
	uint8_t *mem;		/* work area, >= MALI_MEM_SIZE, 4 KiB aligned */
	uint32_t mem_pa;	/* its physical address */
	void (*udelay)(unsigned us);
	void (*flush)(void *p, uint32_t len);	/* make CPU writes visible to the GPU */
};

/* DTE + 144 page tables + clear stream + RSW/shader, then the job pool */
#define MALI_W_PT_NUM		144
#define MALI_W_POOL		0xa0000
#define MALI_MEM_SIZE		0x180000

struct mali_fb {
	uint32_t pa;		/* identity-mapped for the GPU */
	uint32_t width, height, stride;	/* stride in bytes, 32 bpp */
};

enum {
	MALI_OK = 0,
	MALI_ERR_POWER = -1,
	MALI_ERR_ID = -2,
	MALI_ERR_L2 = -3,
	MALI_ERR_MMU = -4,
	MALI_ERR_RESET = -5,
	MALI_ERR_MAP = -6,
	MALI_ERR_TIMEOUT = -7,
	MALI_ERR_JOB = -8,	/* PP raised an error irq; see mali_last_status */
};

/* a recorded GP+PP job (gen_job.py): GPU VA ranges, their initial contents, frames */
struct mali_job_range { uint32_t va, size; };
struct mali_job_blob { uint32_t va, n; const uint32_t *w; };
struct mali_job {
	const struct mali_job_range *ranges;
	const struct mali_job_blob *blobs;
	unsigned nranges, nblobs, num_pp;
	uint32_t gp[6];
	uint32_t pp[MALI_PP_FRAME_REGS];
	uint32_t wb[3 * MALI_PP_WB_REGS];	/* wb[0] is the colour buffer, retargeted to fb */
	uint32_t plbu_array[4];
};

extern uint32_t mali_last_status;	/* INT_RAWSTAT or failing register value */

int mali_init(const struct mali_env *env);
int mali_pp_clear(const struct mali_env *env, const struct mali_fb *fb, uint32_t rgba);
int mali_run_job(const struct mali_env *env, const struct mali_job *job, const struct mali_fb *fb);

/* pure helpers, also used by the host test */
void mali_pp_clear_frame(uint32_t frame[MALI_PP_FRAME_REGS], uint32_t wb[3 * MALI_PP_WB_REGS],
			 const struct mali_fb *fb, uint32_t stream_pa, uint32_t rsw_pa, uint32_t rgba);
uint32_t mali_pp_stream(uint32_t *s, uint32_t width, uint32_t height);
void mali_pt_reset(const struct mali_env *env);
int mali_map_va(const struct mali_env *env, uint32_t va, uint32_t pa, uint32_t len);
void mali_unmap_va(const struct mali_env *env, uint32_t va, uint32_t len);

#endif

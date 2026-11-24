// SPDX-License-Identifier: GPL-2.0
/*
 * Mali-400 MP4 bring-up for Exynos 4412: power/clock, Mali PMU, L2, MMU with an
 * identity page table, and a PP-only clear job that writes a solid colour to a
 * linear 32 bpp framebuffer. Sequences are lima's (Linux), frame layout Mesa's.
 */
#include "mali400.h"

/* Exynos 4412 */
#define EXY_PMU_G3D_CONF	0x3c60
#define EXY_PMU_G3D_STAT	0x3c64
#define CMU_SRC_G3D		0xc22c
#define CMU_DIV_G3D		0xc52c
#define CMU_DIV_STAT_G3D	0xc62c
#define CMU_GATE_IP_G3D		0xc92c
#define CMU_GATE_BLOCK		0xc970

/* Mali-400 blocks (lima_device.c, mali400 column) */
#define M_L2			0x1000
#define M_PMU			0x2000
#define M_MMU(i)		(0x3000 + 0x1000 * (i))	/* 0 = GP, 1..4 = PP0..3 */
#define M_PP(i)			(0x8000 + 0x2000 * (i))

/* lima_regs.h */
#define PMU_POWER_UP		0x00
#define PMU_STATUS		0x08
#define PMU_INT_MASK		0x0c
#define PMU_INT_RAWSTAT		0x10
#define PMU_INT_CLEAR		0x18
#define PMU_SW_DELAY		0x1c

#define L2_SIZE			0x04
#define L2_STATUS		0x08
#define L2_COMMAND		0x10
#define L2_MAX_READS		0x18
#define L2_ENABLE		0x1c

#define MMU_DTE_ADDR		0x00
#define MMU_STATUS		0x04
#define MMU_COMMAND		0x08
#define MMU_INT_MASK		0x1c
#define  MMU_CMD_ENABLE_PAGING	0x00
#define  MMU_CMD_ZAP_CACHE	0x04
#define  MMU_CMD_HARD_RESET	0x06
#define  MMU_STATUS_PAGING	(1u << 0)

#define PP_FRAME		0x0000
#define PP_WB(i)		(0x0100 * ((i) + 1))
#define PP_VERSION		0x1000
#define PP_STATUS		0x1008
#define PP_CTRL			0x100c
#define PP_INT_RAWSTAT		0x1020
#define PP_INT_CLEAR		0x1024
#define PP_INT_MASK		0x1028
#define PP_PERF_CNT_0_LIMIT	0x1088
#define  PP_CTRL_STOP_BUS	(1u << 0)
#define  PP_CTRL_FORCE_RESET	(1u << 5)
#define  PP_CTRL_START		(1u << 6)
#define  PP_IRQ_END_OF_FRAME	(1u << 0)
#define  PP_IRQ_ERROR		0x0f18	/* FORCE_HANG BUS_ERROR WRITE_BOUNDARY INVALID_PLIST STACK_UNDER/OVER */
#define  PP_IRQ_ALL		0x1fff

#define GP_FRAME		0x00	/* VSCL start/end, PLBUCL start/end, PLBU alloc start/end */
#define GP_CMD			0x20
#define GP_INT_RAWSTAT		0x24
#define GP_INT_CLEAR		0x28
#define GP_INT_MASK		0x2c
#define  GP_CMD_START_VS	(1u << 0)
#define  GP_CMD_START_PLBU	(1u << 1)
#define  GP_CMD_UPDATE_ALLOC	(1u << 4)
#define  GP_CMD_SOFT_RESET	(1u << 10)
#define  GP_IRQ_VS_END		(1u << 0)
#define  GP_IRQ_PLBU_END	(1u << 1)
#define  GP_IRQ_RESET_DONE	(1u << 19)
#define  GP_IRQ_ERROR		0x706e44	/* OOM FORCE_HANG WRITE_BOUND SYNC AXI INVALID_CMD x2 SEM x2 PTR_OOB */
#define  GP_IRQ_ALL		0x7fffff

#define VM_PRESENT		0x1
#define VM_UNCACHED		0x7	/* LIMA_VM_FLAGS_UNCACHE */
#define VM_CACHED		0x18f	/* read-cacheable only (no WRITE_CACHEABLE/WRITE_BUFFERABLE tile glitches) */

/* work area layout */
#define W_DTE			0x0000
#define W_PT			0x1000
#define W_PT_NUM		128	/* 128 x 4 MiB = 512 MiB GPU VA space (0x1000..0x81000) */
#define W_RSW			0x81000
#define W_CLEAR_PROG		0x81040
#define W_STREAM		0x82000
#define W_POOL			0x90000	/* job memory */

uint32_t mali_last_status;
uint32_t mali_vpll_con0;

static uint32_t rd(uintptr_t a) { return *(volatile uint32_t *)a; }
static void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

/* poll until (rd(a) & mask) == want, at most us microseconds */
static int poll(const struct mali_env *e, uintptr_t a, uint32_t mask, uint32_t want, unsigned us)
{
	for (;;) {
		uint32_t v = rd(a);

		if ((v & mask) == want)
			return 0;
		if (!us--) {
			mali_last_status = v;
			return -1;
		}
		e->udelay(1);
	}
}

static void exynos_g3d_on(const struct mali_env *e)
{
	wr(e->cmu + CMU_GATE_BLOCK, rd(e->cmu + CMU_GATE_BLOCK) | (1u << 3));
	wr(e->cmu + CMU_GATE_IP_G3D, rd(e->cmu + CMU_GATE_IP_G3D) | 1);
	/* Configure VPLL = 440 MHz stock GT-I9300 max (24 * 110 / 3 / 2, M=0x6e, P=3, S=1; BUCK4=1.075V),
	 * with fallback to 400 MHz (M=0x64, P=3, S=1). Dynamic DVFS scales CMU_DIV_G3D (/1, /2, /3). */
	wr(e->cmu + 0xc210, rd(e->cmu + 0xc210) & ~(1u << 8)); /* MUX_VPLL_SEL = FINPLL */
	e->udelay(20);
	wr(e->cmu + 0xc020, 270u * 3u);                        /* VPLL_LOCK */
	wr(e->cmu + 0xc124, 0u);                               /* VPLL_CON1 (K=0) */
	wr(e->cmu + 0xc120, 0x806e0301u);                      /* VPLL_CON0 = 440 MHz (S=1) */
	if (poll(e, e->cmu + 0xc120, 1u << 29, 1u << 29, 10000)) {
		wr(e->cmu + 0xc120, 0x80640301u);                  /* fallback: 400 MHz */
		poll(e, e->cmu + 0xc120, 1u << 29, 1u << 29, 10000);
	}
	mali_vpll_con0 = rd(e->cmu + 0xc120);
	wr(e->cmu + 0xc210, rd(e->cmu + 0xc210) | (1u << 8));  /* MUX_VPLL_SEL = FOUTVPLL */
	e->udelay(20);
	wr(e->cmu + CMU_SRC_G3D, (rd(e->cmu + CMU_SRC_G3D) & ~0x111u) | (1u << 4) | (1u << 8));
	wr(e->cmu + CMU_DIV_G3D, rd(e->cmu + CMU_DIV_G3D) & ~0xfu); /* /1 = 440 MHz */
}

/* map GPU [va, va + len) to [pa, pa + len) */
static int map_va(const struct mali_env *e, uint32_t va, uint32_t pa, uint32_t len)
{
	uint32_t *dte = (uint32_t *)(e->mem + W_DTE);
	uint32_t a, end = va + (len - 1);	/* inclusive: ranges may end at 4 GiB */

	pa &= ~0xfffu;
	for (a = va & ~0xfffu; a <= end; a += 0x1000, pa += 0x1000) {
		uint32_t *pt;

		if (!(dte[a >> 22] & VM_PRESENT)) {
			unsigned i;

			for (i = 0; i < W_PT_NUM; i++) {
				uint32_t pt_pa = e->mem_pa + W_PT + 0x1000 * i;
				unsigned j, used = 0;

				for (j = 0; j < 1024; j++)
					used |= (dte[j] & ~0xfffu) == pt_pa;
				if (!used)
					break;
			}
			if (i == W_PT_NUM)
				return MALI_ERR_MAP;
			dte[a >> 22] = (e->mem_pa + W_PT + 0x1000 * i) | VM_PRESENT;
		}
		pt = (uint32_t *)(e->mem + (dte[a >> 22] & ~0xfffu) - e->mem_pa);
		pt[(a >> 12) & 0x3ff] = pa | VM_CACHED;
		if (a == 0xfffff000)
			break;
	}
	return MALI_OK;
}

static int map(const struct mali_env *e, uint32_t pa, uint32_t len)
{
	return map_va(e, pa, pa, len);
}

static int mmu_init(const struct mali_env *e, uintptr_t mmu)
{
	wr(mmu + MMU_COMMAND, MMU_CMD_HARD_RESET);
	if (poll(e, mmu + MMU_DTE_ADDR, ~0u, 0, 100))
		return MALI_ERR_MMU;
	wr(mmu + MMU_INT_MASK, 3);	/* PAGE_FAULT | READ_BUS_ERROR */
	wr(mmu + MMU_DTE_ADDR, e->mem_pa + W_DTE);
	wr(mmu + MMU_COMMAND, MMU_CMD_ENABLE_PAGING);
	if (poll(e, mmu + MMU_STATUS, MMU_STATUS_PAGING, MMU_STATUS_PAGING, 100))
		return MALI_ERR_MMU;
	return MALI_OK;
}

static int l2_flush(const struct mali_env *e)
{
	wr(e->gpu + M_L2 + L2_COMMAND, 1);	/* CLEAR_ALL */
	return poll(e, e->gpu + M_L2 + L2_STATUS, 1, 0, 1000) ? MALI_ERR_L2 : MALI_OK;
}

static int pp_hard_reset(const struct mali_env *e, uintptr_t pp)
{
	unsigned us;

	wr(pp + PP_PERF_CNT_0_LIMIT, 0xc0ffe000);
	wr(pp + PP_INT_MASK, 0);
	wr(pp + PP_CTRL, PP_CTRL_STOP_BUS);
	if (poll(e, pp + PP_STATUS, 1u << 4, 1u << 4, 1000))
		return MALI_ERR_RESET;
	wr(pp + PP_CTRL, PP_CTRL_FORCE_RESET);
	/* the counter register reads back only once the reset is over */
	for (us = 0;; us++) {
		wr(pp + PP_PERF_CNT_0_LIMIT, 0xc01a0000);
		if (rd(pp + PP_PERF_CNT_0_LIMIT) == 0xc01a0000)
			break;
		if (us == 1000)
			return MALI_ERR_RESET;
		e->udelay(1);
	}
	wr(pp + PP_PERF_CNT_0_LIMIT, 0);
	wr(pp + PP_INT_CLEAR, PP_IRQ_ALL);
	return MALI_OK;
}

int mali_init(const struct mali_env *e)
{
	uintptr_t g = e->gpu;
	uint32_t *w = (uint32_t *)e->mem;
	unsigned i;
	int err;

	wr(e->pmu + EXY_PMU_G3D_CONF, 7);
	if (poll(e, e->pmu + EXY_PMU_G3D_STAT, 7, 7, 10000))
		return MALI_ERR_POWER;
	exynos_g3d_on(e);
	poll(e, e->cmu + CMU_DIV_STAT_G3D, 1, 0, 1000);

	mali_last_status = rd(g + M_PP(0) + PP_VERSION);
	if (mali_last_status >> 16 != 0xcd07)	/* lima_pp_print_version: mali400 */
		return MALI_ERR_ID;

	wr(g + M_PMU + PMU_INT_MASK, 0);
	wr(g + M_PMU + PMU_SW_DELAY, 0xffff);
	if (rd(g + M_PMU + PMU_STATUS)) {	/* 1 = off */
		wr(g + M_PMU + PMU_POWER_UP, rd(g + M_PMU + PMU_STATUS));
		if (poll(e, g + M_PMU + PMU_INT_RAWSTAT, 1, 1, 100000))
			return MALI_ERR_POWER;
		wr(g + M_PMU + PMU_INT_CLEAR, 1);
	}

	if ((err = l2_flush(e)))
		return err;
	wr(g + M_L2 + L2_ENABLE, 3);		/* ACCESS | READ_ALLOCATE */
	wr(g + M_L2 + L2_MAX_READS, 0x1c);

	for (i = 0; i < W_STREAM / 4; i++)
		w[i] = 0;
	if ((err = map(e, e->mem_pa, MALI_MEM_SIZE)))
		return err;
	e->flush(e->mem, W_STREAM);
	for (i = 0; i < 5; i++)		/* GP MMU + 4 PP MMUs */
		if ((err = mmu_init(e, g + M_MMU(i))))
			return err;
	for (i = 0; i < 4; i++)
		if ((err = pp_hard_reset(e, g + M_PP(i))))
			return err;
	return MALI_OK;
}

uint32_t mali_pp_stream(uint32_t *s, uint32_t width, uint32_t height)
{
	uint32_t n = 0, x, y;

	/*
	 * shortcut: tiles carry no polygon-list call, so the PP only clears and writes
	 * back each 16x16 tile. Draws need GP/PLBU output and the 0xE0/0xB0 call per tile.
	 */
	for (y = 0; y < (height + 15) / 16; y++)
		for (x = 0; x < (width + 15) / 16; x++) {
			s[n++] = 0;
			s[n++] = 0xb8000000 | x | (y << 8);
		}
	s[n++] = 0;
	s[n++] = 0xbc000000;
	return n * 4;
}

void mali_pp_clear_frame(uint32_t f[MALI_PP_FRAME_REGS], uint32_t wb[3 * MALI_PP_WB_REGS],
			 const struct mali_fb *fb, uint32_t stream_pa, uint32_t rsw_pa, uint32_t rgba)
{
	unsigned i;

	for (i = 0; i < MALI_PP_FRAME_REGS; i++)
		f[i] = 0;
	for (i = 0; i < 3 * MALI_PP_WB_REGS; i++)
		wb[i] = 0;
	/* field values as in Mesa lima_pack_pp_frame_reg */
	f[0] = stream_pa;
	f[1] = rsw_pa;
	f[3] = 1u << 1;				/* early z */
	f[4] = 0xffffff;			/* depth clear */
	f[6] = f[7] = f[8] = f[9] = rgba;	/* R | G << 8 | B << 16 | A << 24 */
	f[10] = fb->width - 1;
	f[11] = fb->height - 1;
	f[16] = 1;				/* origin x */
	f[17] = fb->height * 2 - 1;		/* origin y */
	f[18] = 0x77;				/* subpixel specifier */
	f[19] = 1;				/* tiebreak mode */
	f[21] = (1u << 2) | (1u << 3) | (1u << 9) | (1u << 10) | (1u << 11);
	f[22] = 0x8888;				/* tilebuffer 8:8:8:8 */

	wb[0] = 2;				/* colour buffer */
	wb[1] = fb->pa;
	wb[2] = 0x03;				/* LIMA_PIXEL_FORMAT_B8G8R8A8 */
	wb[4] = 0;				/* linear */
	wb[5] = fb->stride / 8;
}

static void pp_start(uintptr_t pp, const uint32_t *f, const uint32_t *wb)
{
	unsigned i, j;

	for (i = 0; i < MALI_PP_FRAME_REGS; i++)
		wr(pp + PP_FRAME + 4 * i, f[i]);
	for (i = 0; i < 3; i++) {
		const uint32_t *w = &wb[i * MALI_PP_WB_REGS];
		wr(pp + PP_WB(i), w[0]);
		if (w[0]) {
			for (j = 1; j < MALI_PP_WB_REGS; j++)
				wr(pp + PP_WB(i) + 4 * j, w[j]);
		}
	}
	wr(pp + PP_INT_CLEAR, PP_IRQ_ALL);
	wr(pp + PP_CTRL, PP_CTRL_START);
}

static int pp_wait(const struct mali_env *e, uintptr_t pp)
{
	unsigned i;

	for (i = 0; i < 1000000; i++) {
		uint32_t s = rd(pp + PP_INT_RAWSTAT);

		mali_last_status = s;
		if (s & PP_IRQ_ERROR)
			return MALI_ERR_JOB;
		if (s & PP_IRQ_END_OF_FRAME) {
			wr(pp + PP_INT_CLEAR, PP_IRQ_ALL);
			return MALI_OK;
		}
		e->udelay(1);
	}
	pp_hard_reset(e, pp);
	return MALI_ERR_TIMEOUT;
}

int mali_pp_clear(const struct mali_env *e, const struct mali_fb *fb, uint32_t rgba)
{
	static const uint32_t clear_prog[] = {	/* Mesa PP_CLEAR_PROGRAM */
		0x00020425, 0x0000000c, 0x01e007cf, 0xb0000000,
		0x000005f5, 0x00000000, 0x00000000, 0x00000000,
	};
	uintptr_t pp = e->gpu + M_PP(0);
	uint32_t *rsw = (uint32_t *)(e->mem + W_RSW);
	uint32_t f[MALI_PP_FRAME_REGS], wb[3 * MALI_PP_WB_REGS], len;
	unsigned i;
	int err;

	if (fb->width > 4096 || fb->height > 4096 ||
	    ((fb->width + 15) / 16) * ((fb->height + 15) / 16) * 8 + 8 > W_POOL - W_STREAM)
		return MALI_ERR_MAP;
	if ((err = map(e, fb->pa, fb->stride * fb->height)))
		return err;

	for (i = 0; i < 16; i++)		/* Mesa pp_frame_rsw */
		rsw[i] = 0;
	rsw[8] = 0x0000f008;
	rsw[9] = e->mem_pa + W_CLEAR_PROG;
	rsw[13] = 0x00000100;
	for (i = 0; i < 8; i++)
		((uint32_t *)(e->mem + W_CLEAR_PROG))[i] = clear_prog[i];
	len = mali_pp_stream((uint32_t *)(e->mem + W_STREAM), fb->width, fb->height);
	e->flush(e->mem, W_STREAM + len);

	for (i = 1; i < 5; i++)
		wr(e->gpu + M_MMU(i) + MMU_COMMAND, MMU_CMD_ZAP_CACHE);
	if ((err = l2_flush(e)))
		return err;

	mali_pp_clear_frame(f, wb, fb, e->mem_pa + W_STREAM, e->mem_pa + W_RSW, rgba);
	pp_start(pp, f, wb);
	return pp_wait(e, pp);
}

int mali_run_job(const struct mali_env *e, const struct mali_job *job, const struct mali_fb *fb)
{
	uintptr_t gp = e->gpu;
	uint32_t pa = e->mem_pa + W_POOL, f[MALI_PP_FRAME_REGS], wb[3 * MALI_PP_WB_REGS];
	unsigned i, j;
	int err;

	if ((err = map(e, fb->pa, fb->stride * fb->height)))
		return err;
	for (i = 0; i < job->nranges; i++) {
		const struct mali_job_range *r = &job->ranges[i];
		uint32_t *p = (uint32_t *)(e->mem + (pa - e->mem_pa));

		if (pa - e->mem_pa + r->size > MALI_MEM_SIZE)
			return MALI_ERR_MAP;
		for (j = 0; j < r->size / 4; j++)
			p[j] = 0;
		for (j = 0; j < job->nblobs; j++) {
			const struct mali_job_blob *b = &job->blobs[j];
			unsigned k;

			if (b->va - r->va < r->size)
				for (k = 0; k < b->n; k++)
					p[(b->va - r->va) / 4 + k] = b->w[k];
		}
		if ((err = map_va(e, r->va, pa, r->size)))
			return err;
		pa += r->size;
	}
	e->flush(e->mem, pa - e->mem_pa);
	for (i = 0; i < 5; i++)
		wr(e->gpu + M_MMU(i) + MMU_COMMAND, MMU_CMD_ZAP_CACHE);
	if ((err = l2_flush(e)))
		return err;

	/* GP: vertex shader, then PLBU bins the triangles into the PLB (lima_gp_task_run) */
	wr(gp + GP_INT_MASK, 0);
	wr(gp + GP_INT_CLEAR, GP_IRQ_RESET_DONE);
	wr(gp + GP_CMD, GP_CMD_SOFT_RESET);
	if (poll(e, gp + GP_INT_RAWSTAT, GP_IRQ_RESET_DONE, GP_IRQ_RESET_DONE, 1000))
		return MALI_ERR_RESET;
	wr(gp + GP_INT_CLEAR, GP_IRQ_ALL);
	for (i = 0; i < 6; i++)
		wr(gp + GP_FRAME + 4 * i, job->gp[i]);
	wr(gp + GP_CMD, GP_CMD_UPDATE_ALLOC);
	wr(gp + GP_CMD, GP_CMD_START_VS | GP_CMD_START_PLBU);
	for (i = 0;; i++) {
		uint32_t s = rd(gp + GP_INT_RAWSTAT);

		mali_last_status = s;
		if (s & GP_IRQ_ERROR)
			return MALI_ERR_JOB;
		if ((s & (GP_IRQ_VS_END | GP_IRQ_PLBU_END)) == (GP_IRQ_VS_END | GP_IRQ_PLBU_END))
			break;
		if (i == 1000000)
			return MALI_ERR_TIMEOUT;
		e->udelay(1);
	}
	wr(gp + GP_INT_CLEAR, GP_IRQ_ALL);

	/* PP: one PLB stream per core, colour buffer retargeted to the linear fb */
	for (i = 0; i < MALI_PP_FRAME_REGS; i++)
		f[i] = job->pp[i];
	for (i = 0; i < 3 * MALI_PP_WB_REGS; i++)
		wb[i] = job->wb[i];
	wb[1] = fb->pa;
	wb[2] = 0x03;				/* B8G8R8A8 */
	wb[4] = 0;				/* linear, not tiled */
	wb[5] = fb->stride / 8;
	wb[6] = 0;				/* no R/B swap */
	for (i = 0; i < job->num_pp; i++) {
		f[0] = job->plbu_array[i];
		pp_start(e->gpu + M_PP(i), f, wb);
	}
	for (i = 0; i < job->num_pp; i++)
		if ((err = pp_wait(e, e->gpu + M_PP(i))))
			return err;
	return MALI_OK;
}

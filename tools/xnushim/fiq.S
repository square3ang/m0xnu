/*
 * tbd FIQ handler & Exynos 4412 SMP secondary boot entry.
 *
 * The kernel's FIQ vector does "mov pc, r9" in FIQ mode with banked
 * r8 = cpu_data, r10 = timer_base (MCT VA). r12 and sp are scratch.
 * On Exynos 4412 (unbanked GICv1), CPU n's GICC is at gicc + (n << 14),
 * its GICD banked/SGIR window is at gicd + (n << 14), and its dedicated
 * MCT local timer is Ln at MCT + 0x300 + (n << 8) (GIC PPI 12 = ID 28).
 */
	.syntax unified
	.arm
	.global exy_fiq
exy_fiq:
	mrc	p15, 0, r9, c0, c0, 5	@ MPIDR
	and	r9, r9, #3		@ r9 = cpu_id (0..3)
	ldr	sp, =gicc
	ldr	sp, [sp]
	add	sp, sp, r9, lsl #14	@ this CPU's GICC
	ldr	r12, [sp, #0x0c]	@ GICC_IAR
	ubfx	sp, r12, #0, #10
	cmp	sp, #28			@ Only clear L_INT_CSTAT if we actually acked PPI 12 (ID 28)
	bne	4f
	add	sp, r10, #0x300
	add	sp, sp, r9, lsl #8	@ MCT local timer Ln (0x300 + cpu_id*0x100)
	mov	r9, #1
	str	r9, [sp, #0x30]		@ L_INT_CSTAT: clear ICNT interrupt (level PPI 12)
	mrc	p15, 0, r9, c0, c0, 5
	and	r9, r9, #3
4:
	ldr	sp, =gicc
	ldr	sp, [sp]
	add	sp, sp, r9, lsl #14
	str	r12, [sp, #0x10]	@ GICC_EOIR (1023, spurious, is ignored)
	ubfx	sp, r12, #0, #10	@ Acknowledged an IPI (SGI) instead of the timer?
	cmp	sp, #16
	bhs	2f
	mov	r12, #1
	lsl	r12, r12, r9
	orr	sp, sp, r12, lsl #16	@ CPUTargetList = (1 << cpu_id)
	orr	sp, sp, #0x8000		@ NSATT = 1 (group 1 IRQ)
	ldr	r12, =gicd
	ldr	r12, [r12]
	add	r12, r12, r9, lsl #14
	dsb
	str	sp, [r12, #0xf00]	@ GICD_SGIR: re-raise SGI to this CPU
2:
	ldr	sp, =exy_fiq_sample	@ per-CPU FIQ counter at +16 + 4*cpu_id
	add	r12, sp, r9, lsl #2
	ldr	r12, [r12, #16]
	add	r12, r12, #1
	add	sp, sp, r9, lsl #2
	str	r12, [sp, #16]
	cmp	r9, #0
	bne	3f
	ldr	sp, =exy_fiq_sample
	str	lr, [sp]
	ldr	r12, [sp, #4]
	add	r12, r12, #1
	str	r12, [sp, #4]
	ldr	r12, [r10, #0x100]	@ MCT G_CNT_L
	str	r12, [sp, #8]
	mrs	r12, spsr
	str	r12, [sp, #12]
3:
	mrs	r12, spsr
	tst	r12, #0x80		@ Interrupted with IRQs masked? Defer tick via self-IPI
	beq	1f
	ldr	r12, [r8, #0x30]
	tst	r12, #0x80000000
	beq	1f
	ldr	r12, =gicd
	ldr	r12, [r12]
	add	r12, r12, r9, lsl #14
	mov	sp, #1
	lsl	sp, sp, r9
	lsl	sp, sp, #16		@ CPUTargetList = (1 << cpu_id)
	orr	sp, sp, #0x8000		@ NSATT = 1, SGI 0
	dsb
	str	sp, [r12, #0xf00]
1:
	mvn	sp, #0
	str	sp, [r8, #0x70]		@ cpu_data decrementer = expired
	ldr	r9, =exy_fiq
	ldr	pc, =0x800c6c4c		@ generic fleh_fiq

	.ltorg

/*
 * tbd get_timebase/get_decrementer(_, _, _, cpu_data). Leaves without a stack,
 * like the A5 originals: the kernel calls them where sp is not set up (fast
 * mach_absolute_time trap from user mode). r12 = timer_base (cpu_data+0x84) = MCT.
 */
	.global	exy_get_timebase
exy_get_timebase:
	ldr	r12, [r3, #0x84]
1:	ldr	r1, [r12, #0x104]	@ G_CNT_U, G_CNT_L, G_CNT_U again: no carry in between
	ldr	r0, [r12, #0x100]
	ldr	r2, [r12, #0x104]
	cmp	r1, r2
	bne	1b
	ldr	r2, [r3, #0x50]		@ + cpu_data timebase offset
	adds	r0, r0, r2
	ldr	r2, [r3, #0x54]
	adc	r1, r1, r2
	bx	lr

	.global	exy_get_decrementer
exy_get_decrementer:			@ ticks to exy_deadline[cpu], clamped to [0, 0x7fffffff]
	ldr	r12, [r3, #0x84]
1:	ldr	r1, [r12, #0x104]
	ldr	r0, [r12, #0x100]
	ldr	r2, [r12, #0x104]
	cmp	r1, r2
	bne	1b
	ldrh	r12, [r3, #0]		@ cpu_data->cpu_number (0..3)
	and	r12, r12, #3
	ldr	r2, =exy_deadline
	add	r12, r2, r12, lsl #3
	ldrd	r2, r3, [r12]		@ single uninterruptible 64-bit load (8-byte aligned)
	subs	r0, r2, r0
	sbcs	r1, r3, r1
	movlt	r0, #0			@ passed
	bxlt	lr
	cmp	r1, #0
	cmpeq	r0, #0x80000000
	mvnhs	r0, #0x80000000		@ far away
	bx	lr
	.ltorg

/*
 * Physical entry point for Exynos 4412 secondary cores (CPU1..CPU3), called by
 * iROM via SYSRAM 0x02020000 + 4*cpu with MMU & caches OFF in Secure SVC mode.
 * Initializes this CPU's unbanked GICD SGI0 + GICC interface, then enters XNU's
 * native start_cpu (0x400c7000) with r0 = boot_info_pa, r1 = cpu_data_pa.
 */
	.global	exy_smp_entry
exy_smp_entry:
	cpsid	aif, #0x13
	mrc	p15, 0, r2, c0, c0, 5	@ MPIDR
	and	r2, r2, #3		@ cpu_id (1..3)

	@ Per-CPU banked GICD registers (Exynos 4412: 0x10490000 + cpu_id*0x4000)
	ldr	r0, =0x10490000
	add	r0, r0, r2, lsl #14
	mvn	r1, #0
	str	r1, [r0, #0x180]	@ GICD_ICENABLER0
	str	r1, [r0, #0x280]	@ GICD_ICPENDR0
	ldr	r1, =0xefffffff		@ GICD_IGROUPR0: PPI 12 (ID 28) = group 0 (FIQ), others = group 1 (IRQ)
	str	r1, [r0, #0x080]
	ldr	r1, =0x80808080
	add	r3, r0, #0x400		@ GICD_IPRIORITYR0..7
	mov	r4, #8
1:	str	r1, [r3], #4
	subs	r4, r4, #1
	bne	1b
	mov	r1, #0
	strb	r1, [r0, #0x41c]	@ ID 28 (PPI 12, MCT local timer) = highest priority 0x00
	ldr	r1, =0x10000001		@ GICD_ISENABLER0: enable PPI 12 (bit 28) + SGI 0 (bit 0)
	str	r1, [r0, #0x100]

	@ Per-CPU GICC interface (Exynos 4412: 0x10480000 + cpu_id*0x4000)
	ldr	r0, =0x10480000
	add	r0, r0, r2, lsl #14
	mov	r1, #0xf0
	str	r1, [r0, #0x04]		@ GICC_PMR
	mov	r1, #0x0f
	str	r1, [r0, #0x00]		@ GICC_CTLR: EnableGrp0|EnableGrp1|AckCtl|FIQEn

	@ Record entry in physical DRAM diagnostic counter exy_smp_entered[cpu_id]
	ldr	r3, =exy_smp_entered
	sub	r3, r3, #0x40000000
	ldr	r4, [r3, r2, lsl #2]
	add	r4, r4, #1
	str	r4, [r3, r2, lsl #2]

	@ Load r0 = pa(exy_smp_boot_info), r1 = exy_smp_cpu_data_pa[cpu_id], jump to start_cpu (0x400c7010 -> arm_init_cpu)
	ldr	r0, =exy_smp_boot_info
	sub	r0, r0, #0x40000000
	ldr	r3, =exy_smp_cpu_data_pa
	sub	r3, r3, #0x40000000
	ldr	r1, [r3, r2, lsl #2]
	dsb
	isb
	ldr	pc, =0x400c7010
	.ltorg

	.data
	.balign	32			@ one cache line
	.global	exy_fiq_sample
exy_fiq_sample:
	.word	0, 0, 0, 0, 0, 0, 0, 0	@ lr, count0, G_CNT_L, spsr, fiq_cnt[0..3]
	.space	64			@ ring: last 16 interrupted pcs (+4)
	.space	68			@ +96: r7, 8 return addresses; +160: saved PAR

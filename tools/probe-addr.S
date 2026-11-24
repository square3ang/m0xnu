@ Probe: is the fastboot payload executed at the header kernel address?
@ Yes -> spin a few seconds, then reset. No -> reset at once.
@ Either way the installed U-Boot comes back in fastboot mode; the time until
@ USB re-enumerates tells which branch ran, without anyone touching the phone.
	.syntax unified
	.arm
	.global _start
_start:
	adr	r0, _start
	ldr	r1, =0x43e00000
	cmp	r0, r1
	bne	reset
	ldr	r0, =0x40000000		@ ~seconds with caches off
1:	subs	r0, r0, #1
	bne	1b
reset:
	ldr	r2, =0x10020400		@ Exynos4412 PMU SWRESET
	mov	r3, #1
	str	r3, [r2]
2:	b	2b
	.ltorg
	.space	4096			@ fastboot refuses tiny kernels

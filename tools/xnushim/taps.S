/*
 * Taps: kernel function entries patched to "ldr.w pc, [pc]" jump here; we look
 * at the arguments, replay the 8 displaced bytes and continue in the original.
 */
	.syntax unified
	.arm

	.global	exy_tap_conslog
exy_tap_conslog:			@ conslog_putc(char): every printf character
	push	{r0-r5, r12, lr}
	bl	exy_putc
	pop	{r0-r5, r12, lr}
	push	{r4, r5, r7, lr}	@ displaced: push {r4, r5, r7, lr}
	add	r7, sp, #8		@            add r7, sp, #0x8
	movw	r5, #0xde9e		@            movw r5, #0xde9e
	ldr	pc, =0x800287c5		@ continue (Thumb)
	.ltorg

	.global	exy_cons_putc
exy_cons_putc:				@ replaces pe_serial serial_putc(unit, line, c), cons_ops[0]:
	push	{r0-r3, r12, lr}	@ IOLog, panic and /dev/console output (uart never "initted")
	mov	r0, r2
	bl	exy_putc
	pop	{r0-r3, r12, lr}
	ldr	pc, =0x800ce3c9		@ and on to the video console putc, cons_ops[1] (Thumb):
	.ltorg				@ serial=3 keeps the RAM log and still draws on --fb

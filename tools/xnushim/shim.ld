/* Code and data go to different places (mkxnu.py passes the addresses):
 * code into padding inside the kernel's __text (stays executable), data after
 * the kernel image (the kernel later maps that memory execute-never), and the
 * bigger drivers (gpu.c, sections renamed to .drv.*) into DRV_BASE, the __text of
 * a prelinked kext that never loads. */
SECTIONS {
	.text TEXT_BASE : { *(EXCLUDE_FILE(*libgcc.a) .text*) *(EXCLUDE_FILE(*libgcc.a) .rodata*) }
	.drv DRV_BASE : { *(.drv.text*) *(.drv.rodata*) *libgcc.a:*(.text* .rodata*) }
	.data DATA_BASE : { *(.data*) *(.drv.data*) *(.bss*) *(.drv.bss*) *(COMMON) }
	/DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}

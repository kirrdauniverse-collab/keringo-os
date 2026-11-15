/* linker.ld — Keringő OS kernel elrendezés */
ENTRY(_start)
SECTIONS
{
    . = 1M;

    .multiboot : ALIGN(4) { KEEP(*(.multiboot)) }

    .text : ALIGN(4K) { *(.text*) }

    .rodata : ALIGN(4K) { *(.rodata*) }

    .data : ALIGN(4K) { *(.data*) }

    .bss : ALIGN(4K) {
        *(COMMON)
        *(.bss*)
        *(.bss.stack)
    }

    __kernel_end = ALIGN(4K);

    /DISCARD/ : { *(.eh_frame) *(.note*) *(.comment) *(.interp) }
}

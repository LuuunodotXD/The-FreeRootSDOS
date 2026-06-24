ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : {
        /* entry.o PRIMEIRO — garante _start exatamente em 0x10000 */
        entry.o(.text)
        *(.text .text.*)
    }

    .rodata : { *(.rodata .rodata.*) }

    .data : { *(.data .data.*) }

    /* Símbolos para zerar a BSS no entry.asm */
    _bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    _bss_end = .;
}

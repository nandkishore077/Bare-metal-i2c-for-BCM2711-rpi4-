/**
 * @file    bcm2711.ld
 * @brief   Linker script — simplified from proven polling project
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
    . = 0x80000;

    /* Boot code first */
    .text.boot : {
        KEEP(*(.text.boot))
    }

    /* Vector table — 2 KiB aligned */
    . = ALIGN(2048);
    .text.vectors : {
        KEEP(*(.text.vectors))
    }

    /* All other code */
    . = ALIGN(8);
    .text : {
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    /* BSS */
    . = ALIGN(16);
    __bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    . = ALIGN(16);
    __bss_end = .;

    /* Stack — 64 KiB above BSS, grows downward */
    . = ALIGN(16);
    . = . + 0x10000;
    _stack_top = .;
}

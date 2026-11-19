/* Pocket firmware: loaded (data slot 0) at the start of the SDRAM, run in
 * place.  The first 8 MiB are the firmware's: image, .bss (the DOS memory,
 * g_mem, is 1.2 MiB of it), heap and stack.  The game data pack is loaded
 * right after, at 0x10800000 (data slot 1). */
OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x10000000, LENGTH = 8M
}

SECTIONS
{
    .text : {
        KEEP(*(.text.start))
        *(.text.trap)
        /* the main menu, for the Esc filter in pocket.c */
        __menu_start = .;
        *(.text.FUN_1000_4e36)
        __menu_end = .;
        /* the controls menu, for its filter in pocket.c */
        __ctlmenu_start = .;
        *(.text.FUN_1000_4c04)
        __ctlmenu_end = .;
        *(.text .text.*)
    } > RAM
    .rodata : ALIGN(16) {
        *(.rodata .rodata.* .srodata .srodata.*)
    } > RAM
    .data : ALIGN(16) {
        __data_start = .;               /* save states keep from here to the heap */
        *(.data .data.*)
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
    } > RAM
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.sbss .sbss.* .bss .bss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > RAM
    _end = .;
    _stack_top = ORIGIN(RAM) + LENGTH(RAM);
    _heap_end = _stack_top - 256K;      /* 256 KiB of stack */
    /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) }
}

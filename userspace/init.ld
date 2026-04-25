OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5); /* R E */
    data PT_LOAD FLAGS(6); /* RW */
}

SECTIONS
{
    /*
     * Start at 0x400000 — the canonical user-space text base on x86_64.
     * Virtual address 0x0 is the null page and cannot be mapped by the kernel.
     * This matches the default layout produced by ld for static ET_EXEC binaries.
     */
    . = 0x400000;

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(0x1000);

    .rodata : {
        *(.rodata .rodata.*)
    } :text

    . = ALIGN(0x1000);

    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}

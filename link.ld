ENTRY(_start)
SECTIONS
{
    . = 0x10000;

    .text : {
        *(.text)
        *(.text.*)
    }

    . = ALIGN(4096);
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    . = ALIGN(4096);
    .data : {
        *(.data)
        *(.data.*)
    }

    . = 0x100000;

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.lbss)
        *(.lbss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
}

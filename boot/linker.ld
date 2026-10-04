/* boot/linker.ld - layout de memoria do kernel RubyOS (i386, carregado em 1 MiB) */

ENTRY(_start)

SECTIONS
{
    . = 1M;
    _kernel_start = .;

    /* O cabecalho Multiboot precisa estar nos primeiros 8 KiB da imagem. */
    .text ALIGN(4K) : {
        *(.multiboot)
        *(.text .text.*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) : {
        *(.data .data.*)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss .bss.*)
    }

    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note.*)
    }
}

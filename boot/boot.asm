; boot/boot.asm - ponto de entrada do RubyOS (NASM, elf32)
;
; O GRUB (ou o "-kernel" do QEMU) reconhece o cabecalho Multiboot abaixo,
; carrega o kernel em 1 MiB e salta para _start ja em modo protegido de
; 32 bits, com EAX = 0x2BADB002 e EBX = ponteiro para multiboot_info.
; Aqui so configuramos a pilha e chamamos kmain(magic, mbi) em C.

bits 32

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000003              ; bit 0: alinhar modulos; bit 1: pedir info de memoria
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

section .bss
align 16
stack_bottom:
    resb 16384                          ; pilha de 16 KiB
stack_top:

section .text
global _start
extern kmain

_start:
    cli
    mov esp, stack_top
    xor ebp, ebp
    push ebx                            ; arg 2: struct multiboot_info *
    push eax                            ; arg 1: magic do bootloader
    call kmain
.halt:
    cli
    hlt
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits

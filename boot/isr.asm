; boot/isr.asm - GDT/IDT loaders e stubs de interrupcao (NASM, elf32)
;
; Cada vetor 0..47 (32 excecoes da CPU + 16 IRQs remapeadas) tem um stub que
; empilha (codigo de erro, numero) e salta para isr_common. isr_common salva o
; contexto, chama isr_handler(struct regs *) em C e restaura tudo com iret.

bits 32

extern isr_handler

global gdt_flush
global idt_flush
global isr_stub_table

section .text

; void gdt_flush(uint32_t gdt_ptr)
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10                        ; seletor de dados (entrada 2)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.reload_cs                 ; seletor de codigo (entrada 1)
.reload_cs:
    ret

; void idt_flush(uint32_t idt_ptr)
idt_flush:
    mov eax, [esp + 4]
    lidt [eax]
    ret

; Excecao sem codigo de erro: empilha um 0 para manter a mesma estrutura.
%macro ISR_NOERR 1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

; Excecao em que a CPU ja empilhou o codigo de erro.
%macro ISR_ERR 1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

%assign n 0
%rep 48
    %if n == 8 || (n >= 10 && n <= 14) || n == 17 || n == 21 || n == 29 || n == 30
        ISR_ERR %[n]
    %else
        ISR_NOERR %[n]
    %endif
    %assign n n+1
%endrep

isr_common:
    pusha                               ; eax ecx edx ebx esp ebp esi edi
    mov ax, ds
    push eax                            ; salva DS
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    cld
    push esp                            ; arg: struct regs *
    call isr_handler
    add esp, 4
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    popa
    add esp, 8                          ; descarta numero e codigo de erro
    iret

section .rodata
isr_stub_table:
%assign n 0
%rep 48
    dd isr%+n
    %assign n n+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits

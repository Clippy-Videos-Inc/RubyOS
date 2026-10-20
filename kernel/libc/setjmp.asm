; kernel/libc/setjmp.asm - setjmp/longjmp para i386 (cdecl).
; jmp_buf = 6 palavras: ebx, esi, edi, ebp, esp (apos o retorno), eip (endereco de retorno).
; O mruby usa isso para o tratamento de excecoes Ruby (MRB_TRY/MRB_THROW).

bits 32
section .text

global setjmp
global longjmp

setjmp:
    mov eax, [esp + 4]          ; eax = env
    mov [eax],      ebx
    mov [eax + 4],  esi
    mov [eax + 8],  edi
    mov [eax + 12], ebp
    lea ecx, [esp + 4]          ; esp como sera depois do ret
    mov [eax + 16], ecx
    mov ecx, [esp]              ; endereco de retorno
    mov [eax + 20], ecx
    xor eax, eax
    ret

longjmp:
    mov edx, [esp + 4]          ; edx = env
    mov eax, [esp + 8]          ; eax = val
    test eax, eax
    jnz .restore
    inc eax                     ; longjmp(env, 0) retorna 1
.restore:
    mov ebx, [edx]
    mov esi, [edx + 4]
    mov edi, [edx + 8]
    mov ebp, [edx + 12]
    mov esp, [edx + 16]
    jmp dword [edx + 20]

section .note.GNU-stack noalloc noexec nowrite progbits

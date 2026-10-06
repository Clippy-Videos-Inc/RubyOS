; boot/switch.asm - troca de contexto entre tarefas do kernel (NASM, elf32)
;
; void context_switch(uint32_t *old_esp, uint32_t new_esp)
;
; Salva os registradores que o ABI cdecl manda a funcao preservar (ebp, ebx, esi,
; edi) na pilha da tarefa atual, grava o ESP dela em *old_esp, carrega o ESP da
; proxima tarefa, restaura os mesmos registradores a partir da pilha dela e retorna
; (o "ret" volta para onde aquela tarefa parou, ou para task_start no primeiro uso).
;
; Precisa ser chamada com interrupcoes desabilitadas.

bits 32

global context_switch

section .text

context_switch:
    mov eax, [esp + 4]          ; old_esp
    mov edx, [esp + 8]          ; new_esp
    push ebp
    push ebx
    push esi
    push edi
    mov [eax], esp
    mov esp, edx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits

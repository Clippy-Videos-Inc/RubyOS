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
global enter_usermode

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

; void enter_usermode(uint32_t eip, uint32_t user_esp) -- NUNCA RETORNA.
;
; Salta de anel 0 para anel 3 "manualmente", montando na pilha o mesmo quadro que a CPU
; monta sozinha ao entrar numa interrupcao vinda do anel 3, e executando "iret": como o
; CS de destino (0x1B) tem RPL 3, diferente do CS atual (anel 0), o iret entende que e
; uma troca de privilegio e troca SS:ESP tambem, alem de CS:EIP e EFLAGS -- exatamente
; a mesma logica que, mais tarde, vai trazer esse processo de volta ao anel 0 quando ele
; fizer "int 0x80" ou sofrer uma excecao (ver boot/isr.asm: isr_common, e TSS.esp0 em
; interrupts.c). So chamada por kernel/process.c (task_start()), uma vez por tarefa de
; anel 3, logo no primeiro despacho dela.
;
; 0x1B = GDT_SEL_UCODE (codigo de usuario, RPL 3); 0x23 = GDT_SEL_UDATA (dados de
; usuario, RPL 3) -- ver kernel/interrupts.h.
enter_usermode:
    mov eax, [esp + 4]          ; eip de destino (modo usuario)
    mov ecx, [esp + 8]          ; esp de destino (pilha de usuario)
    mov dx, 0x23
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    push dword 0x23              ; ss (usuario)
    push ecx                     ; esp (usuario)
    pushfd
    pop edx
    or edx, 0x200                ; garante IF=1: o processo de anel 3 roda com interrupcoes ligadas
    push edx                     ; eflags
    push dword 0x1B              ; cs (usuario)
    push eax                     ; eip
    iret

section .note.GNU-stack noalloc noexec nowrite progbits

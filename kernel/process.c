/* kernel/process.c - tabela de processos, troca de contexto e escalonador cooperativo */
#include "process.h"
#include "heap.h"
#include "io.h"
#include "kernel.h"
#include "kstring.h"
#include "terminal.h"
#include "interrupts.h"
#include "pmm.h"
#include "paging.h"
#include "drivers/timer.h"

#define STACK_CANARY 0x5AFEC0DEu

extern void context_switch(uint32_t *old_esp, uint32_t new_esp);
extern void enter_usermode(uint32_t eip, uint32_t user_esp) __attribute__((noreturn));
extern char stack_bottom[];       /* pilha de boot (boot/boot.asm), usada pelo init */
extern char stack_top[];

static struct process table[PROC_MAX];
static struct process *current;
static int next_pid = 1;
static bool preempt_enabled = true;

/* O "dono" no heap de um processo e o indice da vaga + 1 (0 e o kernel). */
static uint32_t owner_of(const struct process *p)
{
    return (uint32_t)(p - table) + 1u;
}

static void wake_sleepers(void)
{
    uint32_t now = timer_ticks();
    int i;

    for (i = 0; i < PROC_MAX; i++) {
        if (table[i].state == PROC_SLEEPING && (int32_t)(now - table[i].wake_tick) >= 0)
            table[i].state = PROC_READY;
    }
}

/* Libera os recursos de processos encerrados. Nunca mexe no processo em execucao,
 * pois ele ainda esta usando a propria pilha. */
static void reap_zombies(void)
{
    int i;

    for (i = 0; i < PROC_MAX; i++) {
        struct process *p = &table[i];
        if (p->state == PROC_ZOMBIE && p != current) {
            if (p->user_mode && p->user_page_addr != 0) {
                /* devolve a pagina de usuario ao kernel (supervisor) antes de liberar o
                 * quadro fisico: se outro processo o reusar depois para algo que NAO seja
                 * de anel 3, ele nao deve continuar acessivel do anel 3 por acidente. */
                pg_revoke_user_access(p->user_page_addr);
                pmm_free_region(p->user_page_addr, 1u);
            }
            heap_free_owner(owner_of(p));          /* pilha e tudo mais que o processo alocou */
            heap_set_owner_limit(owner_of(p), 0);
            memset(p, 0, sizeof(*p));              /* volta a PROC_UNUSED */
        }
    }
}

static struct process *pick_next(const struct process *prev)
{
    int start = (int)(prev - table);
    int k;

    for (k = 1; k <= PROC_MAX; k++) {              /* k == PROC_MAX examina o proprio prev por ultimo */
        struct process *q = &table[(start + k) % PROC_MAX];
        if (q->state == PROC_READY)
            return q;
    }
    return NULL;
}

/* Escolhe e executa a proxima tarefa. Chamada com interrupcoes DESABILITADAS. */
static void schedule(void)
{
    struct process *prev = current;
    struct process *next;

    if (prev->stack != NULL && *(uint32_t *)(void *)prev->stack != STACK_CANARY)
        kernel_panic("estouro de pilha em uma tarefa");

    if (prev->state == PROC_RUNNING)
        prev->state = PROC_READY;

    for (;;) {
        wake_sleepers();
        next = pick_next(prev);
        if (next != NULL)
            break;
        cpu_sti();                                 /* nada pronto: espera uma interrupcao */
        cpu_hlt();
        cpu_cli();
    }

    next->state = PROC_RUNNING;
    next->slice = PROC_TIMESLICE_TICKS;    /* fatia nova sempre que uma tarefa (re)assume a CPU */

    /* TSS.esp0: pilha de kernel que a CPU vai usar se uma interrupcao pegar "next" rodando
     * em anel 3. Atualizado sempre (mesmo para tarefas de anel 0, que nunca a consultam de
     * verdade): e so uma palavra de 32 bits, mais barato que checar user_mode aqui. */
    tss_set_kernel_stack(next->stack != NULL
                              ? (uint32_t)(uintptr_t)(next->stack + next->stack_size)
                              : (uint32_t)(uintptr_t)stack_top);

    if (next == prev)
        return;

    current = next;
    heap_set_owner(owner_of(next));
    context_switch(&prev->esp, next->esp);

    /* Aqui "prev" voltou a executar (quem nos trouxe de volta ja ajustou current e o dono). */
    reap_zombies();
}

/* Primeira funcao executada por uma tarefa nova (o "ret" do context_switch cai aqui).
 * Para uma tarefa de anel 3 (0.6), em vez de chamar entry(arg) (ela nao tem: o "programa"
 * e so os bytes copiados para a pagina de usuario), salta para o modo usuario de verdade
 * via enter_usermode() -- que nunca retorna por si so; so "volta" para ca se o processo
 * terminar (SYS_EXIT) ou sofrer uma excecao de protecao, e nos dois casos quem retoma a
 * CPU e outra tarefa (ver process_exit() e process_handle_protection_fault()), nunca este
 * ponto do codigo. */
static void task_start(void)
{
    cpu_sti();
    reap_zombies();
    if (current->user_mode)
        enter_usermode(current->user_entry, current->user_stack_top);
    else
        current->entry(current->arg);
    process_exit();
}

void process_init(struct fs_node *root)
{
    struct process *p = &table[0];

    memset(table, 0, sizeof(table));
    p->pid = next_pid++;
    p->ppid = 0;
    memcpy(p->name, "init", 5);
    p->state = PROC_RUNNING;
    p->essential = true;
    p->uid = UID_ROOT;                     /* o boot (init) e o kernel: sempre root */
    p->base_mem = (uint32_t)(stack_top - stack_bottom);
    p->start_tick = timer_ticks();
    p->slice = PROC_TIMESLICE_TICKS;
    p->cwd = root;
    current = p;
    heap_set_owner(owner_of(p));
}

int process_create(const char *name, void (*entry)(void *), void *arg, uint32_t flags)
{
    return process_create_ex(name, entry, arg, flags, 0, 0);
}

int process_create_ex(const char *name, void (*entry)(void *), void *arg, uint32_t flags,
                      size_t stack_size, size_t heap_limit)
{
    uint32_t saved = cpu_save_flags_cli();
    struct process *p = NULL;
    uint8_t *stack;
    uint32_t *sp;
    uintptr_t top;
    size_t i;
    int pid;

    if (stack_size == 0)
        stack_size = PROC_STACK_SIZE;
    if (stack_size < 4096u)
        stack_size = 4096u;
    stack_size &= ~(size_t)15u;
    if (heap_limit == 0)
        heap_limit = PROC_HEAP_LIMIT;

    for (i = 0; i < PROC_MAX; i++) {
        if (table[i].state == PROC_UNUSED) {
            p = &table[i];
            break;
        }
    }
    if (p == NULL) {
        cpu_restore_flags(saved);
        return PROC_ENOSLOT;
    }

    stack = kmalloc_owner(stack_size, owner_of(p));
    if (stack == NULL) {
        cpu_restore_flags(saved);
        return PROC_ENOMEM;
    }
    *(uint32_t *)(void *)stack = STACK_CANARY;     /* detecta estouro de pilha */

    memset(p, 0, sizeof(*p));
    pid = next_pid++;
    p->pid = pid;
    p->ppid = current->pid;
    for (i = 0; i < PROC_NAME_MAX && name[i] != '\0'; i++)
        p->name[i] = name[i];
    p->name[i] = '\0';
    p->essential = (flags & PROC_FLAG_ESSENTIAL) != 0;
    p->uid = current->uid;                 /* herda o dono de quem criou (su troca depois) */
    p->stack = stack;
    p->stack_size = stack_size;
    p->entry = entry;
    p->arg = arg;
    p->start_tick = timer_ticks();
    p->slice = PROC_TIMESLICE_TICKS;
    p->cwd = current->cwd;
    heap_set_owner_limit(owner_of(p), heap_limit);

    /* Quadro inicial que context_switch vai "restaurar": edi esi ebx ebp, o endereco
     * de retorno (task_start) e um endereco de retorno falso para o ABI. */
    top = ((uintptr_t)stack + stack_size) & ~(uintptr_t)15u;
    sp = (uint32_t *)(void *)(top - 6u * sizeof(uint32_t));
    sp[0] = 0;                                     /* edi */
    sp[1] = 0;                                     /* esi */
    sp[2] = 0;                                     /* ebx */
    sp[3] = 0;                                     /* ebp */
    sp[4] = (uint32_t)(uintptr_t)task_start;       /* ret */
    sp[5] = 0;                                     /* retorno falso de task_start */
    p->esp = (uint32_t)(uintptr_t)sp;

    p->state = PROC_READY;                         /* por ultimo: so agora o escalonador a ve */
    cpu_restore_flags(saved);
    return pid;
}

int process_create_user(const char *name, const uint8_t *code, size_t code_len, uint32_t flags)
{
    uint32_t saved = cpu_save_flags_cli();
    struct process *p = NULL;
    uint8_t *kstack;
    uint32_t *sp;
    uintptr_t top;
    uint32_t user_phys;
    size_t i;
    int pid;

    if (!pg_enabled()) {
        cpu_restore_flags(saved);
        return PROC_ENOMEM;     /* CPU sem PSE: anel 3 indisponivel nesta maquina (ver paging.h) */
    }
    if (code_len == 0 || code_len > PMM_FRAME_SIZE - 64u) {
        cpu_restore_flags(saved);
        return PROC_ENOMEM;     /* nao cabe (com folga para a pilha) na unica pagina do usuario */
    }

    for (i = 0; i < PROC_MAX; i++) {
        if (table[i].state == PROC_UNUSED) {
            p = &table[i];
            break;
        }
    }
    if (p == NULL) {
        cpu_restore_flags(saved);
        return PROC_ENOSLOT;
    }

    kstack = kmalloc_owner(PROC_STACK_SIZE, owner_of(p));
    if (kstack == NULL) {
        cpu_restore_flags(saved);
        return PROC_ENOMEM;
    }
    *(uint32_t *)(void *)kstack = STACK_CANARY;

    user_phys = pmm_alloc_contiguous(1u);
    if (user_phys == 0 || !pg_make_user_accessible(user_phys)) {
        if (user_phys != 0)
            pmm_free_region(user_phys, 1u);
        heap_free_owner(owner_of(p));
        cpu_restore_flags(saved);
        return PROC_ENOMEM;
    }
    memset((void *)(uintptr_t)user_phys, 0, PMM_FRAME_SIZE);
    memcpy((void *)(uintptr_t)user_phys, code, code_len);

    memset(p, 0, sizeof(*p));
    pid = next_pid++;
    p->pid = pid;
    p->ppid = current->pid;
    for (i = 0; i < PROC_NAME_MAX && name[i] != '\0'; i++)
        p->name[i] = name[i];
    p->name[i] = '\0';
    p->essential = (flags & PROC_FLAG_ESSENTIAL) != 0;
    p->uid = current->uid;
    p->stack = kstack;                             /* pilha de KERNEL (usada ao entrar por syscall/excecao) */
    p->stack_size = PROC_STACK_SIZE;
    p->start_tick = timer_ticks();
    p->slice = PROC_TIMESLICE_TICKS;
    p->cwd = current->cwd;
    p->user_mode = true;
    p->user_page_addr = user_phys;
    p->user_page_size = PMM_FRAME_SIZE;
    p->user_entry = user_phys;                                       /* codigo comeca no inicio da pagina */
    p->user_stack_top = (user_phys + PMM_FRAME_SIZE) & ~(uint32_t)15u; /* topo da pagina, alinhado */
    heap_set_owner_limit(owner_of(p), PROC_HEAP_LIMIT);

    top = ((uintptr_t)kstack + PROC_STACK_SIZE) & ~(uintptr_t)15u;
    sp = (uint32_t *)(void *)(top - 6u * sizeof(uint32_t));
    sp[0] = 0; sp[1] = 0; sp[2] = 0; sp[3] = 0;     /* edi esi ebx ebp */
    sp[4] = (uint32_t)(uintptr_t)task_start;        /* ret */
    sp[5] = 0;                                      /* retorno falso de task_start */
    p->esp = (uint32_t)(uintptr_t)sp;

    p->state = PROC_READY;
    cpu_restore_flags(saved);
    return pid;
}

void process_exit(void)
{
    cpu_cli();
    current->state = PROC_ZOMBIE;
    schedule();                                    /* nao volta: o zumbi nunca e escolhido */
    kernel_panic("process_exit: tarefa encerrada voltou a executar");
}

int process_kill(int pid)
{
    uint32_t saved = cpu_save_flags_cli();
    struct process *p = NULL;
    int i;

    for (i = 0; i < PROC_MAX; i++) {
        if (table[i].state != PROC_UNUSED && table[i].pid == pid) {
            p = &table[i];
            break;
        }
    }
    if (p == NULL || p->state == PROC_ZOMBIE) {
        cpu_restore_flags(saved);
        return PROC_ESRCH;
    }
    if (p->essential) {
        cpu_restore_flags(saved);
        return PROC_EPERM;
    }
    if (p == current)
        process_exit();

    p->state = PROC_ZOMBIE;                        /* ela nao esta executando: pode ser liberada ja */
    reap_zombies();
    cpu_restore_flags(saved);
    return PROC_OK;
}

void process_yield(void)
{
    uint32_t saved = cpu_save_flags_cli();
    schedule();
    cpu_restore_flags(saved);
}

void process_sleep(uint32_t ticks)
{
    uint32_t saved;

    if (ticks == 0) {
        process_yield();
        return;
    }
    saved = cpu_save_flags_cli();
    current->wake_tick = timer_ticks() + ticks;
    current->state = PROC_SLEEPING;
    schedule();
    cpu_restore_flags(saved);
}

/* Chamada pelo driver do timer a cada IRQ0 (ver drivers/timer.c), com interrupcoes ja
 * desabilitadas (convencao de qualquer manipulador de IRQ nesta kernel). So decrementa e
 * forca uma troca quando a tarefa atual esta mesmo RUNNING: durante o laco ocioso de
 * schedule() (cpu_hlt a espera de algo pronto) o estado e READY, e um tick ali nao deve
 * provocar uma chamada recursiva a schedule(). */
void process_on_timer_tick(void)
{
    if (!preempt_enabled || current == NULL || current->state != PROC_RUNNING)
        return;
    if (current->slice > 0)
        current->slice--;
    if (current->slice == 0)
        schedule();                        /* schedule() da um slice novo a quem for escolhida */
}

void process_set_preempt(bool enabled)
{
    preempt_enabled = enabled;
}

struct process *process_current(void)
{
    return current;
}

int process_set_uid(int pid, uint32_t uid)
{
    uint32_t saved = cpu_save_flags_cli();
    int i;

    for (i = 0; i < PROC_MAX; i++) {
        if (table[i].state != PROC_UNUSED && table[i].pid == pid) {
            table[i].uid = uid;
            cpu_restore_flags(saved);
            return PROC_OK;
        }
    }
    cpu_restore_flags(saved);
    return PROC_ESRCH;
}

bool process_alive(int pid)
{
    int i;

    for (i = 0; i < PROC_MAX; i++) {
        if (table[i].state != PROC_UNUSED && table[i].state != PROC_ZOMBIE && table[i].pid == pid)
            return true;
    }
    return false;
}

const struct process *process_at(int index)
{
    if (index < 0 || index >= PROC_MAX || table[index].state == PROC_UNUSED)
        return NULL;
    return &table[index];
}

size_t process_mem_bytes(const struct process *p)
{
    return (size_t)p->base_mem + heap_owner_bytes(owner_of(p));
}

const char *process_state_name(enum proc_state s)
{
    switch (s) {
    case PROC_READY:    return "READY";
    case PROC_RUNNING:  return "RUNNING";
    case PROC_SLEEPING: return "SLEEPING";
    case PROC_ZOMBIE:   return "ZOMBIE";
    default:            return "UNUSED";
    }
}

bool process_handle_protection_fault(uint32_t int_no, uint32_t cr2, uint32_t err_code,
                                      uint32_t eip, uint32_t cs)
{
    const char *kind = (int_no == 14u) ? "FALHA DE PAGINA" : "PROTECAO GERAL (GPF)";
    bool from_user = (cs & 3u) == 3u;

    if (current == NULL || current->essential)
        return false;                      /* kernel panic de sempre: bug do proprio kernel */

    terminal_setcolor(VGA_YELLOW, VGA_BLACK);
    kprintf("\n*** %s: processo %d (%s) encerrado -- modo %s ***\n",
            kind, current->pid, current->name, from_user ? "usuario" : "kernel");
    if (int_no == 14u)
        kprintf("    endereco: 0x%x  EIP: 0x%x  erro: 0x%x\n", cr2, eip, err_code);
    else
        kprintf("    EIP: 0x%x  erro: 0x%x\n", eip, err_code);
    terminal_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("    sistema continua: so esse processo foi encerrado.\n");

    current->state = PROC_ZOMBIE;
    schedule();                            /* nunca retorna: o zumbi nunca volta a ser escolhido */
    return true;                           /* inalcancavel */
}

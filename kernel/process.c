/* kernel/process.c - tabela de processos, troca de contexto e escalonador cooperativo */
#include "process.h"
#include "heap.h"
#include "io.h"
#include "kernel.h"
#include "kstring.h"
#include "drivers/timer.h"

#define STACK_CANARY 0x5AFEC0DEu

extern void context_switch(uint32_t *old_esp, uint32_t new_esp);
extern char stack_bottom[];       /* pilha de boot (boot/boot.asm), usada pelo init */
extern char stack_top[];

static struct process table[PROC_MAX];
static struct process *current;
static int next_pid = 1;

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
    if (next == prev)
        return;

    current = next;
    heap_set_owner(owner_of(next));
    context_switch(&prev->esp, next->esp);

    /* Aqui "prev" voltou a executar (quem nos trouxe de volta ja ajustou current e o dono). */
    reap_zombies();
}

/* Primeira funcao executada por uma tarefa nova (o "ret" do context_switch cai aqui). */
static void task_start(void)
{
    cpu_sti();
    reap_zombies();
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
    p->base_mem = (uint32_t)(stack_top - stack_bottom);
    p->start_tick = timer_ticks();
    p->cwd = root;
    current = p;
    heap_set_owner(owner_of(p));
}

int process_create(const char *name, void (*entry)(void *), void *arg, uint32_t flags)
{
    uint32_t saved = cpu_save_flags_cli();
    struct process *p = NULL;
    uint8_t *stack;
    uint32_t *sp;
    uintptr_t top;
    size_t i;
    int pid;

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

    stack = kmalloc_owner(PROC_STACK_SIZE, owner_of(p));
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
    p->stack = stack;
    p->stack_size = PROC_STACK_SIZE;
    p->entry = entry;
    p->arg = arg;
    p->start_tick = timer_ticks();
    p->cwd = current->cwd;
    heap_set_owner_limit(owner_of(p), PROC_HEAP_LIMIT);

    /* Quadro inicial que context_switch vai "restaurar": edi esi ebx ebp, o endereco
     * de retorno (task_start) e um endereco de retorno falso para o ABI. */
    top = ((uintptr_t)stack + PROC_STACK_SIZE) & ~(uintptr_t)15u;
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

struct process *process_current(void)
{
    return current;
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

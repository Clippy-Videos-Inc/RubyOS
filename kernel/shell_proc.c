/* kernel/shell_proc.c - comandos de processos da RubyShell: ps, spawn, kill
 *
 * "spawn" cria tarefas de demonstracao embutidas no kernel (a execucao de programas
 * Ruby, com o comando "run", chega na 0.3). */
#include "shell_internal.h"
#include "terminal.h"
#include "util.h"
#include "heap.h"
#include "process.h"
#include "drivers/timer.h"

#define WORKER_DEFAULT_SECONDS 30u
#define WORKER_MAX_SECONDS     3600u

/* -------------------------------------------------------------------- ps */

static int cmd_ps(int argc, char **argv)
{
    uint32_t now = timer_ticks();
    char num[34];
    int i;

    (void)argc;
    (void)argv;
    shell_print_padded("PID", 6);
    shell_print_padded("PPID", 6);
    shell_print_padded("NAME", 16);
    shell_print_padded("STATE", 10);
    shell_print_padded("MEM", 10);
    kprintf("UP\n");

    for (i = 0; i < PROC_MAX; i++) {
        const struct process *p = process_at(i);
        size_t kb;

        if (p == NULL)
            continue;
        kb = (process_mem_bytes(p) + 1023u) / 1024u;

        shell_print_padded(k_itoa(p->pid, num, 10), 6);
        shell_print_padded(k_itoa(p->ppid, num, 10), 6);
        shell_print_padded(p->name, 16);
        shell_print_padded(process_state_name(p->state), 10);
        {
            size_t len;
            k_utoa((uint32_t)kb, num, 10);
            len = k_strlen(num);
            num[len] = ' ';
            num[len + 1] = 'K';
            num[len + 2] = 'B';
            num[len + 3] = '\0';
            shell_print_padded(num, 10);
        }
        kprintf("%us\n", (now - p->start_tick) / TIMER_HZ);
    }
    return 0;
}

/* ----------------------------------------------------------------- spawn */

static void task_hello(void *arg)
{
    const struct process *self = process_current();

    (void)arg;
    kprintf("[hello] ola do processo %d (%s)\n", self->pid, self->name);
}

/* Aloca 1 KiB por segundo, de proposito sem liberar, e dorme: serve para ver o estado
 * SLEEPING e a memoria crescendo no ps. Ao terminar (ou levar kill) o kernel recupera tudo. */
static void task_worker(void *arg)
{
    uint32_t seconds = (uint32_t)(uintptr_t)arg;
    uint32_t i;

    for (i = 0; i < seconds; i++) {
        void *block = kmalloc(1024);
        (void)block;
        process_sleep(TIMER_HZ);
    }
}

/* Aloca blocos de 64 KiB ate a cota do processo (1 MiB) impedir. Prova que o limite
 * de memoria por processo e aplicado. */
static void task_hog(void *arg)
{
    const struct process *self = process_current();
    uint32_t total = 0;

    (void)arg;
    while (kmalloc(64u * 1024u) != NULL)
        total += 64u;
    kprintf("[hog] limite de memoria atingido apos %u KB (processo %d)\n", total, self->pid);
}

static int cmd_spawn(int argc, char **argv)
{
    void (*entry)(void *) = NULL;
    uint32_t seconds = WORKER_DEFAULT_SECONDS;
    int pid;

    if (argc < 2 || argc > 3) {
        kprintf("Uso: spawn <hello|worker|hog> [segundos]\n");
        return 1;
    }
    if (k_streq(argv[1], "hello")) {
        entry = task_hello;
    } else if (k_streq(argv[1], "worker")) {
        entry = task_worker;
        if (argc == 3 && (!k_parse_uint(argv[2], &seconds) || seconds == 0 || seconds > WORKER_MAX_SECONDS)) {
            kprintf("spawn: segundos deve ser de 1 a %u\n", WORKER_MAX_SECONDS);
            return 1;
        }
    } else if (k_streq(argv[1], "hog")) {
        entry = task_hog;
    } else {
        kprintf("spawn: programa desconhecido: %s (use hello, worker ou hog)\n", argv[1]);
        return 1;
    }

    pid = process_create(argv[1], entry, (void *)(uintptr_t)seconds, 0);
    if (pid == PROC_ENOSLOT) {
        kprintf("spawn: tabela de processos cheia (%d)\n", PROC_MAX);
        return 1;
    }
    if (pid < 0) {
        kprintf("spawn: sem memoria para criar o processo\n");
        return 1;
    }
    kprintf("processo criado: pid %d (%s)\n", pid, argv[1]);
    process_yield();                    /* deixa o novo processo comecar antes do proximo prompt */
    return 0;
}

/* ------------------------------------------------------------------ kill */

static int cmd_kill(int argc, char **argv)
{
    uint32_t pid;
    int err;

    if (argc != 2 || !k_parse_uint(argv[1], &pid) || pid > 0x7FFFFFFFu) {
        kprintf("Uso: kill <pid>\n");
        return 1;
    }
    err = process_kill((int)pid);
    if (err == PROC_ESRCH) {
        kprintf("kill: %u: processo inexistente\n", pid);
        return 1;
    }
    if (err == PROC_EPERM) {
        kprintf("kill: %u: processo essencial (init e shell nao podem ser encerrados)\n", pid);
        return 1;
    }
    kprintf("processo %u encerrado\n", pid);
    return 0;
}

static const struct command commands[] = {
    { "ps",        "ps",                              "lista os processos (PID, estado, memoria)",    cmd_ps },
    { "processes", "processes",                       "o mesmo que ps",                               cmd_ps },
    { "spawn",     "spawn <hello|worker|hog> [seg]",  "cria uma tarefa de demonstracao",              cmd_spawn },
    { "kill",      "kill <pid>",                      "encerra um processo e libera a memoria dele",  cmd_kill },
};

const struct command_group shell_group_proc = {
    "Processos:", commands, sizeof(commands) / sizeof(commands[0])
};

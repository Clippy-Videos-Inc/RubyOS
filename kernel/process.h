/* kernel/process.h - processos (tarefas do kernel) da RubyOS 0.2.
 *
 * O que existe de verdade nesta versao:
 *   - tabela de processos com PID, nome, pai, estado e diretorio atual;
 *   - uma pilha propria de 16 KiB por tarefa e TROCA DE CONTEXTO real (boot/switch.asm);
 *   - criacao, termino (exit/kill), sleep e yield;
 *   - contabilidade de memoria por processo e cota de 1 MiB de heap por tarefa;
 *   - ao terminar, tudo o que o processo alocou (inclusive a pilha) e liberado.
 *
 * O que NAO existe ainda (planejado para a 0.4 em diante):
 *   - preempcao: o escalonamento e COOPERATIVO (round-robin; troca so em yield/sleep/exit);
 *   - isolamento: as tarefas rodam no anel 0, no mesmo espaco de enderecos do kernel;
 *   - modo usuario e chamadas de sistema.
 *
 * Os processos sao "threads do kernel". Programas de usuario (Ruby) chegam na 0.3+. */
#ifndef RUBYOS_PROCESS_H
#define RUBYOS_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct fs_node;

#define PROC_MAX            16
#define PROC_NAME_MAX       15
#define PROC_STACK_SIZE     (16u * 1024u)
#define PROC_HEAP_LIMIT     (1024u * 1024u)    /* cota de heap por tarefa */

#define PROC_FLAG_ESSENTIAL 1u                 /* nao pode ser encerrada com kill */

#define PROC_OK       0
#define PROC_ENOSLOT (-1)                      /* tabela de processos cheia */
#define PROC_ENOMEM  (-2)                      /* sem memoria para a pilha */
#define PROC_ESRCH   (-3)                      /* PID inexistente */
#define PROC_EPERM   (-4)                      /* processo essencial */

enum proc_state {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_SLEEPING,
    PROC_ZOMBIE        /* encerrado, aguardando a liberacao dos recursos */
};

struct process {
    int pid;
    int ppid;
    char name[PROC_NAME_MAX + 1];
    enum proc_state state;
    bool essential;
    uint32_t esp;                      /* ESP salvo quando a tarefa nao esta executando */
    uint8_t *stack;                    /* base da pilha no heap (NULL para o init) */
    size_t stack_size;
    uint32_t base_mem;                 /* memoria fixa fora do heap (pilha de boot do init) */
    void (*entry)(void *);
    void *arg;
    uint32_t wake_tick;
    uint32_t start_tick;
    struct fs_node *cwd;
};

/* Adota o contexto de boot como processo 1 ("init"). Deve ser chamado uma vez, depois
 * de inicializar o heap e o timer. root e o diretorio atual inicial. */
void process_init(struct fs_node *root);

/* Cria uma tarefa READY que executara entry(arg). Retorna o PID (> 0) ou um erro PROC_E*.
 * A tarefa herda o diretorio atual de quem a criou. */
int process_create(const char *name, void (*entry)(void *), void *arg, uint32_t flags);

/* Encerra o processo atual. Nunca retorna. */
void process_exit(void) __attribute__((noreturn));

/* Encerra outro processo (ou o proprio). Retorna PROC_OK, PROC_ESRCH ou PROC_EPERM. */
int process_kill(int pid);

/* Cede a CPU para a proxima tarefa pronta. Retorna quando o escalonador voltar aqui. */
void process_yield(void);

/* Dorme por "ticks" ticks do timer (100 por segundo). */
void process_sleep(uint32_t ticks);

struct process *process_current(void);

/* Acesso a tabela: retorna o processo do indice i (0..PROC_MAX-1) ou NULL se vazio. */
const struct process *process_at(int index);

/* Memoria usada: memoria fixa + bytes de heap alocados pelo processo (pilha inclusa). */
size_t process_mem_bytes(const struct process *p);

const char *process_state_name(enum proc_state s);

#endif

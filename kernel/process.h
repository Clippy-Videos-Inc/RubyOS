/* kernel/process.h - processos (tarefas do kernel) da RubyOS 0.6.
 *
 * O que existe de verdade nesta versao:
 *   - tabela de processos com PID, nome, pai, estado, dono (uid) e diretorio atual;
 *   - uma pilha propria de 16 KiB por tarefa e TROCA DE CONTEXTO real (boot/switch.asm);
 *   - criacao, termino (exit/kill), sleep e yield;
 *   - contabilidade de memoria por processo e cota de 1 MiB de heap por tarefa;
 *   - ao terminar, tudo o que o processo alocou (inclusive a pilha) e liberado;
 *   - escalonamento PREEMPTIVO por fatia de tempo (round-robin; o timer forca a troca
 *     quando uma tarefa nao cede a CPU sozinha), alem do yield/sleep cooperativos de sempre;
 *   - a partir da 0.6: process_create_user() cria tarefas que rodam de verdade em
 *     ANEL 3 (ver paging.h e syscall.h), isoladas do kernel pelo hardware de paginacao;
 *     e process_handle_protection_fault() deixa uma FALHA DE PAGINA ou uma PROTECAO
 *     GERAL matarem so o processo causador (de anel 0 ou 3) em vez de derrubar o
 *     sistema inteiro, contanto que o processo nao seja essencial.
 *
 * O que NAO existe ainda (planejado para depois da 0.6):
 *   - espacos de enderecamento separados por processo (um CR3 por processo): ha um so
 *     diretorio de paginas global, entao dois processos de anel 3 ao mesmo tempo
 *     enxergam a pagina de usuario um do outro (ver a limitacao documentada em paging.h);
 *   - um carregador de executaveis de verdade (ELF, etc.): os processos de anel 3 desta
 *     versao sao so os programas de demonstracao embutidos no kernel (ring3_demo.h).
 *
 * Os processos sao "threads do kernel" (anel 0) OU as tarefas de anel 3 acima. Um
 * runtime Ruby completo rodando em anel 3 fica para uma versao futura. */
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
#define PROC_RUBY_STACK     (256u * 1024u)     /* pilha de um processo Ruby */
#define PROC_RUBY_HEAP      (6u * 1024u * 1024u) /* cota de um processo Ruby */

#define PROC_FLAG_ESSENTIAL 1u                 /* nao pode ser encerrada com kill */

#define PROC_TIMESLICE_TICKS 5u                /* fatia de tempo: 5 ticks a 100 Hz = 50 ms */

#define PROC_OK       0
#define PROC_ENOSLOT (-1)                      /* tabela de processos cheia */
#define PROC_ENOMEM  (-2)                      /* sem memoria para a pilha */
#define PROC_ESRCH   (-3)                      /* PID inexistente */
#define PROC_EPERM   (-4)                      /* processo essencial */

/* uids: nao ha autenticacao (planejada para versao futura); UID_ROOT so e especial por
 * ignorar as permissoes do sistema de arquivos (ver ramfs.h). */
#define UID_ROOT 0u
#define UID_USER 1000u

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
    uint32_t uid;                      /* dono do processo: controla as permissoes de arquivo */
    uint32_t esp;                      /* ESP salvo quando a tarefa nao esta executando */
    uint8_t *stack;                    /* base da pilha no heap (NULL para o init) */
    size_t stack_size;
    uint32_t base_mem;                 /* memoria fixa fora do heap (pilha de boot do init) */
    void (*entry)(void *);
    void *arg;
    uint32_t wake_tick;
    uint32_t start_tick;
    uint32_t slice;                    /* ticks restantes da fatia de tempo atual */
    struct fs_node *cwd;
    bool user_mode;                    /* true: tarefa de anel 3 (0.6) */
    uint32_t user_entry;               /* EIP inicial em modo usuario (== user_page_addr) */
    uint32_t user_stack_top;           /* ESP inicial em modo usuario */
    uint32_t user_page_addr;           /* pagina fisica (== virtual) de codigo+pilha do usuario */
    uint32_t user_page_size;           /* tamanho dela, usado pelos syscalls para validar ponteiros */
};

/* Adota o contexto de boot como processo 1 ("init"). Deve ser chamado uma vez, depois
 * de inicializar o heap e o timer. root e o diretorio atual inicial. */
void process_init(struct fs_node *root);

/* Cria uma tarefa READY que executara entry(arg). Retorna o PID (> 0) ou um erro PROC_E*.
 * A tarefa herda o diretorio atual de quem a criou. Usa a pilha e a cota padrao. */
int process_create(const char *name, void (*entry)(void *), void *arg, uint32_t flags);

/* Igual a process_create, com pilha (stack_size, minimo 4 KiB, 0 = padrao) e cota de
 * heap (heap_limit em bytes, a pilha conta na cota; 0 = padrao) escolhidas. O runtime
 * Ruby precisa de pilha e cota maiores que as tarefas simples em C. */
int process_create_ex(const char *name, void (*entry)(void *), void *arg, uint32_t flags,
                      size_t stack_size, size_t heap_limit);

/* Cria uma tarefa de ANEL 3 (0.6): copia "code_len" bytes de "code" (binario plano,
 * posicao-independente -- ver ring3_demo.h) para uma pagina fisica dedicada, marca essa
 * pagina acessivel em modo usuario (paging.h: pg_make_user_accessible()) e a tarefa
 * comeca a executar no offset 0 dela, em anel 3, com uma pilha de usuario no topo da
 * mesma pagina. So pode chamar chamadas de sistema (syscall.h); qualquer outro acesso a
 * memoria fora dessa pagina gera uma excecao de protecao tratada por
 * process_handle_protection_fault() (mata so essa tarefa). Retorna o PID (> 0) ou um
 * erro PROC_E* -- em especial, PROC_ENOMEM se a CPU nao tiver suporte a paginacao (PSE)
 * e o anel 3 estiver indisponivel nesta maquina. */
int process_create_user(const char *name, const uint8_t *code, size_t code_len, uint32_t flags);

/* Encerra o processo atual. Nunca retorna. */
void process_exit(void) __attribute__((noreturn));

/* Encerra outro processo (ou o proprio). Retorna PROC_OK, PROC_ESRCH ou PROC_EPERM. */
int process_kill(int pid);

/* Cede a CPU para a proxima tarefa pronta. Retorna quando o escalonador voltar aqui. */
void process_yield(void);

/* Dorme por "ticks" ticks do timer (100 por segundo). */
void process_sleep(uint32_t ticks);

/* Chamada pelo driver do timer (kernel/drivers/timer.c) a cada interrupcao do PIT. So
 * conta e eventualmente forca uma troca de contexto se a tarefa atual estiver mesmo
 * executando (RUNNING); nao faz nada durante o laco ocioso do escalonador. Precisa ser
 * chamada com interrupcoes desabilitadas (como qualquer manipulador de IRQ). */
void process_on_timer_tick(void);

/* Liga/desliga a preempcao por tempo (ligada por padrao). Usada para depuracao e pelo
 * parametro de boot "preempt=off". Com a preempcao desligada o escalonamento volta a ser
 * puramente cooperativo, como na 0.2/0.3. */
void process_set_preempt(bool enabled);

struct process *process_current(void);

/* Troca o uid do processo "pid" (usado por su/login). Mecanismo puro: nao ha politica
 * de autenticacao aqui (ver UID_ROOT em process.h e o comando "su" da shell). */
int process_set_uid(int pid, uint32_t uid);

/* true se existe um processo com esse PID que ainda nao terminou. */
bool process_alive(int pid);

/* Acesso a tabela: retorna o processo do indice i (0..PROC_MAX-1) ou NULL se vazio. */
const struct process *process_at(int index);

/* Memoria usada: memoria fixa + bytes de heap alocados pelo processo (pilha inclusa). */
size_t process_mem_bytes(const struct process *p);

const char *process_state_name(enum proc_state s);

/* Chamada pelo tratador de excecoes (kernel/interrupts.c) quando int_no e 13 (protecao
 * geral) ou 14 (falha de pagina). cr2 so e significativo para a 14 (endereco que faltou).
 * Se o processo atual nao for essencial, imprime o motivo, marca-o ZUMBI e troca de
 * contexto -- NUNCA RETORNA nesse caso, igual a process_exit(). Se nao houver processo
 * atual ou ele for essencial (o "init"), devolve false para o chamador decidir (hoje,
 * um kernel panic: nesse caso o bug e do proprio kernel, nao ha o que recuperar). */
bool process_handle_protection_fault(uint32_t int_no, uint32_t cr2, uint32_t err_code,
                                      uint32_t eip, uint32_t cs);

#endif

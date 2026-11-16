/* kernel/interrupts.c - GDT (com segmentos de anel 3 e TSS), IDT, PIC 8259 e despacho
 * de excecoes/IRQs/chamadas de sistema. */
#include "interrupts.h"
#include "kernel.h"
#include "terminal.h"
#include "io.h"
#include "process.h"
#include "syscall.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

#define GDT_ENTRIES 6           /* nulo, kcode, kdata, ucode, udata, tss (0.6) */
#define IDT_ENTRIES 256
#define VECTORS_USED 48
#define VECTOR_SYSCALL 0x80

#define INT_NO_PAGE_FAULT 14u
#define INT_NO_GPF        13u

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
} __attribute__((packed));

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct table_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* TSS minima: so os campos que a troca de anel (ring3 -> ring0) numa interrupcao usa de
 * verdade (ss0/esp0) precisam estar certos; o resto fica zerado. iomap_base = tamanho da
 * propria struct desativa o bitmap de permissao de E/S (nenhuma porta liberada para o
 * anel 3, coerente com nao haver nenhum driver pensado para rodar em modo usuario). */
struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0, ss0;
    uint32_t esp1, ss1;
    uint32_t esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap, iomap_base;
} __attribute__((packed));

extern void gdt_flush(uint32_t ptr);
extern void idt_flush(uint32_t ptr);
extern void tss_flush(void);
extern void isr_syscall(void);
extern uint32_t isr_stub_table[VECTORS_USED];

static struct gdt_entry gdt[GDT_ENTRIES];
static struct idt_entry idt[IDT_ENTRIES];
static struct table_ptr gdt_ptr;
static struct table_ptr idt_ptr;
static struct tss_entry tss;
static irq_handler_t irq_handlers[16];

static const char *const exception_names[32] = {
    "Divisao por zero", "Debug", "NMI", "Breakpoint",
    "Overflow", "Limite excedido (BOUND)", "Opcode invalido", "Dispositivo indisponivel",
    "Falha dupla", "Segmento do coprocessador", "TSS invalido", "Segmento ausente",
    "Falha de pilha", "Protecao geral (GPF)", "Falha de pagina", "Reservado",
    "Erro de ponto flutuante", "Verificacao de alinhamento", "Machine check", "Erro SIMD",
    "Virtualizacao", "Protecao de controle", "Reservado", "Reservado",
    "Reservado", "Reservado", "Reservado", "Reservado",
    "Hypervisor", "VMM communication", "Seguranca", "Reservado"
};

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt[i].base_low  = (uint16_t)(base & 0xFFFF);
    gdt[i].base_mid  = (uint8_t)((base >> 16) & 0xFF);
    gdt[i].base_high = (uint8_t)((base >> 24) & 0xFF);
    gdt[i].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[i].gran      = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].access    = access;
}

static void idt_set_gate(int i, uint32_t handler, uint16_t selector, uint8_t flags)
{
    idt[i].base_low  = (uint16_t)(handler & 0xFFFF);
    idt[i].base_high = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[i].selector  = selector;
    idt[i].zero      = 0;
    idt[i].flags     = flags;
}

static void memset_tss(void);

static void pic_remap(void)
{
    outb(PIC1_CMD, 0x11);  io_wait();      /* ICW1: init + espera ICW4 */
    outb(PIC2_CMD, 0x11);  io_wait();
    outb(PIC1_DATA, 0x20); io_wait();      /* ICW2: mestre -> vetores 32..39 */
    outb(PIC2_DATA, 0x28); io_wait();      /*       escravo -> vetores 40..47 */
    outb(PIC1_DATA, 0x04); io_wait();      /* ICW3: escravo ligado na IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();      /* ICW4: modo 8086 */
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, 0xFF);                 /* mascara tudo */
    outb(PIC2_DATA, 0xFF);
}

void interrupts_init(void)
{
    int i;

    /* GDT plana, agora com segmentos de anel 3 e uma TSS (0.6):
     *   0 nulo, 1 codigo kernel (0x08), 2 dados kernel (0x10),
     *   3 codigo usuario (0x1B, DPL 3), 4 dados usuario (0x23, DPL 3), 5 TSS (0x28). */
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_set(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    gdt_set(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    memset_tss();
    tss.ss0 = GDT_SEL_KDATA;
    tss.iomap_base = (uint16_t)sizeof(tss);
    gdt_set(5, (uint32_t)(uintptr_t)&tss, (uint32_t)(sizeof(tss) - 1u), 0x89, 0x00);

    gdt_ptr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_ptr.base  = (uint32_t)(uintptr_t)&gdt;
    gdt_flush((uint32_t)(uintptr_t)&gdt_ptr);
    tss_flush();

    pic_remap();

    for (i = 0; i < VECTORS_USED; i++)
        idt_set_gate(i, isr_stub_table[i], GDT_SEL_KCODE, 0x8E);   /* presente, anel 0, interrupt gate 32 bits */
    /* Vetor 0x80: a unica interrupcao que o anel 3 pode disparar de proposito com
     * "int 0x80" (chamadas de sistema, ver syscall.h). DPL 3 no proprio flags (0xEE)
     * e o que permite isso; todos os outros vetores continuam DPL 0 (0x8E) -- um
     * processo de anel 3 que tentar "int" em qualquer outro numero leva uma GPF. */
    idt_set_gate(VECTOR_SYSCALL, (uint32_t)(uintptr_t)isr_syscall, GDT_SEL_KCODE, 0xEE);
    idt_ptr.limit = (uint16_t)(sizeof(idt) - 1);
    idt_ptr.base  = (uint32_t)(uintptr_t)&idt;
    idt_flush((uint32_t)(uintptr_t)&idt_ptr);
}

/* memset manual (kstring.h ainda nao foi incluido aqui de proposito: interrupts.c e
 * inicializado antes do resto, e so precisamos zerar uma struct pequena e estatica). */
static void memset_tss(void)
{
    uint8_t *p = (uint8_t *)&tss;
    size_t i;

    for (i = 0; i < sizeof(tss); i++)
        p[i] = 0;
}

void interrupts_enable(void)
{
    cpu_sti();
}

void irq_install_handler(uint8_t irq, irq_handler_t handler)
{
    if (irq < 16)
        irq_handlers[irq] = handler;
}

void irq_unmask(uint8_t irq)
{
    if (irq < 8) {
        outb(PIC1_DATA, (uint8_t)(inb(PIC1_DATA) & ~(1u << irq)));
    } else if (irq < 16) {
        outb(PIC1_DATA, (uint8_t)(inb(PIC1_DATA) & ~(1u << 2)));   /* cascata */
        outb(PIC2_DATA, (uint8_t)(inb(PIC2_DATA) & ~(1u << (irq - 8))));
    }
}

void tss_set_kernel_stack(uint32_t esp0)
{
    tss.esp0 = esp0;
}

static uint32_t read_cr2(void)
{
    uint32_t v;
    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

/* Chamado por boot/isr.asm para todos os vetores 0..47 e para 0x80 (chamadas de
 * sistema). A partir da 0.6, FALHA DE PAGINA (14) e PROTECAO GERAL (13) nao derrubam
 * mais o sistema inteiro: process_handle_protection_fault() (process.c) mata so o
 * processo que causou a excecao e devolve a CPU ao escalonador, A MENOS que o processo
 * seja essencial (o "init") -- nesse caso (ou se nao houver processo algum ainda) ela
 * devolve false e caimos no kernel panic de sempre, porque ai sim e um bug do kernel. */
void isr_handler(struct regs *r)
{
    if (r->int_no == VECTOR_SYSCALL) {
        syscall_dispatch(r);
        return;
    }

    if (r->int_no == INT_NO_PAGE_FAULT || r->int_no == INT_NO_GPF) {
        uint32_t cr2 = (r->int_no == INT_NO_PAGE_FAULT) ? read_cr2() : 0;

        if (process_handle_protection_fault(r->int_no, cr2, r->err_code, r->eip, r->cs))
            return;     /* so alcancavel se nao houve recuperacao (ver process.c) */
    }

    if (r->int_no < 32) {
        terminal_setcolor(VGA_WHITE, VGA_RED);
        kprintf("\nEXCECAO %u: %s\n", r->int_no, exception_names[r->int_no]);
        kprintf("  EIP=0x%x CS=0x%x EFLAGS=0x%x ERR=0x%x\n",
                r->eip, r->cs, r->eflags, r->err_code);
        kernel_panic("excecao da CPU nao tratada");
    } else {
        uint8_t irq = (uint8_t)(r->int_no - 32);

        /* O EOI e enviado ANTES do manipulador, nao depois: a partir da 0.4 o manipulador
         * do timer pode trocar de contexto (preempcao) sem retornar logo em seguida, e o
         * PIC so entrega novas interrupcoes depois do EOI desta. Se o EOI esperasse o
         * retorno do manipulador, o sistema inteiro (teclado incluido) ficaria sem receber
         * interrupcoes enquanto a tarefa interrompida estivesse fora da CPU. Isso e seguro
         * porque IF continua zerado (interrupcoes de hardware desligadas) ate o iret que
         * fecha esta ISR controlar o contrario; nao ha reentrancia de verdade. */
        if (irq >= 8)
            outb(PIC2_CMD, PIC_EOI);
        outb(PIC1_CMD, PIC_EOI);
        if (irq_handlers[irq] != NULL)
            irq_handlers[irq](r);
    }
}

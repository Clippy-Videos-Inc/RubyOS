/* kernel/interrupts.c - GDT plana, IDT, PIC 8259 e despacho de excecoes/IRQs */
#include "interrupts.h"
#include "kernel.h"
#include "terminal.h"
#include "io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

#define GDT_ENTRIES 3
#define IDT_ENTRIES 256
#define VECTORS_USED 48

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

extern void gdt_flush(uint32_t ptr);
extern void idt_flush(uint32_t ptr);
extern uint32_t isr_stub_table[VECTORS_USED];

static struct gdt_entry gdt[GDT_ENTRIES];
static struct idt_entry idt[IDT_ENTRIES];
static struct table_ptr gdt_ptr;
static struct table_ptr idt_ptr;
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

    /* GDT plana: nulo, codigo ring 0 (0x08), dados ring 0 (0x10). */
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_ptr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdt_ptr.base  = (uint32_t)(uintptr_t)&gdt;
    gdt_flush((uint32_t)(uintptr_t)&gdt_ptr);

    pic_remap();

    for (i = 0; i < VECTORS_USED; i++)
        idt_set_gate(i, isr_stub_table[i], 0x08, 0x8E);   /* presente, ring 0, interrupt gate 32 bits */
    idt_ptr.limit = (uint16_t)(sizeof(idt) - 1);
    idt_ptr.base  = (uint32_t)(uintptr_t)&idt;
    idt_flush((uint32_t)(uintptr_t)&idt_ptr);
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

/* Chamado por boot/isr.asm para todos os vetores 0..47. */
void isr_handler(struct regs *r)
{
    if (r->int_no < 32) {
        terminal_setcolor(VGA_WHITE, VGA_RED);
        kprintf("\nEXCECAO %u: %s\n", r->int_no, exception_names[r->int_no]);
        kprintf("  EIP=0x%x CS=0x%x EFLAGS=0x%x ERR=0x%x\n",
                r->eip, r->cs, r->eflags, r->err_code);
        kernel_panic("excecao da CPU nao tratada");
    } else {
        uint8_t irq = (uint8_t)(r->int_no - 32);
        if (irq_handlers[irq] != NULL)
            irq_handlers[irq](r);
        if (irq >= 8)
            outb(PIC2_CMD, PIC_EOI);
        outb(PIC1_CMD, PIC_EOI);
    }
}

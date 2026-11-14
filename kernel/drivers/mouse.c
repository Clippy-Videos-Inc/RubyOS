/* kernel/drivers/mouse.c - ver mouse.h */
#include "mouse.h"
#include "../interrupts.h"
#include "../io.h"

#define PS2_DATA   0x60u
#define PS2_STATUS 0x64u
#define PS2_CMD    0x64u

#define PS2_STATUS_OUT_FULL 0x01u
#define PS2_STATUS_IN_FULL  0x02u

static volatile int32_t g_x;
static volatile int32_t g_y;
static volatile bool g_left, g_right, g_middle;
static int32_t g_max_x = 639;
static int32_t g_max_y = 479;

static uint8_t g_packet[3];
static uint8_t g_packet_index;

/* Algumas implementacoes do controlador 8042 (notadamente a do QEMU, pelo menos para o
 * comando 0x20 abaixo) nunca levantam o bit de "saida cheia" para certos comandos, mesmo
 * entregando o byte -- por isso os laços de espera tem um limite, em vez de esperar para
 * sempre: se estourar, o codigo chamador segue em frente e le a porta assim mesmo. */
#define PS2_TIMEOUT_LOOPS 100000u

static bool wait_input_clear(void)
{
    uint32_t n = PS2_TIMEOUT_LOOPS;

    while ((inb(PS2_STATUS) & PS2_STATUS_IN_FULL) != 0) {
        if (--n == 0)
            return false;
    }
    return true;
}

static bool wait_output_full(void)
{
    uint32_t n = PS2_TIMEOUT_LOOPS;

    while ((inb(PS2_STATUS) & PS2_STATUS_OUT_FULL) == 0) {
        if (--n == 0)
            return false;
    }
    return true;
}

static void mouse_write(uint8_t data)
{
    wait_input_clear();
    outb(PS2_CMD, 0xD4u);                 /* "o proximo byte e para o mouse (porta auxiliar)" */
    wait_input_clear();
    outb(PS2_DATA, data);
}

static uint8_t mouse_read(void)
{
    wait_output_full();
    return inb(PS2_DATA);
}

static void clamp_position(void)
{
    if (g_x < 0) g_x = 0;
    if (g_y < 0) g_y = 0;
    if (g_x > g_max_x) g_x = g_max_x;
    if (g_y > g_max_y) g_y = g_max_y;
}

static void mouse_irq(struct regs *r)
{
    (void)r;
    g_packet[g_packet_index++] = inb(PS2_DATA);
    if (g_packet_index < 3)
        return;
    g_packet_index = 0;

    /* byte 0: bit3 sempre 1 (sanidade do pacote); bits 0-2 = botoes; bit4/5 = sinal de X/Y;
     * bit6/7 = overflow (descarta o pacote, a leitura ficaria sem sentido). */
    if ((g_packet[0] & 0x08u) == 0 || (g_packet[0] & 0xC0u) != 0)
        return;

    g_left   = (g_packet[0] & 0x01u) != 0;
    g_right  = (g_packet[0] & 0x02u) != 0;
    g_middle = (g_packet[0] & 0x04u) != 0;

    {
        int32_t dx = g_packet[1];
        int32_t dy = g_packet[2];

        if ((g_packet[0] & 0x10u) != 0)
            dx -= 256;                     /* estende o sinal (complemento de 2 em 9 bits) */
        if ((g_packet[0] & 0x20u) != 0)
            dy -= 256;
        g_x += dx;
        g_y -= dy;                         /* o PS/2 usa Y positivo para cima; a tela, para baixo */
        clamp_position();
    }
}

void mouse_init(uint32_t screen_width, uint32_t screen_height)
{
    uint8_t status;

    g_max_x = screen_width > 0 ? (int32_t)screen_width - 1 : 0;
    g_max_y = screen_height > 0 ? (int32_t)screen_height - 1 : 0;
    g_x = g_max_x / 2;
    g_y = g_max_y / 2;

    wait_input_clear();
    outb(PS2_CMD, 0xA8u);                  /* habilita a porta auxiliar */

    wait_input_clear();
    outb(PS2_CMD, 0x20u);                  /* le o byte de configuracao do controlador */
    status = mouse_read();
    status |= 0x02u;                       /* bit 1: habilita a IRQ 12 (porta auxiliar) */
    status &= (uint8_t)~0x20u;             /* bit 5: tira o clock da porta auxiliar do mudo */
    wait_input_clear();
    outb(PS2_CMD, 0x60u);                  /* escreve de volta o byte de configuracao */
    wait_input_clear();
    outb(PS2_DATA, status);

    mouse_write(0xF6u); mouse_read();      /* "set defaults" (resolucao/taxa padrao), ACK */
    mouse_write(0xF4u); mouse_read();      /* habilita o envio de pacotes, ACK */

    irq_install_handler(12, mouse_irq);
    irq_unmask(12);
    irq_unmask(2);                         /* IRQ 2: cascata do PIC escravo (onde a 12 mora) */
}

void mouse_get_state(struct mouse_state *out)
{
    out->x = g_x;
    out->y = g_y;
    out->left = g_left;
    out->right = g_right;
    out->middle = g_middle;
}

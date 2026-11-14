/* kernel/drivers/vbe.c - ver vbe.h */
#include "vbe.h"
#include "pci.h"
#include "../io.h"

#define VBE_DISPI_IOPORT_INDEX 0x01CEu
#define VBE_DISPI_IOPORT_DATA  0x01CFu

#define VBE_DISPI_INDEX_ID          0u
#define VBE_DISPI_INDEX_XRES        1u
#define VBE_DISPI_INDEX_YRES        2u
#define VBE_DISPI_INDEX_BPP         3u
#define VBE_DISPI_INDEX_ENABLE      4u
#define VBE_DISPI_INDEX_VIRT_WIDTH  6u
#define VBE_DISPI_INDEX_VIRT_HEIGHT 7u

#define VBE_DISPI_DISABLED     0x00u
#define VBE_DISPI_ENABLED      0x01u
#define VBE_DISPI_LFB_ENABLED  0x40u
#define VBE_DISPI_NOCLEARMEM   0x80u

#define VBE_DISPI_ID0 0xB0C0u
#define VBE_DISPI_ID5 0xB0C5u

static void dispi_write(uint16_t index, uint16_t value)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t dispi_read(uint16_t index)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

bool vbe_present(void)
{
    uint16_t id = dispi_read(VBE_DISPI_INDEX_ID);

    /* As versoes conhecidas da interface dispi vao de VBE_DISPI_ID0 a ID5 (e versoes mais
     * novas continuam >= ID0); qualquer coisa fora dessa faixa significa "sem dispi". */
    return id >= VBE_DISPI_ID0 && id <= 0xB0CFu;
    (void)VBE_DISPI_ID5;
}

bool vbe_set_mode(uint32_t width, uint32_t height, struct vbe_mode *out)
{
    struct pci_device vga;
    uint32_t fb_phys;

    if (!vbe_present())
        return false;
    if (!pci_find_display_controller(&vga))
        return false;
    fb_phys = pci_bar_address(&vga, 0);
    if (fb_phys == 0)
        return false;

    dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    dispi_write(VBE_DISPI_INDEX_XRES, (uint16_t)width);
    dispi_write(VBE_DISPI_INDEX_YRES, (uint16_t)height);
    dispi_write(VBE_DISPI_INDEX_BPP, 32u);
    dispi_write(VBE_DISPI_INDEX_VIRT_WIDTH, (uint16_t)width);
    dispi_write(VBE_DISPI_INDEX_VIRT_HEIGHT, (uint16_t)height);
    dispi_write(VBE_DISPI_INDEX_ENABLE,
                VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM);

    /* Confere se o modo realmente "pegou" (hardware antigo/sem suporte ignoraria o pedido). */
    if (dispi_read(VBE_DISPI_INDEX_XRES) != (uint16_t)width ||
        dispi_read(VBE_DISPI_INDEX_YRES) != (uint16_t)height) {
        dispi_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
        return false;
    }

    out->width = width;
    out->height = height;
    out->bpp = 32u;
    out->pitch = width * 4u;
    out->framebuffer = (volatile uint32_t *)(uintptr_t)fb_phys;
    return true;
}

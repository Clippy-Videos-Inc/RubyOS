/* kernel/drivers/pci.c - ver pci.h */
#include "pci.h"
#include "../io.h"

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA    0xCFCu

static uint32_t pci_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    /* Bit 31 = enable; bits 23-16 = bus; 15-11 = slot (device); 10-8 = func; offset
     * alinhado em 4 bytes (bits 1-0 sempre zero). */
    return 0x80000000u
         | ((uint32_t)bus << 16)
         | ((uint32_t)(slot & 0x1Fu) << 11)
         | ((uint32_t)(func & 0x07u) << 8)
         | (offset & 0xFCu);
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS, pci_address(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
    outl(PCI_CONFIG_ADDRESS, pci_address(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

bool pci_find_display_controller(struct pci_device *out)
{
    unsigned bus;

    for (bus = 0; bus < 256u; bus++) {
        unsigned slot;

        for (slot = 0; slot < 32u; slot++) {
            uint32_t id = pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x00);
            uint16_t vendor_id = (uint16_t)(id & 0xFFFFu);
            uint32_t class_reg;
            uint8_t header_type;
            unsigned max_func;
            unsigned func;

            if (vendor_id == 0xFFFFu)
                continue;                        /* nada nesse slot */

            class_reg = pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C);
            header_type = (uint8_t)((class_reg >> 16) & 0xFFu);
            max_func = (header_type & 0x80u) ? 8u : 1u;    /* bit 7: multifuncao */

            for (func = 0; func < max_func; func++) {
                uint32_t id2 = pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x00);
                uint32_t classcode;
                uint8_t base_class;
                uint8_t sub_class;

                if ((id2 & 0xFFFFu) == 0xFFFFu)
                    continue;
                classcode = pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x08);
                base_class = (uint8_t)((classcode >> 24) & 0xFFu);
                sub_class = (uint8_t)((classcode >> 16) & 0xFFu);
                if (base_class != 0x03u)          /* 0x03 = controlador de video */
                    continue;

                out->bus = (uint8_t)bus;
                out->slot = (uint8_t)slot;
                out->func = (uint8_t)func;
                out->vendor_id = (uint16_t)(id2 & 0xFFFFu);
                out->device_id = (uint16_t)((id2 >> 16) & 0xFFFFu);
                out->class_code = base_class;
                out->subclass = sub_class;
                return true;
            }
        }
    }
    return false;
}

uint32_t pci_bar_address(const struct pci_device *dev, int bar_index)
{
    uint8_t offset = (uint8_t)(0x10u + 4u * bar_index);
    uint32_t bar = pci_config_read32(dev->bus, dev->slot, dev->func, offset);

    if ((bar & 0x1u) != 0)
        return 0;                                /* BAR de E/S, nao de memoria: nao serve aqui */
    if (((bar >> 1) & 0x3u) == 0x2u)
        return 0;                                /* BAR de memoria de 64 bits: o endereco alto
                                                    * fica no BAR seguinte; nao usado aqui */
    return bar & 0xFFFFFFF0u;                     /* descarta as flags dos 4 bits baixos */
}

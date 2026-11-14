/* kernel/drivers/pci.h - acesso minimo ao espaco de configuracao PCI (mecanismo #1, via
 * as portas 0xCF8/0xCFC). So o necessario para achar o controlador de video (classe 0x03)
 * e ler o endereco fisico do seu framebuffer linear (BAR0). */
#ifndef RUBYOS_DRIVERS_PCI_H
#define RUBYOS_DRIVERS_PCI_H

#include <stdint.h>
#include <stdbool.h>

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;     /* 0x03 = controlador de video (VGA/display) */
    uint8_t subclass;
};

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

/* Percorre bus 0..255 x slot 0..31 x func 0..7 (so a func 0, exceto se o dispositivo for
 * multifuncao) e devolve o primeiro controlador de video (classe 0x03) encontrado. */
bool pci_find_display_controller(struct pci_device *out);

/* Endereco fisico (mascarando as flags dos bits baixos) do BAR indicado (0..5). Para um BAR
 * de memoria de 32 bits (o caso comum do framebuffer linear), o endereco vem direto; BARs de
 * E/S ou de 64 bits nao sao necessarios aqui e dao 0. */
uint32_t pci_bar_address(const struct pci_device *dev, int bar_index);

#endif

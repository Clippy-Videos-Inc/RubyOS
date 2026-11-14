/* kernel/drivers/vbe.h - modo grafico via a interface Bochs VBE ("dispi"), suportada pelo
 * QEMU ("-vga std", o padrao) e por outros emuladores/hypervisors compativeis (Bochs,
 * VirtualBox). Troca de modo por E/S em portas, sem chamadas de BIOS em modo real: o
 * dispositivo de video fica, ao mesmo tempo, mapeado como um controlador PCI comum (ver
 * pci.h) com o framebuffer linear no BAR0. */
#ifndef RUBYOS_DRIVERS_VBE_H
#define RUBYOS_DRIVERS_VBE_H

#include <stdint.h>
#include <stdbool.h>

struct vbe_mode {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;             /* bits por pixel (sempre 32 aqui) */
    uint32_t pitch;           /* bytes por linha (= width * bpp / 8, sem preenchimento) */
    volatile uint32_t *framebuffer; /* endereco linear (= fisico; nao ha paginacao no RubyOS) */
};

/* true se a interface dispi do Bochs VBE responde (ID de versao reconhecido). Nao precisa de
 * PCI: funciona so com as portas 0x1CE/0x1CF. */
bool vbe_present(void);

/* Pede o modo width x height a 32 bits por pixel e preenche *out com o framebuffer (achado
 * via o BAR0 do controlador de video no barramento PCI). Retorna false (e nao mexe em nada)
 * se a interface dispi nao responder ou se o controlador de video nao aparecer no PCI. */
bool vbe_set_mode(uint32_t width, uint32_t height, struct vbe_mode *out);

#endif

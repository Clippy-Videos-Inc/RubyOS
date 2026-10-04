/* kernel/kstring.h - funcoes de memoria/string que o compilador exige mesmo em
 * modo freestanding (ele pode gerar chamadas a memcpy/memset por conta propria). */
#ifndef RUBYOS_KSTRING_H
#define RUBYOS_KSTRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int value, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

#endif

/* libc minima do RubyOS - stdlib.h */
#ifndef RUBYOS_LIBC_STDLIB_H
#define RUBYOS_LIBC_STDLIB_H
#include <stddef.h>

void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void  free(void *p);
void  abort(void) __attribute__((noreturn));
void  exit(int status) __attribute__((noreturn));
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
int   abs(int v);
long  strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);

#endif

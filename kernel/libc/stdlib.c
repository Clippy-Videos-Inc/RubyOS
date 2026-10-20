/* kernel/libc/stdlib.c - malloc & cia. sobre o heap do kernel.
 *
 * malloc() usa kmalloc(): o bloco pertence ao processo que esta executando, entao tudo
 * o que o mruby aloca e contabilizado (e limitado) por processo, e e devolvido ao heap
 * quando o processo termina, mesmo que ele seja morto no meio de uma execucao. */
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include "../heap.h"
#include "../kernel.h"

void *malloc(size_t n)                 { return kmalloc(n); }
void *calloc(size_t n, size_t size)    { return kcalloc(n, size); }
void *realloc(void *p, size_t n)       { return krealloc(p, n); }
void  free(void *p)                    { kfree(p); }
int   abs(int v)                       { return v < 0 ? -v : v; }

void abort(void)
{
    kernel_panic("abort() chamado pelo runtime");
    for (;;)
        ;
}

void exit(int status)
{
    (void)status;
    kernel_panic("exit() chamado pelo runtime");
    for (;;)
        ;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    unsigned long v = 0;
    int digit;
    int any = 0;

    while (isspace((unsigned char)*s))
        s++;
    if (*s == '+')
        s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0 && s[0] == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }
    for (;; s++) {
        if (isdigit((unsigned char)*s))
            digit = *s - '0';
        else if (isalpha((unsigned char)*s))
            digit = tolower((unsigned char)*s) - 'a' + 10;
        else
            break;
        if (digit >= base)
            break;
        if (v > (ULONG_MAX - (unsigned long)digit) / (unsigned long)base) {
            errno = ERANGE;
            v = ULONG_MAX;
        } else if (v != ULONG_MAX) {
            v = v * (unsigned long)base + (unsigned long)digit;
        }
        any = 1;
    }
    if (end != NULL)
        *end = (char *)(any ? s : (const char *)s);
    return v;
}

long strtol(const char *s, char **end, int base)
{
    int neg = 0;
    unsigned long v;
    const char *p = s;

    while (isspace((unsigned char)*p))
        p++;
    if (*p == '-') {
        neg = 1;
        p++;
    }
    v = strtoul(p, end, base);
    if (end != NULL && *end == p)
        *end = (char *)s;
    if (neg)
        return v > (unsigned long)LONG_MAX + 1ul ? LONG_MIN : (long)(0ul - v);
    return v > (unsigned long)LONG_MAX ? LONG_MAX : (long)v;
}

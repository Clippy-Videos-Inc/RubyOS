/* tests/host/test_libc.c - testes da libc minima do RubyOS (kernel/libc) no PC hospedeiro.
 * As funcoes sao compiladas com nomes rbl_* (tests/host/libc_rename.h). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>

#include "heap.h"
#include "kernel.h"

void *rbl_memchr(const void *s, int c, size_t n);
int rbl_strcmp(const char *a, const char *b);
int rbl_strncmp(const char *a, const char *b, size_t n);
char *rbl_strcpy(char *d, const char *s);
char *rbl_strncpy(char *d, const char *s, size_t n);
char *rbl_strcat(char *d, const char *s);
char *rbl_strncat(char *d, const char *s, size_t n);
char *rbl_strchr(const char *s, int c);
char *rbl_strrchr(const char *s, int c);
char *rbl_strstr(const char *h, const char *n);
long rbl_strtol(const char *s, char **end, int base);
unsigned long rbl_strtoul(const char *s, char **end, int base);
void *rbl_malloc(size_t n);
void *rbl_calloc(size_t n, size_t s);
void *rbl_realloc(void *p, size_t n);
void rbl_free(void *p);
int rbl_isdigit(int c);
int rbl_isalpha(int c);
int rbl_isspace(int c);
int rbl_isxdigit(int c);
int rbl_ispunct(int c);
int rbl_toupper(int c);
int rbl_tolower(int c);

static int failures;
static int checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

void kernel_panic(const char *msg)
{
    printf("  PANIC inesperado: %s\n", msg);
    exit(2);
}

static _Alignas(16) uint8_t arena[64 * 1024];

int main(void)
{
    char buf[32];
    char *end;

    heap_init(arena, sizeof(arena));

    CHECK(rbl_strcmp("abc", "abc") == 0);
    CHECK(rbl_strcmp("abc", "abd") < 0 && rbl_strcmp("b", "a") > 0 && rbl_strcmp("", "a") < 0);
    CHECK(rbl_strcmp("\xff", "a") > 0);                       /* compara como unsigned char */
    CHECK(rbl_strncmp("abcdef", "abcxyz", 3) == 0 && rbl_strncmp("abcdef", "abcxyz", 4) < 0);
    CHECK(rbl_strncmp("ab", "abc", 5) < 0 && rbl_strncmp("x", "y", 0) == 0);

    CHECK(strcmp(rbl_strcpy(buf, "ola"), "ola") == 0);
    memset(buf, 'x', sizeof(buf));
    rbl_strncpy(buf, "ab", 5);
    CHECK(buf[0] == 'a' && buf[1] == 'b' && buf[2] == 0 && buf[4] == 0 && buf[5] == 'x');
    rbl_strcpy(buf, "foo");
    rbl_strcat(buf, "bar");
    CHECK(strcmp(buf, "foobar") == 0);
    rbl_strncat(buf, "bazqux", 3);
    CHECK(strcmp(buf, "foobarbaz") == 0);

    CHECK(rbl_strchr("hello", 'l') == (char *)0 + 0 + (rbl_strchr("hello", 'l') - (char *)0) &&
          *rbl_strchr("hello", 'l') == 'l');
    CHECK(rbl_strchr("hello", 'z') == NULL && *rbl_strchr("hello", 0) == 0);
    CHECK(rbl_strrchr("hello", 'l') != NULL && strcmp(rbl_strrchr("hello", 'l'), "lo") == 0);
    CHECK(strcmp(rbl_strstr("um dois tres", "dois"), "dois tres") == 0);
    CHECK(rbl_strstr("abc", "") != NULL && rbl_strstr("abc", "abcd") == NULL && rbl_strstr("abc", "x") == NULL);
    CHECK(rbl_memchr("abc\0def", 'd', 7) != NULL && rbl_memchr("abc", 'z', 3) == NULL);

    CHECK(rbl_strtol("123", &end, 10) == 123 && *end == 0);
    CHECK(rbl_strtol("  -42x", &end, 10) == -42 && *end == 'x');
    CHECK(rbl_strtol("0x1F", &end, 16) == 31 && *end == 0);
    CHECK(rbl_strtol("0x1F", &end, 0) == 31 && rbl_strtol("017", &end, 0) == 15);
    CHECK(rbl_strtol("zz", &end, 36) == 35 * 36 + 35);
    CHECK(rbl_strtol("abc", &end, 10) == 0 && end[0] == 'a');       /* nada convertido: end = inicio */
    errno = 0;
    CHECK(rbl_strtoul("99999999999999999999999", &end, 10) == ULONG_MAX && errno == ERANGE);
    CHECK(rbl_strtol("2147483647", &end, 10) == 2147483647L);
    CHECK(rbl_strtoul("4294967295", &end, 10) == 4294967295UL);

    CHECK(rbl_isdigit('5') && !rbl_isdigit('a') && rbl_isalpha('Q') && !rbl_isalpha('1'));
    CHECK(rbl_isspace(' ') && rbl_isspace('\n') && rbl_isspace('\t') && !rbl_isspace('x'));
    CHECK(rbl_isxdigit('f') && rbl_isxdigit('A') && !rbl_isxdigit('g') && rbl_ispunct('!') && !rbl_ispunct('a'));
    CHECK(rbl_toupper('a') == 'A' && rbl_toupper('A') == 'A' && rbl_tolower('Z') == 'z' && rbl_tolower('1') == '1');

    {
        int *a = rbl_calloc(4, sizeof(int));
        CHECK(a != NULL && a[0] == 0 && a[3] == 0);
        a[2] = 77;
        a = rbl_realloc(a, 4096);
        CHECK(a != NULL && a[2] == 77);
        rbl_free(a);
        CHECK(rbl_malloc(0) == NULL);
        CHECK(heap_check());
    }

    if (failures == 0)
        printf("libc: %d verificacoes OK\n", checks);
    else
        printf("libc: %d de %d verificacoes falharam\n", failures, checks);
    return failures == 0 ? 0 : 1;
}

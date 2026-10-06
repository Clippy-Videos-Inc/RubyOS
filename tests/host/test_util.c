/* tests/host/test_util.c - testes de kernel/util.c no PC hospedeiro (sem QEMU).
 *
 * Compilar e rodar:
 *   gcc -Wall -Wextra -I kernel tests/host/test_util.c kernel/util.c -o build/test_util && build/test_util
 * (ou simplesmente: ruby tests/run_tests.rb) */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "util.h"

static int failures;
static int checks;

#define CHECK(cond)                                                     \
    do {                                                                \
        checks++;                                                       \
        if (!(cond)) {                                                  \
            printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                 \
        }                                                               \
    } while (0)

static void test_utoa(void)
{
    char buf[40];

    CHECK(strcmp(k_utoa(0, buf, 10), "0") == 0);
    CHECK(strcmp(k_utoa(7, buf, 10), "7") == 0);
    CHECK(strcmp(k_utoa(1234567890u, buf, 10), "1234567890") == 0);
    CHECK(strcmp(k_utoa(4294967295u, buf, 10), "4294967295") == 0);
    CHECK(strcmp(k_utoa(255, buf, 16), "ff") == 0);
    CHECK(strcmp(k_utoa(0xDEADBEEFu, buf, 16), "deadbeef") == 0);
    CHECK(strcmp(k_utoa(5, buf, 2), "101") == 0);
    CHECK(strcmp(k_utoa(5, buf, 1), "") == 0);     /* base invalida */
    CHECK(strcmp(k_utoa(5, buf, 17), "") == 0);    /* base invalida */
}

static void test_itoa(void)
{
    char buf[40];

    CHECK(strcmp(k_itoa(0, buf, 10), "0") == 0);
    CHECK(strcmp(k_itoa(42, buf, 10), "42") == 0);
    CHECK(strcmp(k_itoa(-42, buf, 10), "-42") == 0);
    CHECK(strcmp(k_itoa(INT32_MAX, buf, 10), "2147483647") == 0);
    CHECK(strcmp(k_itoa(INT32_MIN, buf, 10), "-2147483648") == 0);
    CHECK(strcmp(k_itoa(-255, buf, 16), "-ff") == 0);
}

static void test_strings(void)
{
    CHECK(k_strlen("") == 0);
    CHECK(k_strlen("RubyOS") == 6);
    CHECK(k_streq("help", "help"));
    CHECK(!k_streq("help", "hel"));
    CHECK(!k_streq("hel", "help"));
    CHECK(!k_streq("a", "b"));
    CHECK(k_streq("", ""));
}

static void test_parse_uint(void)
{
    uint32_t v = 99;

    CHECK(k_parse_uint("0", &v) && v == 0);
    CHECK(k_parse_uint("123", &v) && v == 123);
    CHECK(k_parse_uint("4294967295", &v) && v == 4294967295u);
    CHECK(!k_parse_uint("4294967296", &v));    /* estouro */
    CHECK(!k_parse_uint("99999999999", &v));   /* estouro */
    CHECK(!k_parse_uint("", &v));
    CHECK(!k_parse_uint("12a", &v));
    CHECK(!k_parse_uint("-1", &v));
    CHECK(!k_parse_uint(" 1", &v));
}

static void test_tokenize(void)
{
    char *argv[8];
    char l1[] = "echo  hello   world";
    char l2[] = "echo \"a b\" c";
    char l3[] = "   ";
    char l4[] = "";
    char l5[] = "\tls\t/home ";
    char l6[] = "a b c d e f";
    char l7[] = "echo \"sem fechar";
    int n;

    n = k_tokenize(l1, argv, 8);
    CHECK(n == 3);
    CHECK(strcmp(argv[0], "echo") == 0 && strcmp(argv[1], "hello") == 0 && strcmp(argv[2], "world") == 0);

    n = k_tokenize(l2, argv, 8);
    CHECK(n == 3);
    CHECK(strcmp(argv[1], "a b") == 0 && strcmp(argv[2], "c") == 0);

    CHECK(k_tokenize(l3, argv, 8) == 0);
    CHECK(k_tokenize(l4, argv, 8) == 0);

    n = k_tokenize(l5, argv, 8);
    CHECK(n == 2 && strcmp(argv[0], "ls") == 0 && strcmp(argv[1], "/home") == 0);

    CHECK(k_tokenize(l6, argv, 3) == 3);       /* respeita o limite */

    n = k_tokenize(l7, argv, 8);
    CHECK(n == 2 && strcmp(argv[1], "sem fechar") == 0);
}

int main(void)
{
    test_utoa();
    test_itoa();
    test_strings();
    test_parse_uint();
    test_tokenize();

    if (failures == 0)
        printf("util: %d verificacoes OK\n", checks);
    else
        printf("util: %d de %d verificacoes falharam\n", failures, checks);
    return failures == 0 ? 0 : 1;
}

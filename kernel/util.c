/* kernel/util.c - funcoes puras do kernel (ver util.h) */
#include "util.h"

char *k_utoa(uint32_t v, char *buf, unsigned base)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[33];
    int i = 0;
    int j = 0;

    if (base < 2 || base > 16) {
        buf[0] = '\0';
        return buf;
    }
    if (v == 0)
        tmp[i++] = '0';
    while (v > 0) {
        tmp[i++] = digits[v % base];
        v /= base;
    }
    while (i > 0)
        buf[j++] = tmp[--i];
    buf[j] = '\0';
    return buf;
}

char *k_itoa(int32_t v, char *buf, unsigned base)
{
    if (v < 0) {
        buf[0] = '-';
        k_utoa(0u - (uint32_t)v, buf + 1, base);
    } else {
        k_utoa((uint32_t)v, buf, base);
    }
    return buf;
}

size_t k_strlen(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0')
        n++;
    return n;
}

bool k_streq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

bool k_parse_uint(const char *s, uint32_t *out)
{
    uint32_t v = 0;

    if (*s == '\0')
        return false;
    for (; *s != '\0'; s++) {
        uint32_t d;
        if (*s < '0' || *s > '9')
            return false;
        d = (uint32_t)(*s - '0');
        if (v > (0xFFFFFFFFu - d) / 10u)
            return false;
        v = v * 10u + d;
    }
    *out = v;
    return true;
}

int k_tokenize(char *line, char **argv, int max)
{
    int argc = 0;
    char *p = line;

    while (*p != '\0' && argc < max) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            while (*p != '\0' && *p != '"')
                p++;
            if (*p == '"')
                *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p != '\0' && *p != ' ' && *p != '\t')
                p++;
            if (*p != '\0')
                *p++ = '\0';
        }
    }
    return argc;
}

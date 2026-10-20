/* kernel/libc/string.c - complemento de string para o runtime Ruby (mruby).
 * memcpy/memmove/memset/memcmp/strlen ja vivem em kernel/kstring.c. */
#include <string.h>

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;

    for (; n != 0; n--, p++) {
        if (*p == (unsigned char)c)
            return (void *)p;
    }
    return NULL;
}

int strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n != 0; n--, a++, b++) {
        if (*a != *b)
            return (int)(unsigned char)*a - (int)(unsigned char)*b;
        if (*a == '\0')
            return 0;
    }
    return 0;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;

    while ((*d++ = *src++) != '\0')
        ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i;

    for (i = 0; i < n && src[i] != '\0'; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src)
{
    strcpy(dst + strlen(dst), src);
    return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *d = dst + strlen(dst);

    for (; n != 0 && *src != '\0'; n--)
        *d++ = *src++;
    *d = '\0';
    return dst;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (*s == '\0')
            return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;

    for (;; s++) {
        if (*s == (char)c)
            last = s;
        if (*s == '\0')
            return (char *)last;
    }
}

char *strstr(const char *hay, const char *needle)
{
    size_t n = strlen(needle);

    if (n == 0)
        return (char *)hay;
    for (; *hay != '\0'; hay++) {
        if (*hay == *needle && strncmp(hay, needle, n) == 0)
            return (char *)hay;
    }
    return NULL;
}

char *strerror(int errnum)
{
    (void)errnum;
    return (char *)"erro";
}

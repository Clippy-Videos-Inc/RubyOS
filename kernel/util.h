/* kernel/util.h - funcoes puras (sem hardware): conversao de numeros e tokenizacao.
 * Por nao dependerem do hardware, sao testadas no PC hospedeiro (tests/host). */
#ifndef RUBYOS_UTIL_H
#define RUBYOS_UTIL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Escreve v em buf na base 2..16 (minusculas). Retorna buf. buf precisa de 33+ bytes. */
char *k_utoa(uint32_t v, char *buf, unsigned base);

/* Igual a k_utoa, mas aceita sinal (usa '-' para negativos). buf precisa de 34+ bytes. */
char *k_itoa(int32_t v, char *buf, unsigned base);

size_t k_strlen(const char *s);
bool k_streq(const char *a, const char *b);

/* Converte uma string decimal para uint32_t. Retorna false se vazia, com
 * caracteres invalidos ou se estourar 32 bits. */
bool k_parse_uint(const char *s, uint32_t *out);

/* Divide line em argumentos separados por espaco/tab, in-place (escreve '\0'
 * nos separadores). Texto entre aspas duplas forma um unico argumento.
 * Retorna argc (no maximo max). */
int k_tokenize(char *line, char **argv, int max);

#endif

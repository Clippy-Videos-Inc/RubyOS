/* tests/host/libc_rename.h - usado com "gcc -include": renomeia as funcoes da libc minima do
 * RubyOS (os arquivos de kernel/libc) para que possam ser testadas no PC sem colidir com a libc do
 * hospedeiro. Nao faz parte do kernel. */
#ifndef RUBYOS_TEST_LIBC_RENAME_H
#define RUBYOS_TEST_LIBC_RENAME_H
#define memchr rbl_memchr
#define strcmp rbl_strcmp
#define strncmp rbl_strncmp
#define strcpy rbl_strcpy
#define strncpy rbl_strncpy
#define strcat rbl_strcat
#define strncat rbl_strncat
#define strchr rbl_strchr
#define strrchr rbl_strrchr
#define strstr rbl_strstr
#define strerror rbl_strerror
#define strtol rbl_strtol
#define strtoul rbl_strtoul
#define abs rbl_abs
#define malloc rbl_malloc
#define calloc rbl_calloc
#define realloc rbl_realloc
#define free rbl_free
#define abort rbl_abort
#define exit rbl_exit
#define isdigit rbl_isdigit
#define isupper rbl_isupper
#define islower rbl_islower
#define isalpha rbl_isalpha
#define isalnum rbl_isalnum
#define isspace rbl_isspace
#define isxdigit rbl_isxdigit
#define isprint rbl_isprint
#define iscntrl rbl_iscntrl
#define ispunct rbl_ispunct
#define toupper rbl_toupper
#define tolower rbl_tolower
#endif

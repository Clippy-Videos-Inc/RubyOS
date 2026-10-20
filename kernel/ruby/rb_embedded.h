/* kernel/ruby/rb_embedded.h - formato da tabela gerada por tools/embed.rb */
#ifndef RUBYOS_RB_EMBEDDED_H
#define RUBYOS_RB_EMBEDDED_H

#include <stdint.h>

struct rb_embedded_file {
    const char *path;            /* caminho absoluto no ramfs, ex.: "/apps/calc.rb" */
    const unsigned char *data;
    uint32_t size;
};

extern const struct rb_embedded_file rb_embedded_files[];
extern const uint32_t rb_embedded_count;

#endif

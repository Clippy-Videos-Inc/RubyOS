#!/usr/bin/env python3
# tools/gen_icons.py - gera kernel/gfx/icons_data.c a partir dos PNGs em assets/icons/.
#
# Ferramenta de desenvolvimento, usada so quando os icones mudam (NAO faz parte do build
# normal: o .c gerado ja fica versionado no repositorio, entao compilar o RubyOS nunca
# precisa de Python nem da Pillow). Uso:
#
#   python3 tools/gen_icons.py
#
# Cada icone vira um array de pixels uint32_t no formato 0xAARRGGBB (alfa reto, 8 bits por
# canal) e uma struct gfx_icon (ver kernel/gfx/icons.h) com a largura e a altura.

import os
import sys

try:
    from PIL import Image
except ImportError:
    print("Este script precisa da Pillow: pip install --break-system-packages Pillow", file=sys.stderr)
    sys.exit(1)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICONS_DIR = os.path.join(ROOT, 'assets', 'icons')
OUT_C = os.path.join(ROOT, 'kernel', 'gfx', 'icons_data.c')

# (nome do simbolo C, arquivo PNG, tamanho final em pixels)
ICONS = [
    ('calculator', 'calculator.png', 32),
    ('computer', 'computer.png', 32),
    ('cursor', 'cursor.png', 32),
    ('directory', 'directory.png', 32),
    ('ruby_start', 'ruby-start.png', 32),
]


def load_icon(path, size):
    img = Image.open(path).convert('RGBA')
    if img.size != (size, size):
        img = img.resize((size, size), Image.LANCZOS)
    pixels = []
    for y in range(size):
        for x in range(size):
            r, g, b, a = img.getpixel((x, y))
            pixels.append((a << 24) | (r << 16) | (g << 8) | b)
    return pixels


def main():
    lines = []
    lines.append('/* kernel/gfx/icons_data.c - dados dos icones embutidos no kernel.')
    lines.append(' *')
    lines.append(' * GERADO por tools/gen_icons.py a partir dos PNGs em assets/icons/ -- nao edite a mao.')
    lines.append(' * Cada pixel e 0xAARRGGBB (alfa reto, 8 bits por canal, veja kernel/gfx/icons.h). */')
    lines.append('#include "icons.h"')
    lines.append('')

    entries = []
    for sym, filename, size in ICONS:
        path = os.path.join(ICONS_DIR, filename)
        pixels = load_icon(path, size)
        arr_name = f'g_icon_{sym}_pixels'
        lines.append(f'static const uint32_t {arr_name}[{size * size}] = {{')
        for row in range(size):
            row_vals = pixels[row * size:(row + 1) * size]
            lines.append('    ' + ', '.join(f'0x{v:08X}u' for v in row_vals) + ',')
        lines.append('};')
        lines.append('')
        entries.append((sym, arr_name, size))

    lines.append('/* Tabela publica: ver gfx_icon_get() em icons.h. */')
    for sym, arr_name, size in entries:
        lines.append(f'const struct gfx_icon g_icon_{sym} = {{ {size}u, {size}u, {arr_name} }};')
    lines.append('')

    with open(OUT_C, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print(f'escrito: {OUT_C} ({len(entries)} icones)')


if __name__ == '__main__':
    main()

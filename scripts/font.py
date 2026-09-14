#!/usr/bin/env python3
"""Regenerate the checked-in 8x16 font using FreeType and DejaVu Sans Mono."""
import ctypes as c
from pathlib import Path

class Generic(c.Structure):
    _fields_ = [('data', c.c_void_p), ('finalizer', c.c_void_p)]
class Vector(c.Structure):
    _fields_ = [('x', c.c_long), ('y', c.c_long)]
class Bitmap(c.Structure):
    _fields_ = [('rows', c.c_uint), ('width', c.c_uint), ('pitch', c.c_int),
                ('buffer', c.POINTER(c.c_ubyte)), ('grays', c.c_ushort),
                ('mode', c.c_ubyte), ('palette_mode', c.c_ubyte), ('palette', c.c_void_p)]
class Slot(c.Structure):
    _fields_ = [('library', c.c_void_p), ('face', c.c_void_p), ('next', c.c_void_p),
                ('index', c.c_uint), ('generic', Generic), ('metrics', c.c_long * 8),
                ('linear_hori', c.c_long), ('linear_vert', c.c_long), ('advance', Vector),
                ('format', c.c_uint), ('bitmap', Bitmap), ('left', c.c_int), ('top', c.c_int)]
class Face(c.Structure):
    _fields_ = [('num_faces', c.c_long), ('index', c.c_long), ('flags', c.c_long),
                ('style_flags', c.c_long), ('num_glyphs', c.c_long), ('family', c.c_char_p),
                ('style', c.c_char_p), ('num_sizes', c.c_int), ('sizes', c.c_void_p),
                ('num_charmaps', c.c_int), ('charmaps', c.c_void_p), ('generic', Generic),
                ('bbox', c.c_long * 4), ('units', c.c_ushort), ('ascender', c.c_short),
                ('descender', c.c_short), ('height', c.c_short), ('max_width', c.c_short),
                ('max_height', c.c_short), ('underline_pos', c.c_short), ('underline_thickness', c.c_short),
                ('glyph', c.POINTER(Slot))]

ft = c.CDLL('libfreetype.so.6')
lib = c.c_void_p()
face = c.POINTER(Face)()
assert ft.FT_Init_FreeType(c.byref(lib)) == 0
ft.FT_New_Face.argtypes = [c.c_void_p, c.c_char_p, c.c_long, c.c_void_p]
assert ft.FT_New_Face(lib, b'/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', 0, c.byref(face)) == 0
assert ft.FT_Set_Pixel_Sizes(face, 0, 14) == 0
ft.FT_Load_Char.argtypes = [c.c_void_p, c.c_ulong, c.c_int32]
rows = []
for code in range(128):
    assert ft.FT_Load_Char(face, code if code >= 32 else 32, 4 | 4096 | (2 << 16)) == 0
    glyph = face.contents.glyph.contents
    bitmap = glyph.bitmap
    assert bitmap.mode == 1
    pixels = [0] * 16
    for y in range(bitmap.rows):
        for x in range(bitmap.width):
            dx, dy = glyph.left + x, 12 - glyph.top + y
            if 0 <= dx < 8 and 0 <= dy < 16 and bitmap.buffer[y * bitmap.pitch + x // 8] & (0x80 >> (x % 8)):
                pixels[dy] |= 0x80 >> dx
    rows.append('    {' + ','.join(f'0x{row:02x}' for row in pixels) + '},')
Path('kernel/font.hpp').write_text('// Taha Mono bitmap, derived from DejaVu Sans Mono. See assets/FONT-LICENSE.txt.\n'
    '#pragma once\n#include <stdint.h>\ninline constexpr uint8_t font[128][16] = {\n' + '\n'.join(rows) + '\n};\n')
ft.FT_Done_Face(face)
ft.FT_Done_FreeType(lib)

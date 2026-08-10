#!/usr/bin/env python3
"""Synthesize a .fontdat and its atlas, so the generator can be tested.

The shipped fonts cannot be checked into this repository - they are Raven's -
and a font tool with no font to run on is a tool nobody knows is broken. This
builds a stand-in from any TrueType face on the machine, in exactly the layout
qcommon/qfiles.h describes, with the conventions the engine reads:

    width, height   the glyph's own pixels
    horizAdvance    what the pen moves by
    horizOffset     x of the left edge, relative to the pen
    baseline        y of the top edge, measured UP from the drawing line -
                    tr_font.cpp draws at (foy - baseline), so a capital has a
                    positive baseline and a comma's is near nought
    s, t, s2, t2    the rectangle in the atlas, as texture coordinates

Getting that sign wrong is not a hypothetical: it is what the first version of
this fixture did, and the atlas built from it was correct while the preview
drew every letter off the bottom of the image.
"""

import argparse
import os
import struct
import sys

import numpy as np

try:
    import freetype
except ImportError:
    sys.stderr.write("this needs freetype-py: pip install freetype-py\n")
    raise

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("this needs pillow: pip install pillow\n")
    raise


GLYPH_COUNT = 256
GLYPH_FMT = '<4hi4f'
GLYPH_SIZE = struct.calcsize(GLYPH_FMT)


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--font', required=True, help='any .ttf on the machine')
    p.add_argument('--size', type=int, default=24, help='point size to rasterise at')
    p.add_argument('--atlas', type=int, default=512)
    p.add_argument('--out', required=True, help='path of the .fontdat; the image goes beside it')
    args = p.parse_args(argv[1:])

    face = freetype.Face(args.font)
    face.set_pixel_sizes(0, args.size)

    aw = args.atlas
    cells = []
    x = y = shelf = 0
    max_h = 0

    for cp in range(GLYPH_COUNT):
        index = face.get_char_index(cp)
        if cp < 32 or index == 0:
            cells.append(None)
            continue

        face.load_char(chr(cp), freetype.FT_LOAD_RENDER)
        g = face.glyph
        bmp = g.bitmap
        w, h = bmp.width, bmp.rows
        advance = g.advance.x >> 6

        buf = (np.array(bmp.buffer, dtype=np.uint8).reshape(h, bmp.pitch)[:, :w]
               if w and h else np.zeros((0, 0), dtype=np.uint8))

        if w and h:
            if x + w > aw:
                x = 0
                y += shelf
                shelf = 0
            cells.append({'cp': cp, 'w': w, 'h': h, 'x': x, 'y': y,
                          'advance': advance, 'xoff': g.bitmap_left,
                          'baseline': g.bitmap_top, 'bmp': buf})
            x += w
            shelf = max(shelf, h)
            max_h = max(max_h, h)
        else:
            cells.append({'cp': cp, 'w': 0, 'h': 0, 'x': 0, 'y': 0,
                          'advance': advance, 'xoff': 0, 'baseline': 0, 'bmp': buf})

    ah = 1
    while ah < y + shelf:
        ah *= 2

    atlas = np.zeros((ah, aw), dtype=np.uint8)
    for c in cells:
        if c and c['w']:
            atlas[c['y']:c['y'] + c['h'], c['x']:c['x'] + c['w']] = c['bmp']

    image_path = os.path.splitext(args.out)[0] + '.png'
    Image.fromarray(atlas, 'L').save(image_path)

    out = bytearray()
    for cp in range(GLYPH_COUNT):
        c = cells[cp]
        if c is None:
            out += struct.pack(GLYPH_FMT, 0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0)
            continue
        out += struct.pack(GLYPH_FMT,
                           c['w'], c['h'], c['advance'], c['xoff'], c['baseline'],
                           c['x'] / float(aw), c['y'] / float(ah),
                           (c['x'] + c['w']) / float(aw), (c['y'] + c['h']) / float(ah))

    ascender = face.size.ascender >> 6
    descender = -(face.size.descender >> 6)
    out += struct.pack('<5h', args.size, max_h, ascender, descender, 0)

    with open(args.out, 'wb') as f:
        f.write(bytes(out))

    print("%s (%d bytes) + %s (%dx%d)"
          % (args.out, len(out), image_path, aw, ah))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

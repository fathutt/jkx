#!/usr/bin/env python3
"""The biggest jump between two neighbouring pixels inside a shape.

This is the measurement a seam needs and a colour count cannot give. A hard edge
in shading is not a colour that is present or absent - both sides of it are the
same material lit differently - it is a STEP between two pixels that ought to be
a gradient. So the question is not "which colours are here" but "how far apart
are any two pixels that touch".

Only pixels inside the shape are compared. A model against a background has a
huge step at its outline and always will; that is the silhouette, not a seam. So
a pixel counts only when it and its neighbour are both part of the shape, and
what "part of the shape" means is given as a colour test rather than guessed:
the fixture lights its model through the light grid, which is red, so the model's
pixels are the ones where red clearly leads.

    tga_max_step.py <shot.tga> --dominant r --min 40 [--max-step N]

  --dominant   which channel has to lead for a pixel to be part of the shape
  --min        by how much it has to lead the others
  --max-step   fail if the biggest step is at least this. Without it the number
               is printed and nothing is asserted, which is the right mode when
               establishing what the number is.

Targa stores blue, green, red. That is handled here, and it matters: a channel
mix-up would silently select the background instead of the model and then
measure the steps in a flat grey rectangle, which are zero, and pass.
"""

import struct
import sys


def read_tga(path):
    with open(path, "rb") as f:
        data = f.read()

    if len(data) < 18:
        raise ValueError("%s is too short to be a Targa" % path)

    id_len = data[0]
    bpp = data[16]
    descriptor = data[17]
    width, height = struct.unpack_from("<HH", data, 12)

    if bpp not in (24, 32):
        raise ValueError("%s is %d bits per pixel, expected 24 or 32" % (path, bpp))

    stride = bpp // 8
    start = 18 + id_len
    top_down = bool(descriptor & 0x20)

    rows = []
    for y in range(height):
        source = y if top_down else (height - 1 - y)
        at = start + source * width * stride
        row = []
        for x in range(width):
            p = at + x * stride
            row.append((data[p + 2], data[p + 1], data[p]))   # BGR on disk
        rows.append(row)

    return width, height, rows


CHANNEL = {"r": 0, "g": 1, "b": 2}


def in_shape(pixel, lead, margin):
    others = [pixel[i] for i in range(3) if i != lead]
    return all(pixel[lead] - o >= margin for o in others)


def main():
    argv = sys.argv[1:]

    if not argv:
        print("usage: tga_max_step.py <shot.tga> --dominant r --min 40 [--max-step N]",
              file=sys.stderr)
        return 2

    path = argv[0]
    lead = CHANNEL["r"]
    margin = 40
    limit = None

    i = 1
    while i < len(argv):
        if argv[i] == "--dominant" and i + 1 < len(argv):
            lead = CHANNEL[argv[i + 1]]
            i += 2
        elif argv[i] == "--min" and i + 1 < len(argv):
            margin = int(argv[i + 1])
            i += 2
        elif argv[i] == "--max-step" and i + 1 < len(argv):
            limit = int(argv[i + 1])
            i += 2
        else:
            print("unknown argument: %s" % argv[i], file=sys.stderr)
            return 2

    width, height, rows = read_tga(path)

    worst = 0
    where = None
    counted = 0

    for y in range(height):
        for x in range(width - 1):
            a = rows[y][x]
            b = rows[y][x + 1]

            if not in_shape(a, lead, margin) or not in_shape(b, lead, margin):
                continue

            counted += 1
            step = max(abs(a[c] - b[c]) for c in range(3))

            if step > worst:
                worst = step
                where = (x, y)

    if counted == 0:
        print("%s: nothing in the picture is that colour, so no step was measured"
              % path, file=sys.stderr)
        return 1

    print("%s: %d neighbouring pair(s) inside the shape, biggest step %d%s"
          % (path, counted, worst,
             (" at %d,%d" % where) if where else ""))

    if limit is not None and worst >= limit:
        print("%s: a step of %d is a visible seam; the limit here is %d"
              % (path, worst, limit), file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Where on the screen a colour is, not just whether it is there.

tga_has_colour.py answers "did this get drawn". That is the right question for a
head-up display and the wrong one for a sky: a sky face drawn upside down, or
mirrored, or on the wrong axis, contains exactly the same pixels as one drawn
correctly. What changes is where they are.

So this reports, for each colour asked about, how many pixels carry it and where
their centre of mass is, in fractions of the image - 0,0 top left, 1,1 bottom
right - and optionally asserts that the centre falls inside a box.

    tga_colour_where.py <shot.tga> R,G,B [R,G,B ...]
    tga_colour_where.py <shot.tga> R,G,B@x0,y0,x1,y1 [...]

The second form is the assertion: the colour must be present and its centroid
must land inside the given box. A colour that is absent fails either way, which
is deliberate - "not drawn at all" and "drawn in the wrong place" are the same
kind of wrong here.

Targa stores blue, green, red; that is handled, and it matters more than usual
because the fixture's sky colours are chosen so a channel swap turns one face's
colour into another's.
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
    top_down = bool(descriptor & 0x20)
    return width, height, data, 18 + id_len, bpp // 8, top_down


def parse_spec(spec):
    box = None
    if "@" in spec:
        spec, _, boxpart = spec.partition("@")
        x0, y0, x1, y1 = (float(v) for v in boxpart.split(","))
        box = (x0, y0, x1, y1)
    r, g, b = (int(v) for v in spec.split(","))
    return (r, g, b), box


def main(argv):
    if len(argv) < 3:
        print("usage: %s <shot.tga> R,G,B[@x0,y0,x1,y1] ..." % argv[0],
              file=sys.stderr)
        return 2

    path = argv[1]
    wanted = [parse_spec(a) for a in argv[2:]]

    width, height, data, offset, stride, top_down = read_tga(path)

    stats = {c: [0, 0.0, 0.0] for c, _ in wanted}
    for row in range(height):
        # Targa is bottom-up unless the descriptor says otherwise; the fraction
        # reported is in screen terms, so flip when it is not.
        y = row if top_down else (height - 1 - row)
        base = offset + row * width * stride
        for x in range(width):
            i = base + x * stride
            pixel = (data[i + 2], data[i + 1], data[i])
            s = stats.get(pixel)
            if s is not None:
                s[0] += 1
                s[1] += x
                s[2] += y

    failures = []
    for colour, box in wanted:
        count, sx, sy = stats[colour]
        if count == 0:
            print("%s: rgb%s absent" % (path, colour))
            if box:
                failures.append("%s: rgb%s is not in the picture at all"
                                % (path, colour))
            continue

        cx = (sx / count) / max(width - 1, 1)
        cy = (sy / count) / max(height - 1, 1)
        print("%s: rgb%s x%d, centre %.3f,%.3f" % (path, colour, count, cx, cy))

        if box:
            x0, y0, x1, y1 = box
            if not (x0 <= cx <= x1 and y0 <= cy <= y1):
                failures.append(
                    "%s: rgb%s centres at %.3f,%.3f, wanted inside %.2f,%.2f-%.2f,%.2f"
                    % (path, colour, cx, cy, x0, y0, x1, y1))

    for f in failures:
        print(f, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

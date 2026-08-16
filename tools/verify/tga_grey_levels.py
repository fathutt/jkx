#!/usr/bin/env python3
"""Report the distribution of grey levels in a frame.

Written for one question the bench could not ask: is a lit surface CLIPPING?

Every other picture check here asks whether a colour is present and where. That
is the right question for a flat fill and the wrong one for shading, because a
surface whose light has run past the top of the range still contains the colour
it was meant to have - it just contains it everywhere, with the gradient gone.
"the model is white" and "the model is white in the middle and shaded at the
edge" both pass a check for white.

So this counts levels rather than colours. A surface lit by
`ambient + directed * NdotL` and drawn against a white albedo puts the light
value itself on the screen, so the histogram of its greys IS the lighting
calculation, read back. If the top of it is a wall at 255 rather than a tail,
the sum went over one before it was written.

Grey pixels only, and that is the selection: this fixture's interface is
coloured on purpose - the background is blue, the panel is orange - so r==g==b
picks out the white-lit model and nothing else. Pure black is dropped with them
because the fitted frame's margins are black and there are a great many of them.

    tga_grey_levels.py <shot.tga> [--min-pixels N] [--max-clipped N]

With neither option it prints and exits zero: it is a measurement first. The
options turn it into a gate once there is a number worth gating on.
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
    width, height = struct.unpack_from("<HH", data, 12)
    if bpp not in (24, 32):
        raise ValueError("%s is %d bits per pixel, expected 24 or 32" % (path, bpp))
    return width, height, data, 18 + id_len, bpp // 8


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2

    path = argv[1]
    min_pixels = 0
    max_clipped = None

    i = 2
    while i < len(argv):
        if argv[i] == "--min-pixels":
            min_pixels = int(argv[i + 1])
            i += 2
        elif argv[i] == "--max-clipped":
            max_clipped = int(argv[i + 1])
            i += 2
        else:
            sys.stderr.write("unknown option %s\n" % argv[i])
            return 2

    width, height, data, off, stride = read_tga(path)

    counts = [0] * 256
    total = 0
    for p in range(width * height):
        base = off + p * stride
        b = data[base]
        g = data[base + 1]
        r = data[base + 2]
        if r != g or g != b:
            continue
        if r == 0:
            continue
        counts[r] += 1
        total += 1

    if total == 0:
        print("%s: no grey pixels at all" % path)
        return 1 if min_pixels else 0

    levels = [v for v in range(256) if counts[v]]
    clipped = counts[255]
    top = sorted(range(256), key=lambda v: counts[v], reverse=True)[:5]

    print("%s: %d grey pixel(s), %d distinct level(s), range %d..%d, "
          "%d at 255 (%.1f%%)"
          % (path, total, len(levels), levels[0], levels[-1],
             clipped, 100.0 * clipped / total))
    print("  most common: %s"
          % ", ".join("%d x%d" % (v, counts[v]) for v in top if counts[v]))

    rc = 0
    if total < min_pixels:
        print("  FAIL: wanted at least %d grey pixel(s)" % min_pixels)
        rc = 1
    if max_clipped is not None and clipped > max_clipped:
        print("  FAIL: %d pixel(s) at 255, wanted at most %d - the lighting sum "
              "is running past the top of the range and the shading is gone "
              "where it does" % (clipped, max_clipped))
        rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))

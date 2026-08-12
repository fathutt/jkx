#!/usr/bin/env python3
"""Six sky faces, each one identifiable on sight.

A skybox is six images and a set of conventions about which way up each of them
goes. The conventions are not written down anywhere in the engine; they are
implied by a table of axis swaps in tr_sky.cpp and by whatever the original
artists happened to do. Anyone changing how the sky is sampled - to a cubemap,
say - is changing exactly those conventions, and a flat blue sky cannot tell
them whether they got it right.

So these faces are not flat. Each carries:

  - its own base colour, so which face is on screen is a colour count;
  - a marker square in one corner, in a second colour, so which way the face is
    rotated is a position;
  - a stripe along one edge, so a mirrored face is not mistaken for a rotated
    one - a mirror and a rotation can put the marker in the same corner.

The base colours are far apart in RGB and none of them is a colour the fixture
uses elsewhere. The marker is white and the stripe is black, which no base
colour comes near.

    make_test_sky.py <dir> <basename>     writes <basename>_{rt,bk,lf,ft,up,dn}.tga

The suffix order is ParseSkyParms's. Which face you actually see looking in a
given direction is NOT that order: DrawSkyBox indexes the images through
sky_texorder = { 0, 2, 1, 3, 4, 5 }, so the second and third are swapped. Read
off the code and then confirmed by looking at the picture:

    +X -> rt    -X -> lf    +Y -> bk    -Y -> ft    +Z -> up    -Z -> dn

so the fixture's player, who starts at the origin looking along +Y, sees "bk".
Which is the sort of thing this fixture exists to pin down: the first guess here
was that the suffixes lined up with the axes, and the picture said otherwise.
"""

import os
import struct
import sys

SIZE = 64                   # power of two: R_FindImageFile refuses anything else
MARKER = 12                 # side of the corner square
STRIPE = 6                  # thickness of the edge stripe

# suffix, base colour, and the direction you have to look to see it
FACES = [
    ("rt", (204, 0, 0)),      # +X
    ("bk", (0, 153, 0)),      # +Y   <- straight ahead in the fixture
    ("lf", (0, 51, 255)),     # -X
    ("ft", (255, 204, 0)),    # -Y
    ("up", (153, 0, 204)),    # +Z
    ("dn", (0, 204, 204)),    # -Z
]

WHITE = (255, 255, 255)
BLACK = (0, 0, 0)


def face_pixels(base):
    """Base colour, a white square in the top-left, a black stripe down the left.

    Top-left and left are in image space - row 0 is the top row of the picture
    as an artist sees it. Where that lands on screen is the question the bench
    asks; it is not assumed here.
    """
    rows = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            if x < MARKER and y < MARKER:
                row.append(WHITE)
            elif x < STRIPE:
                row.append(BLACK)
            else:
                row.append(base)
        rows.append(row)
    return rows


def write_tga(path, rows):
    """Uncompressed 24-bit Targa, bottom-up.

    Targa stores blue, green, red. Getting that backwards produces an image
    that looks plausible and tests the wrong thing, which is why the colours
    above are chosen so that a channel swap is obvious: (204,0,0) read
    backwards is (0,0,204), and the bench asks for both.
    """
    header = struct.pack("<3B2HB4H2B",
                         0,      # no id field
                         0,      # no colour map
                         2,      # uncompressed true colour
                         0, 0, 0,
                         0, 0, SIZE, SIZE,
                         24, 0)  # bits per pixel, descriptor: origin at bottom

    body = bytearray()
    for y in range(SIZE - 1, -1, -1):       # bottom-up
        for r, g, b in rows[y]:
            body += bytes((b, g, r))

    with open(path, "wb") as f:
        f.write(header)
        f.write(bytes(body))


def main(argv):
    if len(argv) < 3:
        print("usage: %s <dir> <basename>" % argv[0], file=sys.stderr)
        return 2

    directory, base = argv[1], argv[2]
    os.makedirs(directory, exist_ok=True)

    for suffix, colour in FACES:
        path = os.path.join(directory, "%s_%s.tga" % (base, suffix))
        write_tga(path, face_pixels(colour))
        print("%s: rgb%s" % (path, colour))

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

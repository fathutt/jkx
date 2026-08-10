#!/usr/bin/env python3
"""Is this screenshot a picture, or one flat colour?

A frame that drew nothing at all still gets presented and still writes a file,
so "the screenshot exists" says almost nothing. Counting how many distinct
colours are in it separates a frame that drew from a frame that cleared.

With a threshold given, it separates rather more. Antialiased text is mostly
partial coverage - a page of it is hundreds of greys - so a console frame that
clears the requested number has drawn glyphs and not just a background panel.
That is what makes the second screenshot in the smoke test a check on the font
path rather than a check that a rectangle was filled.

    tga_is_a_picture.py <file.tga> [minimum distinct colours]

Deliberately no image library: this reads the one format the engine writes -
uncompressed 24-bit bottom-up TGA - and nothing else, so the smoke test does not
need a package installed to say whether the renderer drew.
"""

import sys

# Two colours is a picture; one is a clear. The margin is for a screenshot whose
# only content is a single anti-aliased edge, which is still more than nothing.
MIN_DISTINCT_COLOURS = 2


def read_tga(path):
    with open(path, "rb") as f:
        data = f.read()

    if len(data) < 18:
        raise ValueError("shorter than a TGA header")

    id_length = data[0]
    colour_map_type = data[1]
    image_type = data[2]
    width = data[12] | (data[13] << 8)
    height = data[14] | (data[15] << 8)
    depth = data[16]

    if colour_map_type != 0 or image_type != 2:
        raise ValueError(
            "not an uncompressed true-colour TGA (map %d, type %d)"
            % (colour_map_type, image_type)
        )
    if depth != 24:
        raise ValueError("not 24 bits per pixel (%d)" % depth)

    start = 18 + id_length
    expected = width * height * 3
    if len(data) - start < expected:
        raise ValueError(
            "truncated: %d bytes of pixels, expected %d"
            % (len(data) - start, expected)
        )

    return width, height, data[start:start + expected]


def main(argv):
    if len(argv) not in (2, 3):
        print("usage: %s <file.tga> [minimum distinct colours]" % argv[0], file=sys.stderr)
        return 2

    want = int(argv[2]) if len(argv) == 3 else MIN_DISTINCT_COLOURS

    try:
        width, height, pixels = read_tga(argv[1])
    except (OSError, ValueError) as exc:
        print("%s: %s" % (argv[1], exc), file=sys.stderr)
        return 2

    colours = set()
    for i in range(0, len(pixels), 3):
        colours.add(pixels[i:i + 3])
        if len(colours) >= want:
            print("%dx%d, at least %d colours" % (width, height, len(colours)))
            return 0

    print(
        "%dx%d, %d colour(s), wanted %d: not enough was drawn"
        % (width, height, len(colours), want),
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

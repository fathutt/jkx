#!/usr/bin/env python3
"""Is the picture in the frame, and are the margins beside it black?

tga_is_a_picture.py answers "did anything draw". It cannot answer "did it draw
in the right place", and that turned out to be a whole class of defect this
project shipped without noticing: a splash stretched across the window instead
of fitted, black bars that were grey because the state they were needed in was
not in the condition that draws them, and a model in a menu positioned against
the window while the menu it belonged to was positioned against the fitted
frame. Every one of those is arithmetic, none of them needs a GPU or a retail
asset, and none of them was caught by anything before a person looked at a
screen.

So this checks the geometry, which is the part that can be checked here.

The interface is drawn into SPACE2D_FRAME: 640x480 fitted into the window,
centred, never stretched. From the screenshot's own dimensions that fixes three
things exactly, with no tolerance needed on the arithmetic:

  - where the fitted frame starts and ends,
  - that everything outside it is black, because the client fills it,
  - where a rectangle given in virtual units lands in pixels.

The last one is what makes this more than a bar check. The smoke fixture draws
one picture at a known place in virtual coordinates; if the frame is computed
one way and the content another - which is exactly what the menu model did -
the picture is not where the arithmetic says it should be, and that is visible
here without anyone looking.

    tga_frame_geometry.py <file.tga> [--rect X Y W H] [--min-colours N]

--rect is in virtual 640x480 units and defaults to the fixture's picture.
"""

import sys

VIRTUAL_W = 640.0
VIRTUAL_H = 480.0

# The fixture's menu fills its own 640x480 with a colour it asks for by name, so
# the default rect is the whole of it, inset. The edge of what it painted is then
# the edge of the fitted frame, and comparing the two sides of that edge measures
# the mapping directly. --rect narrows this to a smaller element when there is
# one worth pinning down separately.
DEFAULT_RECT = (0.0, 0.0, 640.0, 480.0)

# Bars are drawn with colorBlack through a white shader, so they are 0. The
# renderer's clear colour is 0.75 grey and a missing bar shows that instead;
# nothing in between is expected, so this only has to separate 0 from 191.
BLACK = 8

# The fixture's item is one flat tone on purpose, so counting colours inside it
# proves nothing and this defaults to accepting one. The check that carries the
# weight is the difference between the item and the frame around it, which is
# what says it landed where the arithmetic put it. --min-colours raises this for
# a rect that is supposed to have detail in it.
MIN_CONTENT_COLOURS = 1


def read_tga(path):
    """Width, height, and rows top to bottom, as the engine writes them.

    The engine writes uncompressed 24-bit TGA. Byte 17 bit 5 says whether the
    first row in the file is the top or the bottom one; the bottom-up form is
    what it actually emits, and getting this backwards would put the content
    rect in the wrong half of the picture and pass or fail for the wrong
    reason.
    """
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
    top_down = bool(data[17] & 0x20)

    if colour_map_type != 0 or image_type != 2:
        raise ValueError("not an uncompressed true-colour TGA (map %d, type %d)"
                         % (colour_map_type, image_type))
    if depth != 24:
        raise ValueError("not 24 bits per pixel (%d)" % depth)
    if width <= 0 or height <= 0:
        raise ValueError("empty image (%dx%d)" % (width, height))

    start = 18 + id_length
    stride = width * 3
    expected = stride * height
    if len(data) - start < expected:
        raise ValueError("truncated: %d bytes of pixels, expected %d"
                         % (len(data) - start, expected))

    rows = [data[start + y * stride:start + (y + 1) * stride] for y in range(height)]
    if not top_down:
        rows.reverse()

    return width, height, rows


def frame_rect(width, height):
    """The fitted 4:3 rectangle inside a window, as vk_get_2d_viewport computes it."""
    want = VIRTUAL_W / VIRTUAL_H
    have = float(width) / float(height)

    if have > want:                 # window is wider: margins at the sides
        h = float(height)
        w = h * want
    else:                           # window is taller: margins top and bottom
        w = float(width)
        h = w / want

    x = (width - w) * 0.5
    y = (height - h) * 0.5
    return x, y, w, h


def worst_outside(width, height, rows, fx, fy, fw, fh):
    """The brightest pixel outside the fitted frame, and where it is.

    One pixel of slack on each edge: the frame's own boundary lands on a
    fraction of a pixel and the sampler can put a partial one either side of it.
    A bar that is not drawn at all is a hundred and ninety-one, so nothing here
    depends on the exact rounding.
    """
    x0 = int(fx) - 2
    x1 = int(fx + fw) + 2
    y0 = int(fy) - 2
    y1 = int(fy + fh) + 2

    worst = (-1, 0, 0)
    for y in range(height):
        row = rows[y]
        inside_y = y0 <= y < y1
        for x in range(width):
            if inside_y and x0 <= x < x1:
                continue
            i = x * 3
            v = max(row[i], row[i + 1], row[i + 2])
            if v > worst[0]:
                worst = (v, x, y)
                if v > 250:
                    return worst
    return worst


def mean_colour(rows, x0, y0, x1, y1):
    total = [0, 0, 0]
    n = 0
    for y in range(max(y0, 0), max(y1, 1)):
        row = rows[y]
        for x in range(max(x0, 0), max(x1, 1)):
            i = x * 3
            total[0] += row[i]
            total[1] += row[i + 1]
            total[2] += row[i + 2]
            n += 1
    if n == 0:
        return (0, 0, 0)
    return tuple(v // n for v in total)


def content_colours(rows, x0, y0, x1, y1):
    colours = set()
    for y in range(y0, y1):
        row = rows[y]
        for x in range(x0, x1):
            colours.add(row[x * 3:x * 3 + 3])
    return colours


def main(argv):
    args = argv[1:]
    rect = DEFAULT_RECT
    min_colours = MIN_CONTENT_COLOURS
    margins = "black"
    path = None

    i = 0
    while i < len(args):
        if args[i] == "--rect" and i + 4 < len(args):
            rect = tuple(float(v) for v in args[i + 1:i + 5])
            i += 5
        elif args[i] == "--margins" and i + 1 < len(args):
            margins = args[i + 1]
            i += 2
        elif args[i] == "--min-colours" and i + 1 < len(args):
            min_colours = int(args[i + 1])
            i += 2
        elif path is None:
            path = args[i]
            i += 1
        else:
            print("unexpected argument: %s" % args[i], file=sys.stderr)
            return 2

    if path is None:
        print("usage: %s <file.tga> [--rect X Y W H] [--min-colours N]" % argv[0],
              file=sys.stderr)
        return 2

    try:
        width, height, rows = read_tga(path)
    except (OSError, ValueError) as exc:
        print("%s: %s" % (path, exc), file=sys.stderr)
        return 2

    fx, fy, fw, fh = frame_rect(width, height)
    aspect = float(width) / float(height)

    failed = False
    margins_ok = True

    # 1. Everything outside the fitted frame is black, because the client fills
    #    it. Grey here is the renderer's clear colour showing through, which
    #    means the bars were not drawn for this state at all.
    if fw < width - 1.5 or fh < height - 1.5:
        value, wx, wy = worst_outside(width, height, rows, fx, fy, fw, fh)
        if value > BLACK:
            margins_ok = False
            where = sys.stderr if margins == "black" else sys.stdout
            if margins != "black":
                print("  KNOWN, not a pass: the margins are not black -", file=where)
            print("%s: outside the fitted frame is not black - %d at (%d,%d).\n"
                  "  The frame is %.0fx%.0f at (%.0f,%.0f) in a %dx%d window; the "
                  "margins should be filled by SCR_FillFrameMargins.\n"
                  "  0.75 grey (191) is the renderer's clear colour showing "
                  "through, which means nothing filled them."
                  % (path, value, wx, wy, fw, fh, fx, fy, width, height),
                  file=where)
            if margins == "black":
                failed = True
    else:
        print("  %dx%d is the frame's own shape, no margins to check" % (width, height))

    # 2. A rectangle given in virtual units lands where the fitted frame says it
    #    should. This is the half that catches content positioned against the
    #    window while the frame is positioned against the fit.
    rx, ry, rw, rh = rect
    sx = fw / VIRTUAL_W
    sy = fh / VIRTUAL_H

    x0 = int(fx + rx * sx) + 2
    y0 = int(fy + ry * sy) + 2
    x1 = int(fx + (rx + rw) * sx) - 2
    y1 = int(fy + (ry + rh) * sy) - 2

    if x1 <= x0 or y1 <= y0:
        print("%s: the content rect is empty in a %dx%d picture" % (path, width, height),
              file=sys.stderr)
        return 2

    colours = content_colours(rows, x0, y0, x1, y1)

    # Counting colours would say "something is here" and nothing more, and the
    # rect is one flat tone on purpose. What matters is that it is a different
    # tone from what is outside the frame: the boundary between the two is the
    # fitted rectangle itself, measured to the pixel from both sides at once.
    # Content computed against the window instead of against the frame puts that
    # boundary somewhere else, and then one of these two samples is wrong.
    inside = mean_colour(rows, x0, y0, x1, y1)

    if fw < width - 1.5:
        outside = mean_colour(rows, 0, int(fy) + 3,
                              max(int(fx) - 3, 1), int(fy + fh) - 3)
    elif fh < height - 1.5:
        outside = mean_colour(rows, int(fx) + 3, 0,
                              int(fx + fw) - 3, max(int(fy) - 3, 1))
    else:
        outside = None                  # 4:3: no margin to compare against

    if outside is not None:
        delta = max(abs(a - b) for a, b in zip(inside, outside))
        if delta < 16:
            print("%s: the rect (%d,%d)-(%d,%d) averages %s and outside the "
                  "frame averages %s - a difference of %d.\n"
                  "  The boundary between them is supposed to be the fitted "
                  "frame. If they match, the content is not where the mapping "
                  "says it is - which is what content placed against the window "
                  "instead of the frame looks like."
                  % (path, x0, y0, x1, y1, inside, outside, delta),
                  file=sys.stderr)
            failed = True

    if len(colours) < min_colours:
        print("%s: the content rect has %d colour(s), wanted %d"
              % (path, len(colours), min_colours), file=sys.stderr)
        failed = True

    if failed:
        return 1

    # Never say "margins black" when they are not. A summary line that
    # contradicts the report three lines above it is how a known defect turns
    # into a forgotten one.
    print("  %dx%d (%.2f:1), frame %.0fx%.0f at %.0f,%.0f, margins %s, "
          "content in place"
          % (width, height, aspect, fw, fh, fx, fy,
             "black" if margins_ok else "NOT BLACK (known)"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

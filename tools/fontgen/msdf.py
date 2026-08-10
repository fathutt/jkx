#!/usr/bin/env python3
"""Build a signed distance field atlas for the engine's text.

Two inputs, because the game's own typefaces are not TrueType. The menus are set
in Raven's bitmap fonts - a .fontdat of metrics beside a .tga of glyphs - and
those are the look of the game. Generating the atlas from some system font would
give sharp text in the wrong typeface, which is not an improvement. So the
bitmap fonts are the primary input, and a TrueType face is the fallback for the
code points they do not have, which is most of the world: the shipped fonts stop
at 256 glyphs and there is no Cyrillic anywhere in them.

Why this exists rather than a call to msdf-atlas-gen: the atlas is an input to
the build, it has to be reproducible on any machine that can run the rest of the
tools, and adding a C++ project with three submodules to get one PNG is a worse
trade than three hundred lines here. The output format is ours either way,
because the renderer has to read it.

From an outline the field is multi-channel; from a bitmap it is one channel
copied into three. That is not a shortcut being hidden: multi-channel needs to
know where the edges are, and a bitmap only says how much ink covers each texel.
The shader takes the median of the three either way, and on a replicated channel
the median is that channel, so one shader reads both. What is lost is corner
sharpness the source bitmap never had - these glyphs were rasterised at sixteen
to thirty-two pixels and their corners are already soft.

What an MSDF is, in one paragraph. A signed distance field stores, per texel,
how far that texel is from the glyph outline - negative inside, positive out -
so the shape can be recovered at any size by asking where the distance crosses
zero. One channel rounds off corners, because a corner is exactly where two
edges are equidistant and the field has to pick one. Multi-channel keeps three
fields, assigns each edge a pair of channels, and takes the median of the three
at lookup: at a corner the two edges disagree in one channel and agree in the
other two, so the median follows the corner instead of rounding it.

Curves are flattened to line segments before distances are measured. msdfgen
solves the cubic exactly; at the sizes an atlas is generated for - a 48 pixel em
with a 4 pixel range - the difference is below a texel, and a flattening
tolerance that says so is worth more than the algebra.
"""

import argparse
import json
import math
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


# Channel masks. Each edge gets two of the three channels, so that any two edges
# meeting at a corner share exactly one - which is what makes the median at that
# texel follow the corner.
WHITE = 0b111
YELLOW = 0b011      # red + green
MAGENTA = 0b101     # red + blue
CYAN = 0b110        # green + blue

ROTATION = [YELLOW, MAGENTA, CYAN]

# Beyond this angle between two edges, the join is a corner rather than a smooth
# continuation. Three degrees short of straight, which is msdfgen's default.
CORNER_ANGLE = math.radians(3.0)


def flatten_quadratic(p0, p1, p2, tolerance):
    """A quadratic bezier as a list of points, subdivided until flat enough."""
    # Distance from the control point to the chord bounds the flattening error.
    dx = p1[0] - 0.5 * (p0[0] + p2[0])
    dy = p1[1] - 0.5 * (p0[1] + p2[1])
    steps = max(2, int(math.ceil(math.sqrt(math.hypot(dx, dy) / max(tolerance, 1e-6)) * 2)))
    steps = min(steps, 32)

    out = []
    for i in range(1, steps + 1):
        t = i / steps
        u = 1.0 - t
        out.append((
            u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
            u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1],
        ))
    return out


def flatten_cubic(p0, p1, p2, p3, tolerance):
    d = max(math.hypot(p1[0] - p0[0], p1[1] - p0[1]),
            math.hypot(p2[0] - p1[0], p2[1] - p1[1]),
            math.hypot(p3[0] - p2[0], p3[1] - p2[1]))
    steps = max(2, int(math.ceil(math.sqrt(d / max(tolerance, 1e-6)) * 2)))
    steps = min(steps, 48)

    out = []
    for i in range(1, steps + 1):
        t = i / steps
        u = 1.0 - t
        out.append((
            u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0],
            u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1],
        ))
    return out


def outline_contours(face, tolerance):
    """The glyph outline as a list of contours, each a list of edges.

    An edge is (points, colour) where points is a polyline of two or more
    points. Curves keep their identity as one edge so that colouring and corner
    detection see the shape the designer drew rather than the subdivision.
    """
    outline = face.glyph.outline
    contours = []

    start = 0
    for end in outline.contours:
        points = [(p[0], p[1]) for p in outline.points[start:end + 1]]
        tags = [t & 1 for t in outline.tags[start:end + 1]]      # bit 0: on-curve
        cubic = [(t & 2) != 0 for t in outline.tags[start:end + 1]]
        start = end + 1

        if not points:
            continue

        # Rotate so the contour begins on-curve. A contour of all off-curve
        # points is legal in TrueType and starts at an implied midpoint.
        if 1 in tags:
            k = tags.index(1)
            points = points[k:] + points[:k]
            tags = tags[k:] + tags[:k]
            cubic = cubic[k:] + cubic[:k]
        else:
            mid = ((points[0][0] + points[-1][0]) * 0.5, (points[0][1] + points[-1][1]) * 0.5)
            points = [mid] + points
            tags = [1] + tags
            cubic = [False] + cubic

        edges = []
        cur = points[0]
        i = 1
        n = len(points)
        pending = []
        while i <= n:
            p = points[i % n]
            on = tags[i % n]
            is_cubic = cubic[i % n]

            if on:
                if not pending:
                    edges.append(([cur, p], WHITE))
                elif len(pending) == 1 and not is_cubic:
                    edges.append(([cur] + flatten_quadratic(cur, pending[0], p, tolerance), WHITE))
                elif len(pending) == 2:
                    edges.append(([cur] + flatten_cubic(cur, pending[0], pending[1], p, tolerance), WHITE))
                else:
                    # A run of quadratic control points with implied on-curve
                    # midpoints between them, which is how TrueType stores a
                    # chain of quadratics.
                    prev = cur
                    for a, b in zip(pending, pending[1:]):
                        mid = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
                        edges.append(([prev] + flatten_quadratic(prev, a, mid, tolerance), WHITE))
                        prev = mid
                    edges.append(([prev] + flatten_quadratic(prev, pending[-1], p, tolerance), WHITE))
                cur = p
                pending = []
            else:
                pending.append(p)
            i += 1

        if edges:
            contours.append(edges)

    return contours


def direction(edge, at_start):
    pts = edge[0]
    if at_start:
        a, b = pts[0], pts[1]
    else:
        a, b = pts[-2], pts[-1]
    dx, dy = b[0] - a[0], b[1] - a[1]
    n = math.hypot(dx, dy)
    if n < 1e-12:
        return (0.0, 0.0)
    return (dx / n, dy / n)


def colour_contour(edges):
    """Give every edge two channels, changing colour at every corner.

    A smooth join keeps the colour, so a curve made of several edges reads as
    one edge to the median. At a corner the colour rotates, which is what leaves
    exactly one channel disagreeing there.
    """
    if len(edges) == 1:
        # A contour with no corners at all - a circle, the bowl of an o. It gets
        # every channel, and stays round.
        edges[0] = (edges[0][0], WHITE)
        return

    corners = []
    for i in range(len(edges)):
        prev_dir = direction(edges[i - 1], at_start=False)
        this_dir = direction(edges[i], at_start=True)
        dot = max(-1.0, min(1.0, prev_dir[0] * this_dir[0] + prev_dir[1] * this_dir[1]))
        if math.acos(dot) > CORNER_ANGLE:
            corners.append(i)

    if not corners:
        for i in range(len(edges)):
            edges[i] = (edges[i][0], WHITE)
        return

    colour = 0
    # Start at a corner so the first run is a real run.
    order = corners[0]
    for k in range(len(edges)):
        i = (order + k) % len(edges)
        if k > 0 and i in corners:
            colour = (colour + 1) % len(ROTATION)
        edges[i] = (edges[i][0], ROTATION[colour])

    # If the rotation closed on the same colour it started with, the seam is a
    # corner that lost its disagreement. Nudge the last run.
    if len(corners) > 1 and edges[corners[0]][1] == edges[corners[-1]][1]:
        i = corners[-1]
        edges[i] = (edges[i][0], ROTATION[(ROTATION.index(edges[i][1]) + 1) % len(ROTATION)])


def segments_of(contours):
    """Every edge as arrays of segment endpoints plus its channel mask."""
    ax, ay, bx, by, mask = [], [], [], [], []
    for edges in contours:
        for pts, colour in edges:
            for p, q in zip(pts, pts[1:]):
                ax.append(p[0]); ay.append(p[1])
                bx.append(q[0]); by.append(q[1])
                mask.append(colour)
    return (np.array(ax), np.array(ay), np.array(bx), np.array(by), np.array(mask))


def distances_to_segments(px, py, ax, ay, bx, by):
    """Unsigned distance from every pixel to every segment. Shape (pixels, segs)."""
    ex = bx - ax
    ey = by - ay
    len2 = ex * ex + ey * ey
    len2 = np.where(len2 < 1e-12, 1e-12, len2)

    dx = px[:, None] - ax[None, :]
    dy = py[:, None] - ay[None, :]

    t = (dx * ex[None, :] + dy * ey[None, :]) / len2[None, :]
    t = np.clip(t, 0.0, 1.0)

    cx = dx - t * ex[None, :]
    cy = dy - t * ey[None, :]

    return np.sqrt(cx * cx + cy * cy)


def winding_inside(px, py, ax, ay, bx, by):
    """Even-odd crossing test, vectorised: is each pixel inside the outline."""
    # A horizontal ray to +x. Count segments it crosses.
    y0 = ay[None, :]
    y1 = by[None, :]
    x0 = ax[None, :]
    x1 = bx[None, :]

    py2 = py[:, None]
    px2 = px[:, None]

    straddles = ((y0 > py2) != (y1 > py2))
    denom = np.where(np.abs(y1 - y0) < 1e-12, 1e-12, y1 - y0)
    xint = x0 + (py2 - y0) * (x1 - x0) / denom

    crossings = np.logical_and(straddles, xint > px2)

    return (np.count_nonzero(crossings, axis=1) % 2) == 1


def glyph_msdf(face, width, height, ox, oy, scale, px_range):
    """The three distance fields for the glyph currently loaded in face."""
    contours = outline_contours(face, tolerance=1.0 / max(scale, 1e-6))
    for edges in contours:
        colour_contour(edges)

    field = np.full((height, width, 3), 0.5, dtype=np.float32)
    if not contours:
        return field

    ax, ay, bx, by, mask = segments_of(contours)

    # Pixel centres, in font units.
    xs = (np.arange(width) + 0.5) / scale - ox
    ys = (np.arange(height) + 0.5) / scale - oy
    gx, gy = np.meshgrid(xs, ys)
    px = gx.reshape(-1)
    py = gy.reshape(-1)

    dist = distances_to_segments(px, py, ax, ay, bx, by)
    inside = winding_inside(px, py, ax, ay, bx, by)

    sign = np.where(inside, -1.0, 1.0)
    rng = px_range / scale

    for channel in range(3):
        bit = 1 << channel
        sel = (mask & bit) != 0
        if not np.any(sel):
            d = np.min(dist, axis=1)
        else:
            d = np.min(dist[:, sel], axis=1)
        signed = sign * d
        # Map [-range/2, +range/2] to [0,1], as the shader expects.
        norm = np.clip(0.5 - signed / rng, 0.0, 1.0)
        field[:, :, channel] = norm.reshape(height, width)

    return field


# --- the game's own fonts -------------------------------------------------
#
# .fontdat is a straight dump of dfontdat_t from qcommon/qfiles.h: 256 glyphs of
# metrics, then the point size, height, ascender and descender. The glyph
# rectangles are texture coordinates into a .tga of the same name.

GLYPH_COUNT = 256
GLYPH_FMT = '<4hi4f'        # width, height, horizAdvance, horizOffset, baseline, s, t, s2, t2
GLYPH_SIZE = struct.calcsize(GLYPH_FMT)
FONTDAT_SIZE = GLYPH_COUNT * GLYPH_SIZE + struct.calcsize('<5h')


def read_fontdat(path):
    with open(path, 'rb') as f:
        data = f.read()

    if len(data) != FONTDAT_SIZE:
        raise ValueError('%s is %d bytes, a .fontdat is %d'
                         % (path, len(data), FONTDAT_SIZE))

    glyphs = []
    for i in range(GLYPH_COUNT):
        w, h, adv, xoff, baseline, s, t, s2, t2 = struct.unpack_from(
            GLYPH_FMT, data, i * GLYPH_SIZE)
        glyphs.append({'w': w, 'h': h, 'advance': adv, 'xoff': xoff,
                       'baseline': baseline, 's': s, 't': t, 's2': s2, 't2': t2})

    point_size, height, ascender, descender, _korean = struct.unpack_from(
        '<5h', data, GLYPH_COUNT * GLYPH_SIZE)

    return glyphs, point_size, height, ascender, descender


def coverage_of(image, glyph):
    """The glyph's own pixels, as coverage in 0..1."""
    iw, ih = image.shape[1], image.shape[0]

    x0 = int(round(glyph['s'] * iw))
    y0 = int(round(glyph['t'] * ih))
    x1 = x0 + glyph['w']
    y1 = y0 + glyph['h']

    x0 = max(0, min(iw, x0)); x1 = max(0, min(iw, x1))
    y0 = max(0, min(ih, y0)); y1 = max(0, min(ih, y1))

    if x1 <= x0 or y1 <= y0:
        return None

    return image[y0:y1, x0:x1]


def sdf_from_coverage(cover, upscale, px_range):
    """A signed distance field from a coverage bitmap.

    Two things decide where the outline is. Away from it, the binary mask does:
    coverage is either nought or one and the distance is a euclidean transform
    of the threshold at one half. Across it, the coverage itself does, and it
    knows more than the mask - a texel that is six tenths covered says the edge
    passes a tenth of a texel past its centre, which no transform of the
    thresholded mask can recover. So the transform gives the field everywhere,
    and the partially covered texels are overwritten with what their own value
    says. Without that correction a stem lands on whichever side of the texel
    grid it happens to fall and the text visibly shimmers as it scrolls.

    That is also why upscaling is not the answer and defaults to one. Repeating
    each source texel four times does not add information about where the edge
    is; it only quadruples the atlas. The whole reason to store a distance
    field is that it resolves at any size from a low resolution grid.
    """
    if upscale > 1:
        cover = np.repeat(np.repeat(cover, upscale, axis=0), upscale, axis=1)

    inside = cover >= 0.5
    h, w = inside.shape

    d_in = _edt(~inside)        # inside a cell: how far to the nearest outside
    d_out = _edt(inside)        # outside a cell: how far to the nearest inside

    # Half a texel puts the boundary between the two rows of cell centres it
    # separates rather than on top of one of them.
    signed = np.where(inside, -(d_in - 0.5), d_out - 0.5)

    partial = (cover > 0.0) & (cover < 1.0)
    signed = np.where(partial, 0.5 - cover, signed)

    signed = signed / float(upscale)        # back into source texels

    field = np.clip(0.5 - signed / float(px_range), 0.0, 1.0)

    return field.astype(np.float32), w, h


def _edt(mask):
    """Exact euclidean distance from every false cell to the nearest true cell.

    Felzenszwalb and Huttenlocher: a squared-distance transform along one axis,
    then along the other. Linear in the number of cells, and exact, which the
    chamfer approximation everyone reaches for first is not - and the error
    shows up as a wobble along diagonal stems.
    """
    INF = 1e20
    if not np.any(mask):
        # Nothing to be near. The parabola arithmetic below subtracts infinity
        # from infinity if let anywhere near this case.
        return np.full(mask.shape, math.sqrt(INF), dtype=np.float64)

    f = np.where(mask, 0.0, INF)

    # Along rows, then along columns.
    for axis in (0, 1):
        f = np.apply_along_axis(_edt_1d, axis, f)

    return np.sqrt(f)


def _edt_1d(f):
    n = len(f)
    d = np.empty(n)
    v = np.zeros(n, dtype=np.int64)
    z = np.empty(n + 1)

    k = 0
    v[0] = 0
    z[0] = -1e20
    z[1] = 1e20

    for q in range(1, n):
        s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0 * q - 2.0 * v[k])
        while s <= z[k]:
            k -= 1
            s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2.0 * q - 2.0 * v[k])
        k += 1
        v[k] = q
        z[k] = s
        z[k + 1] = 1e20

    k = 0
    for q in range(n):
        while z[k + 1] < q:
            k += 1
        d[q] = (q - v[k]) ** 2 + f[v[k]]

    return d


def build_from_fontdat(args):
    glyphs_in, point_size, height, ascender, descender = read_fontdat(args.fontdat)

    image = np.asarray(Image.open(args.fontdat_image).convert('L')).astype(np.float32) / 255.0

    upscale = args.upscale
    pad = args.range

    glyphs = []
    for cp in range(GLYPH_COUNT):
        g = glyphs_in[cp]

        # A glyph with no rectangle still has an advance - that is what a space
        # is - and the metrics have to survive it, because line breaking adds
        # advances and nothing else.
        blank = {'cp': cp, 'field': None, 'w': 0, 'h': 0,
                 'advance': g['advance'], 'xoff': g['xoff'],
                 'baseline': g['baseline'], 'gw': 0, 'gh': 0}

        if g['w'] <= 0 or g['h'] <= 0:
            glyphs.append(blank)
            continue

        cover = coverage_of(image, g)
        if cover is None:
            glyphs.append(blank)
            continue

        cover = np.pad(cover, pad, mode='constant', constant_values=0.0)

        field, ow, oh = sdf_from_coverage(cover, upscale, pad)

        # One channel copied into three. See the note at the top: the shader
        # takes the median, and the median of three equal numbers is that
        # number, so the same shader reads a bitmap-derived field and an
        # outline-derived one.
        rgb = np.repeat(field[:, :, None], 3, axis=2)

        glyphs.append({
            'cp': cp,
            'w': ow, 'h': oh, 'field': rgb,
            # The metrics the engine already uses, with the padding folded in.
            # advance is untouched, which is the whole point: line breaking
            # sums advances, so every line breaks exactly where it used to.
            'advance': g['advance'],
            'xoff': g['xoff'] - pad,
            'baseline': g['baseline'] + pad,
            'gw': g['w'] + 2 * pad,
            'gh': g['h'] + 2 * pad,
        })

    return glyphs, {
        'font': os.path.basename(args.fontdat),
        'source': 'fontdat',
        'pointSize': point_size,
        'range': args.range,
        'upscale': upscale,
        'ascender': ascender,
        'descender': descender,
        'lineHeight': height,
    }


def build(args):
    face = freetype.Face(args.font)
    face.set_char_size(args.size * 64)

    upem = face.units_per_EM
    scale = args.size / float(upem)          # font units -> field texels
    pad = args.range                          # texels of field around the glyph

    # How big the atlas is and how big the text is are two different questions,
    # and tying them together is a mistake worth not making. The field is
    # generated at --size texels per em, which is a quality knob: more texels
    # means a more accurate outline. The metrics come out at --point-size, which
    # is what the engine believes the font's size to be and therefore what it
    # lays the interface out with. Generate at 48 and report 16 and you get a
    # sharp 16 pixel font; tie them together and asking for a better atlas
    # doubles the size of every menu.
    point = args.point_size if args.point_size else args.size
    metric = point / float(upem)             # font units -> the size the game draws at
    shrink = metric / scale                  # field texels -> the same

    chars = []
    seen = set()
    for spec in args.charset.split(','):
        spec = spec.strip()
        if '-' in spec and len(spec) > 1:
            a, b = spec.split('-', 1)
            rng = range(int(a, 0), int(b, 0) + 1)
        else:
            rng = [int(spec, 0)]
        for cp in rng:
            if cp not in seen:
                seen.add(cp)
                chars.append(cp)

    glyphs = []
    for cp in chars:
        index = face.get_char_index(cp)
        if index == 0 and cp != 32:
            continue
        face.load_glyph(index, freetype.FT_LOAD_NO_SCALE | freetype.FT_LOAD_NO_BITMAP)

        bbox = face.glyph.outline.get_bbox()
        advance = face.glyph.metrics.horiAdvance * metric

        if face.glyph.outline.n_points == 0:
            glyphs.append({'cp': cp, 'w': 0, 'h': 0, 'field': None,
                           'advance': advance, 'xoff': 0, 'baseline': 0,
                           'gw': 0, 'gh': 0})
            continue

        x0 = math.floor(bbox.xMin * scale) - pad
        y0 = math.floor(bbox.yMin * scale) - pad
        x1 = math.ceil(bbox.xMax * scale) + pad
        y1 = math.ceil(bbox.yMax * scale) + pad

        w = int(x1 - x0)
        h = int(y1 - y0)

        field = glyph_msdf(face, w, h, -x0 / scale, -y0 / scale, scale, pad)

        # Font units go up, image rows go down. Everything above sampled row 0
        # at the bottom of the glyph, so the atlas gets it the other way up.
        field = field[::-1, :, :]

        # Same metric names as the bitmap path, in the same convention the
        # engine already draws in: baseline is how far above the pen the top of
        # the quad sits, xoff how far right of it the left edge does.
        glyphs.append({'cp': cp, 'w': w, 'h': h, 'field': field,
                       'advance': advance,
                       'xoff': x0 * shrink, 'baseline': y1 * shrink,
                       'gw': w * shrink, 'gh': h * shrink})

    return glyphs, {
        'font': os.path.basename(args.font),
        'source': 'truetype',
        'pointSize': point,
        # In texels of the atlas, which is where the shader measures it.
        'range': args.range,
        'upscale': 1,
        'ascender': round(face.ascender * metric, 3),
        'descender': round(face.descender * metric, 3),
        'lineHeight': round(face.height * metric, 3),
    }


# --- what the engine actually reads --------------------------------------
#
# The JSON above is for reading and for preview.py. The engine gets this
# instead, because a renderer that has to carry a JSON parser to load a font
# has acquired a dependency for the sake of a file format nobody types.
#
# Glyphs are sorted by code point so a lookup is a binary search, which matters:
# every character of every string drawn does one.

JKXFONT_MAGIC = b'JKXF'
JKXFONT_VERSION = 1
JKXFONT_HEADER = '<4sIHHhhhhfI'      # magic, version, atlas w/h, metrics, range, count
JKXFONT_GLYPH = '<I4H5f'             # cp, x,y,w,h, xoff,baseline,gw,gh,advance

# The placement is float and not integer pixels. With the field generated at one
# resolution and the metrics reported at another - which is the whole point of
# --point-size - a glyph 41 texels wide in a 48 texel em reported at 16 point is
# 13.67 pixels wide, and rounding that to 14 is a two per cent error in the size
# of every letter. It also stops the atlas resolution being a free choice, which
# was the reason for separating them.


def write_jkxfont(path, meta):
    glyphs = sorted(meta['glyphs'], key=lambda g: g['cp'])

    out = bytearray()
    out += struct.pack(JKXFONT_HEADER,
                       JKXFONT_MAGIC, JKXFONT_VERSION,
                       meta['atlasWidth'], meta['atlasHeight'],
                       int(meta['pointSize']), int(round(meta['lineHeight'])),
                       int(round(meta['ascender'])), int(round(meta['descender'])),
                       float(meta['range'] * meta.get('upscale', 1)),
                       len(glyphs))

    for g in glyphs:
        out += struct.pack(JKXFONT_GLYPH,
                           g['cp'],
                           g['x'], g['y'], g['w'], g['h'],
                           float(g['xoff']), float(g['baseline']),
                           float(g['gw']), float(g['gh']),
                           float(g['advance']))

    with open(path, 'wb') as f:
        f.write(bytes(out))

    print("%s: %d bytes" % (path, len(out)))


def pack_and_write(glyphs, meta, args):
    """Shelf-pack the glyph fields into one atlas and write it out.

    Two details here are not cosmetic. The atlas is cleared to nought, not to a
    half: a half is the value that means "exactly on the outline", so an empty
    atlas painted with it draws a hairline everywhere two glyphs meet. And the
    glyphs are packed a texel apart, because the sampler is bilinear and a quad
    drawn across a rectangle's full width reads half a texel outside it at each
    edge - straight into the neighbour, if there is no gap.
    """
    gutter = args.gutter
    order = sorted(range(len(glyphs)), key=lambda i: -glyphs[i]['h'])
    aw = args.atlas
    x = y = shelf = 0
    for i in order:
        g = glyphs[i]
        if g['field'] is None:
            g['x'] = g['y'] = 0
            continue
        if g['w'] + 2 * gutter > aw:
            raise ValueError('glyph %d is %d wide, wider than the atlas'
                             % (g['cp'], g['w']))
        if x + g['w'] + gutter > aw:
            x = 0
            y += shelf + gutter
            shelf = 0
        g['x'] = x
        g['y'] = y
        x += g['w'] + gutter
        shelf = max(shelf, g['h'])
    ah = y + shelf

    # A power of two: not required by Vulkan, but it keeps the atlas the same
    # shape on every driver and makes a mip chain exact.
    ah_pot = 1
    while ah_pot < ah:
        ah_pot *= 2

    atlas = np.zeros((ah_pot, aw, 3), dtype=np.float32)
    for g in glyphs:
        if g['field'] is None:
            continue
        atlas[g['y']:g['y'] + g['h'], g['x']:g['x'] + g['w'], :] = g['field']

    Image.fromarray((np.clip(atlas, 0, 1) * 255.0 + 0.5).astype(np.uint8), 'RGB').save(args.out_image)

    meta = dict(meta)
    meta['atlasWidth'] = aw
    meta['atlasHeight'] = ah_pot
    meta['glyphs'] = [
        {
            'cp': g['cp'],
            # where in the atlas
            'x': g['x'], 'y': g['y'], 'w': g['w'], 'h': g['h'],
            # and where on the screen, in the units the engine already uses
            'gw': g['gw'], 'gh': g['gh'],
            'xoff': g['xoff'], 'baseline': g['baseline'],
            'advance': round(float(g['advance']), 3),
        }
        for g in glyphs if g['field'] is not None or g['advance']
    ]

    with open(args.out_meta, 'w') as f:
        json.dump(meta, f, indent=1)
        f.write('\n')

    if args.out_font:
        write_jkxfont(args.out_font, meta)

    drawn = sum(1 for g in glyphs if g['field'] is not None)
    print("%s: %d glyphs (%d drawn), atlas %dx%d, %s + %s"
          % (meta['font'], len(meta['glyphs']), drawn, aw, ah_pot,
             args.out_image, args.out_meta))


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--font', help='a .ttf or .otf, for code points the game font lacks')
    p.add_argument('--fontdat', help="one of the game's own fonts")
    p.add_argument('--fontdat-image', help='the .tga or .png beside it')
    p.add_argument('--upscale', type=int, default=1,
                   help='generate the field on a finer grid than the source bitmap; '
                        'rarely worth it, see sdf_from_coverage')
    p.add_argument('--size', type=int, default=48,
                   help='texels per em the field is generated at: a quality knob')
    p.add_argument('--point-size', type=int, default=0,
                   help='the size the engine lays out with, if different from --size')
    p.add_argument('--range', type=int, default=4, help='width of the distance field in texels')
    p.add_argument('--atlas', type=int, default=512, help='atlas width in texels')
    p.add_argument('--gutter', type=int, default=1,
                   help='texels of empty space between packed glyphs')
    p.add_argument('--charset', default='0x20-0x7e,0xa0-0xff,0x400-0x45f',
                   help='comma separated code points and ranges')
    p.add_argument('--out-image', required=True)
    p.add_argument('--out-meta', required=True, help='JSON, for reading and for preview.py')
    p.add_argument('--out-font', help='.jkxfont, which is what the engine loads')
    args = p.parse_args(argv[1:])

    if not args.font and not args.fontdat:
        p.error('give --fontdat (the game font) or --font (a TrueType face), or both')

    if args.fontdat:
        if not args.fontdat_image:
            p.error('--fontdat needs --fontdat-image beside it')
        glyphs, meta = build_from_fontdat(args)
    else:
        glyphs, meta = build(args)

    pack_and_write(glyphs, meta, args)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

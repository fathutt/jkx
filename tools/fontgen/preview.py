#!/usr/bin/env python3
"""Draw a string from an atlas the way the shader will, so it can be looked at.

The point of this is not the picture. It is that the shader is four lines of
GLSL and every way of getting it wrong produces text - blurry text, fat text,
text with the stems half a pixel off - and none of those announce themselves in
a build log. Rendering the same string here, with the same arithmetic, at the
sizes the game actually draws at, is how a bad atlas gets caught before it is
in a commit.

The arithmetic is deliberately the shader's, not something equivalent:

    d = median(r, g, b) - 0.5
    w = 0.5 / screenPxRange           (what fwidth gives, one texel's worth)
    a = clamp(d / w + 0.5, 0, 1)

where screenPxRange is how many screen pixels one unit of the distance range
covers - range * (drawn size / source size). Below about 1.5 that clamp has no
room to work and the edges go hard; the checker below says so rather than
leaving it to the eye.
"""

import argparse
import json
import sys

import numpy as np

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("this needs pillow: pip install pillow\n")
    raise


def sample(atlas, x, y):
    """Bilinear, because that is what the sampler does."""
    h, w = atlas.shape[0], atlas.shape[1]

    x = np.clip(x - 0.5, 0, w - 1)
    y = np.clip(y - 0.5, 0, h - 1)

    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    x1 = np.minimum(x0 + 1, w - 1)
    y1 = np.minimum(y0 + 1, h - 1)

    fx = (x - x0)[..., None]
    fy = (y - y0)[..., None]

    return ((atlas[y0, x0] * (1 - fx) + atlas[y0, x1] * fx) * (1 - fy) +
            (atlas[y1, x0] * (1 - fx) + atlas[y1, x1] * fx) * fy)


def draw(meta, atlas, text, scale, out_w, out_h, baseline_y):
    glyphs = {g['cp']: g for g in meta['glyphs']}
    canvas = np.zeros((out_h, out_w), dtype=np.float32)

    pen = 4.0
    for ch in text:
        g = glyphs.get(ord(ch))
        if g is None:
            continue
        if g['w'] and g['h']:
            gw = g['gw'] * scale
            gh = g['gh'] * scale
            px = pen + g['xoff'] * scale
            py = baseline_y - g['baseline'] * scale

            x0 = int(np.floor(px)); y0 = int(np.floor(py))
            x1 = int(np.ceil(px + gw)); y1 = int(np.ceil(py + gh))
            x0 = max(0, x0); y0 = max(0, y0)
            x1 = min(out_w, x1); y1 = min(out_h, y1)

            if x1 > x0 and y1 > y0:
                xs = np.arange(x0, x1) + 0.5
                ys = np.arange(y0, y1) + 0.5
                gx, gy = np.meshgrid(xs, ys)

                u = g['x'] + (gx - px) / gw * g['w']
                v = g['y'] + (gy - py) / gh * g['h']

                texel = sample(atlas, u, v)
                d = np.median(texel, axis=2) - 0.5

                # How many screen pixels one texel of the atlas covers here.
                px_per_texel = gw / float(g['w'])
                screen_px_range = meta['range'] * px_per_texel * meta.get('upscale', 1)
                a = np.clip(d * screen_px_range + 0.5, 0.0, 1.0)

                region = canvas[y0:y1, x0:x1]
                canvas[y0:y1, x0:x1] = np.maximum(region, a)

        pen += g['advance'] * scale

    return canvas, pen


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--meta', required=True)
    p.add_argument('--image', required=True)
    p.add_argument('--text', default='A long time ago in a galaxy far, far away...')
    p.add_argument('--scales', default='1,2,4',
                   help='comma separated multipliers of the font size to draw at')
    p.add_argument('--out', required=True)
    args = p.parse_args(argv[1:])

    with open(args.meta) as f:
        meta = json.load(f)
    atlas = np.asarray(Image.open(args.image).convert('RGB')).astype(np.float32) / 255.0

    # The atlas should be a distance field, which means most of it is neither
    # nought nor one. A flat atlas - the failure mode when the sign convention
    # is wrong - is uniformly one half, and that is worth failing on.
    spread = float(np.std(atlas))
    mid = float(np.mean((atlas > 0.02) & (atlas < 0.98)))
    if spread < 0.05:
        sys.stderr.write("atlas has no contrast (std %.4f): this is not a distance field\n" % spread)
        return 1
    if mid < 0.005:
        sys.stderr.write("atlas has no gradient (%.3f%% of texels are between): "
                         "this is a bitmap, not a distance field\n" % (mid * 100.0))
        return 1

    scales = [float(s) for s in args.scales.split(',')]
    line_h = meta['lineHeight'] or meta['pointSize']

    rows = []
    for s in scales:
        h = int(line_h * s * 1.6) + 8
        w = 2048
        canvas, used = draw(meta, atlas, args.text, s, w, h, baseline_y=h * 0.75)
        rows.append(canvas[:, :max(64, int(used) + 8)])

        px_range = meta['range'] * s * meta.get('upscale', 1)
        note = '' if px_range >= 1.5 else '   <- under 1.5, edges will be hard'
        print("x%-5g  %d px line, %.2f px of range%s" % (s, int(line_h * s), px_range, note))

    width = max(r.shape[1] for r in rows)
    total = sum(r.shape[0] for r in rows)
    sheet = np.zeros((total, width), dtype=np.float32)
    y = 0
    for r in rows:
        sheet[y:y + r.shape[0], :r.shape[1]] = r
        y += r.shape[0]

    Image.fromarray((np.clip(sheet, 0, 1) * 255.0 + 0.5).astype(np.uint8), 'L').save(args.out)
    print("%s: %dx%d" % (args.out, width, total))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

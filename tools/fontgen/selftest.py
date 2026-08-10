#!/usr/bin/env python3
"""Self-test for the font atlas generator.

The generator's output is a picture, and a picture is exactly the kind of
artifact that is wrong in ways a build log never mentions. Three things get
checked here, and each of them has already been wrong once:

  1. The atlas is a distance field. The first version had the two euclidean
     transforms the wrong way round, which produced a uniformly grey atlas
     that saved, packed and reported success.

  2. The metrics survive. Line breaking in the game sums horizAdvance and
     nothing else - CG_ScrollText walks the string adding advances until it
     passes the width, and that is how the opening crawl decides where each
     line ends. If a single advance moves, the crawl breaks in different
     places. So every advance is compared against the .fontdat it came from.

  3. The field draws the glyph that went in. At one to one, the shader's
     output is compared against the source bitmap's own coverage; they should
     agree on nearly every pixel. This is the check that would catch a sign
     error, an off-by-one in the padding, or a rectangle read from the wrong
     corner of the atlas - none of which the first two notice.

    python3 tools/fontgen/selftest.py
"""

from __future__ import annotations

import glob
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import numpy as np

FAILURES: list[str] = []


def check(condition: bool, what: str) -> None:
    print(("ok   " if condition else "FAIL ") + what)
    if not condition:
        FAILURES.append(what)


def find_ttf() -> str | None:
    """Any TrueType face on the machine. The test is of the tool, not the font."""
    for pattern in (
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/*/*.ttf",
        "/usr/share/fonts/**/*.ttf",
        "/Library/Fonts/*.ttf",
        "C:/Windows/Fonts/arial.ttf",
    ):
        hits = sorted(glob.glob(pattern, recursive=True))
        if hits:
            return hits[0]
    return None


def run(*argv: str) -> None:
    subprocess.run([sys.executable, *argv], check=True,
                   stdout=subprocess.DEVNULL)


def main() -> int:
    try:
        import freetype  # noqa: F401
        from PIL import Image
    except ImportError as exc:
        print("skipped: %s" % exc)
        return 0

    ttf = find_ttf()
    if ttf is None:
        print("skipped: no TrueType face on this machine to build a fixture from")
        return 0

    import msdf

    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        fontdat = d / "testfont.fontdat"
        source = d / "testfont.png"
        atlas_img = d / "atlas.png"
        atlas_meta = d / "atlas.json"

        run(str(HERE / "make_test_font.py"), "--font", ttf, "--size", "24",
            "--out", str(fontdat))
        check(fontdat.stat().st_size == msdf.FONTDAT_SIZE,
              "fixture is a .fontdat: %d bytes" % fontdat.stat().st_size)

        run(str(HERE / "msdf.py"), "--fontdat", str(fontdat),
            "--fontdat-image", str(source),
            "--out-image", str(atlas_img), "--out-meta", str(atlas_meta))

        meta = json.loads(atlas_meta.read_text())
        atlas = np.asarray(Image.open(atlas_img).convert("RGB")).astype(np.float32) / 255.0
        src = np.asarray(Image.open(source).convert("L")).astype(np.float32) / 255.0

        # 1. it is a field, not a bitmap and not a flat sheet
        check(float(np.std(atlas)) > 0.05,
              "atlas has contrast (std %.3f)" % float(np.std(atlas)))
        between = float(np.mean((atlas > 0.02) & (atlas < 0.98)))
        check(between > 0.02,
              "atlas has a gradient (%.1f%% of texels are between)" % (between * 100.0))

        # 2. every advance came through untouched
        raw, point_size, height, ascender, descender = msdf.read_fontdat(str(fontdat))
        out = {g["cp"]: g for g in meta["glyphs"]}
        moved = [cp for cp in range(msdf.GLYPH_COUNT)
                 if cp in out and out[cp]["advance"] != raw[cp]["advance"]]
        check(not moved,
              "every horizAdvance is unchanged"
              + ("" if not moved else " (%d moved, first is %d)" % (len(moved), moved[0])))
        check(meta["pointSize"] == point_size and meta["lineHeight"] == height
              and meta["ascender"] == ascender and meta["descender"] == descender,
              "the font header is unchanged")

        # 3. drawn at one to one, the field reproduces the source glyph
        import preview

        compared, worst_cp, worst = 0, "-", 0.0
        for ch in "AWgoil.,#@Mmw1":
            cp = ord(ch)
            g = out.get(cp)
            r = raw[cp]
            if not g or not g["w"] or not r["w"]:
                continue

            pad = (g["gw"] - r["w"]) // 2

            # preview.draw starts the pen at x=4 and puts the top of the quad
            # at (baseline_y - baseline), so asking for a baseline_y of
            # baseline+8 lands the padded quad at (4 + xoff, 8). The glyph
            # itself is the pad inset from that.
            canvas, _ = preview.draw(meta, atlas, ch, 1.0,
                                     g["gw"] + 16, g["gh"] + 16,
                                     baseline_y=g["baseline"] + 8)
            x0 = 4 + g["xoff"] + pad
            y0 = 8 + pad
            drawn = canvas[y0:y0 + r["h"], x0:x0 + r["w"]]

            sx = int(round(r["s"] * src.shape[1]))
            sy = int(round(r["t"] * src.shape[0]))
            want = src[sy:sy + r["h"], sx:sx + r["w"]]

            if drawn.shape != want.shape or want.size == 0:
                FAILURES.append("glyph '%s' does not line up: %s vs %s"
                                % (ch, drawn.shape, want.shape))
                continue

            # Compare where each says it is solid. Antialiased edge pixels are
            # allowed to disagree - that is the half the field is reconstructing
            # rather than copying - so the comparison is of the thresholded
            # shapes, and the tolerance is the outline's own perimeter.
            diff = float(np.mean((drawn >= 0.5) != (want >= 0.5)))
            compared += 1
            if diff >= worst:
                worst_cp, worst = ch, diff

        check(compared >= 10, "enough glyphs to compare (%d)" % compared)
        check(worst < 0.10,
              "the field redraws the source glyphs (%d glyphs, worst is '%s' at "
              "%.1f%% of pixels)" % (compared, worst_cp, worst * 100.0))

    print()
    if FAILURES:
        print("FAILED:")
        for f in FAILURES:
            print("  " + f)
        return 1
    print("font atlas generator: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

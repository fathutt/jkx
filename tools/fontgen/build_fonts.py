#!/usr/bin/env python3
"""Build every font the game asks for, from the faces in assets/fonts/src.

The engine registers fonts by the names the retail game used - ergoec, arialnb,
anewhope, ocr_a - and those names now resolve to weights of Libre Franklin
rather than to Raven's bitmaps. What is in the manifest below is that mapping.

Two numbers per font and they do different jobs:

  size        texels per em the distance field is generated at. A quality knob
              and nothing else: a larger atlas resolves the outline better and
              costs texture memory.
  pointSize   what the engine believes the font's size to be, and therefore
              what every menu, HUD element and line break is laid out with.

They used to be the same number, which meant asking for a better atlas doubled
the size of the interface.

pointSize has to match what the retail .fontdat said, or the whole interface
changes height. The values here are the shipped ones, but if you have the game
installed you can have them read out of the real files instead of trusted:

    python3 tools/fontgen/build_fonts.py --assets /path/to/base

Advances are NOT matched - that was the decision, taken deliberately: the text
is set in Libre Franklin's own metrics, so it has Libre Franklin's spacing and
every line breaks where that spacing puts it rather than where ergoec's did.
See the project's phase 2 font notes.

aurabesh is not here. It is not a typeface but an invented alphabet, and no
Latin face can stand in for it without turning alien signage into readable
English. Build it from the game's own files, which needs the game installed:

    python3 tools/fontgen/msdf.py --fontdat  <base>/fonts/aurabesh.fontdat \\
                                 --fontdat-image <base>/fonts/aurabesh.tga \\
                                 --out-image assets/fonts/aurabesh.png \\
                                 --out-meta  /tmp/aurabesh.json \\
                                 --out-font  assets/fonts/aurabesh.jkxfont
"""

from __future__ import annotations

import argparse
import filecmp
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SRC = ROOT / "assets" / "fonts" / "src"
OUT = ROOT / "assets" / "fonts"

# Latin, Latin-1 supplement, and Cyrillic. The last is the reason a TrueType
# face is involved at all: the shipped fonts have 256 glyphs and not one of them
# is Cyrillic.
CHARSET = "0x20-0x7e,0xa0-0xff,0x400-0x45f"

FONTS = [
    # name        face                          size  pointSize  atlas
    ("ergoec",   "LibreFranklin-Medium.ttf",     48,     16,      1024),
    ("arialnb",  "LibreFranklin-Bold.ttf",       48,     20,      1024),
    ("anewhope", "LibreFranklin-SemiBold.ttf",   48,     24,      1024),
    ("ocr_a",    "LibreFranklin-Regular.ttf",    48,     12,      1024),
]

# Where the .fontdat header keeps mPointSize: 256 glyphs of 28 bytes, then it.
FONTDAT_POINT_SIZE_OFFSET = 256 * 28


def retail_point_size(assets: Path, name: str) -> int | None:
    """mPointSize out of the game's own .fontdat, if the game is here."""
    path = assets / "fonts" / (name + ".fontdat")
    if not path.is_file():
        return None
    data = path.read_bytes()
    if len(data) < FONTDAT_POINT_SIZE_OFFSET + 2:
        return None
    return struct.unpack_from("<h", data, FONTDAT_POINT_SIZE_OFFSET)[0]


def build_one(name, face, size, point_size, atlas, out_dir, quiet=False):
    face_path = SRC / face
    if not face_path.is_file():
        raise SystemExit("missing face: %s" % face_path)

    # The JSON is the readable form of what went into the .jkxfont, and
    # preview.py reads it. It is derivable from the two files beside it, so it
    # goes somewhere temporary rather than into the repository.
    scratch = tempfile.mkdtemp(prefix="fontgen-")
    cmd = [
        sys.executable, str(HERE / "msdf.py"),
        "--font", str(face_path),
        "--size", str(size),
        "--point-size", str(point_size),
        "--atlas", str(atlas),
        "--charset", CHARSET,
        "--out-image", str(out_dir / (name + ".png")),
        "--out-meta", os.path.join(scratch, name + ".json"),
        "--out-font", str(out_dir / (name + ".jkxfont")),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit("failed to build %s" % name)
    if not quiet:
        print("%-10s %-30s %2d point from a %d texel em" % (name, face, point_size, size))


def main(argv):
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--assets", type=Path,
                   help="a game base/ directory, to read the real point sizes from")
    p.add_argument("--check", action="store_true",
                   help="rebuild into a temporary directory and compare, rather than "
                        "writing: this is what CI runs so the checked-in atlases "
                        "cannot drift from the tool that made them")
    args = p.parse_args(argv[1:])

    plan = []
    for name, face, size, point_size, atlas in FONTS:
        if args.assets:
            real = retail_point_size(args.assets, name)
            if real:
                if real != point_size:
                    print("%s: the game says %d point, the manifest says %d - using %d"
                          % (name, real, point_size, real))
                point_size = real
            else:
                print("%s: no .fontdat in %s, using the manifest's %d"
                      % (name, args.assets, point_size))
        plan.append((name, face, size, point_size, atlas))

    if args.check:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_dir = Path(tmp)
            for entry in plan:
                build_one(*entry, out_dir=tmp_dir, quiet=True)

            stale = []
            for name, _, _, _, _ in plan:
                for ext in (".jkxfont", ".png"):
                    have = OUT / (name + ext)
                    want = tmp_dir / (name + ext)
                    if not have.is_file() or not filecmp.cmp(have, want, shallow=False):
                        stale.append(name + ext)

            if stale:
                print("out of date, regenerate with tools/fontgen/build_fonts.py:")
                for s in stale:
                    print("  " + s)
                return 1

            print("%d font(s) match the generator" % len(plan))
            return 0

    OUT.mkdir(parents=True, exist_ok=True)
    for entry in plan:
        build_one(*entry, out_dir=OUT)

    print("wrote %d font(s) to %s" % (len(plan), OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

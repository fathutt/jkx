#!/usr/bin/env python3
"""Blank every surface's shader name in a Ghoul2 model, and change nothing else.

This exists because of a measurement. Every one of the eighty-two surfaces in
the retail models/players/kyle/model.glm carries an EMPTY shader name, so a
retail character's textures come from its .skin file and from nowhere else. The
gamecode said the opposite - "it still loads the default skin's tga's because
they're referenced in the .glm" - and had said it since 2003, and that sentence
is the reason a skin which fails to register was thought to be harmless. It is
not: it leaves the model with no materials at all, drawn through the default
shader.

The fixture's own model.glm bakes a shader into its one surface, which means it
can never show that failure. Rather than generate a second model - the committed
one has tags and a bone count that the gamecode's animation setup needs, and a
model built from make_test_glm.py's defaults is not accepted as a player - this
takes the committed model and removes exactly one thing from it. Same geometry,
same bones, same tags, same offsets; a surface hierarchy whose names are gone.

MDXM surface hierarchy entry (mdx_format.h):

    char  name[64]
    dword flags
    char  shader[64]        <- the 64 bytes this blanks
    int   shaderIndex
    int   parentIndex
    int   numChildren
    int   childIndexes[numChildren]

Usage:
    glm_strip_shaders.py <in.glm> <out.glm>
    glm_strip_shaders.py --check
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

MDXM_IDENT = b"2LGM"
HEADER = 8 + 64 + 64          # ident, version, name, animName
SURF_FIXED = 64 + 4 + 64 + 4 + 4 + 4


def strip(data: bytes) -> tuple[bytes, int]:
    """Return the model with blank surface shader names, and how many it blanked."""
    if len(data) < HEADER + 28 or data[:4] != MDXM_IDENT:
        raise ValueError("not a Ghoul2 model (no 2LGM ident)")

    (_animIndex, _numBones, _numLODs, _ofsLODs,
     numSurfaces, ofsSurfHierarchy, _ofsEnd) = struct.unpack_from("<7i", data, HEADER)

    out = bytearray(data)
    offset = ofsSurfHierarchy
    blanked = 0

    for _ in range(numSurfaces):
        if offset + SURF_FIXED > len(out):
            raise ValueError("surface hierarchy runs past the end of the file")
        shader_at = offset + 64 + 4
        if any(out[shader_at:shader_at + 64]):
            blanked += 1
        out[shader_at:shader_at + 64] = b"\0" * 64
        num_children = struct.unpack_from("<i", out, offset + 64 + 4 + 64 + 8)[0]
        if num_children < 0:
            raise ValueError("negative child count in the surface hierarchy")
        offset += SURF_FIXED + num_children * 4

    return bytes(out), blanked


def check() -> int:
    """Build a model, strip it, and prove that only the names went.

    The negative control is the point: a version of this that wrote zeros over
    the wrong 64 bytes would still produce a file, and the file would still
    load, and the model would still be missing its textures - which is exactly
    the thing being tested for, so it would pass. So the check asserts on the
    bytes that must NOT have changed as well as on the ones that must.
    """
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import make_test_glm

    data = make_test_glm.build()
    stripped, blanked = strip(data)

    if len(stripped) != len(data):
        print("error: stripping changed the file length", file=sys.stderr)
        return 1
    if blanked < 1:
        print("error: nothing was blanked, so this check proves nothing",
              file=sys.stderr)
        return 1

    differing = [i for i, (a, b) in enumerate(zip(data, stripped)) if a != b]
    if not differing:
        print("error: the file came back unchanged", file=sys.stderr)
        return 1

    (_ai, _nb, _nl, _ol, numSurfaces, ofsSurfHierarchy,
     _oe) = struct.unpack_from("<7i", data, HEADER)

    # Every changed byte has to sit inside a shader name field, and the whole
    # header and every LOD has to be untouched.
    allowed: set[int] = set()
    offset = ofsSurfHierarchy
    for _ in range(numSurfaces):
        shader_at = offset + 64 + 4
        allowed.update(range(shader_at, shader_at + 64))
        num_children = struct.unpack_from("<i", data, offset + 64 + 4 + 64 + 8)[0]
        offset += SURF_FIXED + num_children * 4

    stray = [i for i in differing if i not in allowed]
    if stray:
        print(f"error: {len(stray)} byte(s) changed outside a shader name, "
              f"first at {stray[0]}", file=sys.stderr)
        return 1

    # And stripping twice is stripping once.
    twice, again = strip(stripped)
    if twice != stripped or again != 0:
        print("error: stripping is not idempotent", file=sys.stderr)
        return 1

    print(f"glm_strip_shaders: {numSurfaces} surface(s), {blanked} name(s) "
          f"blanked, {len(differing)} byte(s) changed, all inside shader names")
    print("OK")
    return 0


def main(argv: list[str]) -> int:
    if len(argv) == 2 and argv[1] == "--check":
        return check()
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[-3].strip(), file=sys.stderr)
        print("usage: %s <in.glm> <out.glm> | --check" % argv[0], file=sys.stderr)
        return 2

    source = Path(argv[1])
    stripped, blanked = strip(source.read_bytes())
    Path(argv[2]).write_bytes(stripped)
    print("%s: %d shader name(s) blanked" % (argv[2], blanked))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

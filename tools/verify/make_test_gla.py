#!/usr/bin/env python3
"""Write a skeleton, so that there is something to hang bones and bolts on.

make_test_glm.py avoids needing one of these by naming its animation "*default",
which mdx_format.h says means "no .gla required". That was the right shortcut for
getting a map to draw, and it is the wrong one for everything after: a mesh with
no skeleton has no bones, so nothing that asks for a bone by name can be tested,
and that is most of Ghoul2 - G2API_AddBolt, G2API_GetBoltMatrix, the bone cache,
bolted weapons and sabers. It is also what blocks the Jedi Outcast lane on the
bench, because JK2 hard-codes "kyle" as the player model and its cgame turns a
missing animation set into an error rather than a warning.

So this writes a real one. MDXA version 6, one frame, a chain of named bones.

    make_test_gla.py <out.gla> [--name PATH] [--bones a,b,c]
    make_test_gla.py --check

The default bone list is not invented. It is the set of names the game asks for
by name across code/ and games/jo/, arranged into the hierarchy the real
_humanoid has: every "WARNING: Failed to add bone X" the bench printed is a line
in this list.

FORMAT NOTES, since none of this is documented outside the struct definitions:

  * Both offset arrays are relative to the start of the array, not to the file -
    the same convention as MDXM, and the same way to get it four bytes wrong.
  * An mdxaSkel_t is NOT sizeof(mdxaSkel_t) long. children[] is declared [1] and
    the real size is "&((mdxaSkel_t *)0)->children[numChildren]", so a bone with
    no children is four bytes shorter than the struct.
  * A frame is an array of three-byte little-endian indices, one per bone, into
    a pool of compressed bones - an index, not a byte offset.
  * A compressed bone is seven unsigned shorts: w, x, y, z of a quaternion at
    (v / 16383) - 2, then the translation at (v / 64) - 512. See
    MC_UnCompressQuat in matcomp.cpp, which is the only specification there is.
"""

import struct
import sys

MDXA_IDENT = (ord('2') | (ord('L') << 8) | (ord('G') << 16) | (ord('A') << 24))
MDXA_VERSION = 6
MAX_QPATH = 64

# name, parent, offset from the parent
DEFAULT_SKELETON = [
    ("model_root",   -1, (0.0, 0.0,  0.0)),
    ("Motion",        0, (0.0, 0.0,  0.0)),
    ("pelvis",        1, (0.0, 0.0, 24.0)),
    ("lower_lumbar",  2, (0.0, 0.0,  8.0)),
    ("upper_lumbar",  3, (0.0, 0.0,  8.0)),
    ("thoracic",      4, (0.0, 0.0,  8.0)),
    ("cervical",      5, (0.0, 0.0,  8.0)),
    ("cranium",       6, (0.0, 0.0,  6.0)),
    ("face",          7, (0.0, 4.0,  0.0)),
    ("rhumerus",      5, (0.0, -8.0, 4.0)),
    ("lhumerus",      5, (0.0,  8.0, 4.0)),
    ("rhand",         9, (0.0, -8.0, 0.0)),
    ("lhand",        10, (0.0,  8.0, 0.0)),
    ("rtalus",        2, (0.0, -4.0, -24.0)),
    ("ltalus",        2, (0.0,  4.0, -24.0)),
]

# A quaternion component encodes as (value + 2) * 16383 and a translation as
# (value + 512) * 64, which is the inverse of what MC_UnCompressQuat does.
QUAT_BIAS = 2.0
QUAT_SCALE = 16383.0
XLAT_BIAS = 512.0
XLAT_SCALE = 64.0


def qpath(name):
    b = name.encode("ascii")
    if len(b) >= MAX_QPATH:
        raise ValueError("name too long: %s" % name)
    return b + b"\0" * (MAX_QPATH - len(b))


def identity_matrix():
    """mdxaBone_t: three rows of four floats, rotation then translation."""
    return struct.pack("<12f",
                       1.0, 0.0, 0.0, 0.0,
                       0.0, 1.0, 0.0, 0.0,
                       0.0, 0.0, 1.0, 0.0)


def compressed_bone(offset):
    """One pool entry: no rotation, and the offset as the translation."""
    def quat(v):
        return int(round((v + QUAT_BIAS) * QUAT_SCALE))

    def xlat(v):
        return int(round((v + XLAT_BIAS) * XLAT_SCALE))

    words = [quat(1.0), quat(0.0), quat(0.0), quat(0.0),
             xlat(offset[0]), xlat(offset[1]), xlat(offset[2])]
    for w in words:
        if not 0 <= w <= 0xFFFF:
            raise ValueError("value out of range for the compressed form: %d" % w)
    return struct.pack("<7H", *words)


def skel_entry(name, parent, children):
    """One bone. The trailing array is exactly numChildren long, not one."""
    out = qpath(name)
    out += struct.pack("<I", 0)              # flags
    out += struct.pack("<i", parent)
    out += identity_matrix()                 # BasePoseMat
    out += identity_matrix()                 # BasePoseMatInv, its own inverse
    out += struct.pack("<i", len(children))
    for c in children:
        out += struct.pack("<i", c)
    return out


def index3(value):
    if not 0 <= value < (1 << 24):
        raise ValueError("pool index does not fit in three bytes: %d" % value)
    return bytes([value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF])


def build(gla_name="models/players/_humanoid/_humanoid", skeleton=None):
    skeleton = skeleton or DEFAULT_SKELETON
    num_bones = len(skeleton)
    num_frames = 1

    children = [[] for _ in skeleton]
    for i, (_, parent, _) in enumerate(skeleton):
        if parent >= 0:
            children[parent].append(i)

    header_size = 4 + 4 + MAX_QPATH + 4 + 4 * 6

    # The skeleton: an offset array, then the bones, both relative to the start
    # of the offset array.
    offsets_size = 4 * num_bones
    entries = [skel_entry(name, parent, children[i])
               for i, (name, parent, _) in enumerate(skeleton)]

    offsets = []
    cursor = offsets_size
    for e in entries:
        offsets.append(cursor)
        cursor += len(e)

    skel_block = b"".join(struct.pack("<i", o) for o in offsets) + b"".join(entries)

    ofs_skel = header_size
    ofs_frames = ofs_skel + len(skel_block)

    # One index per bone per frame, and each bone gets its own pool entry so the
    # bones sit somewhere rather than all at the origin.
    frames = b"".join(index3(bone) for _ in range(num_frames)
                      for bone in range(num_bones))
    pad = (-len(frames)) % 4
    frames += b"\0" * pad

    ofs_comp_bone_pool = ofs_frames + len(frames)
    pool = b"".join(compressed_bone(offset) for _, _, offset in skeleton)

    ofs_end = ofs_comp_bone_pool + len(pool)

    header = struct.pack("<2i", MDXA_IDENT, MDXA_VERSION)
    header += qpath(gla_name)
    header += struct.pack("<f", 1.0)         # fScale
    header += struct.pack("<2i", num_frames, ofs_frames)
    header += struct.pack("<i", num_bones)
    header += struct.pack("<2i", ofs_comp_bone_pool, ofs_skel)
    header += struct.pack("<i", ofs_end)

    assert len(header) == header_size, (len(header), header_size)

    return header + skel_block + frames + pool


def check():
    data = build()
    failures = []

    ident, version = struct.unpack_from("<2i", data, 0)
    if ident != MDXA_IDENT:
        failures.append("ident is %08x, not %08x" % (ident, MDXA_IDENT))
    if version != MDXA_VERSION:
        failures.append("version is %d, not %d" % (version, MDXA_VERSION))

    base = 4 + 4 + MAX_QPATH + 4
    num_frames, ofs_frames, num_bones, ofs_pool, ofs_skel, ofs_end = \
        struct.unpack_from("<6i", data, base)

    if ofs_end != len(data):
        failures.append("ofsEnd is %d, file is %d bytes" % (ofs_end, len(data)))
    if num_frames < 1:
        failures.append("R_LoadMDXA rejects a file with no frames")

    # The skeleton, walked the way the engine walks it: the offset array sits at
    # sizeof(mdxaHeader_t) and every offset is relative to that.
    header_size = base + 4 * 6
    if ofs_skel != header_size:
        failures.append("ofsSkel is %d, expected %d" % (ofs_skel, header_size))

    seen = {}
    for i in range(num_bones):
        ofs = struct.unpack_from("<i", data, header_size + 4 * i)[0]
        at = header_size + ofs
        if not header_size < at < ofs_frames:
            failures.append("bone %d sits at %d, outside the skeleton block" % (i, at))
            continue
        name = data[at:at + MAX_QPATH].split(b"\0")[0].decode("ascii")
        parent = struct.unpack_from("<i", data, at + MAX_QPATH + 4)[0]
        seen[name] = i
        if parent < -1 or parent >= num_bones:
            failures.append("bone %s claims parent %d" % (name, parent))
        if parent >= i:
            failures.append("bone %s is declared before its parent, which the "
                            "hierarchy walk assumes it is not" % name)

    if "model_root" not in seen:
        failures.append("no model_root, which is what the game asks for first")

    # Every index has to name a pool entry that exists.
    pool_entries = (ofs_end - ofs_pool) // 14
    for frame in range(num_frames):
        for bone in range(num_bones):
            at = ofs_frames + (frame * num_bones + bone) * 3
            idx = data[at] | (data[at + 1] << 8) | (data[at + 2] << 16)
            if idx >= pool_entries:
                failures.append("frame %d bone %d indexes pool entry %d of %d"
                                % (frame, bone, idx, pool_entries))

    # And the compressed form has to survive a round trip through the engine's
    # own arithmetic, because getting the bias backwards is silent: it produces a
    # skeleton that loads and stands somewhere else.
    for bone, (name, _, offset) in enumerate(DEFAULT_SKELETON):
        at = ofs_pool + bone * 14
        words = struct.unpack_from("<7H", data, at)
        w, x, y, z = (v / QUAT_SCALE - QUAT_BIAS for v in words[:4])
        xlat = tuple(v / XLAT_SCALE - XLAT_BIAS for v in words[4:])
        if abs(w - 1.0) > 1e-3 or max(abs(x), abs(y), abs(z)) > 1e-3:
            failures.append("bone %s does not decode to no rotation" % name)
        for a, b in zip(xlat, offset):
            if abs(a - b) > 1e-2:
                failures.append("bone %s decodes to %s, not %s" % (name, xlat, offset))
                break

    for f in failures:
        print("error: %s" % f, file=sys.stderr)
    if failures:
        return 1

    print("test skeleton: %d bytes, %d bone(s), %d frame(s), root '%s'"
          % (len(data), num_bones, num_frames, DEFAULT_SKELETON[0][0]))
    return 0


def main(argv):
    args = argv[1:]
    if "--check" in args:
        return check()

    name = "models/players/_humanoid/_humanoid"
    bones = None
    path = None
    i = 0
    while i < len(args):
        if args[i] == "--name" and i + 1 < len(args):
            name = args[i + 1]
            i += 2
        elif args[i] == "--bones" and i + 1 < len(args):
            wanted = [b.strip() for b in args[i + 1].split(",") if b.strip()]
            byname = {b[0]: b for b in DEFAULT_SKELETON}
            missing = [b for b in wanted if b not in byname]
            if missing:
                print("not in the default skeleton: %s" % ", ".join(missing),
                      file=sys.stderr)
                return 2
            # Keep the declared order, so parents still come before children.
            keep = [b for b in DEFAULT_SKELETON if b[0] in wanted]
            index = {b[0]: n for n, b in enumerate(keep)}
            bones = [(n, index.get(DEFAULT_SKELETON[p][0], -1) if p >= 0 else -1, o)
                     for n, p, o in keep]
            i += 2
        elif path is None:
            path = args[i]
            i += 1
        else:
            print("unexpected argument: %s" % args[i], file=sys.stderr)
            return 2

    if path is None:
        print("usage: %s <out.gla> [--name PATH] [--bones a,b,c]" % argv[0],
              file=sys.stderr)
        return 2

    data = build(name, bones)
    with open(path, "wb") as f:
        f.write(data)
    print("%s: %d bytes, %d bones, animation name %s"
          % (path, len(data), len(bones or DEFAULT_SKELETON), name))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

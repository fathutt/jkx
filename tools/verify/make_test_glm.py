#!/usr/bin/env python3
"""Write a player model, so that a map can be spawned on.

The last rung of docs/Backlog.md section 16. With the map, the item tables and
the head-up display in place, the server gets as far as spawning a player and
stops:

    ERROR: Cannot fall back to default model stormtrooper!

Every player in Jedi Academy is a Ghoul2 mesh - g_client.cpp asks for
models/players/<name>/model.glm and an .md3 will not do - so there is no way
past this without one, and retail models are the game's.

So this writes one. A single quad on a single bone, in MDXM version 6.

The shortcut that makes it small is mdx_format.h's own: a mesh whose animName is
"*default" needs no .gla beside it. The comment there says the name is "used
when making special simple ghoul2 models, usually from MD3 files", which is
exactly what this is. Without it the file would have to come with a skeleton and
a compressed bone pool, and be ten times the size for no more coverage.

    make_test_glm.py <out.glm> [--shader NAME] [--surface NAME]
                     [--gla NAME] [--gla-bones N] [--bone I] [--model-name PATH]
    make_test_glm.py --check

--gla names the skeleton this mesh animates against, without the .gla extension;
the default is still "*default", which needs no file. Pass a real one together
with make_test_gla.py and the mesh gets real bones - which is what anything
testing bolts, the bone cache or a bolted weapon needs.

--check writes nothing and verifies the offsets it would write. Every offset in
this format is relative to something different - the hierarchy's to the start of
the offset array, a surface's back to the file header, and it is negative - so
getting one wrong produces a model that loads and draws somewhere unexpected
rather than an error.
"""

import struct
import sys

MDXM_IDENT = (ord('2') | (ord('L') << 8) | (ord('G') << 16) | (ord('M') << 24))
MDXM_VERSION = 6
MAX_QPATH = 64

# mdx_format.h: a mesh naming this as its animation file is not asking for one.
DEFAULT_GLA_NAME = "*default"

# One bone, referenced by every vertex at full weight. The packed word below is
# the format's, not ours: two bits of weight count at the top, five bits per
# bone reference, and two-bit overflows for the weights.
WEIGHT_COUNT_SHIFT = 30

HALF = 12.0


def qpath(name):
    b = name.encode("ascii")
    if len(b) >= MAX_QPATH:
        raise ValueError("name too long: %s" % name)
    return b + b"\0" * (MAX_QPATH - len(b))


def surf_hierarchy(surface, shader):
    """One entry, no children, no parent.

    Size is "&((mdxmSurfHierarchy_t *)0)->childIndexes[numChildren]", so with no
    children it still carries one int - the array is declared [1] and the
    engine's own size calculation reads it that way.
    """
    out = qpath(surface)
    out += struct.pack("<I", 0)          # flags
    out += qpath(shader)
    out += struct.pack("<i", 0)          # shaderIndex, filled in at load
    out += struct.pack("<i", -1)         # parentIndex: root
    out += struct.pack("<i", 0)          # numChildren
    out += struct.pack("<i", 0)          # childIndexes[1], present even at zero
    return out


def vertices():
    """Four corners of a quad, all on bone 0.

    mdxmVertex_t is normal, position, the packed weight word, then four weight
    bytes - thirty-two bytes, and the comment in mdx_format.h says that size is
    deliberate. One weight of 1023/1023 is the whole vertex on one bone: the
    low eight bits go in BoneWeightings[0] and the top two into the packed word.
    """
    out = b""
    packed = (0 << WEIGHT_COUNT_SHIFT)   # weight count 0 means one weight
    packed |= 0x300                      # top two bits of a full 1023 weight
    for x, y in ((-HALF, -HALF), (HALF, -HALF), (HALF, HALF), (-HALF, HALF)):
        out += struct.pack("<3f", 0.0, 0.0, 1.0)        # normal
        out += struct.pack("<3f", x, y, 0.0)            # position
        out += struct.pack("<I", packed)
        out += bytes([0xFF, 0, 0, 0])                   # low bits of the weight
    return out


def texcoords():
    out = b""
    for s, t in ((0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)):
        out += struct.pack("<2f", s, t)
    return out


def triangles():
    return struct.pack("<6i", 0, 1, 2, 0, 2, 3)


def build(surface="body", shader="jkx/smoke",
          gla_name=DEFAULT_GLA_NAME, num_bones=1, bone=0, model_name=None):
    header_size = 4 + 4 + MAX_QPATH * 2 + 4 * 7
    num_surfaces = 1
    num_lods = 1

    hierarchy_offsets_size = 4 * num_surfaces
    hierarchy = surf_hierarchy(surface, shader)

    ofs_surf_hierarchy = header_size
    # The engine asserts that a hierarchy entry is at
    # (byte *)surfIndexes + offsets[i] - so these are relative to the start of
    # the offset array, not to the file.
    hierarchy_block = struct.pack("<i", hierarchy_offsets_size) + hierarchy

    ofs_lods = ofs_surf_hierarchy + len(hierarchy_block)

    # Inside a LOD: the LOD header, then one offset per surface, then the
    # surfaces. The offsets are relative to the start of the LOD.
    lod_header_size = 4
    lod_offsets_size = 4 * num_surfaces
    surface_start = lod_header_size + lod_offsets_size

    tris = triangles()
    verts = vertices()
    sts = texcoords()

    surface_header_size = 4 * 10
    ofs_triangles = surface_header_size
    ofs_verts = ofs_triangles + len(tris)
    # Texture coordinates follow the vertex block; the engine reads them as an
    # array of mdxmVertexTexCoord_t starting after numVerts vertices.
    ofs_bone_references = ofs_verts + len(verts) + len(sts)
    # Local bone slot 0 of this surface names a bone in the skeleton. With the
    # fake "*default" skeleton that can only be bone 0; with a real .gla beside
    # it, this is how the quad gets hung off a named bone.
    bone_references = struct.pack("<i", bone)
    surface_end = ofs_bone_references + len(bone_references)

    surf = struct.pack("<i", 0)                     # ident
    surf += struct.pack("<i", 0)                    # thisSurfaceIndex
    surf += struct.pack("<i", -(ofs_lods + surface_start))   # ofsHeader, back to the file header
    surf += struct.pack("<2i", 4, ofs_verts)
    surf += struct.pack("<2i", 2, ofs_triangles)
    surf += struct.pack("<2i", 1, ofs_bone_references)
    surf += struct.pack("<i", surface_end)          # ofsEnd
    surf += tris + verts + sts + bone_references

    assert len(surf) == surface_end, (len(surf), surface_end)

    lod_end = surface_start + len(surf)
    lod = struct.pack("<i", lod_end)
    # G2_FindSurface adds this to the address of the offset array, not to the
    # start of the LOD - it steps over the mdxmLOD_t first and then adds. Writing
    # it relative to the LOD put every surface four bytes late, which is not an
    # error anywhere: the engine read ident as thisSurfaceIndex, thisSurfaceIndex
    # as ofsHeader, and indexed the hierarchy array by -324.
    lod += struct.pack("<i", surface_start - lod_header_size)
    lod += surf

    ofs_end = ofs_lods + len(lod)

    header = struct.pack("<2i", MDXM_IDENT, MDXM_VERSION)
    header += qpath(model_name or "models/players/jkx/model.glm")
    header += qpath(gla_name)
    header += struct.pack("<i", 0)                  # animIndex
    header += struct.pack("<i", num_bones)
    header += struct.pack("<2i", num_lods, ofs_lods)
    header += struct.pack("<2i", num_surfaces, ofs_surf_hierarchy)
    header += struct.pack("<i", ofs_end)

    assert len(header) == header_size, (len(header), header_size)

    return header + hierarchy_block + lod


def check():
    data = build()
    failures = []

    ident, version = struct.unpack_from("<2i", data, 0)
    if ident != MDXM_IDENT:
        failures.append("ident is %08x, not %08x" % (ident, MDXM_IDENT))
    if version != MDXM_VERSION:
        failures.append("version is %d, not %d" % (version, MDXM_VERSION))

    base = 4 + 4 + MAX_QPATH * 2 + 4
    num_bones, num_lods, ofs_lods, num_surfaces, ofs_hier, ofs_end = \
        struct.unpack_from("<6i", data, base)

    if ofs_end != len(data):
        failures.append("ofsEnd is %d, file is %d bytes" % (ofs_end, len(data)))
    for name, ofs in (("ofsLODs", ofs_lods), ("ofsSurfHierarchy", ofs_hier)):
        if not 0 < ofs < len(data):
            failures.append("%s is %d, outside a %d byte file" % (name, ofs, len(data)))

    # The one the engine asserts on: a hierarchy entry sits at the offset array
    # plus its own offset.
    hier_offset = struct.unpack_from("<i", data, ofs_hier)[0]
    if hier_offset != 4 * num_surfaces:
        failures.append("hierarchy offset is %d, expected %d"
                        % (hier_offset, 4 * num_surfaces))

    # The LOD's surface offsets, which are relative to the offset array in the
    # same way. This is the one that was wrong and reported nothing: a surface
    # four bytes late still parses, it just parses the neighbouring fields.
    indexes_at = ofs_lods + 4
    surf_at = indexes_at + struct.unpack_from("<i", data, indexes_at)[0]
    expected_surf_at = indexes_at + 4 * num_surfaces
    if surf_at != expected_surf_at:
        failures.append("LOD surface offset puts surface 0 at %d, expected %d"
                        % (surf_at, expected_surf_at))
    surf_ident, this_index = struct.unpack_from("<2i", data, surf_at)
    if surf_ident != 0 or this_index != 0:
        failures.append("surface 0 reads ident %d index %d, so the offset is off"
                        % (surf_ident, this_index))

    # And the one that is negative on purpose.
    ofs_header = struct.unpack_from("<i", data, surf_at + 8)[0]
    if surf_at + ofs_header != 0:
        failures.append("surface ofsHeader is %d from %d, which is not the file "
                        "header" % (ofs_header, surf_at))

    for f in failures:
        print("error: %s" % f, file=sys.stderr)
    if failures:
        return 1

    print("test model: %d bytes, %d surface(s), %d LOD(s), %d bone, animation "
          "'%s'" % (len(data), num_surfaces, num_lods, num_bones, DEFAULT_GLA_NAME))
    return 0


def main(argv):
    args = argv[1:]
    if "--check" in args:
        return check()

    surface, shader, path = "body", "jkx/smoke", None
    gla_name, num_bones, bone, model_name = DEFAULT_GLA_NAME, 1, 0, None
    i = 0
    while i < len(args):
        if args[i] == "--shader" and i + 1 < len(args):
            shader = args[i + 1]
            i += 2
        elif args[i] == "--surface" and i + 1 < len(args):
            surface = args[i + 1]
            i += 2
        elif args[i] == "--gla" and i + 1 < len(args):
            gla_name = args[i + 1]
            i += 2
        elif args[i] == "--gla-bones" and i + 1 < len(args):
            num_bones = int(args[i + 1])
            i += 2
        elif args[i] == "--bone" and i + 1 < len(args):
            bone = int(args[i + 1])
            i += 2
        elif args[i] == "--model-name" and i + 1 < len(args):
            model_name = args[i + 1]
            i += 2
        elif path is None:
            path = args[i]
            i += 1
        else:
            print("unexpected argument: %s" % args[i], file=sys.stderr)
            return 2

    if path is None:
        print("usage: %s <out.glm> [--shader NAME] [--surface NAME]" % argv[0],
              file=sys.stderr)
        return 2

    if bone >= num_bones:
        print("bone %d is outside a %d bone skeleton" % (bone, num_bones),
              file=sys.stderr)
        return 2

    data = build(surface, shader, gla_name, num_bones, bone, model_name)
    with open(path, "wb") as f:
        f.write(data)
    print("%s: %d bytes, surface %s, shader %s, animation %s, bone %d"
          % (path, len(data), surface, shader, gla_name, bone))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

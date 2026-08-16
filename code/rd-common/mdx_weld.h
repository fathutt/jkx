/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#pragma once

#include <stddef.h>

// The visible facets along the seams of a character, and where they come from.
//
// Kyle has had a hard edge across his shoulder and another across his face
// since 2003, and the first guess - that this is a renderer problem - is wrong.
// The measurement, taken over the retail models/players/kyle/model.glm:
//
//   2920 vertices in LOD 0, at 1725 distinct positions. 859 of those positions
//   carry more than one vertex; 646 of the duplicates are inside a single
//   surface and 213 span two.
//
// A vertex is duplicated wherever the texture mapping is cut, because a vertex
// carries one texture coordinate, and again wherever the model is cut into
// parts, because Ghoul2 draws a body as separate surfaces. Each copy also
// carries its own normal, and the exporter computed those per copy rather than
// per point in space. So two triangles that meet along an edge in the mesh are
// lit as if they did not, and the seam is visible exactly where the mapping or
// the surface boundary is - the shoulder and the face, which is what a person
// looking at Kyle reports.
//
// The fix is to give every copy of a point the same normal. Not all of them:
// the same measurement says the angle between coincident normals runs the whole
// way from zero to a hundred and eighty degrees, and the wide ones are real
// edges - fingers, a belt, the rim of a nostril, and the caps that close a limb
// off when a surface is switched away. Averaging those would round off geometry
// that is meant to be sharp. So the weld takes an angle, and pairs further
// apart than that are left alone.
//
// This works on the bytes rather than through mdx_format.h, for the same reason
// mdx_check.cpp does: it is then a unit that a test can build on its own, with
// a model the test wrote itself, and no renderer behind it.

// One vertex, as a place and a normal that can be changed.
//
// The callers hold their vertices differently - an MDXM is a byte walk through
// a file, an MD3 has been unpacked into structs by the time it gets here, and
// its frames are separate arrays - so what they share is a list of pointers
// rather than a layout. Gathering that list is a few lines at each call site
// and it keeps the part worth testing in one place.
typedef struct {
	const float	*position;		// three floats, compared on their exact bits
	float		*normal;		// three floats, replaced when it welds
} weldVertex_t;

// Weld the normals of coincident vertices in a gathered list.
//
// Vertices are matched on the exact bits of their position, which is what an
// exporter that duplicated a point writes: not a near-match, the same three
// floats. Every vertex is averaged from the normals the list arrived with, so
// the result does not depend on the order they sit in.
//
// Returns how many normals changed, or -1 if the angle has no meaning.
int MDX_WeldVertexNormals( const weldVertex_t *verts, int count, float maxAngleDegrees );

// Weld the normals of coincident vertices in every LOD of an MDXM.
//
// The data must already have passed MDX_CheckModel - this trusts the offsets it
// walks. Vertices are matched on the exact bits of their position, which is
// what an exporter that duplicated a point writes: not a near-match, the same
// three floats.
//
// Returns how many normals changed, or -1 if the data is not an MDXM this can
// walk.
//
// It is not idempotent and does not claim to be. Every vertex is averaged from
// the normals the file arrived with, so one pass does not depend on the order
// the vertices sit in - but a second pass reads the results of the first, and a
// vertex that has moved can now be inside the angle limit of a neighbour that
// was outside it. It converges, and the test measures how fast, but the
// contract is one pass over data as loaded.
int MDX_WeldNormals( unsigned char *data, size_t len, float maxAngleDegrees );

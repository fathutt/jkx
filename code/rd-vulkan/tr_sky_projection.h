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

// Which way a point on the sky box faces, and where on a face a direction
// lands. The two are inverses of each other, and until now they lived as two
// unrelated tables in the middle of two unrelated functions in tr_sky.cpp:
// st_to_vec inside MakeSkyVec, vec_to_st inside AddSkyPolygon.
//
// They are here because a third caller needs them. Building a cubemap out of
// the six box faces means asking, for every texel of the cube, which box face
// that direction belongs to and where on it - which is exactly the second
// table - and the answer has to agree with the first one exactly, or the
// cubemap draws a different sky from the one the box drew.
//
// Header-only and free of the engine on purpose: it is float arithmetic and two
// tables, so it can be tested without a renderer, a window or a graphics card.
// See tests/sky_projection_test.cpp, which round-trips every face.
//
// The axes are +X, -X, +Y, -Y, +Z, -Z, in that order. Note that this is NOT the
// order the six images are named in - ParseSkyParms reads rt, bk, lf, ft, up,
// dn and DrawSkyBox looks them up through sky_texorder = { 0, 2, 1, 3, 4, 5 },
// which swaps the second and third. Looking along +Y shows "bk".

#include <math.h>

// 1 = s, 2 = t, 3 = the box size. A negative entry means the negated component.
static const int sky_st_to_vec[6][3] =
{
	{  3, -1,  2 },
	{ -3,  1,  2 },

	{  1,  3,  2 },
	{ -1, -3,  2 },

	{ -2, -1,  3 },		// straight up
	{  2, -1, -3 }		// straight down
};

// The inverse: s = [0]/[2], t = [1]/[2], same negation convention.
static const int sky_vec_to_st[6][3] =
{
	{ -2,  3,  1 },
	{  2,  3, -1 },

	{  1,  3,  2 },
	{ -1,  3, -2 },

	{ -2, -1,  3 },
	{ -2,  1, -3 }
};

// The cube map's own parameterisation, which is a different thing from the box
// above and is not negotiable: it is fixed by the Vulkan and OpenGL
// specifications, in the (ma, sc, tc) table that says which face a direction
// selects and where on that face it lands. Layer order is +X, -X, +Y, -Y, +Z,
// -Z, and s = (sc/|ma| + 1)/2, t = (tc/|ma| + 1)/2.
//
// It exists here because the box tables and this one agree on exactly one face
// out of six - +Y - and that was the face the test fixture's camera happened to
// look at. Building the cube's layers with the box's table therefore produced a
// picture that was correct in the middle of the screen and rotated or mirrored
// at every edge, and the box/cube comparison showed it as a red border with a
// clean centre. Which is a hard defect to read backwards from a screenshot and
// a trivial one to read off a table.
//
// Same encoding as above: 1 = s, 2 = t, 3 = the major axis, negative for the
// negated component.
static const int sky_cube_st_to_vec[6][3] =
{
	{  3, -2, -1 },
	{ -3, -2,  1 },

	{  1,  3,  2 },
	{  1, -3, -2 },

	{  1, -2,  3 },
	{ -1, -2, -3 }
};

// The inverse: s = [0]/[2], t = [1]/[2].
static const int sky_cube_vec_to_st[6][3] =
{
	{ -3, -2,  1 },
	{  3, -2, -1 },

	{  1,  3,  2 },
	{  1, -3, -2 },

	{  1, -2,  3 },
	{ -1, -2, -3 }
};

/*
================
SkyVecForST

The direction of the point at (s,t) on a face, with s and t in [-1,1]. Scale is
arbitrary - callers normalise or multiply by the box size themselves.
================
*/
static inline void SkyVecForST( int axis, float s, float t, float out[3] )
{
	float b[3];
	int j, k;

	b[0] = s;
	b[1] = t;
	b[2] = 1.0f;

	for ( j = 0; j < 3; j++ ) {
		k = sky_st_to_vec[axis][j];
		out[j] = ( k < 0 ) ? -b[-k - 1] : b[k - 1];
	}
}

/*
================
SkyAxisForVec

Which of the six faces a direction belongs to: the one its largest component
points at. Ties go the same way AddSkyPolygon takes them, which is what makes
this the inverse of the table above rather than merely a similar idea.
================
*/
static inline int SkyAxisForVec( const float v[3] )
{
	const float ax = (float)fabs( v[0] );
	const float ay = (float)fabs( v[1] );
	const float az = (float)fabs( v[2] );

	if ( ax > ay && ax > az ) {
		return ( v[0] < 0.0f ) ? 1 : 0;
	}
	if ( ay > az && ay > ax ) {
		return ( v[1] < 0.0f ) ? 3 : 2;
	}
	return ( v[2] < 0.0f ) ? 5 : 4;
}

/*
================
SkySTForVec

Where on its face a direction lands, in the same [-1,1] the function above
takes. Returns false when the direction is edge-on to the face, which is the
divide-by-zero AddSkyPolygon skips rather than clamps.
================
*/
static inline int SkySTForVec( int axis, const float v[3], float *s, float *t )
{
	int j;
	float dv;

	j = sky_vec_to_st[axis][2];
	dv = ( j > 0 ) ? v[j - 1] : -v[-j - 1];

	if ( dv < 0.001f ) {
		return 0;
	}

	j = sky_vec_to_st[axis][0];
	*s = ( j < 0 ) ? -v[-j - 1] / dv : v[j - 1] / dv;

	j = sky_vec_to_st[axis][1];
	*t = ( j < 0 ) ? -v[-j - 1] / dv : v[j - 1] / dv;

	return 1;
}

/*
================
SkyCubeVecForST

The direction the hardware will sample when it reaches texel (s,t) of cube face
`face`, with s and t in [-1,1] across the face image and t increasing downwards,
the way cube face images are stored.

This is what the cubemap builder has to walk, rather than the box's own
parameterisation: the builder fills the layers, and the hardware reads them
back by its own rule, so the two only meet if the builder speaks the hardware's
rule.
================
*/
static inline void SkyCubeVecForST( int face, float s, float t, float out[3] )
{
	float b[3];
	int j, k;

	b[0] = s;
	b[1] = t;
	b[2] = 1.0f;

	for ( j = 0; j < 3; j++ ) {
		k = sky_cube_st_to_vec[face][j];
		out[j] = ( k < 0 ) ? -b[-k - 1] : b[k - 1];
	}
}

/*
================
SkyCubeSTForVec

Where on cube face `face` a direction lands. Returns false when the direction
does not point at that face at all.
================
*/
static inline int SkyCubeSTForVec( int face, const float v[3], float *s, float *t )
{
	int j;
	float dv;

	j = sky_cube_vec_to_st[face][2];
	dv = ( j > 0 ) ? v[j - 1] : -v[-j - 1];

	if ( dv < 0.001f ) {
		return 0;
	}

	j = sky_cube_vec_to_st[face][0];
	*s = ( j < 0 ) ? -v[-j - 1] / dv : v[j - 1] / dv;

	j = sky_cube_vec_to_st[face][1];
	*t = ( j < 0 ) ? -v[-j - 1] / dv : v[j - 1] / dv;

	return 1;
}

/*
================
SkyTexCoordForST

The texture coordinate the box path samples for a point at (s,t) on a face.
BuildSkyTexCoords does this to every grid vertex; the cubemap builder has to do
the same thing to every texel, so it is written down once here.

The vertical flip is not decoration - it is why a face drawn from the cubemap
without it comes out upside down.
================
*/
static inline void SkyTexCoordForST( float s, float t, float *u, float *v )
{
	*u = ( s + 1.0f ) * 0.5f;
	*v = 1.0f - ( t + 1.0f ) * 0.5f;
}

/*
================
SkyCubeSourceForTexel

The whole of the cubemap build, minus the pixels: for the texel at (cs,ct) on
cube face `face`, which of the six box images it should be copied from and
where in that image. Returns false when the direction is edge-on, which cannot
happen for a texel centre but is checked rather than assumed.

Note that `axis` is a box axis and still has to go through sky_texorder to
become an image index - the renderer owns that table, so it is done by the
caller.

It is here rather than inline in the builder so that a test with no renderer can
walk the same path the builder walks. That test is not decoration: this function
is the one place the two parameterisations meet, and the first version of it
used the box's table for both halves.
================
*/
static inline int SkyCubeSourceForTexel( int face, float cs, float ct,
	int *axis, float *u, float *v )
{
	float dir[3];
	float fs, ft;

	SkyCubeVecForST( face, cs, ct, dir );

	*axis = SkyAxisForVec( dir );

	if ( !SkySTForVec( *axis, dir, &fs, &ft ) ) {
		return 0;
	}

	SkyTexCoordForST( fs, ft, u, v );

	return 1;
}

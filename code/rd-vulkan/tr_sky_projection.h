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

/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// The sky box parameterisation, round-tripped.
//
// A cubemap built out of the six box faces has to sample the same texel for a
// given direction that the box itself did, or it draws a different sky. That
// makes the two tables in tr_sky_projection.h inverses of each other by
// requirement rather than by intention, and inverses are exactly the kind of
// claim that is cheap to check and expensive to get wrong: the first guess at
// which face is straight ahead was off by one entry, and only a picture caught
// it.
//
// No renderer, no window, no graphics card. Float arithmetic and two tables.

#include "../code/rd-vulkan/tr_sky_projection.h"

#include <cstdio>
#include <cmath>

namespace
{

int g_failures = 0;
int g_checks = 0;
int g_printed = 0;

// A wrong table fails every point on a face, and three hundred identical lines
// bury the one fact worth reading.
bool shouldPrint()
{
	return g_printed++ < 8;
}

void check( bool condition, const char *what )
{
	g_checks++;
	if ( !condition ) {
		g_failures++;
		if ( shouldPrint() ) {
			printf( "FAIL: %s\n", what );
		}
	}
}

const char *axisName( int axis )
{
	static const char *names[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
	return ( axis >= 0 && axis < 6 ) ? names[axis] : "?";
}

// Every face, over a grid of (s,t): the direction that comes out has to be
// claimed by the same face and project back to the same point on it.
void testRoundTrip()
{
	const int steps = 16;

	for ( int axis = 0; axis < 6; axis++ ) {
		for ( int i = 0; i <= steps; i++ ) {
			for ( int j = 0; j <= steps; j++ ) {
				// Kept inside the face rather than on its edge: on an edge two
				// faces are equally entitled to the direction, which is a real
				// ambiguity and not something to assert about.
				const float s = -0.9f + 1.8f * ( (float)j / (float)steps );
				const float t = -0.9f + 1.8f * ( (float)i / (float)steps );

				float v[3];
				SkyVecForST( axis, s, t, v );

				const int back = SkyAxisForVec( v );
				if ( back != axis ) {
					if ( shouldPrint() ) {
						printf( "FAIL: %s (%.2f,%.2f) came back as %s\n",
							axisName( axis ), s, t, axisName( back ) );
					}
					g_failures++;
					g_checks++;
					continue;
				}

				float s2 = 0.0f, t2 = 0.0f;
				const int ok = SkySTForVec( axis, v, &s2, &t2 );

				g_checks++;
				if ( !ok ) {
					if ( shouldPrint() ) {
						printf( "FAIL: %s (%.2f,%.2f) projected edge-on\n",
							axisName( axis ), s, t );
					}
					g_failures++;
					continue;
				}

				if ( fabsf( s2 - s ) > 1e-4f || fabsf( t2 - t ) > 1e-4f ) {
					if ( shouldPrint() ) {
						printf( "FAIL: %s (%.3f,%.3f) came back as (%.3f,%.3f)\n",
							axisName( axis ), s, t, s2, t2 );
					}
					g_failures++;
				}
			}
		}
	}
}

// The centre of each face is the axis itself, and nothing else.
void testCentres()
{
	static const float dirs[6][3] = {
		{  1,  0,  0 }, { -1,  0,  0 },
		{  0,  1,  0 }, {  0, -1,  0 },
		{  0,  0,  1 }, {  0,  0, -1 },
	};

	for ( int axis = 0; axis < 6; axis++ ) {
		const int got = SkyAxisForVec( dirs[axis] );
		check( got == axis, "the cardinal direction belongs to its own face" );
		if ( got != axis ) {
			printf( "      %s was claimed by %s\n", axisName( axis ), axisName( got ) );
			continue;
		}

		float s = 9.0f, t = 9.0f;
		check( SkySTForVec( axis, dirs[axis], &s, &t ) != 0,
			"the cardinal direction projects onto its face" );
		check( fabsf( s ) < 1e-5f && fabsf( t ) < 1e-5f,
			"the cardinal direction lands at the centre of its face" );
	}
}

// The texture coordinate is flipped vertically, and a cubemap built without
// that flip draws an upside-down sky. Pinned here so the flip cannot quietly
// disappear.
void testTexCoord()
{
	float u = 0.0f, v = 0.0f;

	SkyTexCoordForST( -1.0f, -1.0f, &u, &v );
	check( fabsf( u - 0.0f ) < 1e-6f && fabsf( v - 1.0f ) < 1e-6f,
		"s,t of -1,-1 is the bottom left of the image" );

	SkyTexCoordForST( 1.0f, 1.0f, &u, &v );
	check( fabsf( u - 1.0f ) < 1e-6f && fabsf( v - 0.0f ) < 1e-6f,
		"s,t of 1,1 is the top right of the image" );

	SkyTexCoordForST( 0.0f, 0.0f, &u, &v );
	check( fabsf( u - 0.5f ) < 1e-6f && fabsf( v - 0.5f ) < 1e-6f,
		"the middle of a face is the middle of its image" );
}

} // namespace

int main( void )
{
	testRoundTrip();
	testCentres();
	testTexCoord();

	if ( g_failures > g_printed ) {
		printf( "... and %d more\n", g_failures - g_printed );
	}
	printf( "sky projection: %d checks, %d failure(s)\n", g_checks, g_failures );
	return g_failures ? 1 : 0;
}

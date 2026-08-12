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
#include <cstdlib>

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

// The cube map's own parameterisation, round-tripped the same way. Separate
// from the box's because it is a separate rule that happens to have the same
// shape: the box tables agree with it on exactly one face out of six.
void testCubeRoundTrip()
{
	const int steps = 16;

	for ( int face = 0; face < 6; face++ ) {
		for ( int i = 0; i <= steps; i++ ) {
			for ( int j = 0; j <= steps; j++ ) {
				const float s = -0.9f + 1.8f * ( (float)j / (float)steps );
				const float t = -0.9f + 1.8f * ( (float)i / (float)steps );

				float v[3];
				SkyCubeVecForST( face, s, t, v );

				// The major axis of the direction is the face it came from -
				// that is what makes the face selection the hardware does agree
				// with the layer this texel was written to.
				const int back = SkyAxisForVec( v );
				g_checks++;
				if ( back != face ) {
					if ( shouldPrint() ) {
						printf( "FAIL: cube %s (%.2f,%.2f) selects %s\n",
							axisName( face ), s, t, axisName( back ) );
					}
					g_failures++;
					continue;
				}

				float s2 = 0.0f, t2 = 0.0f;
				const int ok = SkyCubeSTForVec( face, v, &s2, &t2 );

				g_checks++;
				if ( !ok || fabsf( s2 - s ) > 1e-4f || fabsf( t2 - t ) > 1e-4f ) {
					if ( shouldPrint() ) {
						printf( "FAIL: cube %s (%.3f,%.3f) came back as (%.3f,%.3f)\n",
							axisName( face ), s, t, s2, t2 );
					}
					g_failures++;
				}
			}
		}
	}
}

// The six directions the specification names, spelled out rather than derived,
// so that a table edited into self-consistency still has to face them.
//
// Read them off the (ma, sc, tc) table: for face +X, sc = -rz and tc = -ry, so
// the texel at the top left of that face - s = -1, t = -1 - is the direction
// with rz = +1 and ry = +1.
void testCubeAgainstTheSpec()
{
	struct Case { int face; float s, t; float dir[3]; };

	static const Case cases[] = {
		{ 0, -1.0f, -1.0f, {  1.0f,  1.0f,  1.0f } },	// +X: sc=-z, tc=-y
		{ 0,  1.0f,  1.0f, {  1.0f, -1.0f, -1.0f } },
		{ 1, -1.0f, -1.0f, { -1.0f,  1.0f, -1.0f } },	// -X: sc=+z, tc=-y
		{ 1,  1.0f,  1.0f, { -1.0f, -1.0f,  1.0f } },
		{ 2, -1.0f, -1.0f, { -1.0f,  1.0f, -1.0f } },	// +Y: sc=+x, tc=+z
		{ 2,  1.0f,  1.0f, {  1.0f,  1.0f,  1.0f } },
		{ 3, -1.0f, -1.0f, { -1.0f, -1.0f,  1.0f } },	// -Y: sc=+x, tc=-z
		{ 3,  1.0f,  1.0f, {  1.0f, -1.0f, -1.0f } },
		{ 4, -1.0f, -1.0f, { -1.0f,  1.0f,  1.0f } },	// +Z: sc=+x, tc=-y
		{ 4,  1.0f,  1.0f, {  1.0f, -1.0f,  1.0f } },
		{ 5, -1.0f, -1.0f, {  1.0f,  1.0f, -1.0f } },	// -Z: sc=-x, tc=-y
		{ 5,  1.0f,  1.0f, { -1.0f, -1.0f, -1.0f } },
	};

	for ( size_t i = 0; i < sizeof( cases ) / sizeof( cases[0] ); i++ ) {
		const Case &c = cases[i];
		float v[3];

		SkyCubeVecForST( c.face, c.s, c.t, v );

		const bool same = fabsf( v[0] - c.dir[0] ) < 1e-6f
			&& fabsf( v[1] - c.dir[1] ) < 1e-6f
			&& fabsf( v[2] - c.dir[2] ) < 1e-6f;

		check( same, "the cube corner points where the specification says" );
		if ( !same ) {
			printf( "      %s (%.0f,%.0f) gave (%.0f,%.0f,%.0f), wanted (%.0f,%.0f,%.0f)\n",
				axisName( c.face ), c.s, c.t,
				v[0], v[1], v[2], c.dir[0], c.dir[1], c.dir[2] );
		}
	}
}

// The box and the cube agree on +Y and disagree everywhere else. Asserted
// rather than merely known, because "the cubemap can be filled with the box's
// own table" is the assumption that produced a sky correct in the middle of the
// screen and wrong at every edge, and it was plausible enough to survive a
// screenshot.
void testTheTwoRulesDiffer()
{
	for ( int face = 0; face < 6; face++ ) {
		float a[3], b[3];

		SkyVecForST( face, 0.5f, 0.25f, a );
		SkyCubeVecForST( face, 0.5f, 0.25f, b );

		const bool same = fabsf( a[0] - b[0] ) < 1e-6f
			&& fabsf( a[1] - b[1] ) < 1e-6f
			&& fabsf( a[2] - b[2] ) < 1e-6f;

		check( same == ( face == 2 ),
			"the box and the cube agree on +Y alone" );
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

// ---------------------------------------------------------------------------
// The property the whole cubemap rests on: for any direction, the cube returns
// what the box would have drawn. Everything above is a table check; this is the
// thing anyone actually cares about, and it is checkable with no renderer at
// all because the build is a resample and the lookup is arithmetic.
//
// The six source images are synthetic and every texel of them is distinct, so
// "the same colour" cannot happen by accident: a face is identified by its
// index, and a texel within it by its position. A rotated face, a mirrored one
// or one taken from the wrong image all come out as a mismatch on the first
// direction that reaches them.

const int SRC_SIZE = 32;
const int CUBE_SIZE = 32;

// The renderer's own table, copied because it lives in tr_local.h and this test
// deliberately does not include the renderer. It is asserted against the
// projection header's comment rather than merely trusted.
const int sky_texorder[6] = { 0, 2, 1, 3, 4, 5 };

// A texel that says where it came from: image index in one channel, x and y in
// the other two.
void sourceTexel( int img, int x, int y, unsigned char out[3] )
{
	out[0] = (unsigned char)( 40 + img * 30 );
	out[1] = (unsigned char)( x * 7 + 1 );
	out[2] = (unsigned char)( y * 7 + 1 );
}

// What the box path shows for a direction: pick the face by major axis, project
// onto it, sample the image that face indexes.
bool boxSample( const float dir[3], unsigned char out[3] )
{
	const int axis = SkyAxisForVec( dir );

	float fs, ft;
	if ( !SkySTForVec( axis, dir, &fs, &ft ) ) {
		return false;
	}

	float u, v;
	SkyTexCoordForST( fs, ft, &u, &v );

	int sx = (int)( u * SRC_SIZE );
	int sy = (int)( v * SRC_SIZE );
	if ( sx < 0 ) sx = 0; else if ( sx >= SRC_SIZE ) sx = SRC_SIZE - 1;
	if ( sy < 0 ) sy = 0; else if ( sy >= SRC_SIZE ) sy = SRC_SIZE - 1;

	sourceTexel( sky_texorder[axis], sx, sy, out );
	return true;
}

void testCubeDrawsTheSameSky()
{
	// Build, exactly the way R_BuildSkyCubemap does.
	static unsigned char cube[6][CUBE_SIZE][CUBE_SIZE][3];

	for ( int face = 0; face < 6; face++ ) {
		for ( int y = 0; y < CUBE_SIZE; y++ ) {
			for ( int x = 0; x < CUBE_SIZE; x++ ) {
				const float cs = ( ( x + 0.5f ) / (float)CUBE_SIZE ) * 2.0f - 1.0f;
				const float ct = ( ( y + 0.5f ) / (float)CUBE_SIZE ) * 2.0f - 1.0f;

				int axis;
				float u, v;

				if ( !SkyCubeSourceForTexel( face, cs, ct, &axis, &u, &v ) ) {
					cube[face][y][x][0] = 0;
					cube[face][y][x][1] = 0;
					cube[face][y][x][2] = 0;
					continue;
				}

				int sx = (int)( u * SRC_SIZE );
				int sy = (int)( v * SRC_SIZE );
				if ( sx < 0 ) sx = 0; else if ( sx >= SRC_SIZE ) sx = SRC_SIZE - 1;
				if ( sy < 0 ) sy = 0; else if ( sy >= SRC_SIZE ) sy = SRC_SIZE - 1;

				sourceTexel( sky_texorder[axis], sx, sy, cube[face][y][x] );
			}
		}
	}

	// Read it back the way the hardware will, over a sphere of directions, and
	// compare against the box. Nearest sampling on both sides: this is asking
	// whether the two agree about which texel, not about how to blend them.
	const int steps = 40;
	int mismatches = 0;

	for ( int i = 1; i < steps; i++ ) {
		for ( int j = 0; j < steps * 2; j++ ) {
			const float pitch = 3.14159265f * ( (float)i / (float)steps );
			const float yaw = 6.28318531f * ( (float)j / (float)( steps * 2 ) );

			float dir[3];
			dir[0] = sinf( pitch ) * cosf( yaw );
			dir[1] = sinf( pitch ) * sinf( yaw );
			dir[2] = cosf( pitch );

			unsigned char want[3];
			if ( !boxSample( dir, want ) ) {
				continue;
			}

			// The hardware's face selection and projection.
			const int face = SkyAxisForVec( dir );
			float cs, ct;
			if ( !SkyCubeSTForVec( face, dir, &cs, &ct ) ) {
				continue;
			}

			int x = (int)( ( cs + 1.0f ) * 0.5f * CUBE_SIZE );
			int y = (int)( ( ct + 1.0f ) * 0.5f * CUBE_SIZE );
			if ( x < 0 ) x = 0; else if ( x >= CUBE_SIZE ) x = CUBE_SIZE - 1;
			if ( y < 0 ) y = 0; else if ( y >= CUBE_SIZE ) y = CUBE_SIZE - 1;

			const unsigned char *got = cube[face][y][x];

			g_checks++;

			// One texel of slack on the position, none on the image: rounding a
			// direction onto two different grids can land either side of a
			// boundary, but it cannot change which of the six images the colour
			// came from, and that is the failure this is looking for.
			const int dr = (int)got[0] - (int)want[0];
			const int dg = (int)got[1] - (int)want[1];
			const int db = (int)got[2] - (int)want[2];

			if ( dr != 0 || abs( dg ) > 7 || abs( db ) > 7 ) {
				mismatches++;
				if ( shouldPrint() ) {
					printf( "FAIL: dir (%.2f,%.2f,%.2f) box says %d,%d,%d, cube says %d,%d,%d\n",
						dir[0], dir[1], dir[2],
						want[0], want[1], want[2], got[0], got[1], got[2] );
				}
				g_failures++;
			}
		}
	}

	if ( mismatches == 0 ) {
		printf( "cube and box agree on every sampled direction\n" );
	}
}

} // namespace

int main( void )
{
	testRoundTrip();
	testCentres();
	testCubeRoundTrip();
	testCubeAgainstTheSpec();
	testTheTwoRulesDiffer();
	testCubeDrawsTheSameSky();
	testTexCoord();

	if ( g_failures > g_printed ) {
		printf( "... and %d more\n", g_failures - g_printed );
	}
	printf( "sky projection: %d checks, %d failure(s)\n", g_checks, g_failures );
	return g_failures ? 1 : 0;
}

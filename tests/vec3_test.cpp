/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// The vector type, against the array it has to replace.
//
// Two things are being checked and only one of them is arithmetic.
//
//   1. The memory is the same memory. Savegames write vectors, BSPs and models
//      contain them, and the engine casts between float* and vec3_t in both
//      directions in dozens of places. So the layout is checked against a real
//      float[3] - written through one view and read through the other - rather
//      than against the type's own idea of itself. A type that agrees with
//      itself about its layout proves nothing.
//   2. The arithmetic agrees with the macros and functions it replaces. Each
//      operator is checked against the same computation spelled out in the old
//      way, on the same numbers, so that "it does what VectorSubtract did" is a
//      statement someone measured rather than assumed.

#include "../code/qcommon/jkx_vec3.h"

#include <cstdio>
#include <cstring>

namespace
{

int g_failures = 0;
int g_checks = 0;

void check( bool condition, const char *what )
{
	g_checks++;
	if ( !condition ) {
		g_failures++;
		printf( "FAIL: %s\n", what );
	}
}


// The old shape, so that the two can be compared rather than described.
typedef float vec3_array[3];


void testLayout()
{
	Vec3	v{ 1.0f, 2.0f, 3.0f };
	float	raw[3];

	check( sizeof( Vec3 ) == sizeof( vec3_array ), "the type is the size of the array" );

	// Written as the struct, read as the array.
	memcpy( raw, &v, sizeof( raw ) );
	check( raw[0] == 1.0f && raw[1] == 2.0f && raw[2] == 3.0f,
		"the struct's bytes read back as three floats in order" );

	// Written as the array, read as the struct.
	raw[0] = 4.0f; raw[1] = 5.0f; raw[2] = 6.0f;
	memcpy( &v, raw, sizeof( raw ) );
	check( v.x == 4.0f && v.y == 5.0f && v.z == 6.0f,
		"three floats in order read back as the struct" );

	// The subscript and the pointer are the same storage as the members. This
	// is what the engine's casts depend on.
	v[0] = 7.0f;
	check( v.x == 7.0f, "subscript zero is x" );
	check( v.data()[1] == v.y, "the pointer's second float is y" );
	check( &v.data()[2] == &v.z, "the pointer's third float is z" );

	// An array of them is a flat array of floats, which is what a vertex
	// buffer upload and a savegame chunk both assume.
	Vec3	many[3] = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } };
	float	flat[9];

	memcpy( flat, many, sizeof( flat ) );
	for ( int i = 0; i < 9; i++ ) {
		if ( flat[i] != (float)( i + 1 ) ) {
			check( false, "an array of vectors is a flat array of floats" );
			return;
		}
	}
	check( true, "an array of vectors is a flat array of floats" );
}


void testArithmeticAgreesWithTheOldWay()
{
	const float	ax = 1.5f, ay = -2.25f, az = 0.75f;
	const float	bx = -0.5f, by = 4.0f, bz = 2.5f;
	const Vec3	a{ ax, ay, az };
	const Vec3	b{ bx, by, bz };
	float		expect[3];
	Vec3		got;

	got = a + b;
	expect[0] = ax + bx; expect[1] = ay + by; expect[2] = az + bz;
	check( got.x == expect[0] && got.y == expect[1] && got.z == expect[2],
		"addition matches VectorAdd" );

	got = a - b;
	expect[0] = ax - bx; expect[1] = ay - by; expect[2] = az - bz;
	check( got.x == expect[0] && got.y == expect[1] && got.z == expect[2],
		"subtraction matches VectorSubtract" );

	got = a * 3.0f;
	expect[0] = ax * 3.0f; expect[1] = ay * 3.0f; expect[2] = az * 3.0f;
	check( got.x == expect[0] && got.y == expect[1] && got.z == expect[2],
		"scaling matches VectorScale" );

	check( 3.0f * a == a * 3.0f, "a scalar on either side is the same vector" );

	got = -a;
	check( got.x == -ax && got.y == -ay && got.z == -az,
		"negation matches VectorNegate" );

	check( Dot( a, b ) == ax * bx + ay * by + az * bz,
		"the dot product matches DotProduct" );

	{
		const Vec3	c = Cross( a, b );

		check( c.x == ay * bz - az * by
			&& c.y == az * bx - ax * bz
			&& c.z == ax * by - ay * bx,
			"the cross product matches CrossProduct" );
	}

	{
		Vec3	acc = a;

		acc += b;
		check( acc == a + b, "+= is the same as +" );
		acc -= b;
		check( acc == a, "-= undoes it" );
		acc *= 2.0f;
		check( acc == a * 2.0f, "*= is the same as *" );
	}

	// Exact, like VectorCompare. A tolerance belongs at a call site where
	// somebody can say how much.
	check( a == Vec3{ ax, ay, az }, "equality is exact and true when it should be" );
	check( a != b, "and false when it should be" );
	check( !( Vec3{ 0.0f, 0.0f, 0.0f } == Vec3{ 0.0f, 0.0f, 1e-30f } ),
		"and does not round a very small difference away" );
}


// The thing the migration exists to make possible, written down as a test so
// that it is a property rather than a hope.
void testWhatTheArrayCouldNotDo()
{
	Vec3	v{ 1.0f, 2.0f, 3.0f };
	Vec3	copy = v;

	copy.x = 9.0f;
	check( v.x == 1.0f && copy.x == 9.0f,
		"assignment copies rather than aliasing" );

	// sizeof through a parameter, which is the one that has bitten before: for
	// float[3] this would be the size of a pointer.
	struct Local {
		static size_t sizeOfParameter( Vec3 v ) { return sizeof( v ); }
	};
	check( Local::sizeOfParameter( v ) == 3 * sizeof( float ),
		"sizeof a parameter is the vector, not a pointer" );

	// Returned from a function.
	struct Maker {
		static Vec3 make() { return Vec3{ 4.0f, 5.0f, 6.0f }; }
	};
	check( Maker::make() == ( Vec3{ 4.0f, 5.0f, 6.0f } ),
		"a vector can be returned by value" );
}

}	// namespace


int main( void )
{
	testLayout();
	testArithmeticAgreesWithTheOldWay();
	testWhatTheArrayCouldNotDo();

	printf( "vec3: %i check(s), %i failure(s)\n", g_checks, g_failures );
	return g_failures ? 1 : 0;
}

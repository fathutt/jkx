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

// A three-component vector that is a type rather than an array.
//
// vec3_t is `typedef float vec3_t[3]`, and everything awkward about vectors in
// this engine follows from that one line. An array cannot be returned from a
// function, cannot be assigned, decays to a pointer the moment it is passed,
// and gives the size of a pointer when sizeof is applied to a parameter. That
// is why there are thirty-odd VectorSomething helpers doing what `a - b` would
// do, and why nothing anywhere can check that a function was handed three
// components rather than two or four.
//
// WHY THIS IS A NEW NAME AND NOT A REDEFINITION OF vec3_t.
//
// Changing the typedef in place would compile, and it would be silently wrong
// in the worst way this project knows. Today `void f( vec3_t out )` means "here
// is a pointer to the caller's array, write to it". As a struct that same
// signature means "here is a copy", so every output parameter in the engine -
// and there are hundreds - would keep compiling, keep running, and quietly
// write to a temporary that is thrown away on return. The compiler cannot flag
// it, because both readings are legal. A defect that survives compilation,
// survives the tests that do not look at the value, and changes what the game
// does is exactly the class this project spends its time digging out.
//
// So the migration is by name: this type is introduced beside vec3_t, call
// sites move over a file at a time, and vec3_t goes away when nothing uses it.
// Each move is small enough to read and to check against the binary.
//
// THE RULE FOR MIGRATED CODE, and it is the whole reason for the above:
//
//     inputs   const Vec3 &     (or by value, which is three floats)
//     outputs  Vec3 &           - NEVER Vec3, and never a pointer
//
// LAYOUT.
//
// The memory has to stay exactly what it was: three floats, in order, twelve
// bytes, no padding. Savegames write it, BSPs and models contain it, and a
// great deal of code casts between float* and vec3_t in both directions. The
// static_asserts below hold that, and tests/vec3_test.cpp checks it against a
// real float[3] rather than against its own arithmetic - a type that agrees
// with itself about its layout proves nothing.

#include <stddef.h>

struct Vec3
{
	float	x, y, z;

	// Element access, so that the thousands of existing v[0] / v[1] / v[2]
	// sites read the same after a file moves over. Bounds are not checked:
	// this is the innermost thing in the engine and an index that is not 0, 1
	// or 2 is a bug in the caller, not a runtime condition.
	float &operator[]( size_t i ) { return ( &x )[i]; }
	const float &operator[]( size_t i ) const { return ( &x )[i]; }

	// The pointer, spelled out. Every cast to float* that survives the
	// migration should go through this instead, so that "this is being handed
	// to something that wants an array" is visible rather than implied.
	float *data() { return &x; }
	const float *data() const { return &x; }

	Vec3 &operator+=( const Vec3 &b ) { x += b.x; y += b.y; z += b.z; return *this; }
	Vec3 &operator-=( const Vec3 &b ) { x -= b.x; y -= b.y; z -= b.z; return *this; }
	Vec3 &operator*=( float s ) { x *= s; y *= s; z *= s; return *this; }
};

inline Vec3 operator+( const Vec3 &a, const Vec3 &b )
{
	return Vec3{ a.x + b.x, a.y + b.y, a.z + b.z };
}

inline Vec3 operator-( const Vec3 &a, const Vec3 &b )
{
	return Vec3{ a.x - b.x, a.y - b.y, a.z - b.z };
}

inline Vec3 operator-( const Vec3 &a )
{
	return Vec3{ -a.x, -a.y, -a.z };
}

inline Vec3 operator*( const Vec3 &a, float s )
{
	return Vec3{ a.x * s, a.y * s, a.z * s };
}

inline Vec3 operator*( float s, const Vec3 &a )
{
	return a * s;
}

inline bool operator==( const Vec3 &a, const Vec3 &b )
{
	// Exact, like VectorCompare, and for the same reason: this answers "is it
	// the same vector", not "is it near enough". VectorCompare2 is the one
	// with a tolerance, and a tolerance belongs at the call site where somebody
	// can say how much.
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

inline bool operator!=( const Vec3 &a, const Vec3 &b )
{
	return !( a == b );
}

inline float Dot( const Vec3 &a, const Vec3 &b )
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 Cross( const Vec3 &a, const Vec3 &b )
{
	return Vec3{ a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x };
}

// The layout promises. Nothing here is decoration: the engine casts between
// this shape and float[3] in the savegame serialisers, in the BSP and model
// loaders and in every renderer path that uploads vertices.
static_assert( sizeof( Vec3 ) == 3 * sizeof( float ), "Vec3 must be three floats" );
static_assert( offsetof( Vec3, x ) == 0, "Vec3::x must be first" );
static_assert( offsetof( Vec3, y ) == sizeof( float ), "Vec3::y must follow x" );
static_assert( offsetof( Vec3, z ) == 2 * sizeof( float ), "Vec3::z must follow y" );

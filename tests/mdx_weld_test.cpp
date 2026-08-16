/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Welding the normals of coincident vertices, against models this file writes.
//
// The interesting half of this is what must NOT change. A weld that averages
// everything removes the visible seam being complained about and also rounds
// off every deliberate edge in the model - the fingers, the belt, the rim of a
// nostril, the caps that close a limb when a surface is switched off. Measured
// over the retail kyle: the angle between coincident normals runs from zero to
// a hundred and eighty degrees, with three hundred and sixty-three pairs in the
// 75-90 band and two hundred and sixty-three above ninety. So the threshold is
// the feature, and most of what is below is a test of the threshold rather than
// of the averaging.
//
// The models are built here rather than loaded, because a fixture read off disk
// can only ask the questions it happens to contain.

#include "../code/rd-common/mdx_weld.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vector>

static int failures = 0;

static void Fail( const char *what )
{
	printf( "  FAIL: %s\n", what );
	failures++;
}

static void Check( bool condition, const char *what )
{
	if ( !condition )
	{
		Fail( what );
	}
}

// ---------------------------------------------------------------------------
// A minimal MDXM, laid out the way the format says and nothing more.
//
//   header            ident, version, name[64], animName[64], animIndex,
//                     numBones, numLODs, ofsLODs, numSurfaces,
//                     ofsSurfHierarchy, ofsEnd
//   LOD               int ofsEnd, then one int per surface, each relative to
//                     the start of that array
//   surface           ten ints, then the vertices
//   vertex            normal[3], vertCoords[3], weights and bone indexes
// ---------------------------------------------------------------------------

struct Vertex
{
	float normal[3];
	float position[3];
};

static void PutInt( std::vector<unsigned char> &out, int value )
{
	unsigned char bytes[4];
	memcpy( bytes, &value, 4 );
	out.insert( out.end(), bytes, bytes + 4 );
}

static void PutFloat( std::vector<unsigned char> &out, float value )
{
	unsigned char bytes[4];
	memcpy( bytes, &value, 4 );
	out.insert( out.end(), bytes, bytes + 4 );
}

// One LOD, one surface per vector of vertices.
static std::vector<unsigned char> BuildModel( const std::vector< std::vector<Vertex> > &surfaces )
{
	std::vector<unsigned char> out;

	const int numSurfaces = (int)surfaces.size();

	// The bytes a real file starts with, in the order a real file has them.
	// These were the other way round and the weld's own constant was too, so the
	// two agreed and nine passing tests were run against a model that no loader
	// would accept. The negative control for this is not another test - it is
	// running the thing against a file off disk, which is what found it.
	out.push_back( '2' ); out.push_back( 'L' ); out.push_back( 'G' ); out.push_back( 'M' );
	PutInt( out, 6 );							// version
	out.resize( out.size() + 64, 0 );			// name
	out.resize( out.size() + 64, 0 );			// animName
	PutInt( out, 0 );							// animIndex
	PutInt( out, 1 );							// numBones
	PutInt( out, 1 );							// numLODs
	const size_t ofsLODsAt = out.size();
	PutInt( out, 0 );							// ofsLODs, filled in below
	PutInt( out, numSurfaces );
	PutInt( out, 0 );							// ofsSurfHierarchy, unused here
	const size_t ofsEndAt = out.size();
	PutInt( out, 0 );							// ofsEnd, filled in below

	const int ofsLODs = (int)out.size();
	memcpy( &out[ofsLODsAt], &ofsLODs, 4 );

	// The LOD's own size is not known until its surfaces are written, so the
	// surfaces go into a buffer of their own first.
	std::vector<unsigned char> body;
	std::vector<int> surfaceOffsets;

	const int offsetsSize = numSurfaces * 4;

	for ( int s = 0; s < numSurfaces; s++ )
	{
		surfaceOffsets.push_back( offsetsSize + (int)body.size() );

		const std::vector<Vertex> &verts = surfaces[s];
		const int header = 10 * 4;

		PutInt( body, 0 );						// ident
		PutInt( body, s );						// thisSurfaceIndex
		PutInt( body, 0 );						// ofsHeader
		PutInt( body, (int)verts.size() );
		PutInt( body, header );					// ofsVerts
		PutInt( body, 0 );						// numTriangles
		PutInt( body, header + (int)verts.size() * 32 );
		PutInt( body, 0 );						// numBoneReferences
		PutInt( body, header + (int)verts.size() * 32 );
		PutInt( body, header + (int)verts.size() * 32 );

		for ( size_t v = 0; v < verts.size(); v++ )
		{
			PutFloat( body, verts[v].normal[0] );
			PutFloat( body, verts[v].normal[1] );
			PutFloat( body, verts[v].normal[2] );
			PutFloat( body, verts[v].position[0] );
			PutFloat( body, verts[v].position[1] );
			PutFloat( body, verts[v].position[2] );
			PutInt( body, 0 );					// weights and bone indexes
			PutInt( body, 0 );
		}
	}

	PutInt( out, 4 + offsetsSize + (int)body.size() );	// the LOD's ofsEnd

	for ( int s = 0; s < numSurfaces; s++ )
	{
		PutInt( out, surfaceOffsets[s] );
	}

	out.insert( out.end(), body.begin(), body.end() );

	const int ofsEnd = (int)out.size();
	memcpy( &out[ofsEndAt], &ofsEnd, 4 );

	return out;
}

// Where the model's vertices ended up, so a test can read a normal back.
static const unsigned char *VertexAt( const std::vector<unsigned char> &model,
									  int surface, int vertex )
{
	int ofsLODs;
	memcpy( &ofsLODs, &model[8 + 64 + 64 + 4 + 4 + 4], 4 );
	int numSurfaces;
	memcpy( &numSurfaces, &model[8 + 64 + 64 + 4 + 4 + 4 + 4], 4 );

	const size_t offsetsAt = (size_t)ofsLODs + 4;
	int surfOffset;
	memcpy( &surfOffset, &model[offsetsAt + (size_t)surface * 4], 4 );

	const size_t surfAt = offsetsAt + (size_t)surfOffset;
	int ofsVerts;
	memcpy( &ofsVerts, &model[surfAt + 16], 4 );

	return &model[surfAt + (size_t)ofsVerts + (size_t)vertex * 32];
}

static void ReadNormal( const std::vector<unsigned char> &model, int surface, int vertex,
						float out[3] )
{
	const unsigned char *at = VertexAt( model, surface, vertex );
	memcpy( out, at, 12 );
}

static float AngleBetween( const float a[3], const float b[3] )
{
	float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];

	if ( dot > 1.0f ) dot = 1.0f;
	if ( dot < -1.0f ) dot = -1.0f;

	return acosf( dot ) * 180.0f / 3.14159265358979323846f;
}

static Vertex MakeVertex( float angleDegrees, float x, float y, float z )
{
	// A normal in the XY plane at the given angle from +X, so two vertices are
	// exactly the stated number of degrees apart and the test can say what it
	// expects rather than measure it.
	const float radians = angleDegrees * 3.14159265358979323846f / 180.0f;

	Vertex v;
	v.normal[0] = cosf( radians );
	v.normal[1] = sinf( radians );
	v.normal[2] = 0.0f;
	v.position[0] = x;
	v.position[1] = y;
	v.position[2] = z;

	return v;
}

// ---------------------------------------------------------------------------

static void TestTheModelLooksLikeAModel()
{
	// The first four bytes, as text, in the order a file has them.
	//
	// This exists because both the weld and this test had the ident written
	// backwards, agreed with each other, and passed nine cases against a model
	// no loader would accept - while the real function returned -1 for every
	// real file. An assertion a person can read is worth more here than a
	// constant compared against another constant.
	std::vector< std::vector<Vertex> > surfaces( 1 );
	surfaces[0].push_back( MakeVertex( 0.0f, 1.0f, 1.0f, 1.0f ) );

	const std::vector<unsigned char> model = BuildModel( surfaces );

	Check( model.size() > 4 && model[0] == '2' && model[1] == 'L'
		   && model[2] == 'G' && model[3] == 'M',
		   "a Ghoul2 model begins with the four characters 2LGM" );
}

static void TestSmoothPairIsWelded()
{
	std::vector< std::vector<Vertex> > surfaces( 1 );
	surfaces[0].push_back( MakeVertex( -10.0f, 1.0f, 2.0f, 3.0f ) );
	surfaces[0].push_back( MakeVertex(  10.0f, 1.0f, 2.0f, 3.0f ) );

	std::vector<unsigned char> model = BuildModel( surfaces );
	const int changed = MDX_WeldNormals( &model[0], model.size(), 60.0f );

	Check( changed == 2, "both halves of a twenty degree pair should move" );

	float a[3], b[3];
	ReadNormal( model, 0, 0, a );
	ReadNormal( model, 0, 1, b );

	Check( AngleBetween( a, b ) < 0.01f, "a welded pair should share one normal" );

	// And the shared normal is the one between them: +X.
	const float expected[3] = { 1.0f, 0.0f, 0.0f };
	Check( AngleBetween( a, expected ) < 0.01f, "the welded normal should be the average" );
}

static void TestHardEdgeSurvives()
{
	// The negative control, and the reason this file exists. A hundred degrees
	// apart is a real edge; a weld that takes it has removed geometry.
	std::vector< std::vector<Vertex> > surfaces( 1 );
	surfaces[0].push_back( MakeVertex( -50.0f, 1.0f, 2.0f, 3.0f ) );
	surfaces[0].push_back( MakeVertex(  50.0f, 1.0f, 2.0f, 3.0f ) );

	std::vector<unsigned char> model = BuildModel( surfaces );
	float wasA[3], wasB[3];
	ReadNormal( model, 0, 0, wasA );
	ReadNormal( model, 0, 1, wasB );

	const int changed = MDX_WeldNormals( &model[0], model.size(), 60.0f );

	Check( changed == 0, "a hundred degree edge should not be welded at sixty" );

	float a[3], b[3];
	ReadNormal( model, 0, 0, a );
	ReadNormal( model, 0, 1, b );

	Check( AngleBetween( a, wasA ) < 0.001f && AngleBetween( b, wasB ) < 0.001f,
		   "an edge left alone should be left exactly alone" );

	// The same model at a wider threshold does weld, which proves the case
	// above is the threshold refusing rather than the weld failing to see it.
	std::vector<unsigned char> again = BuildModel( surfaces );
	Check( MDX_WeldNormals( &again[0], again.size(), 120.0f ) == 2,
		   "the same pair should weld once the threshold allows it" );
}

static void TestPerVertexDecision()
{
	// Three copies of one point: two close together and one far from both. The
	// close pair must weld to each other and neither must pick up the third.
	std::vector< std::vector<Vertex> > surfaces( 1 );
	surfaces[0].push_back( MakeVertex(   0.0f, 4.0f, 5.0f, 6.0f ) );
	surfaces[0].push_back( MakeVertex(  20.0f, 4.0f, 5.0f, 6.0f ) );
	surfaces[0].push_back( MakeVertex( 150.0f, 4.0f, 5.0f, 6.0f ) );

	std::vector<unsigned char> model = BuildModel( surfaces );
	float wasC[3];
	ReadNormal( model, 0, 2, wasC );

	MDX_WeldNormals( &model[0], model.size(), 60.0f );

	float a[3], b[3], c[3];
	ReadNormal( model, 0, 0, a );
	ReadNormal( model, 0, 1, b );
	ReadNormal( model, 0, 2, c );

	Check( AngleBetween( a, b ) < 0.01f, "the close pair should agree" );

	const float expected[3] = { cosf( 10.0f * 3.14159265358979323846f / 180.0f ),
								sinf( 10.0f * 3.14159265358979323846f / 180.0f ), 0.0f };
	Check( AngleBetween( a, expected ) < 0.01f,
		   "the welded normal should be the average of the pair only" );
	Check( AngleBetween( c, wasC ) < 0.001f, "the far normal should not have moved" );
}

static void TestSurfacesAreCrossed()
{
	// The seam on a shoulder is between two surfaces, not inside one, so the
	// grouping has to span the whole LOD. Measured on the retail kyle: 213 of
	// its 859 shared positions do exactly this.
	std::vector< std::vector<Vertex> > surfaces( 2 );
	surfaces[0].push_back( MakeVertex( -15.0f, 7.0f, 8.0f, 9.0f ) );
	surfaces[1].push_back( MakeVertex(  15.0f, 7.0f, 8.0f, 9.0f ) );

	std::vector<unsigned char> model = BuildModel( surfaces );
	Check( MDX_WeldNormals( &model[0], model.size(), 60.0f ) == 2,
		   "a seam between two surfaces should weld" );

	float a[3], b[3];
	ReadNormal( model, 0, 0, a );
	ReadNormal( model, 1, 0, b );
	Check( AngleBetween( a, b ) < 0.01f, "and the two sides should agree afterwards" );
}

static void TestDifferentPositionsAreNotTouched()
{
	std::vector< std::vector<Vertex> > surfaces( 1 );
	surfaces[0].push_back( MakeVertex( -10.0f, 1.0f, 2.0f, 3.0f ) );
	surfaces[0].push_back( MakeVertex(  10.0f, 1.0f, 2.0f, 3.0001f ) );

	std::vector<unsigned char> model = BuildModel( surfaces );

	Check( MDX_WeldNormals( &model[0], model.size(), 60.0f ) == 0,
		   "two points a ten-thousandth apart are two points" );
}

static void TestCancellingGroupIsLeftAlone()
{
	// Three normals inside the limit of one another pairwise whose sum is very
	// nearly nothing. A zero-length normal is worse than a faceted one: it
	// makes the lighting undefined rather than merely wrong.
	std::vector< std::vector<Vertex> > surfaces( 1 );
	surfaces[0].push_back( MakeVertex(   0.0f, 0.0f, 0.0f, 0.0f ) );
	surfaces[0].push_back( MakeVertex( 120.0f, 0.0f, 0.0f, 0.0f ) );
	surfaces[0].push_back( MakeVertex( 240.0f, 0.0f, 0.0f, 0.0f ) );

	std::vector<unsigned char> model = BuildModel( surfaces );
	MDX_WeldNormals( &model[0], model.size(), 170.0f );

	for ( int v = 0; v < 3; v++ )
	{
		float n[3];
		ReadNormal( model, 0, v, n );
		const float length = sqrtf( n[0] * n[0] + n[1] * n[1] + n[2] * n[2] );
		Check( length > 0.9f && length < 1.1f, "no normal should come out of this with no length" );
	}
}

static void TestRefusals()
{
	std::vector< std::vector<Vertex> > surfaces( 1 );
	surfaces[0].push_back( MakeVertex( 0.0f, 1.0f, 1.0f, 1.0f ) );
	surfaces[0].push_back( MakeVertex( 5.0f, 1.0f, 1.0f, 1.0f ) );

	std::vector<unsigned char> model = BuildModel( surfaces );

	Check( MDX_WeldNormals( NULL, 100, 60.0f ) == -1, "no data is a refusal" );
	Check( MDX_WeldNormals( &model[0], 4, 60.0f ) == -1, "a header that does not fit is a refusal" );
	Check( MDX_WeldNormals( &model[0], model.size(), 0.0f ) == -1, "zero degrees is a refusal" );
	Check( MDX_WeldNormals( &model[0], model.size(), -1.0f ) == -1, "a negative angle is a refusal" );
	Check( MDX_WeldNormals( &model[0], model.size(), 180.0f ) == -1, "a straight angle is a refusal" );

	std::vector<unsigned char> notAModel = model;
	notAModel[0] = 'X';
	Check( MDX_WeldNormals( &notAModel[0], notAModel.size(), 60.0f ) == -1,
		   "a file that is not an MDXM is a refusal" );

	// And a model whose surface says it has more vertices than the file holds.
	// Under the sanitizers this is the case that matters: the data comes out of
	// a pk3, so a count that lies is a stranger's number driving a loop.
	std::vector<unsigned char> lying = model;
	int ofsLODs;
	memcpy( &ofsLODs, &lying[8 + 64 + 64 + 4 + 4 + 4], 4 );
	int surfOffset;
	memcpy( &surfOffset, &lying[(size_t)ofsLODs + 4], 4 );
	const size_t numVertsAt = (size_t)ofsLODs + 4 + (size_t)surfOffset + 12;
	const int huge = 1 << 20;
	memcpy( &lying[numVertsAt], &huge, 4 );

	Check( MDX_WeldNormals( &lying[0], lying.size(), 60.0f ) == -1,
		   "a vertex count past the end of the file is a refusal" );
}

static void TestConvergence()
{
	// Not idempotent, and the header says so. This measures how far from it: a
	// second pass over a chain of normals ten degrees apart can pull in a
	// neighbour that the first pass left outside the limit.
	std::vector< std::vector<Vertex> > surfaces( 1 );

	for ( int i = 0; i < 8; i++ )
	{
		surfaces[0].push_back( MakeVertex( (float)i * 10.0f, 2.0f, 2.0f, 2.0f ) );
	}

	std::vector<unsigned char> model = BuildModel( surfaces );

	// Counting how many normals changed says almost nothing here - a change of
	// a thousandth of a degree counts the same as a change of twenty - so the
	// measurement is how FAR the furthest one moved.
	float moved[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	for ( int pass = 0; pass < 4; pass++ )
	{
		std::vector<float> was( surfaces[0].size() * 3 );

		for ( size_t v = 0; v < surfaces[0].size(); v++ )
		{
			ReadNormal( model, 0, (int)v, &was[v * 3] );
		}

		MDX_WeldNormals( &model[0], model.size(), 45.0f );

		for ( size_t v = 0; v < surfaces[0].size(); v++ )
		{
			float now[3];
			ReadNormal( model, 0, (int)v, now );
			const float angle = AngleBetween( &was[v * 3], now );

			if ( angle > moved[pass] )
			{
				moved[pass] = angle;
			}
		}
	}

	Check( moved[0] > 1.0f, "the first pass should do something" );
	Check( moved[1] < moved[0], "the second pass should move less than the first" );
	Check( moved[3] < 0.5f, "and it should be settling rather than drifting" );

	printf( "  convergence: furthest normal moved %.2f, %.2f, %.2f, %.2f degrees\n",
			moved[0], moved[1], moved[2], moved[3] );
}

int main( void )
{
	printf( "mdx_weld_test\n" );

	TestTheModelLooksLikeAModel();
	TestSmoothPairIsWelded();
	TestHardEdgeSurvives();
	TestPerVertexDecision();
	TestSurfacesAreCrossed();
	TestDifferentPositionsAreNotTouched();
	TestCancellingGroupIsLeftAlone();
	TestRefusals();
	TestConvergence();

	if ( failures )
	{
		printf( "%d check(s) failed\n", failures );
		return 1;
	}

	printf( "OK\n" );
	return 0;
}

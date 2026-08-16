/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#include "mdx_weld.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <unordered_map>
#include <vector>

// The parts of the format this walks, spelled out rather than included, so this
// file builds on its own. These agree with mdx_check.cpp, which is the file
// that validates them before anything here is allowed to trust them.
// The four bytes at the start of the file are '2', 'L', 'G', 'M', and this is
// read back as one little-endian int - so the constant is the last byte first.
// It was written the other way round, which made every call refuse every model,
// and the test agreed with it because the model the test builds wrote its ident
// the same wrong way. A fixture that shares the defect cannot see the defect.
// mdx_format.h has it right and is the one to check against.
#define MDXM_IDENT			( ( 'M' << 24 ) + ( 'G' << 16 ) + ( 'L' << 8 ) + '2' )

#define MDXM_HDR_NUMLODS	( 8 + 64 + 64 + 4 + 4 )		// after ident, version, name, animName, animIndex, numBones
#define MDXM_HDR_OFSLODS	( MDXM_HDR_NUMLODS + 4 )
#define MDXM_HDR_NUMSURFS	( MDXM_HDR_OFSLODS + 4 )

#define MDXM_SURF_NUMVERTS	12							// mdxmSurface_t: ident, thisSurfaceIndex, ofsHeader, numVerts
#define MDXM_SURF_OFSVERTS	16
#define MDXM_VERT_SIZE		32							// normal[3], vertCoords[3], weights and bone indexes
#define MDXM_VERT_POS		12							// vertCoords, straight after the normal

namespace {

int ReadInt( const unsigned char *at )
{
	int value;
	memcpy( &value, at, sizeof( value ) );
	return value;
}

// A position, as the twelve bytes the file holds rather than as three numbers.
//
// The comparison has to be exact and it has to be on the bits. An exporter that
// duplicated a point wrote the same three floats twice, so there is nothing to
// tolerate; and a tolerance here would be a decision about which nearby points
// are "the same", which is a different and much harder question than the one
// this answers.
struct PositionKey
{
	unsigned char bytes[12];

	bool operator==( const PositionKey &other ) const
	{
		return memcmp( bytes, other.bytes, sizeof( bytes ) ) == 0;
	}
};

struct PositionHash
{
	size_t operator()( const PositionKey &key ) const
	{
		// FNV-1a. Twelve bytes, so the loop is short and the constant folding
		// is not worth reaching for a wider mix.
		size_t hash = (size_t)1469598103934665603ULL;

		for ( size_t i = 0; i < sizeof( key.bytes ); i++ )
		{
			hash ^= (size_t)key.bytes[i];
			hash *= (size_t)1099511628211ULL;
		}

		return hash;
	}
};

struct Normal
{
	float x, y, z;
};

}	// namespace

int MDX_WeldVertexNormals( const weldVertex_t *verts, int count, float maxAngleDegrees )
{
	// An angle of zero or less asks for nothing, and a hundred and eighty asks
	// to average a normal with its own opposite, which has no answer. Both are
	// refused rather than clamped: a caller passing either has made a mistake
	// that a silent adjustment would hide.
	if ( !( maxAngleDegrees > 0.0f ) || maxAngleDegrees >= 180.0f ) {
		return -1;
	}

	if ( verts == NULL || count <= 0 ) {
		return 0;
	}

	const float minDot = cosf( maxAngleDegrees * 3.14159265358979323846f / 180.0f );

	// The normals as they arrived. The new ones are computed from these and
	// written afterwards; doing it in place would make the result depend on the
	// order the vertices happen to be visited in, which is a property of the
	// file rather than of the geometry.
	std::vector<Normal> before( (size_t)count );
	std::unordered_map<PositionKey, std::vector<int>, PositionHash> byPosition;

	for ( int i = 0; i < count; i++ ) {
		PositionKey key;
		memcpy( key.bytes, verts[i].position, sizeof( key.bytes ) );

		before[(size_t)i].x = verts[i].normal[0];
		before[(size_t)i].y = verts[i].normal[1];
		before[(size_t)i].z = verts[i].normal[2];

		byPosition[key].push_back( i );
	}

	int changed = 0;

	for ( std::unordered_map<PositionKey, std::vector<int>, PositionHash>::const_iterator
			it = byPosition.begin(); it != byPosition.end(); ++it )
	{
		const std::vector<int> &group = it->second;

		if ( group.size() < 2 ) {
			continue;
		}

		for ( size_t a = 0; a < group.size(); a++ ) {
			const Normal &mine = before[(size_t)group[a]];
			float sx = mine.x, sy = mine.y, sz = mine.z;
			int taken = 1;

			for ( size_t b = 0; b < group.size(); b++ ) {
				if ( a == b ) {
					continue;
				}

				const Normal &other = before[(size_t)group[b]];
				const float dot = mine.x * other.x + mine.y * other.y + mine.z * other.z;

				// Each vertex asks the question from where it stands, so a
				// group holding a smooth pair and one hard edge welds the pair
				// and leaves the edge. A single verdict for the whole group
				// would have to choose between those.
				if ( dot < minDot ) {
					continue;
				}

				sx += other.x;
				sy += other.y;
				sz += other.z;
				taken++;
			}

			if ( taken < 2 ) {
				continue;
			}

			const float length = sqrtf( sx * sx + sy * sy + sz * sz );

			// Normals that cancel. Inside the angle limit this cannot happen
			// for a pair, but three or more can still sum to almost nothing,
			// and a zero-length normal is worse than a wrong one - it makes the
			// lighting undefined rather than merely faceted.
			if ( length < 1e-6f ) {
				continue;
			}

			const float nx = sx / length;
			const float ny = sy / length;
			const float nz = sz / length;
			float * const out = verts[group[a]].normal;

			if ( nx != out[0] || ny != out[1] || nz != out[2] ) {
				changed++;
			}

			out[0] = nx;
			out[1] = ny;
			out[2] = nz;
		}
	}

	return changed;
}

int MDX_WeldNormals( unsigned char *data, size_t len, float maxAngleDegrees )
{
	if ( data == NULL || len < (size_t)( MDXM_HDR_NUMSURFS + 4 ) ) {
		return -1;
	}

	if ( ReadInt( data ) != MDXM_IDENT ) {
		return -1;
	}

	const int numLODs = ReadInt( data + MDXM_HDR_NUMLODS );
	const int ofsLODs = ReadInt( data + MDXM_HDR_OFSLODS );
	const int numSurfaces = ReadInt( data + MDXM_HDR_NUMSURFS );

	if ( numLODs <= 0 || numSurfaces <= 0 || ofsLODs <= 0 || (size_t)ofsLODs >= len ) {
		return -1;
	}

	if ( !( maxAngleDegrees > 0.0f ) || maxAngleDegrees >= 180.0f ) {
		return -1;
	}

	int changed = 0;
	size_t lodAt = (size_t)ofsLODs;

	for ( int lod = 0; lod < numLODs; lod++ ) {
		if ( lodAt + 4 + (size_t)numSurfaces * 4 > len ) {
			return -1;
		}

		const int lodSize = ReadInt( data + lodAt );
		const size_t offsetsAt = lodAt + 4;

		// Every vertex of this LOD, gathered across its surfaces. A seam on a
		// shoulder is between two surfaces rather than inside one, so the
		// grouping has to span the whole LOD.
		std::vector<weldVertex_t> verts;

		for ( int s = 0; s < numSurfaces; s++ ) {
			const int surfOffset = ReadInt( data + offsetsAt + (size_t)s * 4 );

			if ( surfOffset < 0 ) {
				return -1;
			}

			const size_t surfAt = offsetsAt + (size_t)surfOffset;

			if ( surfAt + MDXM_SURF_OFSVERTS + 4 > len ) {
				return -1;
			}

			const int numVerts = ReadInt( data + surfAt + MDXM_SURF_NUMVERTS );
			const int ofsVerts = ReadInt( data + surfAt + MDXM_SURF_OFSVERTS );

			if ( numVerts < 0 || ofsVerts < 0 ) {
				return -1;
			}

			if ( surfAt + (size_t)ofsVerts + (size_t)numVerts * MDXM_VERT_SIZE > len ) {
				return -1;
			}

			// The vertices are read as floats from here on, so where they start
			// has to be a place a float may live. Every offset in this format is
			// a multiple of four when the file is the shape it claims to be -
			// and these offsets came out of a file, which is the whole reason
			// the rest of this function checks them against the length.
			if ( ( (uintptr_t)( data + surfAt + (size_t)ofsVerts ) & 3u ) != 0 ) {
				return -1;
			}

			for ( int v = 0; v < numVerts; v++ ) {
				const size_t at = surfAt + (size_t)ofsVerts + (size_t)v * MDXM_VERT_SIZE;
				weldVertex_t	entry;

				// The file holds these as three floats each, in host order by
				// the time this runs - R_LoadMDXM has already swapped them.
				entry.normal = (float *)( data + at );
				entry.position = (const float *)( data + at + MDXM_VERT_POS );
				verts.push_back( entry );
			}
		}

		const int welded = MDX_WeldVertexNormals(
			verts.empty() ? NULL : &verts[0], (int)verts.size(), maxAngleDegrees );

		if ( welded > 0 ) {
			changed += welded;
		}

		if ( lodSize <= 0 ) {
			break;
		}

		lodAt += (size_t)lodSize;

		if ( lodAt >= len ) {
			break;
		}
	}

	return changed;
}

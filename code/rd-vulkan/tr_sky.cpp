/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/


#include "tr_local.h"
#include "tr_sky_projection.h"

#define	myftol(x) ((int)(x))
#define SKY_SUBDIVISIONS		8
#define HALF_SKY_SUBDIVISIONS	(SKY_SUBDIVISIONS/2)

static float s_cloudTexCoords[6][SKY_SUBDIVISIONS + 1][SKY_SUBDIVISIONS + 1][2];
static const int sky_texorder[6] = { 0, 2, 1, 3, 4, 5 };
static vec3_t	s_skyPoints[SKY_SUBDIVISIONS + 1][SKY_SUBDIVISIONS + 1];
static float	s_skyTexCoords[SKY_SUBDIVISIONS + 1][SKY_SUBDIVISIONS + 1][2];
static float	sky_mins[2][6], sky_maxs[2][6];
static float	sky_min, sky_max;
static float	sky_min_depth;

// polygon to box side projection
static const vec3_t sky_clip[6] =
{
	{ 1, 1, 0},
	{ 1,-1, 0},
	{ 0,-1, 1},
	{ 0, 1, 1},
	{ 1, 0, 1},
	{-1, 0, 1}
};

/*
================
AddSkyPolygon
================
*/
static void AddSkyPolygon( int nump, vec3_t vecs )
{
	int		i;
	vec3_t	v;
	float	s, t;
	int		axis;
	float	*vp;

	// The table this used to carry is now in tr_sky_projection.h, next to its
	// inverse and next to a test that round-trips the two against each other.
	// They were a pair all along - one here, one in MakeSkyVec - and nothing
	// said so.

	// decide which face it maps to
	VectorCopy(vec3_origin, v);
	for (i = 0, vp = vecs; i < nump; i++, vp += 3)
	{
		VectorAdd(vp, v, v);
	}

	axis = SkyAxisForVec( v );

	// project new texture coords
	for (i = 0; i < nump; i++, vecs += 3)
	{
		if ( !SkySTForVec( axis, vecs, &s, &t ) )
			continue;	// edge-on to the face: don't divide by zero

		if (s < sky_mins[0][axis])
			sky_mins[0][axis] = s;
		if (t < sky_mins[1][axis])
			sky_mins[1][axis] = t;
		if (s > sky_maxs[0][axis])
			sky_maxs[0][axis] = s;
		if (t > sky_maxs[1][axis])
			sky_maxs[1][axis] = t;
	}
}

#define	ON_EPSILON		0.1f			// point on plane side epsilon
#define	MAX_CLIP_VERTS	64
/*
================
ClipSkyPolygon
================
*/
static void ClipSkyPolygon( int nump, vec3_t vecs, int stage )
{
	const float	*norm;
	float		*v;
	qboolean	front, back;
	float		d, e;
	float		dists[MAX_CLIP_VERTS];
	int			sides[MAX_CLIP_VERTS];
	vec3_t		newv[2][MAX_CLIP_VERTS];
	int			newc[2];
	int			i, j;

	if (nump > MAX_CLIP_VERTS - 2)
		Com_Error(ERR_DROP, "ClipSkyPolygon: MAX_CLIP_VERTS");
	if (stage == 6)
	{	// fully clipped, so draw it
		AddSkyPolygon(nump, vecs);
		return;
	}

	front = back = qfalse;
	norm = sky_clip[stage];
	for (i = 0, v = vecs; i < nump; i++, v += 3)
	{
		d = DotProduct(v, norm);
		if (d > ON_EPSILON)
		{
			front = qtrue;
			sides[i] = SIDE_FRONT;
		}
		else if (d < -ON_EPSILON)
		{
			back = qtrue;
			sides[i] = SIDE_BACK;
		}
		else
			sides[i] = SIDE_ON;
		dists[i] = d;
	}

	if (!front || !back)
	{	// not clipped
		ClipSkyPolygon(nump, vecs, stage + 1);
		return;
	}

	// clip it
	sides[i] = sides[0];
	dists[i] = dists[0];
	VectorCopy(vecs, (vecs + (i * 3)));
	newc[0] = newc[1] = 0;

	for (i = 0, v = vecs; i < nump; i++, v += 3)
	{
		switch (sides[i])
		{
		case SIDE_FRONT:
			VectorCopy(v, newv[0][newc[0]]);
			newc[0]++;
			break;
		case SIDE_BACK:
			VectorCopy(v, newv[1][newc[1]]);
			newc[1]++;
			break;
		case SIDE_ON:
			VectorCopy(v, newv[0][newc[0]]);
			newc[0]++;
			VectorCopy(v, newv[1][newc[1]]);
			newc[1]++;
			break;
		}

		if (sides[i] == SIDE_ON || sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
			continue;

		d = dists[i] / (dists[i] - dists[i + 1]);
		for (j = 0; j < 3; j++)
		{
			e = v[j] + d * (v[j + 3] - v[j]);
			newv[0][newc[0]][j] = e;
			newv[1][newc[1]][j] = e;
		}
		newc[0]++;
		newc[1]++;
	}

	// continue
	ClipSkyPolygon(newc[0], newv[0][0], stage + 1);
	ClipSkyPolygon(newc[1], newv[1][0], stage + 1);
}

/*
==============
ClearSkyBox
==============
*/
static void ClearSkyBox( void ) {
	int		i;

	for (i = 0; i < 6; i++) {
		sky_mins[0][i] = sky_mins[1][i] = 9999;
		sky_maxs[0][i] = sky_maxs[1][i] = -9999;
	}
}

/*
================
RB_ClipSkyPolygons
================
*/
static void RB_ClipSkyPolygons( const shaderCommands_t *input )
{
	vec3_t		p[5];	// need one extra point for clipping
	int			i, j;

	ClearSkyBox();

	for (i = 0; i < input->numIndexes; i += 3)
	{
		for (j = 0; j < 3; j++)
		{
			VectorSubtract(input->xyz[input->indexes[i + j]],
				backEnd.viewParms. ori .origin,
				p[j]);
		}
		ClipSkyPolygon(3, p[0], 0);
	}
}

/*
===================================================================================

CLOUD VERTEX GENERATION

===================================================================================
*/

/*
** MakeSkyVec
**
** Parms: s, t range from -1 to 1
*/
static void MakeSkyVec( float s, float t, int axis, vec3_t outXYZ )
{
	// The table is in tr_sky_projection.h now, with its inverse and a test.
	const float boxSize = backEnd.viewParms.zFar / 1.75f;		// div sqrt(3)

	SkyVecForST( axis, s, t, outXYZ );
	VectorScale( outXYZ, boxSize, outXYZ );
}

/*
=================
CullPoints
=================
*/
static qboolean CullPoints( vec4_t v[], const int count )
{
	const cplane_t	*frust;
	int				i, j;
	float			dist;

	for (i = 0; i < 5; i++) {
		frust = &backEnd.viewParms.frustum[i];
		for (j = 0; j < count; j++) {
			dist = DotProduct(v[j], frust->normal) - frust->dist;
			if (dist >= 0) {
				break;
			}
		}
		// all points is completely behind at least of one frustum plane
		if (j == count) {
			return qtrue;
		}
	}

	return qfalse;
}

static qboolean CullSkySide( const int mins[2], const int maxs[2] )
{
	int s, t;
	vec4_t v[4];

	if (r_nocull->integer)
		return qfalse;

	s = mins[0] + HALF_SKY_SUBDIVISIONS;
	t = mins[1] + HALF_SKY_SUBDIVISIONS;
	VectorAdd(s_skyPoints[t][s], backEnd.viewParms.ori.origin, v[0]);

	s = mins[0] + HALF_SKY_SUBDIVISIONS;
	t = maxs[1] + HALF_SKY_SUBDIVISIONS;
	VectorAdd(s_skyPoints[t][s], backEnd.viewParms.ori.origin, v[1]);

	s = maxs[0] + HALF_SKY_SUBDIVISIONS;
	t = mins[1] + HALF_SKY_SUBDIVISIONS;
	VectorAdd(s_skyPoints[t][s], backEnd.viewParms.ori.origin, v[2]);

	s = maxs[0] + HALF_SKY_SUBDIVISIONS;
	t = maxs[1] + HALF_SKY_SUBDIVISIONS;
	VectorAdd(s_skyPoints[t][s], backEnd.viewParms.ori.origin, v[3]);

	if (CullPoints(v, 4))
		return qtrue;

	return qfalse;
}

static void FillSkySide( const int mins[2], const int maxs[2], float skyTexCoords[SKY_SUBDIVISIONS + 1][SKY_SUBDIVISIONS + 1][2] )
{
	const int vertexStart = tess.numVertexes;
	const int tHeight = maxs[1] - mins[1] + 1;
	const int sWidth = maxs[0] - mins[0] + 1;
	int s, t;

	if (CullSkySide(mins, maxs))
		return;

#if ( (SKY_SUBDIVISIONS+1) * (SKY_SUBDIVISIONS+1) * 6 > SHADER_MAX_VERTEXES )
	if (tess.numVertexes + tHeight * sWidth > SHADER_MAX_VERTEXES)
		Com_Error(ERR_DROP, "SHADER_MAX_VERTEXES hit in %s()", __func__);
#endif

#if ( SKY_SUBDIVISIONS * SKY_SUBDIVISIONS * 6 * 6 > SHADER_MAX_INDEXES )
	if (tess.numIndexes + (tHeight - 1) * (sWidth - 1) * 6 > SHADER_MAX_INDEXES)
		Com_Error(ERR_DROP, "SHADER_MAX_INDEXES hit in %s()", __func__);
#endif

	for (t = mins[1] + HALF_SKY_SUBDIVISIONS; t <= maxs[1] + HALF_SKY_SUBDIVISIONS; t++)
	{
		for (s = mins[0] + HALF_SKY_SUBDIVISIONS; s <= maxs[0] + HALF_SKY_SUBDIVISIONS; s++)
		{
			VectorAdd(s_skyPoints[t][s], backEnd.viewParms.ori.origin, tess.xyz[tess.numVertexes]);
			tess.texCoords[0][tess.numVertexes][0] = skyTexCoords[t][s][0];
			tess.texCoords[0][tess.numVertexes][1] = skyTexCoords[t][s][1];
			tess.numVertexes++;
		}
	}

	for (t = 0; t < tHeight - 1; t++)
	{
		for (s = 0; s < sWidth - 1; s++)
		{
			tess.indexes[tess.numIndexes] = vertexStart + s + t * (sWidth);
			tess.numIndexes++;
			tess.indexes[tess.numIndexes] = vertexStart + s + (t + 1) * (sWidth);
			tess.numIndexes++;
			tess.indexes[tess.numIndexes] = vertexStart + s + 1 + t * (sWidth);
			tess.numIndexes++;

			tess.indexes[tess.numIndexes] = vertexStart + s + (t + 1) * (sWidth);
			tess.numIndexes++;
			tess.indexes[tess.numIndexes] = vertexStart + s + 1 + (t + 1) * (sWidth);
			tess.numIndexes++;
			tess.indexes[tess.numIndexes] = vertexStart + s + 1 + t * (sWidth);
			tess.numIndexes++;
		}
	}
}

static uint32_t	sky_cube_pipeline;
static qboolean	sky_cube_pipeline_built;

static uint32_t R_SkyCubePipeline( void )
{
	Vk_Pipeline_Def def;

	if ( sky_cube_pipeline_built ) {
		return sky_cube_pipeline;
	}

	Com_Memset( &def, 0, sizeof( def ) );
	def.shader_type = TYPE_SKYCUBE;
	def.face_culling = CT_FRONT_SIDED;

	sky_cube_pipeline = vk_find_pipeline_ext( 0, &def, qtrue );
	sky_cube_pipeline_built = qtrue;

	return sky_cube_pipeline;
}

/*
================
DrawSkySideCube

The same grid of quads as DrawSkySide, sampled out of one cubemap by direction
instead of out of one face by texture coordinate.

The direction rides in the normal attribute. s_skyPoints holds exactly that
already - the box points are generated relative to the camera and only have the
view origin added on their way into tess.xyz - so this is the same number
written twice rather than a second calculation that could disagree with the
first.
================
*/
static void DrawSkySideCube( image_t *cube, const int mins[2], const int maxs[2] )
{
	int s, t;
	int vertexStart = 0;
	int tHeight, sWidth;

	tHeight = maxs[1] - mins[1] + 1;
	sWidth = maxs[0] - mins[0] + 1;

	tess.numVertexes = 0;
	tess.numIndexes = 0;

	for ( t = mins[1] + HALF_SKY_SUBDIVISIONS; t <= maxs[1] + HALF_SKY_SUBDIVISIONS; t++ ) {
		for ( s = mins[0] + HALF_SKY_SUBDIVISIONS; s <= maxs[0] + HALF_SKY_SUBDIVISIONS; s++ ) {
			VectorAdd( s_skyPoints[t][s], backEnd.viewParms.ori.origin,
				tess.xyz[tess.numVertexes] );

			VectorCopy( s_skyPoints[t][s], tess.normal[tess.numVertexes] );
			tess.normal[tess.numVertexes][3] = 0.0f;

			tess.numVertexes++;
		}
	}

	for ( t = 0; t < tHeight - 1; t++ ) {
		for ( s = 0; s < sWidth - 1; s++ ) {
			tess.indexes[tess.numIndexes++] = vertexStart + s + t * sWidth;
			tess.indexes[tess.numIndexes++] = vertexStart + s + ( t + 1 ) * sWidth;
			tess.indexes[tess.numIndexes++] = vertexStart + s + 1 + t * sWidth;

			tess.indexes[tess.numIndexes++] = vertexStart + s + ( t + 1 ) * sWidth;
			tess.indexes[tess.numIndexes++] = vertexStart + s + 1 + ( t + 1 ) * sWidth;
			tess.indexes[tess.numIndexes++] = vertexStart + s + 1 + t * sWidth;
		}
	}

	if ( !tess.numIndexes ) {
		return;
	}

	vk_select_texture( 0 );
	vk_bind( cube );

	vk_bind_index();
	vk_bind_geometry( TESS_XYZ | TESS_NNN );

	{
		DrawItem item = {};
		item.pipeline = R_SkyCubePipeline();
		item.pipeline_layout = vk.pipeline_layout;
		item.depthRange = r_showsky->integer ? DEPTH_RANGE_ZERO : DEPTH_RANGE_ONE;
		item.polygonOffset = qfalse;
		item.identifier = 7;

		RB_AddDrawItemIndexBinding( item );
		RB_AddDrawItemVertexBinding( item );
		RB_AddDrawItemUniformBinding( item, backEnd.currentEntity );

		RB_AddDrawItem( backEndData->currentPass, item );
	}

	tess.numVertexes = 0;
	tess.numIndexes = 0;
}

static void DrawSkySide( image_t *image, const int mins[2], const int maxs[2] )
{
	tess.numVertexes = 0;
	tess.numIndexes = 0;

	FillSkySide(mins, maxs, s_skyTexCoords);

	if (tess.numIndexes)
	{
		vk_bind(image);

		tess.svars.texcoordPtr[0] = tess.texCoords[0];

		vk_bind_index();
		vk_bind_geometry(TESS_XYZ | TESS_ST0);

		// create draw item
		{
			DrawItem item = {};
			item.pipeline = vk.std_pipeline.skybox_pipeline;
			item.pipeline_layout = vk.pipeline_layout;
			item.depthRange = r_showsky->integer ? DEPTH_RANGE_ZERO : DEPTH_RANGE_ONE;
			item.polygonOffset = tess.shader->polygonOffset;
			item.identifier = 6;
		
			RB_AddDrawItemIndexBinding( item );
			RB_AddDrawItemVertexBinding( item );
			RB_AddDrawItemUniformBinding( item, backEnd.currentEntity );
			
			RB_AddDrawItem( backEndData->currentPass, item );
		}

		tess.numVertexes = 0;
		tess.numIndexes = 0;
	}
}

static void DrawSkyBox( const shader_t *shader )
{
	int	i;
	sky_min = 0;
	sky_max = 1;

	for (i = 0; i < 6; i++)
	{
		int sky_mins_subd[2], sky_maxs_subd[2];
		int s, t;

		sky_mins[0][i] = floor(sky_mins[0][i] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;
		sky_mins[1][i] = floor(sky_mins[1][i] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;
		sky_maxs[0][i] = ceil(sky_maxs[0][i] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;
		sky_maxs[1][i] = ceil(sky_maxs[1][i] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;

		if ((sky_mins[0][i] >= sky_maxs[0][i]) || (sky_mins[1][i] >= sky_maxs[1][i]))
		{
			continue;
		}

		sky_mins_subd[0] = sky_mins[0][i] * HALF_SKY_SUBDIVISIONS;
		sky_mins_subd[1] = sky_mins[1][i] * HALF_SKY_SUBDIVISIONS;
		sky_maxs_subd[0] = sky_maxs[0][i] * HALF_SKY_SUBDIVISIONS;
		sky_maxs_subd[1] = sky_maxs[1][i] * HALF_SKY_SUBDIVISIONS;

		if (sky_mins_subd[0] < -HALF_SKY_SUBDIVISIONS)
			sky_mins_subd[0] = -HALF_SKY_SUBDIVISIONS;
		else if (sky_mins_subd[0] > HALF_SKY_SUBDIVISIONS)
			sky_mins_subd[0] = HALF_SKY_SUBDIVISIONS;
		if (sky_mins_subd[1] < -HALF_SKY_SUBDIVISIONS)
			sky_mins_subd[1] = -HALF_SKY_SUBDIVISIONS;
		else if (sky_mins_subd[1] > HALF_SKY_SUBDIVISIONS)
			sky_mins_subd[1] = HALF_SKY_SUBDIVISIONS;

		if (sky_maxs_subd[0] < -HALF_SKY_SUBDIVISIONS)
			sky_maxs_subd[0] = -HALF_SKY_SUBDIVISIONS;
		else if (sky_maxs_subd[0] > HALF_SKY_SUBDIVISIONS)
			sky_maxs_subd[0] = HALF_SKY_SUBDIVISIONS;
		if (sky_maxs_subd[1] < -HALF_SKY_SUBDIVISIONS)
			sky_maxs_subd[1] = -HALF_SKY_SUBDIVISIONS;
		else if (sky_maxs_subd[1] > HALF_SKY_SUBDIVISIONS)
			sky_maxs_subd[1] = HALF_SKY_SUBDIVISIONS;

		//
		// iterate through the subdivisions
		//
		for (t = sky_mins_subd[1] + HALF_SKY_SUBDIVISIONS; t <= sky_maxs_subd[1] + HALF_SKY_SUBDIVISIONS; t++)
		{
			for (s = sky_mins_subd[0] + HALF_SKY_SUBDIVISIONS; s <= sky_maxs_subd[0] + HALF_SKY_SUBDIVISIONS; s++)
			{
				MakeSkyVec((s - HALF_SKY_SUBDIVISIONS) / (float)HALF_SKY_SUBDIVISIONS,
					(t - HALF_SKY_SUBDIVISIONS) / (float)HALF_SKY_SUBDIVISIONS,
					i,
					s_skyPoints[t][s]);
			}
		}

		if ( shader->sky->cube != NULL ) {
			DrawSkySideCube( shader->sky->cube, sky_mins_subd, sky_maxs_subd );
		} else {
			DrawSkySide(shader->sky->outerbox[sky_texorder[i]], sky_mins_subd, sky_maxs_subd);
		}
	}
}

static void FillCloudBox( void )
{
	int i;

	for (i = 0; i < 6; i++)
	{
		int sky_mins_subd[2], sky_maxs_subd[2];
		int s, t;
		float MIN_T;

		if (1) // FIXME? shader->sky.fullClouds )
		{
			MIN_T = -HALF_SKY_SUBDIVISIONS;

			// still don't want to draw the bottom, even if fullClouds
			if (i == 5)
				continue;
		}
		else
		{
			switch (i)
			{
			case 0:
			case 1:
			case 2:
			case 3:
				MIN_T = -1;
				break;
			case 5:
				// don't draw clouds beneath you
				continue;
			case 4:		// top
			default:
				MIN_T = -HALF_SKY_SUBDIVISIONS;
				break;
			}
		}

		sky_mins[0][i] = floor(sky_mins[0][i] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS; // double(sky_mi..)?
		sky_mins[1][i] = floor(sky_mins[1][i] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;
		sky_maxs[0][i] = ceil(sky_maxs[0][i] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;
		sky_maxs[1][i] = ceil(sky_maxs[1][i] * HALF_SKY_SUBDIVISIONS) / HALF_SKY_SUBDIVISIONS;

		if ((sky_mins[0][i] >= sky_maxs[0][i]) ||
			(sky_mins[1][i] >= sky_maxs[1][i]))
		{
			continue;
		}

		sky_mins_subd[0] = myftol(sky_mins[0][i] * HALF_SKY_SUBDIVISIONS);
		sky_mins_subd[1] = myftol(sky_mins[1][i] * HALF_SKY_SUBDIVISIONS);
		sky_maxs_subd[0] = myftol(sky_maxs[0][i] * HALF_SKY_SUBDIVISIONS);
		sky_maxs_subd[1] = myftol(sky_maxs[1][i] * HALF_SKY_SUBDIVISIONS);

		if (sky_mins_subd[0] < -HALF_SKY_SUBDIVISIONS)
			sky_mins_subd[0] = -HALF_SKY_SUBDIVISIONS;
		else if (sky_mins_subd[0] > HALF_SKY_SUBDIVISIONS)
			sky_mins_subd[0] = HALF_SKY_SUBDIVISIONS;
		if (sky_mins_subd[1] < MIN_T)
			sky_mins_subd[1] = MIN_T;
		else if (sky_mins_subd[1] > HALF_SKY_SUBDIVISIONS)
			sky_mins_subd[1] = HALF_SKY_SUBDIVISIONS;

		if (sky_maxs_subd[0] < -HALF_SKY_SUBDIVISIONS)
			sky_maxs_subd[0] = -HALF_SKY_SUBDIVISIONS;
		else if (sky_maxs_subd[0] > HALF_SKY_SUBDIVISIONS)
			sky_maxs_subd[0] = HALF_SKY_SUBDIVISIONS;
		if (sky_maxs_subd[1] < MIN_T)
			sky_maxs_subd[1] = MIN_T;
		else if (sky_maxs_subd[1] > HALF_SKY_SUBDIVISIONS)
			sky_maxs_subd[1] = HALF_SKY_SUBDIVISIONS;

		//
		// iterate through the subdivisions
		//
		for (t = sky_mins_subd[1] + HALF_SKY_SUBDIVISIONS; t <= sky_maxs_subd[1] + HALF_SKY_SUBDIVISIONS; t++)
		{
			for (s = sky_mins_subd[0] + HALF_SKY_SUBDIVISIONS; s <= sky_maxs_subd[0] + HALF_SKY_SUBDIVISIONS; s++)
			{
				MakeSkyVec((s - HALF_SKY_SUBDIVISIONS) / (float)HALF_SKY_SUBDIVISIONS,
					(t - HALF_SKY_SUBDIVISIONS) / (float)HALF_SKY_SUBDIVISIONS,
					i,
					s_skyPoints[t][s]);
			}
		}

		FillSkySide(sky_mins_subd, sky_maxs_subd, s_cloudTexCoords[i]);
	}
}

/*
** R_BuildCloudData
*/
static void R_BuildCloudData( const shaderCommands_t *input )
{
	const shader_t *shader;

	shader = input->shader;

	sky_min = 1.0 / 256.0f;		// FIXME: not correct?
	sky_max = 255.0 / 256.0f;

	// set up for drawing
	tess.numIndexes = 0;
	tess.numVertexes = 0;

	if (shader->sky->cloudHeight)
	{
		if (tess.xstages[0])
		{
			FillCloudBox();
		}
	}
}

static void BuildSkyTexCoords( void )
{
	float s, t;
	int i, j;

	for (i = 0; i <= SKY_SUBDIVISIONS; i++) {
		for (j = 0; j <= SKY_SUBDIVISIONS; j++) {
			s = (j - HALF_SKY_SUBDIVISIONS) / (float)HALF_SKY_SUBDIVISIONS;
			t = (i - HALF_SKY_SUBDIVISIONS) / (float)HALF_SKY_SUBDIVISIONS;

			float u, v;

			SkyTexCoordForST( s, t, &u, &v );

			// The clamp is the "avoid bilerp seam" one. It does nothing for the
			// sky box, whose s and t are already inside [-1,1]; it is the cloud
			// layer, which runs past the edges, that needs it.
			if ( u < 0.0f ) u = 0.0f; else if ( u > 1.0f ) u = 1.0f;
			if ( v < 0.0f ) v = 0.0f; else if ( v > 1.0f ) v = 1.0f;

			s_skyTexCoords[i][j][0] = u;
			s_skyTexCoords[i][j][1] = v;
		}
	}
}

/*
** R_InitSkyTexCoords
** Called when a sky shader is parsed
*/
void R_InitSkyTexCoords( float heightCloud )
{
	int i, s, t;
	float radiusWorld = 4096;
	float p;
	float sRad, tRad;
	vec3_t skyVec;
	vec3_t v;

	if (!Q_stricmp(glConfig.renderer_string, "GDI Generic") && !Q_stricmp(glConfig.version_string, "1.1.0")) {
		// fix skybox rendering on MS software GL implementation
		sky_min_depth = 0.999f;
	}
	else {
		sky_min_depth = 1.0;
	}

	// init zfar so MakeSkyVec works even though
	// a world hasn't been bounded
	backEnd.viewParms.zFar = 1024;

	for (i = 0; i < 6; i++)
	{
		for (t = 0; t <= SKY_SUBDIVISIONS; t++)
		{
			for (s = 0; s <= SKY_SUBDIVISIONS; s++)
			{
				// compute vector from view origin to sky side integral point
				MakeSkyVec((s - HALF_SKY_SUBDIVISIONS) / (float)HALF_SKY_SUBDIVISIONS,
					(t - HALF_SKY_SUBDIVISIONS) / (float)HALF_SKY_SUBDIVISIONS,
					i,
					skyVec);

				// compute parametric value 'p' that intersects with cloud layer
				p = (1.0f / (2 * DotProduct(skyVec, skyVec))) *
					(-2 * skyVec[2] * radiusWorld +
						2 * sqrt(Square(skyVec[2]) * Square(radiusWorld) +
							2 * Square(skyVec[0]) * radiusWorld * heightCloud +
							Square(skyVec[0]) * Square(heightCloud) +
							2 * Square(skyVec[1]) * radiusWorld * heightCloud +
							Square(skyVec[1]) * Square(heightCloud) +
							2 * Square(skyVec[2]) * radiusWorld * heightCloud +
							Square(skyVec[2]) * Square(heightCloud)));

				// compute intersection point based on p
				VectorScale(skyVec, p, v);
				v[2] += radiusWorld;

				// compute vector from world origin to intersection point 'v'
				VectorNormalize(v);

				sRad = Q_acos(v[0]);
				tRad = Q_acos(v[1]);

				s_cloudTexCoords[i][t][s][0] = sRad;
				s_cloudTexCoords[i][t][s][1] = tRad;
			}
		}
	}

	BuildSkyTexCoords();
}

/*
** RB_DrawSun
*/
void RB_DrawSun( float scale, shader_t *shader ) {
	float		size;
	float		dist;
	vec3_t		origin, vec1, vec2;
	color4ub_t	sunColor = { 255, 255, 255, 255 };

	if ( !r_drawSun->integer )
		return;

	if ( !backEnd.skyRenderedThisView )
		return;

	vk_update_mvp( NULL );

	dist = backEnd.viewParms.zFar / 1.75;		// div sqrt(3)
	size = dist * scale;

	VectorMA( backEnd.viewParms.ori.origin, dist, tr.sunDirection, origin );
	PerpendicularVector( vec1, tr.sunDirection );
	CrossProduct( tr.sunDirection, vec1, vec2 );

	VectorScale( vec1, size, vec1 );
	VectorScale( vec2, size, vec2 );

	// farthest depth range
	vk_set_depthrange( DEPTH_RANGE_ONE );

	RB_BeginSurface( shader, 0, 0 );

	RB_AddQuadStamp( origin, vec1, vec2, sunColor );

	// Reset currentEntity to world so that any previously referenced entities
	// don't have influence on the rendering of the sun (i.e. RF_ renderer flags).
	backEnd.currentEntity = &tr.worldEntity;
	backEnd.ori = backEnd.viewParms.world;

	RB_EndSurface();

	// back to normal depth range
	vk_set_depthrange( DEPTH_RANGE_NORMAL );
}

/*
================
R_BuildSkyCubemap

Six flat images and a set of conventions about their orientation, gathered into
one cube that can be sampled by direction.

Why bother: the seams. Each face is a separate texture clamped to its own edge,
so the last texel of one and the first texel of its neighbour are different
colours with a hard line between them, and no amount of texture-coordinate
inset reaches that - the inset fixes sampling PAST an edge, and the problem is
what is AT it. A cubemap has no seams because the hardware filters across faces.

The orientation is not guessed. For every texel of every cube face this asks
which box face that direction belongs to and where on it, using the same two
tables the box path uses - see tr_sky_projection.h, where they live with a test
that round-trips them against each other. Whatever the box drew for a direction,
the cube now holds for that direction, by construction rather than by
inspection.

Returns NULL when the faces cannot be read, and that is not a failure: the box
path is still there and still correct, and a map whose sky images are missing
should look the way it looked before.
================
*/
static image_t *R_BuildSkyCubemap( const char *baseName )
{
	static const char *suf[6] = { "rt", "bk", "lf", "ft", "up", "dn" };

	byte	*pics[6] = { NULL };
	int		widths[6], heights[6];
	char	pathname[MAX_QPATH];
	int		i, size, face, x, y;
	byte	*faceData;
	image_t	*cube = NULL;

	// Read the six, and give up quietly if any of them is missing or is not
	// square: the cube's faces have to agree on one size, and picking one for
	// them is a decision this has no business making.
	if ( !r_skyCubemap->integer ) {
		return NULL;
	}

	size = 0;
	for ( i = 0; i < 6; i++ ) {
		Com_sprintf( pathname, sizeof( pathname ), "%s_%s", baseName, suf[i] );
		R_LoadImage( pathname, &pics[i], &widths[i], &heights[i] );

		if ( pics[i] == NULL || widths[i] != heights[i] || widths[i] < 1 ) {
			goto done;
		}
		if ( size == 0 ) {
			size = widths[i];
		} else if ( widths[i] != size ) {
			goto done;
		}
	}

	// One mip. The sky is drawn at roughly one texel per pixel and never gets
	// far enough away to want a smaller one, and a mip chain that nothing fills
	// is worse than no mip chain at all.
	// The format has to be said out loud. R_CreateImage picks one from the
	// pixels it is given, and a cubemap is created empty and filled afterwards,
	// so there are no pixels to pick from - it comes out VK_FORMAT_UNDEFINED,
	// which the validation layer rejects three times in a row before anything
	// is drawn. Found that way, on the first run.
	cube = R_CreateImage( va( "*skycube_%s", baseName ), NULL, size, size,
		IMGFLAG_CUBEMAP | IMGFLAG_CLAMPTOEDGE | IMGFLAG_NO_COMPRESSION,
		VK_FORMAT_R8G8B8A8_UNORM, 0 );

	if ( cube == NULL ) {
		goto done;
	}

	cube->width = cube->uploadWidth = size;
	cube->height = cube->uploadHeight = size;
	cube->layers = 6;
	vk_create_image( cube, size, size, 1 );

	faceData = (byte *)R_Malloc( size * size * 4, TAG_TEMP_WORKSPACE, qfalse );

	for ( face = 0; face < 6; face++ ) {
		for ( y = 0; y < size; y++ ) {
			for ( x = 0; x < size; x++ ) {
				vec3_t	dir;
				float	fs, ft, u, v;
				int		axis, sx, sy;
				const byte *src;
				byte	*dst = faceData + ( y * size + x ) * 4;

				// The middle of the texel, in the cube's own [-1,1].
				const float cs = ( ( x + 0.5f ) / (float)size ) * 2.0f - 1.0f;
				const float ct = ( ( y + 0.5f ) / (float)size ) * 2.0f - 1.0f;

				// A cube face and a sky face are the same parameterisation, so
				// the direction for this texel is the box's own answer.
				SkyVecForST( face, cs, ct, dir );

				axis = SkyAxisForVec( dir );
				if ( !SkySTForVec( axis, dir, &fs, &ft ) ) {
					dst[0] = dst[1] = dst[2] = 0;
					dst[3] = 255;
					continue;
				}

				SkyTexCoordForST( fs, ft, &u, &v );

				// Which image, and where in it. sky_texorder is the reason a
				// direction along +Y reads "bk" and not "lf".
				{
					const int img = sky_texorder[axis];

					sx = (int)( u * ( widths[img] - 1 ) + 0.5f );
					sy = (int)( v * ( heights[img] - 1 ) + 0.5f );

					if ( sx < 0 ) sx = 0; else if ( sx >= widths[img] ) sx = widths[img] - 1;
					if ( sy < 0 ) sy = 0; else if ( sy >= heights[img] ) sy = heights[img] - 1;

					src = pics[img] + ( sy * widths[img] + sx ) * 4;
				}

				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				dst[3] = 255;
			}
		}

		vk_upload_image_data( cube, 0, 0, size, size, 1, faceData,
			size * size * 4, ( face != 0 ) ? qtrue : qfalse, face );
	}

	R_Free( faceData );

done:
	for ( i = 0; i < 6; i++ ) {
		if ( pics[i] != NULL ) {
			R_Free( pics[i] );
		}
	}

	if ( cube != NULL ) {
		CL_RefPrintf( PRINT_DEVELOPER, "sky cubemap %s: %d x %d per face\n",
			baseName, size, size );
	}

	return cube;
}

void R_SkyBuildCubemap( struct shader_s *sh, const char *baseName )
{
	if ( sh == NULL || sh->sky == NULL ) {
		return;
	}

	sh->sky->cube = R_BuildSkyCubemap( baseName );
}

/*
================
RB_StageIteratorSky

All of the visible sky triangles are in tess

Other things could be stuck in here, like birds in the sky, etc
================
*/
void RB_StageIteratorSky( void )
{
	// r_fastsky means "do not draw the sky, leave whatever cleared the
	// attachment showing". Both spellings of that had one body between them:
	//
	//     #ifndef USE_BUFFER_CLEAR
	//         if ( r_fastsky->integer && vk.clearAttachment )
	//     #else
	//         if ( r_fastsky->integer )
	//             return;
	//     #endif
	//         if ( backEnd.isGlowPass )
	//             return;
	//
	// With USE_BUFFER_CLEAR defined - which it is - that reads correctly by
	// accident. With it undefined the first condition has no statement of its
	// own and swallows the next one, so r_fastsky stops meaning "skip the sky"
	// and starts meaning "skip the sky only during a glow pass", and the glow
	// pass check disappears the rest of the time. A macro nobody turns off is
	// not a reason to leave a dangling if in the file.
	if ( r_fastsky->integer ) {
#ifndef USE_BUFFER_CLEAR
		if ( vk.clearAttachment )
#endif
			return;
	}

	if ( backEnd.isGlowPass )
		return;

	if ( skyboxportal && !( backEnd.refdef.rdflags & RDF_SKYBOXPORTAL ) )
		return;

#ifdef USE_VBO
	VBO_UnBind();
#endif

	// go through all the polygons and project them onto
	// the sky box to see which blocks on each side need
	// to be drawn
	RB_ClipSkyPolygons( &tess );

	// r_showsky will let all the sky blocks be drawn in
	// front of everything to allow developers to see how
	// much sky is getting sucked in
	if ( r_showsky->integer ) {
		vk_set_depthrange( DEPTH_RANGE_ZERO );
	}
	else {
		vk_set_depthrange( DEPTH_RANGE_ONE );
	}

	// draw the outer skybox
	if ( tess.shader->sky->outerbox[0] && tess.shader->sky->outerbox[0] != tr.defaultImage ) {
		DrawSkyBox( tess.shader );
	}

	// generate the vertexes for all the clouds, which will be drawn
	// by the generic shader routine
	R_BuildCloudData( &tess );

	// draw the inner skybox
	if ( tess.numVertexes ) {
		RB_StageIteratorGeneric();
	}

	// back to normal depth range
	vk_set_depthrange( DEPTH_RANGE_NORMAL );

	// note that sky was drawn so we will draw a sun later
	backEnd.skyRenderedThisView = qtrue;
}
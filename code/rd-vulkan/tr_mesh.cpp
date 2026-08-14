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

// tr_mesh.c: triangle model functions

#include "tr_local.h"

float ProjectRadius( float r, vec3_t location )
{
	float pr;
	float dist;
	float c;
	vec3_t	p;
	float width;
	float depth;

	c = DotProduct( tr.viewParms.ori.axis[0], tr.viewParms.ori.origin );
	dist = DotProduct( tr.viewParms.ori.axis[0], location ) - c;

	if ( dist <= 0 )
		return 0;

	p[0] = 0;
	p[1] = Q_fabs( r );
	p[2] = -dist;

	width = p[0] * tr.viewParms.projectionMatrix[1] +
		           p[1] * tr.viewParms.projectionMatrix[5] +
				   p[2] * tr.viewParms.projectionMatrix[9] +
				   tr.viewParms.projectionMatrix[13];

	depth = p[0] * tr.viewParms.projectionMatrix[3] +
		           p[1] * tr.viewParms.projectionMatrix[7] +
				   p[2] * tr.viewParms.projectionMatrix[11] +
				   tr.viewParms.projectionMatrix[15];

	pr = width / depth;

	if ( pr > 1.0f )
		pr = 1.0f;

	return pr;
}

/*
=============
R_CullModel
=============
*/
static int R_CullModel( mdvModel_t *model, const trRefEntity_t *ent, vec3_t bounds[] ) {
	//vec3_t		bounds[2];
	mdvFrame_t	*oldFrame, *newFrame;
	int			i;

	// compute frame pointers
	newFrame = model->frames + ent->e.frame;
	oldFrame = model->frames + ent->e.oldframe;

	// calculate a bounding box in the current coordinate system
	for (i = 0 ; i < 3 ; i++) {
		bounds[0][i] = oldFrame->bounds[0][i] < newFrame->bounds[0][i] ? oldFrame->bounds[0][i] : newFrame->bounds[0][i];
		bounds[1][i] = oldFrame->bounds[1][i] > newFrame->bounds[1][i] ? oldFrame->bounds[1][i] : newFrame->bounds[1][i];
	}

	// cull bounding sphere ONLY if this is not an upscaled entity
	if ( !ent->e.nonNormalizedAxes )
	{
		if ( ent->e.frame == ent->e.oldframe )
		{
			switch ( R_CullLocalPointAndRadius( newFrame->localOrigin, newFrame->radius ) )
			{
			case CULL_OUT:
				tr.pc.c_sphere_cull_md3_out++;
				return CULL_OUT;

			case CULL_IN:
				tr.pc.c_sphere_cull_md3_in++;
				return CULL_IN;

			case CULL_CLIP:
				tr.pc.c_sphere_cull_md3_clip++;
				break;
			}
		}
		else
		{
			int sphereCull, sphereCullB;

			sphereCull  = R_CullLocalPointAndRadius( newFrame->localOrigin, newFrame->radius );
			if ( newFrame == oldFrame ) {
				sphereCullB = sphereCull;
			} else {
				sphereCullB = R_CullLocalPointAndRadius( oldFrame->localOrigin, oldFrame->radius );
			}

			if ( sphereCull == sphereCullB )
			{
				if ( sphereCull == CULL_OUT )
				{
					tr.pc.c_sphere_cull_md3_out++;
					return CULL_OUT;
				}
				else if ( sphereCull == CULL_IN )
				{
					tr.pc.c_sphere_cull_md3_in++;
					return CULL_IN;
				}
				else
				{
					tr.pc.c_sphere_cull_md3_clip++;
				}
			}
		}
	}

	switch ( R_CullLocalBox( bounds ) )
	{
	case CULL_IN:
		tr.pc.c_box_cull_md3_in++;
		return CULL_IN;
	case CULL_CLIP:
		tr.pc.c_box_cull_md3_clip++;
		return CULL_CLIP;
	case CULL_OUT:
	default:
		tr.pc.c_box_cull_md3_out++;
		return CULL_OUT;
	}
}

/*
=================
RE_GetModelBounds

The bounds of one frame of a vertex-animated model, for callers that need the
size of a model they are not drawing. hModel and frame in refEnt are the inputs.

Anything that is not a mesh - a Ghoul2 model, a brush model, a handle that never
loaded - answers an empty box rather than reading through a pointer that is not
there. rd-vanilla asserts instead, which in a release build is the same read.
=================
*/
void RE_GetModelBounds( refEntity_t *refEnt, vec3_t bounds1, vec3_t bounds2 ) {
	VectorClear( bounds1 );
	VectorClear( bounds2 );

	if ( !refEnt ) {
		return;
	}

	const model_t *model = R_GetModelByHandle( refEnt->hModel );
	if ( !model || model->type != MOD_MESH ) {
		return;
	}

	const mdvModel_t *mdv = model->data.mdv[0];
	if ( !mdv || refEnt->frame < 0 || refEnt->frame >= mdv->numFrames ) {
		return;
	}

	const mdvFrame_t *frame = mdv->frames + refEnt->frame;

	VectorCopy( frame->bounds[0], bounds1 );
	VectorCopy( frame->bounds[1], bounds2 );
}

/*
=================
R_ComputeLOD

=================
*/
int R_ComputeLOD( trRefEntity_t *ent ) {
	float radius;
	float flod, lodscale;
	float projectedRadius;
	mdvFrame_t *frame;
	int lod;

#ifdef RF_NOLOD
	if ( tr.currentModel->numLods < 2 || (ent->e.renderfx & RF_NOLOD) )
#else
	if ( tr.currentModel->numLods < 2 )
#endif
	{
		// model has only 1 LOD level, skip computations and bias
		lod = 0;
	}
	else
	{
		// multiple LODs exist, so compute projected bounding sphere
		// and use that as a criteria for selecting LOD

		//frame = ( mdvFrame_t * ) ( ( ( unsigned char * ) tr.currentModel->data.md3[0] ) + tr.currentModel->data.md3[0]->ofsFrames );
		frame = tr.currentModel->data.mdv[0]->frames;

		frame += ent->e.frame;

		radius = RadiusFromBounds( frame->bounds[0], frame->bounds[1] );

		if ( ( projectedRadius = ProjectRadius( radius, ent->e.origin ) ) != 0 )
		{
			lodscale = (r_lodscale->value+r_autolodscalevalue->value);
			if ( lodscale > 20 )
			{
				lodscale = 20;
			}
			else if ( lodscale < 0 )
			{
				lodscale = 0;
			}
			flod = 1.0f - projectedRadius * lodscale;
		}
		else
		{
			// object intersects near view plane, e.g. view weapon
			flod = 0;
		}

		flod *= tr.currentModel->numLods;
		lod = Q_ftol( flod );

		if ( lod < 0 )
		{
			lod = 0;
		}
		else if ( lod >= tr.currentModel->numLods )
		{
			lod = tr.currentModel->numLods - 1;
		}
	}

#ifdef RF_NOLOD
	if (!(ent->e.renderfx & RF_NOLOD))
#endif
	{
		lod += r_lodbias->integer;
	}

	if ( lod >= tr.currentModel->numLods )
		lod = tr.currentModel->numLods - 1;
	if ( lod < 0 )
		lod = 0;

	return lod;
}

/*
=================
R_ComputeFogNum

=================
*/
static int R_ComputeFogNum( mdvModel_t *model, const trRefEntity_t *ent ) {
	int				i, j;
	fog_t			*fog;
	mdvFrame_t		*mdvFrame;
	vec3_t			localOrigin;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return 0;
	}

	if ( tr.refdef.doLAGoggles ) {
		return tr.world->numfogs;
	}

	// FIXME: non-normalized axis issues
	mdvFrame = model->frames + ent->e.frame;
	VectorAdd( ent->e.origin, mdvFrame->localOrigin, localOrigin );
	for ( i = 1 ; i < tr.world->numfogs ; i++ ) {
		fog = &tr.world->fogs[i];
		for ( j = 0 ; j < 3 ; j++ ) {
			if ( localOrigin[j] - mdvFrame->radius >= fog->bounds[1][j] ) {
				break;
			}
			if ( localOrigin[j] + mdvFrame->radius <= fog->bounds[0][j] ) {
				break;
			}
		}
		if ( j == 3 ) {
			return i;
		}
	}

	return 0;
}

/*
=================
R_AddMD3Surfaces

=================
*/
void R_AddMD3Surfaces( trRefEntity_t *ent, int entityNum ) {
	vec3_t			bounds[2];
	int				i;
	mdvModel_t		*model = NULL;
	mdvSurface_t	*surface = NULL;
	shader_t		*shader = 0;
	int				cull;
	int				lod;
	int				fogNum;
	int				cubemapIndex;
	qboolean		personalModel;
#ifdef USE_PMLIGHT
	dlight_t		*dl;
	int				n;
	dlight_t		*dlights[ARRAY_LEN(backEndData->dlights)];
	int				numDlights;
#endif

	// don't add third_person objects if not in a portal
	personalModel = (qboolean)((ent->e.renderfx & RF_THIRD_PERSON) && (tr.viewParms.portalView == PV_NONE));

#ifdef RF_CAP_FRAMES
	// Single-player only, and the counterpart of RF_WRAP_FRAMES below: a
	// one-shot animation holds on its last frame instead of looping back to the
	// start. The gamecode sets it in four places and the Vulkan renderer, being
	// a multiplayer one, ignored it - so those animations looped.
	if ( ent->e.renderfx & RF_CAP_FRAMES ) {
		const int lastFrame = tr.currentModel->data.mdv[0]->numFrames - 1;
		if ( ent->e.frame > lastFrame )
			ent->e.frame = lastFrame;
		if ( ent->e.oldframe > lastFrame )
			ent->e.oldframe = lastFrame;
	}
	else
#endif
	if ( ent->e.renderfx & RF_WRAP_FRAMES ) {
		ent->e.frame %= tr.currentModel->data.mdv[0]->numFrames;
		ent->e.oldframe %= tr.currentModel->data.mdv[0]->numFrames;
	}

	//
	// Validate the frames so there is no chance of a crash.
	// This will write directly into the entity structure, so
	// when the surfaces are rendered, they don't need to be
	// range checked again.
	//
	if ( (ent->e.frame >= tr.currentModel->data.mdv[0]->numFrames)
		|| (ent->e.frame < 0)
		|| (ent->e.oldframe >= tr.currentModel->data.mdv[0]->numFrames)
		|| (ent->e.oldframe < 0) ) {
			CL_RefPrintf( PRINT_DEVELOPER, S_COLOR_RED "R_AddMD3Surfaces: no such frame %d to %d for '%s'\n",
				ent->e.oldframe, ent->e.frame,
				tr.currentModel->name );
			ent->e.frame = 0;
			ent->e.oldframe = 0;
	}

	//
	// compute LOD
	//
	lod = R_ComputeLOD( ent );

	model = tr.currentModel->data.mdv[lod];

	//
	// cull the entire model if merged bounding box of both frames
	// is outside the view frustum.
	//
	cull = R_CullModel ( model, ent, bounds );
	if ( cull == CULL_OUT ) {
		return;
	}

#ifdef USE_PMLIGHT
	numDlights = 0;
#ifdef VK_DLIGHT_GPU
	if ( !vk.useGPUDLight ) 
#endif
	{
		if (r_dlightMode->integer >= 2 && (!personalModel || tr.viewParms.portalView != PV_NONE)) {
			R_TransformDlights(tr.viewParms.num_dlights, tr.viewParms.dlights, &tr.ori );
			for (n = 0; n < tr.viewParms.num_dlights; n++) {
				dl = &tr.viewParms.dlights[n];
				if (!R_LightCullBounds(dl, bounds[0], bounds[1]))
					dlights[numDlights++] = dl;
			}
		}
	}
#endif

	//
	// see if we are in a fog volume
	//
	fogNum = R_ComputeFogNum( model, ent );

	cubemapIndex = R_CubemapForPoint( ent->e.origin );

	//
	// draw all surfaces
	//
	surface = model->surfaces;
	for ( i = 0 ; i < model->numSurfaces ; i++ ) {

		if ( ent->e.customShader ) {
			shader = R_GetShaderByHandle( ent->e.customShader );
		} else if ( ent->e.customSkin > 0 && ent->e.customSkin < tr.numSkins ) {
			const skin_t *skin;
			int		j;

			skin = R_GetSkinByHandle( ent->e.customSkin );

			// match the surface name to something in the skin file
			shader = tr.defaultShader;
			for ( j = 0 ; j < skin->numSurfaces ; j++ ) {
				// the names have both been lowercased
				if ( !strcmp( skin->surfaces[j]->name, surface->name ) ) {
					shader = (shader_t *)skin->surfaces[j]->shader;
					break;
				}
			}
			if (shader == tr.defaultShader) {
				CL_RefPrintf( PRINT_DEVELOPER, S_COLOR_RED "WARNING: no shader for surface %s in skin %s\n", surface->name, skin->name);
			}
			else if (shader->defaultShader) {
				CL_RefPrintf( PRINT_DEVELOPER, S_COLOR_RED "WARNING: shader %s in skin %s not found\n", shader->name, skin->name);
			}
		} else if ( surface->numShaderIndexes <= 0 ) {
			shader = tr.defaultShader;
		} else {
			shader = tr.shaders[ surface->shaderIndexes[ ent->e.skinNum % surface->numShaderIndexes ] ];
		}


		// we will add shadows even if the main object isn't visible in the view

		// stencil shadows can't do personal models unless I polyhedron clip
		if ( !personalModel
			&& r_shadows->integer == 2
			&& fogNum == 0
			&& !(ent->e.renderfx & ( RF_NOSHADOW | RF_DEPTHHACK ) )
			&& shader->sort == SS_OPAQUE ) {
			R_AddDrawSurf( (surfaceType_t *)surface, entityNum, tr.shadowShader, 0, 0 );
		}

		// projection shadows work fine with personal models
		if ( r_shadows->integer == 3
			&& fogNum == 0
			&& (ent->e.renderfx & RF_SHADOW_PLANE )
			&& shader->sort == SS_OPAQUE ) {
			R_AddDrawSurf( (surfaceType_t *)surface, entityNum, tr.projectionShadowShader, 0, 0 );
		}

		// don't add third_person objects if not viewing through a portal
		if ( !personalModel ) {
#ifdef USE_VBO_MDV
			// The vertex buffer holds ONE frame. R_BuildMD3 sizes it numVerts,
			// not numVerts * numFrames, and writes surf->verts from the start -
			// which is frame zero. So a model with more than one frame drawn
			// through here is frozen at its first, and every MD3 in this engine
			// was drawn through here, because vk.vboMdvActive is set
			// unconditionally.
			//
			// That is what killed vertex animation: the weapon in the player's
			// hands is a morph-animated MD3 whose frame is mapped from the
			// character's torso animation, and it has not moved since the
			// buffer path was added. The lerp itself was never deleted -
			// LerpMeshVertexes_ is still here, in two versions - it just stopped
			// being called.
			//
			// A single-frame model has nothing to lerp, so it keeps the buffer,
			// which is nearly everything: props, weapons on the ground, debris.
			// An animated one goes back through the batch. Making the buffer
			// carry every frame and lerping in the vertex shader is the right
			// answer and is a separate piece of work; this is the line that
			// stops the picture being wrong in the meantime.
			if ( vk.vboMdvActive && model->numFrames <= 1 )
				R_AddDrawSurf( (surfaceType_t *)&model->vboSurfaces[i], entityNum, shader, fogNum, cubemapIndex );
			else
#endif
				R_AddDrawSurf( (surfaceType_t *)surface, entityNum, shader, fogNum, 0 );

			
			tr.needScreenMap |= shader->hasScreenMap;
		}

#ifdef USE_PMLIGHT
		if (numDlights && shader->lightingStage >= 0) {
			for (n = 0; n < numDlights; n++) {
				dl = dlights[n];
				tr.light = dl;
				R_AddLitSurf((surfaceType_t*)surface, entityNum, shader, fogNum);
			}
		}
#endif

		surface++;
	}

}

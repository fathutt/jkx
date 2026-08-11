/*
===========================================================================
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors
Copyright (C) 2026 JKX contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.
===========================================================================
*/

// Ghoul2's internal interface, single-player edition.
//
// Multiplayer splits Ghoul2 into a public ghoul2/G2.h and this private header;
// single-player never had the split, so the same declarations live scattered
// across the renderer and code/game. This file re-establishes the split on the
// single-player side, which is where phase 2.3 is taking the G2_ sources
// anyway - out of the renderer and into the engine.
//
// It is deliberately NOT a copy of the multiplayer header. Every signature here
// was taken from the definition in our own sources and confirmed by compiling,
// because a transcribed declaration that disagrees with its definition compiles
// happily and then misbehaves - which is the single most expensive failure mode
// in this whole port.
//
// The harness build does not see this file: it includes the fork's own copy,
// because only code/rd-vulkan is synced into that checkout. So this header is
// exercised by exactly one build, the single-player target, and by nothing
// else until the harness is gone.

#pragma once

// Self-sufficient on purpose: the G2_ sources include this before tr_local.h,
// so it cannot rely on anything having been pulled in first. q_shared.h brings
// CGhoul2Info_v and IGhoul2InfoArray with it, via code/game/ghoul2_shared.h.
#include "qcommon/q_shared.h"
#include "qcommon/MiniHeap.h"
#include "ghoul2/G2.h"

class CRagDollUpdateParams;

// Multiplayer introduced a pure-virtual IHeapAllocator and made CMiniHeap
// implement it. Single-player has only the concrete class - which already has
// both methods the interface declares, ResetHeap() and MiniHeapAlloc(int). The
// renderer calls nothing else, so naming is all that is missing: a typedef
// costs nothing, where adding the interface would put a vtable on a memory
// arena for no one's benefit.
typedef CMiniHeap IHeapAllocator;

//hack for smoothing during ugly situations. forgive me.
#define GHOUL2_CRAZY_SMOOTH 0x2000

void Create_Matrix( const float *angle, mdxaBone_t *matrix );

extern mdxaBone_t worldMatrix;
extern mdxaBone_t worldMatrixInv;

// --- names single-player spells differently ---------------------------------

// Multiplayer's collision record type is single-player's CCollisionRecord
// class; the contents are the same, only the name travelled.
typedef CCollisionRecord CollisionRecord_t;

// Multiplayer puts this in its ghoul2/G2.h, single-player's copy does not have
// it. Same bit, same meaning: the bone still needs its transform computed.
#ifndef BONE_NEED_TRANSFORM
	#define BONE_NEED_TRANSFORM 0x8000
#endif

// Multiplayer's ghoul2_shared.h; marks a Ghoul2 instance whose transform space
// came from the zone rather than the mini heap.
#ifndef GHOUL2_ZONETRANSALLOC
	#define GHOUL2_ZONETRANSALLOC 0x2000
#endif

// Multiplayer has a tag of its own for gore surfaces; single-player's tag list
// has one Ghoul2 tag and no gore, because it never built with gore enabled.
// Same lifetime either way - freed with the rest of Ghoul2.
#ifndef TAG_GHOUL2_GORE
	#define TAG_GHOUL2_GORE TAG_GHOUL2
#endif

// The bottom of a standing player's bounding box, used by the ragdoll code to
// find the floor. It lives in the gameplay headers on both sides, and the
// renderer has no business including those, so it is spelled out here with the
// value both trees agree on (code/game/bg_public.h).
#ifndef DEFAULT_MINS_2
	#define DEFAULT_MINS_2 -24
#endif

// --- our own functions, declared where the other files can see them ---------
//
// Signatures copied from the definitions in code/rd-vulkan, not from the
// multiplayer header, and checked by compiling.

int			G2_IsSurfaceOff( CGhoul2Info *ghlInfo, surfaceInfo_v &slist, const char *surfaceName );
void		G2_RemoveRedundantBolts( boltInfo_v &bltlist, surfaceInfo_v &slist, int *activeSurfaces, int *activeBones );
void		G2_RemoveRedundantBoneOverrides( boneInfo_v &blist, int *activeBones );
void		RemoveBoneCache( CBoneCache *boneCache );

// The Ghoul2 internals this renderer defines.
//
// They used to come from ghoul2/G2.h, which declared single-player's shapes -
// and this renderer's Ghoul2 came from multiplayer, where several of them grew
// or lost a parameter. Nothing outside a renderer ever called any of them, so
// G2.h now carries only the G2API_* surface and each renderer declares its own
// internals. rd-vanilla's copy is code/rd-vanilla/G2_internal.h.
//
// Every signature below was taken from the definition in code/rd-vulkan by
// script, not transcribed. A declaration that disagrees with its definition
// compiles and then misbehaves.

int G2_AddSurface( CGhoul2Info *ghoul2, int surfaceNumber, int polyNumber, float BarycentricI, float BarycentricJ, int lod );
int G2_Add_Bolt( CGhoul2Info *ghlInfo, boltInfo_v &bltlist, surfaceInfo_v &slist, const char *boneName );
int G2_Add_Bolt_Surf_Num( CGhoul2Info *ghlInfo, boltInfo_v &bltlist, surfaceInfo_v &slist, const int surfNum );
void G2_Animate_Bone_List( CGhoul2Info_v &ghoul2, const int currentTime, const int index );
void G2_Animate_Bone_List( CGhoul2Info_v &ghoul2, const int currentTime, const int index, CRagDollUpdateParams *params );

// Single-player's ghoul2/G2.h declares this taking references; this renderer
// defines the pointer form multiplayer uses, and the two are overloads. The
// export table gets the reference one through tr_sp_exports.cpp.
void G2API_GiveMeVectorFromMatrix( mdxaBone_t *boltMatrix, Eorientations flags, vec3_t vec );

// The index-based half of the Ghoul2 API, ported from rd-vanilla. Declared here
// rather than in ghoul2/G2.h because that header carries what the engine calls
// and these are reached through the export table.
qboolean	G2API_SetAnimIndex( CGhoul2Info *ghlInfo, const int index );
int			G2API_GetAnimIndex( CGhoul2Info *ghlInfo );
qboolean	G2API_GetAnimRangeIndex( CGhoul2Info *ghlInfo, const int boneIndex, int *startFrame, int *endFrame );
qboolean	G2API_PauseBoneAnimIndex( CGhoul2Info *ghlInfo, const int boneIndex, const int AcurrentTime );
qboolean	G2API_GetBoneAnimIndex( CGhoul2Info *ghlInfo, const int iBoneIndex, const int AcurrentTime, float *currentFrame, int *startFrame, int *endFrame, int *flags, float *animSpeed, int * );
char *		G2API_GetAnimFileInternalNameIndex( qhandle_t modelIndex );
void		G2API_DetachEnt( int *boltInfo );
void		G2API_AnimateG2Models( CGhoul2Info_v &ghoul2, int AcurrentTime, CRagDollUpdateParams *params );

// Same again: single-player's public declaration takes a reference, this
// renderer defines and calls the pointer form, and they are overloads.
qboolean G2API_RemoveGhoul2Model( CGhoul2Info_v **ghlRemove, const int modelIndex );

void G2_ConstructGhoulSkeleton( CGhoul2Info_v &ghoul2,const int frameNum,bool checkForNewOrigin,const vec3_t scale );
surfaceInfo_t * G2_FindOverrideSurface( int surfaceNum, surfaceInfo_v &surfaceList );
void * G2_FindSurface( void *mod_t, int index, int lod );
void G2_GenerateWorldMatrix( const vec3_t angles, const vec3_t origin );
int G2_Find_Bone_In_List( boneInfo_v &blist, const int boneNum );
int G2_Find_Bolt_Surface_Num( boltInfo_v &bltlist, const int surfaceNum, const int flags );
qboolean G2_GetAnimFileName( const char *fileName, char **filename );
int G2_GetParentSurface( CGhoul2Info *ghlInfo, const int index );
int G2_GetSurfaceIndex( CGhoul2Info *ghlInfo, const char *surfaceName );
qboolean G2_Get_Bone_Anim( CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName, const int currentTime, float *currentFrame, int *startFrame, int *endFrame, int *flags, float *retAnimSpeed, qhandle_t *modelList, int modelIndex );
qboolean G2_Get_Bone_Anim_Range_Index( boneInfo_v &blist, const int boneIndex, int *startFrame, int *endFrame );
qboolean G2_Pause_Bone_Anim_Index( boneInfo_v &blist, const int boneIndex, const int currentTime, int numFrames );
qboolean G2_Get_Bone_Anim_Index( boneInfo_v &blist, const int index, const int currentTime, float *currentFrame, int *startFrame, int *endFrame, int *flags, float *retAnimSpeed, qhandle_t *modelList, int numFrames );
qboolean G2_Get_Bone_Anim_Range( CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName, int *startFrame, int *endFrame );
int G2_Get_Bone_Index( CGhoul2Info *ghoul2, const char *boneName, qboolean bAddIfNotFound );
void G2_Init_Bolt_List( boltInfo_v &bltlist );
void G2_Init_Bone_List( boneInfo_v &blist, int numBones );
qboolean G2_IsPaused( const char *fileName, boneInfo_v &blist, const char *boneName );
int G2_IsSurfaceLegal( void *mod, const char *surfaceName, int *flags );
int G2_IsSurfaceRendered( CGhoul2Info *ghlInfo, const char *surfaceName, surfaceInfo_v &slist );
void G2_List_Model_Bones( const char *fileName, int frame );
void G2_List_Model_Surfaces( const char *fileName );
void G2_LoadGhoul2Model( CGhoul2Info_v &ghoul2, char *buffer );
qboolean G2_Pause_Bone_Anim( CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName, const int currentTime );
qboolean G2_RemoveSurface( surfaceInfo_v &slist, const int index );
qboolean G2_Remove_Bolt( boltInfo_v &bltlist, int index );
qboolean G2_Remove_Bone( CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName );
qboolean G2_SaveGhoul2Models( CGhoul2Info_v &ghoul2, char **buffer, int *size );
qboolean G2_SetRootSurface( CGhoul2Info_v &ghoul2, const int modelIndex, const char *surfaceName );
qboolean G2_SetSurfaceOnOff( CGhoul2Info *ghlInfo, surfaceInfo_v &slist, const char *surfaceName, const int offFlags );
qboolean G2_Set_Bone_Angles( CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName, const float *angles, const int flags, const Eorientations up, const Eorientations left, const Eorientations forward, qhandle_t *modelList, const int modelIndex, const int blendTime, const int currentTime );
qboolean G2_Set_Bone_Angles_Index( const model_s *mod_a, boneInfo_v &blist, const int index, const float *angles, const int flags, const Eorientations yaw, const Eorientations pitch, const Eorientations roll, qhandle_t *modelList, const int modelIndex, const int blendTime, const int currentTime );
qboolean G2_Set_Bone_Angles_Matrix( const char *fileName, boneInfo_v &blist, const char *boneName, const mdxaBone_t &matrix, const int flags, qhandle_t *modelList, const int modelIndex, const int blendTime, const int currentTime );
qboolean G2_Set_Bone_Angles_Matrix_Index( boneInfo_v &blist, const int index, const mdxaBone_t &matrix, const int flags, qhandle_t *modelList, const int modelIndex, const int blendTime, const int currentTime );
qboolean G2_Set_Bone_Anim( CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName, const int startFrame, const int endFrame, const int flags, const float animSpeed, const int currentTime, const float setFrame, const int blendTime );
qboolean G2_Set_Bone_Anim_Index( boneInfo_v &blist, const int index, const int startFrame, const int endFrame, const int flags, const float animSpeed, const int currentTime, const float setFrame, const int blendTime, const int numFrames );
qboolean G2_Stop_Bone_Angles( const char *fileName, boneInfo_v &blist, const char *boneName );
qboolean G2_Stop_Bone_Angles_Index( boneInfo_v &blist, const int index );
qboolean G2_Stop_Bone_Anim( const char *fileName, boneInfo_v &blist, const char *boneName );
qboolean G2_Stop_Bone_Anim_Index( boneInfo_v &blist, const int index );
void Inverse_Matrix( mdxaBone_t *src, mdxaBone_t *dest );
void TransformAndTranslatePoint( const vec3_t in, vec3_t out, mdxaBone_t *mat );
void TransformPoint( const vec3_t in, vec3_t out, mdxaBone_t *mat );

// The gore build changes the shape of these two, exactly as it does in
// single-player's header; the definitions in G2_misc.cpp carry the same pair.
#ifdef _G2_GORE
void		G2_TransformModel( CGhoul2Info_v &ghoul2, const int frameNum, vec3_t scale, IHeapAllocator *G2VertSpace, int useLod, bool ApplyGore );
void		G2_TraceModels( CGhoul2Info_v &ghoul2, vec3_t rayStart, vec3_t rayEnd, CollisionRecord_t *collRecMap, int entNum, int eG2TraceType, int useLod, float fRadius, float ssize, float tsize, float theta, int shader, SSkinGoreData *gore, qboolean skipIfLODNotMatch );
#else
void		G2_TransformModel( CGhoul2Info_v &ghoul2, const int frameNum, vec3_t scale, IHeapAllocator *G2VertSpace, int useLod );
void		G2_TraceModels( CGhoul2Info_v &ghoul2, vec3_t rayStart, vec3_t rayEnd, CollisionRecord_t *collRecMap, int entNum, int eG2TraceType, int useLod, float fRadius );
#endif

// ---------------------------------------------------------------------------
// What Ghoul2 asks the model registry
// ---------------------------------------------------------------------------
//
// Phase 2.3 moves these sources out of the renderer, and the one thing standing
// in the way is model_t: Ghoul2 reaches through it for the two headers it works
// on, plus a name and a level-of-detail count. model_t is the renderer's - it
// also holds brush models, vertex-animated meshes and image handles, none of
// which Ghoul2 has any business seeing.
//
// So the reach becomes four questions. The model registry answers them; Ghoul2
// holds an opaque model_s * and stops knowing what is inside it.
//
// Both header accessors answer NULL when the model is not what was asked for -
// not loaded, or loaded and not a Ghoul2 mesh - which is also what the pile of
// hand-written null checks at the call sites used to work out for themselves.

struct model_s;

// Ghoul2 resolves handles too - a mesh names its animation file by index - so
// the lookup comes with them. The pointer is opaque on this side.
model_s			*R_GetModelByHandle( qhandle_t index );

mdxmHeader_t	*R_GetGhoul2MeshHeader( const model_s *mod );
mdxaHeader_t	*R_GetGhoul2AnimHeader( const model_s *mod );
const char		*R_GetModelName( const model_s *mod );
int				R_GetModelLodCount( const model_s *mod );

// Registering a model by name is the registry's job too, and Ghoul2 does it -
// an instance is created from a file name.
qhandle_t		RE_RegisterModel( const char *name );
qhandle_t		RE_RegisterServerModel( const char *name );

// And the skin registry, for the one place that walks a skin looking for the
// surfaces its shader marks as not drawn.
int				R_GetSkinSurfaceCount( qhandle_t hSkin );
const char		*R_GetSkinSurfaceName( qhandle_t hSkin, int index );
qboolean		R_IsSkinSurfaceOff( qhandle_t hSkin, int index );

// A matrix helper that belongs to Ghoul2 and is still defined beside the
// drawing half; it moves with these sources.
void			Multiply_3x4Matrix( mdxaBone_t *out, mdxaBone_t *in2, mdxaBone_t *in );

// The client's print, which takes a level. Ghoul2 uses PRINT_WARNING and
// PRINT_DEVELOPER and would lose both by printing through Com_Printf.
void QDECL		CL_RefPrintf( int printLevel, const char *fmt, ... ) __attribute__ ((format (printf, 2, 3)));

// The arena Ghoul2 transforms vertices in, which the client owns.
class CMiniHeap;
CMiniHeap		*GetG2VertSpaceServer( void );

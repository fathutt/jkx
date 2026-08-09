/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Ghoul2 internals, as rd-vanilla defines them.
//
// These used to live in ghoul2/G2.h, next to the G2API_* calls the engine
// actually uses. Nothing outside a renderer ever called any of them - one grep
// over the engine, the gamecode and the UI finds exactly zero references - and
// keeping them in a shared header meant the two renderers had to agree on
// signatures they do not agree on: the Vulkan renderer's Ghoul2 came from
// multiplayer, where several of these grew or lost a parameter.
//
// So each renderer declares its own now. This copy dies with rd-vanilla at the
// end of phase 2; the Vulkan renderer's is ghoul2/g2_local.h, which is where
// Ghoul2 ends up when it moves into the engine in 2.3.

#pragma once

struct model_s;

// internal surface calls  G2_surfaces.cpp
qboolean	G2_SetSurfaceOnOff (CGhoul2Info *ghlInfo, const char *surfaceName, const int offFlags);
qboolean	G2_SetRootSurface( CGhoul2Info_v &ghoul2, const int modelIndex,const char *surfaceName);
int			G2_AddSurface(CGhoul2Info *ghoul2, int surfaceNumber, int polyNumber, float BarycentricI, float BarycentricJ, int lod );
qboolean	G2_RemoveSurface(surfaceInfo_v &slist, const int index);
const surfaceInfo_t *G2_FindOverrideSurface(int surfaceNum, const surfaceInfo_v &surfaceList);
int			G2_IsSurfaceLegal(const model_s *, const char *surfaceName, uint32_t *flags);
int			G2_GetParentSurface(CGhoul2Info *ghlInfo, const int index);
int			G2_GetSurfaceIndex(CGhoul2Info *ghlInfo, const char *surfaceName);
int			G2_IsSurfaceRendered(CGhoul2Info *ghlInfo, const char *surfaceName, surfaceInfo_v &slist);

// internal bone calls - G2_Bones.cpp
qboolean	G2_Set_Bone_Angles(CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName, const float *angles, const int flags,
							   const Eorientations up, const Eorientations left, const Eorientations forward,
							   const int blendTime, const int currentTime);
qboolean	G2_Remove_Bone (CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName);
qboolean	G2_Remove_Bone_Index (boneInfo_v &blist, int index);
qboolean	G2_Set_Bone_Anim(CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName, const int startFrame,
							 const int endFrame, const int flags, const float animSpeed, const int currentTime, const float setFrame, const int blendTime);
qboolean	G2_Get_Bone_Anim(CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName, const int currentTime,
						  float *currentFrame, int *startFrame, int *endFrame, int *flags, float *retAnimSpeed);
qboolean	G2_Get_Bone_Anim_Range(CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName, int *startFrame, int *endFrame);
qboolean	G2_Get_Bone_Anim_Range_Index(boneInfo_v &blist, const int boneIndex, int *startFrame, int *endFrame);
qboolean	G2_Pause_Bone_Anim(CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName, const int currentTime);
qboolean	G2_Pause_Bone_Anim_Index(boneInfo_v &blist, const int boneIndex, const int currentTime,int numFrames );
qboolean	G2_IsPaused(CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName);
qboolean	G2_Stop_Bone_Anim(CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName);
qboolean	G2_Stop_Bone_Angles(CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName);
//rww - RAGDOLL_BEGIN
void		G2_Animate_Bone_List(CGhoul2Info_v &ghoul2, const int currentTime, const int index,CRagDollUpdateParams *params);
//rww - RAGDOLL_END

void		G2_Init_Bone_List(boneInfo_v &blist, int numBones);
int			G2_Find_Bone_In_List(boneInfo_v &blist, const int boneNum);
qboolean	G2_Set_Bone_Angles_Matrix(CGhoul2Info *ghlInfo, boneInfo_v &blist, const char *boneName, const mdxaBone_t &matrix,
									  const int flags, const int blendTime, const int currentTime);
int			G2_Get_Bone_Index(CGhoul2Info *ghoul2, const char *boneName, qboolean bAddIfNotFound);
qboolean	G2_Set_Bone_Angles_Index(CGhoul2Info *ghlInfo, boneInfo_v &blist, const int index,
							const float *angles, const int flags, const Eorientations yaw,
							const Eorientations pitch, const Eorientations roll,
							const int blendTime, const int currentTime);
qboolean	G2_Set_Bone_Angles_Matrix_Index(boneInfo_v &blist, const int index,
								   const mdxaBone_t &matrix, const int flags,
								   const int blendTime, const int currentTime);
qboolean	G2_Stop_Bone_Anim_Index(boneInfo_v &blist, const int index);
qboolean	G2_Stop_Bone_Angles_Index(boneInfo_v &blist, const int index);
qboolean	G2_Set_Bone_Anim_Index(boneInfo_v &blist, const int index, const int startFrame,
						  const int endFrame, const int flags, const float animSpeed, const int currentTime, const float setFrame, const int blendTime,int numFrames);
qboolean	G2_Get_Bone_Anim_Index( boneInfo_v &blist, const int index, const int currentTime,
						  float *currentFrame, int *startFrame, int *endFrame, int *flags, float *retAnimSpeed,int numFrames);

// misc functions G2_misc.cpp
void		G2_List_Model_Surfaces(const char *fileName);
void		G2_List_Model_Bones(const char *fileName, int frame);
qboolean	G2_GetAnimFileName(const char *fileName, char **filename);
#ifdef _G2_GORE
void		G2_TraceModels(CGhoul2Info_v &ghoul2, vec3_t rayStart, vec3_t rayEnd, CCollisionRecord *collRecMap, int entNum, EG2_Collision eG2TraceType, int useLod, float fRadius, float ssize,float tsize,float theta,int shader, SSkinGoreData *gore, qboolean skipIfLODNotMatch);
#else
void		G2_TraceModels(CGhoul2Info_v &ghoul2, vec3_t rayStart, vec3_t rayEnd, CCollisionRecord *collRecMap, int entNum, EG2_Collision eG2TraceType, int useLod, float fRadius);
#endif
void		TransformAndTranslatePoint (const vec3_t in, vec3_t out, mdxaBone_t *mat);
#ifdef _G2_GORE
void		G2_TransformModel(CGhoul2Info_v &ghoul2, const int frameNum, vec3_t scale, CMiniHeap *G2VertSpace, int useLod, bool ApplyGore, SSkinGoreData *gore=NULL);
#else
void		G2_TransformModel(CGhoul2Info_v &ghoul2, const int frameNum, vec3_t scale, CMiniHeap *G2VertSpace, int useLod);
#endif
void		G2_GenerateWorldMatrix(const vec3_t angles, const vec3_t origin);
void		TransformPoint (const vec3_t in, vec3_t out, mdxaBone_t *mat);
void		Inverse_Matrix(mdxaBone_t *src, mdxaBone_t *dest);
void		*G2_FindSurface(const model_s *, int index, int lod);
void		G2_SaveGhoul2Models(CGhoul2Info_v &ghoul2);
void		G2_LoadGhoul2Model(CGhoul2Info_v &ghoul2, char *buffer);

// internal bolt calls. G2_bolts.cpp
int			G2_Add_Bolt(CGhoul2Info *ghlInfo, boltInfo_v &bltlist, surfaceInfo_v &slist, const char *boneName);
qboolean	G2_Remove_Bolt (boltInfo_v &bltlist, int index);
void		G2_Init_Bolt_List(boltInfo_v &bltlist);
int			G2_Find_Bolt_Bone_Num(boltInfo_v &bltlist, const int boneNum);
int			G2_Find_Bolt_Surface_Num(boltInfo_v &bltlist, const int surfaceNum, const int flags);
int			G2_Add_Bolt_Surf_Num(CGhoul2Info *ghlInfo, boltInfo_v &bltlist, surfaceInfo_v &slist, const int surfNum);


// From tr_ghoul2.cpp
// From tr_ghoul2.cpp
void		G2_ConstructGhoulSkeleton( CGhoul2Info_v &ghoul2,const int frameNum,bool checkForNewOrigin,const vec3_t scale);
void		G2_GetBoltMatrixLow(CGhoul2Info &ghoul2,int boltNum,const vec3_t scale,mdxaBone_t &retMatrix);
void		G2_TimingModel(boneInfo_t &bone,int time,int numFramesInFile,int &currentFrame,int &newFrame,float &lerp);

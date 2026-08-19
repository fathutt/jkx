#ifndef SHADER_SHARED_H
#define SHADER_SHARED_H

#ifndef GLOBAL_SHADER_C
	#define GLSL
    #define USE_VK_PBR
#endif

#ifdef GLSL
    #define VK_DLIGHT_GPU

    #define M_PI 3.1415926535897932384626433832795

    #if defined(USE_LIGHTMAP) || defined(USE_LIGHT_VECTOR) || defined(USE_LIGHT_VERTEX)
	    #define USE_LIGHT
    #endif

    #if defined(USE_LIGHT) && !defined(USE_FAST_LIGHT)
	    #define PER_PIXEL_LIGHTING 
    #endif
#endif

#if defined(USE_VBO_GHOUL2) || defined(USE_VBO_MDV)
	#define USE_VBO_MODEL
#endif

// descriptor idx
#define VK_DESC_STORAGE					0
#define VK_DESC_UNIFORM					0
#define VK_DESC_TEXTURE0				1
#define VK_DESC_TEXTURE1				2
#define VK_DESC_TEXTURE2				3
#define VK_DESC_FOG_COLLAPSE			4
#ifdef USE_VK_PBR
// One set for all of the physically-based textures, with a binding each,
// instead of one set apiece.
//
// A descriptor SET is the scarce thing in Vulkan: the guaranteed minimum is
// four and eight is what Intel integrated parts, MoltenVK and most mobile
// report. Five sets holding one image each pushed the count to ten, so the
// whole PBR path switched itself off on all of them - and on the software
// rasteriser the bench runs, which is why nothing in this branch had ever been
// looked at by the validation layer.
//
// Bindings are not scarce in the same way, so the same five images at
// VK_DESC_PBR cost one set and the count comes down to six.
#define VK_DESC_PBR						5
#define VK_DESC_PBR_BRDFLUT_BINDING		0
#define VK_DESC_PBR_NORMAL_BINDING		1
#define VK_DESC_PBR_PHYSICAL_BINDING	2
#define VK_DESC_PBR_CUBEMAP_BINDING		3
#define VK_DESC_PBR_DELUXE_BINDING		4
#define VK_DESC_PBR_BINDING_COUNT		5
#define VK_DESC_COUNT					6
#else
#define VK_DESC_COUNT					5
#endif

#define VK_DESC_TEXTURE_BASE			VK_DESC_TEXTURE0
#define VK_DESC_FOG_ONLY				VK_DESC_TEXTURE1
#define VK_DESC_FOG_DLIGHT				VK_DESC_TEXTURE1

// uniform binding idx
#define VK_DESC_UNIFORM_MAIN_BINDING		0
#define VK_DESC_UNIFORM_CAMERA_BINDING		1
#define VK_DESC_UNIFORM_LIGHT_BINDING		2
#define VK_DESC_UNIFORM_ENTITY_BINDING		3
#define VK_DESC_UNIFORM_BONES_BINDING		4
#define VK_DESC_UNIFORM_FOGS_BINDING		5
#define VK_DESC_UNIFORM_GLOBAL_BINDING		6
// Number of DYNAMIC uniform bindings on set 0, and therefore the number of
// dynamic offsets handed to vkCmdBindDescriptorSets. The binding below is not
// one of them and must not be counted here.
#define VK_DESC_UNIFORM_COUNT				7

// Depth of the scene as it stood before the transparent surfaces were drawn.
//
// It rides on the uniform set rather than on a set of its own for one reason:
// set 0 is the only index present in EVERY pipeline layout this renderer
// builds. A device that reports the Vulkan minimum drops to four sets and
// loses VK_DESC_FOG_COLLAPSE and VK_DESC_PBR with them, so anything parked at
// the end would be missing exactly where the fallback path runs - and soft
// particles have no business depending on whether normal mapping is on.
//
// A binding is not the scarce resource a set is; this costs nothing.
#define VK_DESC_UNIFORM_SCENE_DEPTH_BINDING	7
#define VK_DESC_UNIFORM_BINDING_COUNT		8

#ifdef GLSL
    #ifdef USE_TX2
	    #define USE_TX1
    #endif

    #ifdef USE_CL2
	    #define USE_CL1
    #endif

    #define STRUCT(content, name) struct name { content };
    #define INT(n)		int n;
    #define UINT(n)		uint n;
    #define FLOAT(n)	float n;
    #define VEC2(n)		vec2 n;
    #define VEC3(n)		vec3 n;
    #define VEC4(n)		vec4 n;
    #define MAT4(n)		mat4 n;
    #define MAT4X3(n)	mat4x3 n;
    #define MAT3X4(n)	mat3x4 n;
    #define UVEC2(n)	uvec2 n;
    #define UVEC3(n)	uvec3 n;
    #define UVEC4(n)	uvec4 n;
	#define IVEC3(n)	ivec3 n;
	#define IVEC4(n)	ivec4 n;
    #define PAD1(n)
    #define PAD2(n)
    #define PAD3(n)
#else
    #define STRUCT(content, name) typedef struct { content } name;
    #define INT(n)		int32_t n;
    #define UINT(n)		unsigned int n;
    #define FLOAT(n)	float n;
    #define VEC2(n)		vec2_t n;
    #define VEC3(n)		vec3_t n;
    #define VEC4(n)		vec4_t n;
    #define MAT4(n)		mat4_t n;
    #define MAT3X4(n)	mat3x4_t n;
    #define UVEC2(n)	unsigned int n[2];
    #define UVEC3(n)	unsigned int n[3];
    #define UVEC4(n)	unsigned int n[4];
	#define IVEC3(n)	ivec3_t n;
	#define IVEC4(n)	ivec4_t n;
    #define PAD1(n)     int n;
    #define PAD2(n)     vec2_t n;
    #define PAD3(n)     vec3_t n;
#endif

// entity
STRUCT (  
    VEC4	( ambientLight )
    VEC4	( directedLight )
    VEC4	( localLightOrigin )
    VEC4	( localViewOrigin )
    MAT4	( modelMatrix )
, vkUniformEntity_t )

// non-vbo entity fallback
STRUCT(
    MAT4(modelMatrix)
, vkEntityMatrix_t)

// bones
STRUCT(
    MAT3X4( boneMatrices[72] )
, vkUniformBones_t)

// camera
STRUCT(
    VEC4( viewOrigin )
, vkUniformCamera_t)

// fog
#define FOG_ENTRY_T(n) vkUniformFogEntry_t n;

STRUCT (  
    VEC4	( plane )
    VEC4	( color )
    FLOAT	( depthToOpaque )
    INT 	( hasPlane )
    PAD2	( pad0 )
, vkUniformFogEntry_t )

STRUCT (  
	INT		        ( num_fogs )
    PAD3	        ( pad0 )
    FOG_ENTRY_T     ( fogs[16] )
, vkUniformFog_t )

#undef FOG_ENTRY_T

// surface sprites
STRUCT (  
    VEC2	( fxGrow )
    FLOAT	( fxDuration )
    FLOAT	( fadeStartDistance )
    FLOAT	( fadeEndDistance )
    FLOAT	( fadeScale )
    FLOAT	( wind )
    FLOAT	( windIdle )
    FLOAT	( fxAlphaStart )
    FLOAT	( fxAlphaEnd )
    PAD2	( pad0 )
, SurfaceSpriteBlock )

// global
STRUCT (  
    VEC4	( matrix )
    VEC4	( offTurb )
, vktcMod_t )

STRUCT (  
    VEC3	( vector0 )
    PAD1    ( pad0 )
    VEC3	( vector1 )
    INT	    ( type )
, vktcGen_t )

#if defined(PER_PIXEL_LIGHTING) || defined(USE_LIGHT_VECTOR) || defined(USE_VBO_MODEL) || defined(IS_REFRACTION_GLSL) || defined(IS_GEN_FRAG_GLSL)
    #define TCMOD_T(n)          vktcMod_t n;
    #define TCGEN_T(n)          vktcGen_t n;
    #define BUMDLE_T(n)         vkBundle_t n;
    #define DISINTEGRATION_T(n) vkDisintegration_t n;
    #define DEFORM_T(n)         vkDeform_t n;

    STRUCT (  
        VEC4	( baseColor )
        VEC4	( vertColor )
        TCMOD_T	( tcMod )
        TCGEN_T	( tcGen )
        INT	    ( rgbGen )
        INT	    ( alphaGen )
        INT	    ( numTexMods )
        PAD1    ( pad0 )
    , vkBundle_t )

    STRUCT (  
        VEC3	( origin )
        FLOAT	( threshold )
    , vkDisintegration_t )

    STRUCT (  
	    FLOAT	( base )
	    FLOAT	( amplitude )
	    FLOAT	( phase )
	    FLOAT	( frequency )

	    VEC3	( vector )
	    FLOAT	( time )

	    INT		( type )
	    INT		( func )
        PAD2    ( pad0 )
    , vkDeform_t )

    STRUCT (  
        BUMDLE_T            ( bundle[3] )
        DISINTEGRATION_T    ( disintegration )
        DEFORM_T            ( deform )
        FLOAT               ( portalRange )
        PAD3                ( pad0 )
		VEC4				( specularScale )
		VEC4				( normalScale )
		// refraction: x how far the ray is pushed, y how much of the refracted
		// image replaces the surface, z the highest mip the blur may reach
		// (zero means no blur), w how far apart the colour channels refract.
		VEC4				( refraction )
		// softParticle: x the fade distance in world units (zero means the
		// fade is off), y and z the two numbers that turn a depth buffer value
		// back into view-space Z - Z = z / ( depth + y ) - taken from the
		// projection matrix on the CPU so the shader needs neither zNear nor
		// zFar, w unused.
		VEC4				( softParticle )
		// softParticleNear: x how many units in front of the eye a surface is
		// fully faded out (zero means that half is off), y z w unused.
		//
		// The near half of the same effect and a separate number on purpose:
		// the far fade is measured in the gap between two surfaces and the near
		// one in the distance from the camera, so a single distance would be
		// two different quantities wearing one name.
		VEC4				( softParticleNear )
    , vkUniformGlobal_t )

    #undef TCMOD_T
    #undef TCGEN_T
    #undef BUMDLE_T
    #undef DISINTEGRATION_T
    #undef DEFORM_T
#endif

// light
#ifdef VK_DLIGHT_GPU
    #define LIGHT_ENTRY_T(n) vkUniformLightEntry_t n;

    STRUCT (  
	    VEC4				( origin )
	    VEC3				( color )
	    FLOAT				( radius )
    , vkUniformLightEntry_t )

    STRUCT (  
	    UINT				( num_lights )
        PAD3                ( pad0 )
	    LIGHT_ENTRY_T		( light[64] )
    , vkUniformLight_t )

    #undef LIGHT_ENTRY_T
#else
    STRUCT (  
	    VEC4				( item )
    , vkUniformLight_t )
#endif

#endif
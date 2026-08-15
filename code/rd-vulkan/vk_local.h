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

// Sunny: known issues/notes that I havent really looked into yet 
/*
	known issues:
-	broken capturebuffer on stopvideo avi recording
-	no surfacesprites with vbo enabled

	notes:
-	optimizations for worldeffects
-	optimizations for surfacesprites
-	glow & distortion?
-	using USE_REVERSED_DEPTH results in missing decals
-	attachments[MAX_ATTACHMENTS_IN_POOL + 1]; // +1 for SSAA? Need to disable MSAA when SSAA enabled?
-	SSAA enabled: console font scaling. need to change clientgame files. which is not an option rn
-	look into empty descriptorset bindings: descriptorBindingPartiallyBound / VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
-	default tr.projectionShadowShader->sort results in artifacts with saber & shadow sort.

	sources:
-	https://github.com/eternalcodes/EternalJK
-	https://github.com/ec-/Quake3e
-	https://github.com/kennyalive/Quake-III-Arena-Kenny-Edition
-	https://github.com/suijingfeng/vkQuake3
*/

#pragma once

//#define VK_NO_PROTOTYPES
// volk owns the Vulkan prototypes: it defines VK_NO_PROTOTYPES and provides
// every entry point as a loaded pointer, which is why the 111 hand-written
// PFN_vk* declarations that used to live below are gone. Headers come from the
// system Vulkan SDK rather than a vendored copy.
#include "volk.h"
// Forward-declared so the 20k line VMA header is included by exactly one
// translation unit (vk_allocator.cpp) rather than by everything.
VK_DEFINE_HANDLE(VmaAllocator)
VK_DEFINE_HANDLE(VmaAllocation)

typedef enum {
	VK_BUFFER_MEMORY_DEVICE,      // GPU only
	VK_BUFFER_MEMORY_HOST_WRITE,  // CPU writes, GPU reads; persistently mapped
	VK_BUFFER_MEMORY_HOST_READ    // GPU writes, CPU reads; persistently mapped
} vk_buffer_memory_t;

#ifndef VK_CREATE_BUFFER
	#define VK_CREATE_BUFFER(device, info, outBuffer, name)				VK_CHECK( vkCreateBuffer( device, info, NULL, outBuffer ) )
	#define VK_DESTROY_BUFFER(device, buffer)							vkDestroyBuffer( device, buffer, NULL )
	#define VK_ALLOCATE_MEMORY(device, info, outMemory, name)			vkAllocateMemory( device, info, NULL, outMemory )
	#define VK_ALLOCATE_MEMORY_CHECK(device, info, outMemory, name)		VK_CHECK( vkAllocateMemory( device, info, NULL, outMemory ) )
	#define VK_FREE_MEMORY(device, memory)								vkFreeMemory( device, memory, NULL )
#endif


// JKX_VK_TRACE turns the renderer's own diagnostics on in a release build: the
// vk_log.log operation trace and, where the layers are installed, validation
// reporting. A debug build gives the same thing plus a debug CRT and different
// struct layouts, which on this codebase means the engine and the gamecode
// disagree about their interface and the process dies before it can log
// anything at all - a diagnostic build that cannot be diagnosed. This one is a
// release build in every other respect.
#if defined (_DEBUG) || defined (JKX_VK_TRACE)
#define USE_VK_VALIDATION
//#define USE_VK_OBJECT_TRACKER
#define USE_DEBUG_REPORT
//#define USE_DEBUG_UTILS
#endif

typedef float mat4_t[16];
typedef float mat3x4_t[12];
typedef unsigned int uvec4_t[4];

#define BUFFER_OFFSET(i) ((char *)NULL + (i))

#ifndef MAX
#define MAX(x,y) ((x)>(y)?(x):(y))
#endif

#ifndef MIN
#define MIN(x,y) ((x)<(y)?(x):(y))
#endif

//#define USE_REVERSED_DEPTH
#define USE_UPLOAD_QUEUE

//#define USE_VANILLA_SHADOWFINISH
#define USE_VK_STATS

#define	REFRACTION_EXTRACT_SCALE		2

// How far a refracting surface bends what is behind it, before r_refractionScale
// and the client's own multiplier, in the units the surface is modelled in.
// This is the figure the old shader arrived at internally; keeping it means a
// scale of one draws the effect the renderer was already aiming for.
#define	REFRACTION_BASE_THICKNESS		10.0f

// How many levels the extract's mip chain gets. Five halvings from a half-size
// screen is blurred enough for any roughness; the levels past that average
// whole regions of the screen into one colour.
#define	REFRACTION_EXTRACT_MIPS			5
#define NUM_COMMAND_BUFFERS				2
#define VK_NUM_BLUR_PASSES				4

#define MAX_SWAPCHAIN_IMAGES			8
#define MIN_SWAPCHAIN_IMAGES_IMM		3
#define MIN_SWAPCHAIN_IMAGES_FIFO		3
#define MIN_SWAPCHAIN_IMAGES_FIFO_0		4
#define MIN_SWAPCHAIN_IMAGES_MAILBOX	3

#define MAX_VK_SAMPLERS					32
#define MAX_VK_PIPELINES				((1024 + 128)*2)
#ifndef _DEBUG
#define USE_DEDICATED_ALLOCATION
#endif
// depth + msaa + msaa-resolve + screenmap.msaa + screenmap.resolve + screenmap.depth + (bloom_extract + blur pairs + dglow_extract + blur pairs) + dglow-msaa
#define MAX_ATTACHMENTS_IN_POOL			( 9 + ( ( 1 + VK_NUM_BLUR_PASSES * 2 ) * 2 ) + 1 ) + 1 // (6+3=9: cubemap.msaa + cubemap.resolve + cubemap.depth) + refraction_extract

#define GLOBAL_SHADER_C
#include "shaders/glsl/global.h"

//#define MIN_IMAGE_ALIGN				( 128 * 1024 )

#define VERTEX_BUFFER_SIZE				( 4 * 1024 * 1024 )		/* by default */
#define VERTEX_BUFFER_SIZE_HI			( 8 * 1024 * 1024 )

#define STAGING_BUFFER_SIZE				( 2 * 1024 * 1024 )		/* by default */
#define STAGING_BUFFER_SIZE_HI			( 24 * 1024 * 1024 )	/* enough for max.texture size upload with all mip levels at */

#define VERTEX_CHUNK_SIZE				( 768 * 1024)

#define XYZ_SIZE						( 4 * VERTEX_CHUNK_SIZE )
#define COLOR_SIZE						( 1 * VERTEX_CHUNK_SIZE )
#define ST0_SIZE						( 2 * VERTEX_CHUNK_SIZE )
#define ST1_SIZE						( 2 * VERTEX_CHUNK_SIZE )

#define XYZ_OFFSET						0
#define COLOR_OFFSET					( XYZ_OFFSET + XYZ_SIZE )
#define ST0_OFFSET						( COLOR_OFFSET + COLOR_SIZE )
#define ST1_OFFSET						( ST0_OFFSET + ST0_SIZE )

#define TESS_XYZ						( 1 )
#define TESS_RGBA0 						( 2 )
#define TESS_RGBA1 						( 4 )
#define TESS_RGBA2 						( 8 )
#define TESS_ST0   						( 16 )
#define TESS_ST1   						( 32 )
#define TESS_ST2   						( 64 )
#define TESS_NNN   						( 128 )
#define TESS_VPOS  						( 256 )	// uniform with eyePos
#define TESS_ENV   						( 512 )	// mark shader stage with environment mapping

#ifdef USE_VK_PBR
#define TESS_TANGENT   					( 1024 )
#define TESS_LIGHTDIR   				( 2048 ) 

#define PBR_HAS_NORMALMAP				( 1 )
#define PBR_HAS_PHYSICALMAP				( 2 )
#define PBR_HAS_SPECULARMAP				( 4 )
#define PBR_HAS_DELUXEMAP				( 8 )

#define PHYS_NONE						( 1 )
#define PHYS_RMO						( 2 )
#define PHYS_RMOS   					( 4 )
#define PHYS_MOXR   					( 8 )
#define PHYS_MOSR   					( 16 )
#define PHYS_ORM  						( 32 )	
#define PHYS_ORMS   					( 64 )	
#define PHYS_NORMAL   					( 128 )	
#define PHYS_NORMALHEIGHT				( 256 )	
#define PHYS_SPECGLOSS					( 512 )	

#define LIGHTDEF_USE_LIGHTMAP			0x0001
#define LIGHTDEF_USE_LIGHT_VECTOR		0x0002
#define LIGHTDEF_USE_LIGHT_VERTEX		0x0004
#define LIGHTDEF_LIGHTTYPE_MASK			LIGHTDEF_USE_LIGHTMAP | LIGHTDEF_USE_LIGHT_VECTOR | LIGHTDEF_USE_LIGHT_VERTEX

#define BUFFER_OFFSET(i)				((char *)NULL + (i))

#define ByteToFloat(a)					((float)(a) * 1.0f/255.0f)
#define FloatToByte(a)					(byte)((a) * 255.0f)

#define RGBtosRGB(a)					(((a) < 0.0031308f) ? (12.92f * (a)) : (1.055f * pow((a), 0.41666f) - 0.055f))
#define sRGBtoRGB(a)					(((a) <= 0.04045f)  ? ((a) / 12.92f) : (pow((((a) + 0.055f) / 1.055f), 2.4)) )
#endif

typedef struct textureMapType_s {
	uint32_t			type;
	const char			*suffix;
	VkComponentMapping	swizzle;
} textureMapType_t;

const textureMapType_t textureMapTypes[] = {
	{ NULL,					"",			{ VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, } },
#ifdef USE_VK_PBR
	{ PHYS_RMO,				"_rmo",		{ VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_ONE,	} },
	{ PHYS_RMOS,			"_rmos",	{ VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_A, } },
	{ PHYS_MOXR,			"_moxr",	{ VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_A, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE } },
	{ PHYS_MOSR,			"_mosr",	{ VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_A, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_B } },
	{ PHYS_ORM,				"_orm",		{ VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY } },
	{ PHYS_ORMS,			"_orms",	{ VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY } },
	{ PHYS_NORMAL,			"_n",		{ VK_COMPONENT_SWIZZLE_A, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_R } },
	{ PHYS_NORMALHEIGHT,	"_nh",		{ VK_COMPONENT_SWIZZLE_A, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_R } },
#endif
};

// extra math
#define DotProduct4( a , b )			((a)[0]*(b)[0] + (a)[1]*(b)[1] + (a)[2]*(b)[2] + (a)[3]*(b)[3])
#define VectorScale4( a , b , c )		((c)[0]=(a)[0]*(b),(c)[1]=(a)[1]*(b),(c)[2]=(a)[2]*(b),(c)[3]=(a)[3]*(b))
#define Vector4Set( v, x, y, z, w )		((v)[0]=(x),(v)[1]=(y),(v)[2]=(z),v[3]=(w))
#define Vector4Copy( a, b )				((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2],(b)[3]=(a)[3])
#define LERP( a, b, w )					((a)*(1.0f-(w))+(b)*(w))
#define LUMA( r, g, b )					(0.2126f*(r)+0.7152f*(g)+0.0722f*(b))
#define EPSILON 1e-6f
#ifndef SGN
#define SGN( x )						(((x) >= 0) ? !!(x) : -1)
#endif

// shaders
#define GLS_SRCBLEND_ZERO						0x00000001
#define GLS_SRCBLEND_ONE						0x00000002
#define GLS_SRCBLEND_DST_COLOR					0x00000003
#define GLS_SRCBLEND_ONE_MINUS_DST_COLOR		0x00000004
#define GLS_SRCBLEND_SRC_ALPHA					0x00000005
#define GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA		0x00000006
#define GLS_SRCBLEND_DST_ALPHA					0x00000007
#define GLS_SRCBLEND_ONE_MINUS_DST_ALPHA		0x00000008
#define GLS_SRCBLEND_ALPHA_SATURATE				0x00000009
#define	GLS_SRCBLEND_BITS		    			0x0000000f

#define GLS_DSTBLEND_ZERO						0x00000010
#define GLS_DSTBLEND_ONE						0x00000020
#define GLS_DSTBLEND_SRC_COLOR					0x00000030
#define GLS_DSTBLEND_ONE_MINUS_SRC_COLOR		0x00000040
#define GLS_DSTBLEND_SRC_ALPHA					0x00000050
#define GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA		0x00000060
#define GLS_DSTBLEND_DST_ALPHA					0x00000070
#define GLS_DSTBLEND_ONE_MINUS_DST_ALPHA		0x00000080
#define	GLS_DSTBLEND_BITS					    0x000000f0

#define GLS_BLEND_BITS							( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS )

#define GLS_DEPTHMASK_TRUE						0x00000100

#define GLS_POLYMODE_LINE						0x00001000

#define GLS_DEPTHTEST_DISABLE					0x00010000
#define GLS_DEPTHFUNC_EQUAL						0x00020000

#define GLS_ATEST_GT_0							0x10000000
#define GLS_ATEST_LT_80							0x20000000
#define GLS_ATEST_GE_80							0x40000000
#define GLS_ATEST_GE_C0							0x80000000
#define	GLS_ATEST_BITS					    	0xF0000000

#define GLS_DEFAULT								GLS_DEPTHMASK_TRUE

#ifndef GL_REPEAT
#define GL_REPEAT				0x2901
#endif
#ifndef GL_CLAMP
#define GL_CLAMP				0x2900
#endif

typedef enum {
	TYPE_COLOR_WHITE,
	TYPE_COLOR_GREEN,
	TYPE_COLOR_RED,
	TYPE_FOG_ONLY,
	TYPE_DOT,
	TYPE_REFRACTION,

	TYPE_SINGLE_TEXTURE_LIGHTING,
	TYPE_SINGLE_TEXTURE_LIGHTING_LINEAR,

	TYPE_SINGLE_TEXTURE_DF,
	TYPE_SINGLE_TEXTURE_IDENTITY,

	// Text. The texture is a signed distance field rather than a picture of
	// letters, so the fragment shader recovers the outline from it instead of
	// sampling coverage - which is what lets one atlas serve every point size.
	TYPE_SINGLE_TEXTURE_MSDF,

	// The sky, sampled by direction out of one cubemap rather than by texture
	// coordinate out of six separate faces. The vertex carries the direction in
	// the normal slot; see skycube.vert.
	TYPE_SKYCUBE,

	TYPE_GENERIC_BEGIN,
	TYPE_SINGLE_TEXTURE = TYPE_GENERIC_BEGIN,
	TYPE_SINGLE_TEXTURE_ENV,

	TYPE_MULTI_TEXTURE_MUL2,
	TYPE_MULTI_TEXTURE_MUL2_ENV,
	TYPE_MULTI_TEXTURE_ADD2_IDENTITY,
	TYPE_MULTI_TEXTURE_ADD2_IDENTITY_ENV,
	TYPE_MULTI_TEXTURE_ADD2,
	TYPE_MULTI_TEXTURE_ADD2_ENV,

	TYPE_MULTI_TEXTURE_MUL3,
	TYPE_MULTI_TEXTURE_MUL3_ENV,
	TYPE_MULTI_TEXTURE_ADD3_IDENTITY,
	TYPE_MULTI_TEXTURE_ADD3_IDENTITY_ENV,
	TYPE_MULTI_TEXTURE_ADD3,
	TYPE_MULTI_TEXTURE_ADD3_ENV,

	TYPE_BLEND2_ADD,
	TYPE_BLEND2_ADD_ENV,
	TYPE_BLEND2_MUL,
	TYPE_BLEND2_MUL_ENV,
	TYPE_BLEND2_ALPHA,
	TYPE_BLEND2_ALPHA_ENV,
	TYPE_BLEND2_ONE_MINUS_ALPHA,
	TYPE_BLEND2_ONE_MINUS_ALPHA_ENV,
	TYPE_BLEND2_MIX_ALPHA,
	TYPE_BLEND2_MIX_ALPHA_ENV,
	TYPE_BLEND2_MIX_ONE_MINUS_ALPHA,
	TYPE_BLEND2_MIX_ONE_MINUS_ALPHA_ENV,

	TYPE_BLEND2_DST_COLOR_SRC_ALPHA,
	TYPE_BLEND2_DST_COLOR_SRC_ALPHA_ENV,

	TYPE_BLEND3_ADD,
	TYPE_BLEND3_ADD_ENV,
	TYPE_BLEND3_MUL,
	TYPE_BLEND3_MUL_ENV,
	TYPE_BLEND3_ALPHA,
	TYPE_BLEND3_ALPHA_ENV,
	TYPE_BLEND3_ONE_MINUS_ALPHA,
	TYPE_BLEND3_ONE_MINUS_ALPHA_ENV,
	TYPE_BLEND3_MIX_ALPHA,
	TYPE_BLEND3_MIX_ALPHA_ENV,
	TYPE_BLEND3_MIX_ONE_MINUS_ALPHA,
	TYPE_BLEND3_MIX_ONE_MINUS_ALPHA_ENV,

	TYPE_BLEND3_DST_COLOR_SRC_ALPHA,
	TYPE_BLEND3_DST_COLOR_SRC_ALPHA_ENV,

	TYPE_GENERIC_END = TYPE_BLEND3_MIX_ONE_MINUS_ALPHA_ENV

} Vk_Shader_Type;

// used with cg_shadows == 2
typedef enum {
	SHADOW_DISABLED,
	SHADOW_EDGES,
	SHADOW_FS_QUAD,
} Vk_Shadow_Phase;

typedef enum {
	TRIANGLE_LIST = 0,
	TRIANGLE_STRIP,
	LINE_LIST,
	POINT_LIST
} Vk_Primitive_Topology;

// tr_main.cpp
void Matrix16Identity( mat4_t out );
void Matrix16Copy( const mat4_t in, mat4_t out );

// Instance

typedef union floatint_u
{
	int32_t		i;
	uint32_t	u;
	float		f;
	byte		b[4];
} floatint_t;

typedef struct vk_storage_buffer_s {
	VkBuffer			buffer;
	byte				*buffer_ptr;
	VmaAllocation		allocation;
	VkDescriptorSet		descriptor;
} vk_storage_buffer_t;

typedef enum {
	DEPTH_RANGE_NORMAL, // [0..1]
	DEPTH_RANGE_ZERO, // [0..0]
	DEPTH_RANGE_ONE, // [1..1]
	DEPTH_RANGE_WEAPON, // [0..0.3]
	DEPTH_RANGE_COUNT
} Vk_Depth_Range;

typedef enum {
	RENDER_PASS_MAIN = 0,
	RENDER_PASS_SCREENMAP,
	RENDER_PASS_POST_BLEND,
	RENDER_PASS_DGLOW,
	RENDER_PASS_CUBEMAP,
	RENDER_PASS_REFRACTION,
	RENDER_PASS_COUNT
} renderPass_t;

typedef struct {
	uint32_t				state_bits; // GLS_XXX flags
	cullType_t				face_culling;// cullType_t

	qboolean				polygon_offset;
	qboolean				mirror;
	qboolean				vbo_ghoul2;
	qboolean				vbo_mdv;

	// The depth pre-pass variant of this pipeline: same geometry, same vertex
	// shader, no colour. It is a field of the def rather than a flag beside it
	// because vk_find_pipeline_ext keys on a memcmp of the whole struct, so a
	// discriminator that lives outside it does not distinguish anything.
	qboolean				depth_only;

	Vk_Shader_Type			shader_type;	
	Vk_Shadow_Phase			shadow_phase;
	Vk_Primitive_Topology	primitives;
	uint32_t				surface_sprite_flags;

	int fog_stage; // off, fog-in / fog-out
	int line_width;
	int abs_light;
	int allow_discard;

	int						vk_light_flags;

#ifdef USE_VK_PBR
	uint32_t				vk_pbr_flags;
#endif
} Vk_Pipeline_Def;

typedef struct VK_Pipeline {
	Vk_Pipeline_Def def;
	VkPipeline		handle[RENDER_PASS_COUNT];
} VK_Pipeline_t;

// this structure must be in sync with shader uniforms!
//
// renderMode is a float rather than an int because the whole block is memcpy'd
// into a push constant the vertex shader declares as floats; the shader casts it
// back. It carries r_debugView, which is not a debugging leftover - see the note
// over R_DebugView_f in tr_init.cpp for why a way to see one term of the
// lighting at a time is worth a push constant on every draw.
typedef struct {
	float	mvp[16];
	float	renderMode;
} pushConst;

typedef struct vkUniform_s {
	// light/env/material parameters:
	vec4_t eyePos;
	vec4_t lightPos;
	vec4_t lightColor; // rgb + 1/(r*r)
	vec4_t lightVector;

	// fog parameters:
	union {
		struct {
			vec4_t fogDistanceVector;
			vec4_t fogDepthVector;
			vec4_t fogEyeT;
			vec4_t fogColor;
		} fog;

		struct {
			vktcMod_t tcMod;
			vktcGen_t tcGen;
		} refraction;
	};
} vkUniform_t;

typedef struct {
	VkSamplerAddressMode address_mode; // clamp/repeat texture addressing mode

	int gl_mag_filter; // GL_XXX mag filter
	int gl_min_filter; // GL_XXX min filter

	qboolean max_lod_1_0; // fixed 1.0 lod
	qboolean noAnisotropy;
} Vk_Sampler_Def;

struct Image_Upload_Data  {
	byte *buffer;
	int buffer_size;
	int mip_levels;
	int base_level_width;
	int base_level_height;
};

extern int vkSamples;
extern int vkMaxSamples;

extern unsigned char s_intensitytable[256];
extern unsigned char s_gammatable[256];
extern unsigned char s_gammatable_linear[256];

// Vk_World contains vulkan resources/state requested by the game code.
// It is reinitialized on a map change.
typedef struct {
	// memory allocations.

	// This flag is used to decide whether framebuffer's depth attachment should be cleared
	// with vmCmdClearAttachment (dirty_depth_attachment != 0), or it have just been
	// cleared by render pass instance clear op (dirty_depth_attachment == 0).
	int dirty_depth_attachment;

	// MVP
	float modelview_transform[16]  QALIGN(16);
} Vk_World;

typedef struct vk_tess_s {
	VkCommandBuffer		command_buffer;

	VkSemaphore			image_acquired;
	uint32_t			swapchain_image_index;
	qboolean			swapchain_image_acquired;
#ifdef USE_UPLOAD_QUEUE
	VkSemaphore			rendering_finished2;
#endif
	VkFence				rendering_finished_fence;
	qboolean			waitForFence;
	
	VkBuffer			vertex_buffer;
	VmaAllocation		vertex_buffer_allocation;
	byte				*vertex_buffer_ptr; // pointer to mapped vertex buffer
	VkDeviceSize		vertex_buffer_offset; // moved to uint32_t in q3e:63d6d7402d78254bebe28035006fe600f645c8de

	VkBuffer			indirect_buffer;
	VmaAllocation		indirect_buffer_allocation;
	byte				*indirect_buffer_ptr; // pointer to mapped indirect buffer
	VkDeviceSize		indirect_buffer_offset;

	VkDescriptorSet		uniform_descriptor;

	VkDeviceSize		buf_offset[12];	// 10 is ok, bones & weights are ghoul2 vbo only anyway
	VkDeviceSize		vbo_offset[12];

	VkBuffer			curr_index_buffer;
	uint32_t			curr_index_offset;

	struct {
		uint32_t		start, end;
		VkDescriptorSet	current[VK_DESC_COUNT];			// 0:uniform, 1:color0, 2:color1, 3:color2, 4:fog, 5:brdf lut, 6:normal, 7:physical, 8:prefilterd envmap, !9:irradiance envmap
		uint32_t		offset[VK_DESC_UNIFORM_COUNT];	// 0:data, 1: camera, 2:light 3: entity, 4:ghoul2, 5:global
	} descriptor_set;
	
	uint32_t			num_indexes; // value from most recent vk_bind_index() call
	VkPipeline			last_pipeline;
#ifdef USE_VK_PBR
	// Where each of the five physically-based textures for the next draw comes
	// from. What is NOT here is the VkDescriptorImageInfo that gets pushed:
	// that is built from scratch on every push and lives on the stack.
	//
	// It used to be kept here, and keeping it is what crashed the bench. A
	// binding no call site set this draw kept the previous draw's image info,
	// and the test for "nothing set this one" was imageView == VK_NULL_HANDLE -
	// which a view destroyed by a vid_restart is not. So a handle to a
	// destroyed view went to the driver, and the validation layer segfaulted
	// dereferencing it, inside RB_Dissolve on the first wipe after a restart.
	// A stale handle that is null is caught; a stale handle that is not null is
	// exactly the case that cannot be told from a good one by looking at it.
	//
	// So the only state that survives a push is a pointer to the image, whose
	// view is read live, and the raw descriptor for the BRDF lookup table,
	// which is not an image_t and is made once per renderer life.
	const struct image_s	*pbr_source[VK_DESC_PBR_BINDING_COUNT];
	VkDescriptorImageInfo	pbr_raw[VK_DESC_PBR_BINDING_COUNT];
	qboolean				pbr_raw_set[VK_DESC_PBR_BINDING_COUNT];
	qboolean				pbr_dirty;
#endif

	// Set when vk_bind_pipeline was asked for a pipeline that has no handle
	// and could not bind one. The draw that follows has to be skipped too: a
	// draw with no graphics pipeline bound is undefined behaviour whatever the
	// bind did.
	qboolean			pipeline_missing;
	Vk_Depth_Range		depth_range;
	VkRect2D			scissor_rect;

	uint32_t			camera_ubo_offset;
	uint32_t			light_ubo_offset;
	uint32_t			entity_ubo_offset[REFENTITYNUM_WORLD + 1];
	uint32_t			bones_ubo_offset;
	uint32_t			fogs_ubo_offset;
} vk_tess_t;

// Vk_Instance contains engine-specific vulkan resources that persist entire renderer lifetime.
// This structure is initialized/deinitialized by vk_initialize/vk_shutdown functions correspondingly.
typedef struct {
	VkInstance			instance;
	VkPhysicalDevice	physical_device;
	VkSurfaceKHR		surface;
	VkSurfaceFormatKHR	base_format;
	VkSurfaceFormatKHR	present_format;

	// to prevent changes to rd-common, move these here
	char			renderer_string[MAX_STRING_CHARS];
	char			vendor_string[MAX_STRING_CHARS];
	char			version_string[MAX_STRING_CHARS];
	char			device_extensions_string[MAX_STRING_CHARS];
	char			instance_extensions_string[MAX_STRING_CHARS];

#ifdef USE_VK_VALIDATION
	#ifdef USE_DEBUG_REPORT
		VkDebugReportCallbackEXT debug_callback;
	#endif
	#ifdef USE_DEBUG_UTILS
		VkDebugUtilsMessengerEXT debug_utils_messenger;
	#endif
#endif

	uint32_t		queue_family_index;
	VkDevice		device;
	VkQueue			queue;

	VkPhysicalDeviceMemoryProperties devMemProperties;

	VkSwapchainKHR	swapchain;
	uint32_t		swapchain_image_count;
	//uint32_t		swapchain_image_index;
	VkImage			swapchain_images[MAX_SWAPCHAIN_IMAGES];
	qboolean		swapchain_images_inited[MAX_SWAPCHAIN_IMAGES];
	VkImageView		swapchain_image_views[MAX_SWAPCHAIN_IMAGES];
	VkSemaphore		swapchain_rendering_finished[MAX_SWAPCHAIN_IMAGES];

	VmaAllocation	image_memory[MAX_ATTACHMENTS_IN_POOL];
	uint32_t		image_memory_count;

	VkCommandPool	command_pool;
#ifdef USE_UPLOAD_QUEUE
	VkCommandBuffer	staging_command_buffer;
#endif

	VkDescriptorSet	color_descriptor;
	VkDescriptorSet bloom_image_descriptor[1 + VK_NUM_BLUR_PASSES * 2];
	VkDescriptorSet dglow_image_descriptor[1 + VK_NUM_BLUR_PASSES * 2];
	
	VkImage			depth_image;
	VkImageView		depth_image_view;

	VkImage			msaa_image;
	VkImageView		msaa_image_view;

	VkImage			color_image;
	VkImageView		color_image_view;
	
	VkImage			refraction_extract_image;
	VkImageView		refraction_extract_image_view;
	uint32_t		refraction_extract_mips;
	VkDescriptorSet	refraction_extract_descriptor;

	VkImage			bloom_image[1 + VK_NUM_BLUR_PASSES * 2];
	VkImageView		bloom_image_view[1 + VK_NUM_BLUR_PASSES * 2];

	VkImage			dglow_image[1 + VK_NUM_BLUR_PASSES * 2];
	VkImageView		dglow_image_view[1 + VK_NUM_BLUR_PASSES * 2];
	VkImage			dglow_msaa_image;
	VkImageView		dglow_msaa_image_view;

#ifdef VK_PBR_BRDFLUT
	VkImage			brdflut_image;
	VkImageView		brdflut_image_view;
	VkDescriptorSet brdflut_image_descriptor;
#endif

	// screenmap
	struct {
		VkImage			depth_image;
		VkImageView		depth_image_view;

		VkImage			color_image_msaa;
		VkImageView		color_image_view_msaa;

		VkDescriptorSet color_descriptor;
		VkImage			color_image;
		VkImageView		color_image_view;
	} screenMap;

	// cubemap
	struct {
		VkImage			depth_image;
		VkImageView		depth_image_view;

		VkDescriptorSet color_descriptor;
		VkImage			color_image;
		VkImageView		color_image_view[7];
	} cubeMap;


	// render passes
	struct {
		VkRenderPass main;
		VkRenderPass gamma;
		VkRenderPass screenmap;
		VkRenderPass capture;
#ifdef VK_PBR_BRDFLUT
		VkRenderPass brdflut;
#endif
		
		VkRenderPass cubemap;

		struct {
			VkRenderPass extract;
		} refraction;

		struct {
			VkRenderPass blur[VK_NUM_BLUR_PASSES * 2];
			VkRenderPass extract;
			VkRenderPass blend;
		} bloom;

		struct {
			VkRenderPass blur[VK_NUM_BLUR_PASSES * 2];
			VkRenderPass extract;
			VkRenderPass blend;
		} dglow;
	} render_pass;

	struct {
		VkImage		image;
		VkImageView image_view;
	} capture;


	// framebuffers
	struct {
		VkFramebuffer main[MAX_SWAPCHAIN_IMAGES];
		VkFramebuffer gamma[MAX_SWAPCHAIN_IMAGES];
		VkFramebuffer screenmap;
		VkFramebuffer capture;
#ifdef VK_PBR_BRDFLUT
		VkFramebuffer brdflut;
#endif

		VkFramebuffer cubemap[6];

		struct {
			VkFramebuffer extract;
		} refraction;

		struct {
			VkFramebuffer blur[VK_NUM_BLUR_PASSES * 2];
			VkFramebuffer extract;
		} bloom;

		struct {
			VkFramebuffer blur[VK_NUM_BLUR_PASSES * 2];
			VkFramebuffer extract;
		} dglow;
	} framebuffers;

#ifdef USE_UPLOAD_QUEUE
	VkSemaphore rendering_finished;	// reference to vk.cmd->rendering_finished2
	VkSemaphore image_uploaded2;
	VkSemaphore image_uploaded;		// reference to vk.image_uploaded2
#endif

	vk_tess_t tess[NUM_COMMAND_BUFFERS], *cmd;
	int cmd_index;

	vk_storage_buffer_t storage;

	uint32_t uniform_item_size;
	uint32_t uniform_alignment;
	uint32_t storage_alignment;

	uint32_t uniform_camera_item_size;
	uint32_t uniform_global_item_size;
	uint32_t uniform_light_item_size;
	uint32_t uniform_entity_item_size;
	uint32_t uniform_fogs_item_size;

	uint32_t ghoul2_vbo_stride;
	uint32_t mdv_vbo_stride;

	struct {
		VkBuffer		vertex_buffer;
		VmaAllocation	buffer_memory;
	} vbo;

#ifdef USE_VBO_SS
	vk_storage_buffer_t surface_sprites_ssbo[MAX_SUB_BSP + 1];
	uint32_t			surface_sprites_ssbo_item_size;
#endif

	// statistics
	struct {
		VkDeviceSize	vertex_buffer_max;
		uint32_t		push_size;
		uint32_t		push_size_max;
	} stats;

	// host visible memory that holds vertex, index and uniform data
	VkDeviceSize		geometry_buffer_size;
	VkDeviceSize		geometry_buffer_size_new;

	// host visible memory that holds indirect drawdata
	VkDeviceSize		indirect_buffer_size;
	VkDeviceSize		indirect_buffer_size_new;

	VkDescriptorPool		descriptor_pool;
	VkDescriptorSetLayout	set_layout_sampler;		// combined image sampler
#ifdef USE_VK_PBR
	// One set for all five physically-based textures, pushed rather than
	// allocated - see VK_DESC_PBR in shaders/glsl/global.h for why there is
	// one instead of five.
	VkDescriptorSetLayout	set_layout_pbr;
	VkDescriptorImageInfo	brdflut_descriptor_info;
	qboolean				pushDescriptorAvailable;
#endif
	VkDescriptorSetLayout	set_layout_uniform;		// dynamic uniform buffer
	VkDescriptorSetLayout	set_layout_storage;		// feedback buffer

#ifdef VK_COMPUTE_NORMALMAP
	VkDescriptorSetLayout	set_layout_compute_normalmap;
	VkPipelineLayout		pipeline_layout_compute_normalmap;
	VkPipeline				compute_normalmap_pipeline;
#endif

	// pipeline(s)
	VkPipelineLayout pipeline_layout;				// default shaders
	VkPipelineLayout pipeline_layout_storage;		// flare test shader layout
#ifdef USE_VBO_SS
	VkPipelineLayout pipeline_layout_surface_sprite;// surface sprites
#endif
	VkPipelineLayout pipeline_layout_post_process;	// post-processing
	VkPipelineLayout pipeline_layout_blend;			// post-processing
#ifdef VK_PBR_BRDFLUT
	VkPipelineLayout pipeline_layout_brdflut;
#endif

	VkPipeline gamma_pipeline;
	VkPipeline bloom_extract_pipeline;
	VkPipeline bloom_blur_pipeline[VK_NUM_BLUR_PASSES * 2]; // horizontal & vertical pairs
	VkPipeline bloom_blend_pipeline;
	VkPipeline capture_pipeline;
	VkPipeline dglow_blur_pipeline[VK_NUM_BLUR_PASSES * 2]; // horizontal & vertical pairs
	VkPipeline dglow_blend_pipeline;
#ifdef VK_PBR_BRDFLUT
	VkPipeline brdflut_pipeline;
#endif

	VkPipeline refraction_capture_pipeline;

	// Standard pipeline(s)
	struct  {
		uint32_t skybox_pipeline;
		uint32_t worldeffect_pipeline[2];

		// dim 0: 0 - front side, 1 - back size
		// dim 1: 0 - normal view, 1 - mirror view
		uint32_t shadow_volume_pipelines[2][2];
		uint32_t shadow_finish_pipeline;

		// dim 0 is based on fogPass_t: 0 - corresponds to FP_EQUAL, 1 - corresponds to FP_LE.
		// dim 1 is directly a cullType_t enum value.
		// dim 2 is a polygon offset value (0 - off, 1 - on).
		uint32_t fog_pipelines[3][2][3][2];

#ifdef USE_PMLIGHT
		// cullType[3], polygonOffset[2], fogStage[2], absLight[2]
		uint32_t dlight_pipelines_x[3][2][2][2];
		uint32_t dlight1_pipelines_x[3][2][2][2];
#endif

		// debug-visualization pipelines
		uint32_t tris_debug_pipeline;
		uint32_t tris_mirror_debug_pipeline;
		uint32_t tris_debug_green_pipeline;
		uint32_t tris_mirror_debug_green_pipeline;
		uint32_t tris_debug_red_pipeline;
		uint32_t tris_mirror_debug_red_pipeline;

		uint32_t normals_debug_pipeline;
		uint32_t surface_debug_pipeline_solid;
		uint32_t surface_debug_pipeline_outline;
		uint32_t images_debug_pipeline;
		uint32_t surface_beam_pipeline;
		uint32_t surface_axis_pipeline;
		uint32_t dot_pipeline;
	} std_pipeline;

	VK_Pipeline_t	pipelines[MAX_VK_PIPELINES];
	VkPipelineCache pipelineCache;
	VmaAllocator	allocator;

	uint32_t	pipelines_count;
	uint32_t	pipelines_world_base;
	int32_t		pipeline_create_count;

	
	// shader modules.
	struct {
		// vbo 0: cpu or vbo world, 1: vbo ghoul2, 2: vbo mdv

		struct {	
			VkShaderModule gen[3][2][4][3][2][2][2]; // vbo[0,1], pbr[0,1], tx[0,1,2], cl[0,1] env0[0,1] fog[0,1]
			VkShaderModule light[2]; // fog[0,1]
			VkShaderModule gen0_ident;
			VkShaderModule fog[3][2];	// vbo[0,1,2], fog mode[0,1]
		}	vert;

		struct {
			VkShaderModule gen0_ident;
			VkShaderModule gen0_df;
			VkShaderModule gen[2][4][3][2][2]; // pbr[0,1], tx[0,1,2] cl[0,1] fog[0,1]
			VkShaderModule light[2][2]; // linear[0,1] fog[0,1]
			VkShaderModule fog[2];	// vbo[0,1,2], fog mode[0,1]
		}	frag;

		VkShaderModule surface_sprite_fs[2];
		VkShaderModule surface_sprite_vs[2];

		VkShaderModule dot_fs;
		VkShaderModule dot_vs;

		VkShaderModule gamma_fs;
		VkShaderModule gamma_vs;

		VkShaderModule color_vs;
		VkShaderModule color_fs;

		VkShaderModule msdf_fs;
	VkShaderModule		skycube_vs;
	VkShaderModule		skycube_fs;

		VkShaderModule bloom_fs;
		VkShaderModule blur_fs;
		VkShaderModule blend_fs;
#ifdef VK_PBR_BRDFLUT
		VkShaderModule brdflut_fs;
#endif
		VkShaderModule filtercube_vs;
		VkShaderModule filtercube_gm;
		VkShaderModule irradiancecube_fs;
		VkShaderModule prefilterenvmap_fs;
		VkShaderModule refraction_vs[3];
		VkShaderModule refraction_fs[2];	// [0] pbr, [1] fastlight - see refraction_frag.tmpl

		VkShaderModule normalmap;
	} shaders;

	uint32_t frame_count;
	qboolean shaderStorageImageMultisample;
	qboolean samplerAnisotropy;
	qboolean fragmentStores;
	qboolean dedicatedAllocation;
	qboolean debugMarkers;
	qboolean wideLines;
	qboolean clearAttachment;		// requires VK_IMAGE_USAGE_TRANSFER_DST_BIT
	qboolean fboActive;
	qboolean blitEnabled;

	qboolean vboWorldActive;
	qboolean vboGhoul2Active;
	qboolean vboMdvActive;

	qboolean normalMappingActive;
	qboolean specularMappingActive;

	qboolean useFastLight;
#ifdef VK_DLIGHT_GPU
	qboolean useGPUDLight;
#endif

	float maxAnisotropy;
	float maxLod;

	VkFormat depth_format;
	VkFormat color_format;
	VkFormat bloom_format;
	VkFormat capture_format;
	VkFormat compressed_format;

	VkImageLayout initSwapchainLayout;

	qboolean active;
	qboolean msaaActive;
	qboolean bloomActive;
	qboolean dglowActive;
#ifdef VK_CUBEMAP
	qboolean cubemapActive;
#endif
	qboolean refractionActive;

	qboolean	offscreenRender;
	qboolean	windowAdjusted;
	int			blitX0;
	int			blitY0;
	int			blitFilter;

	// Fog mode
	// 1: legacy fog
	// 2: legacy "hardware" fog + stage collapsing
	// 3: legacy fog + stage collapsing
	uint32_t	hw_fog;

	uint32_t screenMapWidth;
	uint32_t screenMapHeight;
	uint32_t screenMapSamples;

	int		ctmu;	// current texture index

	uint32_t renderWidth;
	uint32_t renderHeight;

	float	renderScaleX;
	float	renderScaleY;
	float	yscale2D;
	float	xscale2D;

	renderPass_t renderPassIndex;

	uint32_t maxBoundDescriptorSets;

	// How many sets the main pipeline layout has: the full VK_DESC_COUNT, or
	// four on a device sitting at the Vulkan minimum. Nothing may bind or update
	// a set at or above this.
	uint32_t descriptorSetCount;
	
#ifdef USE_UPLOAD_QUEUE
	VkFence aux_fence;
	qboolean aux_fence_wait;
#endif

	struct staging_buffer_s {
		VkBuffer handle;
		VmaAllocation allocation;
		VkDeviceSize size;
		byte *ptr; // pointer to mapped staging buffer
#ifdef USE_UPLOAD_QUEUE
		VkDeviceSize offset;
#endif
	} staging_buffer;

	struct samplers_s {
		int count;
		Vk_Sampler_Def def[MAX_VK_SAMPLERS];
		VkSampler handle[MAX_VK_SAMPLERS];
		int filter_min;
		int filter_max;
	} samplers;

	struct defaults_t {
		VkDeviceSize staging_size;
		VkDeviceSize geometry_size;
	} defaults;

	char driverNote[200];

	struct {
		VkDescriptorSet *descriptor;
		uint32_t descriptor_size;
	} debug;

} Vk_Instance;

extern Vk_Instance	vk;				// shouldn't be cleared during ref re-init
extern Vk_World		vk_world;		// this data is cleared during ref re-init

// ...
qboolean	vk_surface_format_color_depth( VkFormat format, int* r, int* g, int* b );
void		vk_set_clearcolor( void );
void		vk_create_window( void );
void		vk_initialize( void );
void		vk_shutdown( void );
// Installs a process-wide fault handler that writes a symbolised stack to
// jkx_crash.txt next to the executable. Cheap, always on: the cost of not
// having it is a round trip to the machine that crashed.
//
// Must be paired. The engine unloads this module on every vid_restart, and a
// handler the operating system still points at, in freed memory, turns the next
// exception of any kind into an access violation.
void		vk_staging_note( const char *what, int size );
void		vk_staging_report( void );
void		vk_install_crash_handler( void );
void		vk_remove_crash_handler( void );
void		vk_init_library( void );
void		vk_deinit_library( void );
void		get_viewport( VkViewport *viewport, Vk_Depth_Range depth_range );
void		get_viewport_rect( VkRect2D *r );
void		get_scissor_rect( VkRect2D *r );
void		myGlMultMatrix( const float *a, const float *b, float *out );
qboolean	vk_select_surface_format( VkPhysicalDevice physical_device, VkSurfaceKHR surface );
void		vk_setup_surface_formats( VkPhysicalDevice physical_device );
qboolean	R_CanMinimize( void );

// pipeline
void		vk_create_pipelines(void);
void		vk_alloc_persistent_pipelines( void );
void		vk_create_descriptor_layout( void );
void		vk_create_pipeline_layout( void );
void		vk_destroy_pipelines( qboolean reset );
void		vk_update_post_process_pipelines( void );

// swapchain
void		vk_restart_swapchain( const char *funcname );
void		vk_destroy_swapchain( void );
void		vk_create_swapchain( VkPhysicalDevice physical_device, VkDevice device,
	VkSurfaceKHR surface, VkSurfaceFormatKHR surface_format, VkSwapchainKHR *swapchain);

// frame
void		vk_begin_frame( void );
void		vk_end_frame( void );
void		vk_present_frame( void );
void		vk_create_framebuffers( void );
void		vk_destroy_framebuffers( void );
void		vk_create_sync_primitives( void );
void		vk_destroy_sync_primitives( void );
void		vk_release_geometry_buffers( void );
void		vk_release_indirect_buffers( void );
void		vk_wait_idle( void );
#ifdef USE_UPLOAD_QUEUE
void		vk_flush_staging_buffer( qboolean final );
#endif
void		vk_queue_wait_idle( void );
void		vk_release_resources( void );
void		vk_read_pixels( byte *buffer, uint32_t width, uint32_t height );

// vbo
void		vk_release_vbo( void );
void		vk_release_model_vbo( void );
qboolean	vk_alloc_vbo( const char *name, const byte *vbo_data, int vbo_size );
void		VBO_PrepareQueues( void );
void		VBO_RenderIBOItems( void );
void		VBO_ClearQueue( void );

int			get_mdv_stride( void );
int			get_mdxm_stride( void );

// shader
void		vk_create_shader_modules( void );
void		vk_destroy_shader_modules( void );

// command
VkCommandBuffer vk_begin_command_buffer( void );
void		vk_end_command_buffer( VkCommandBuffer command_buffer, const char *location );
void		vk_create_command_pool( void );
void		vk_create_command_buffer( void );
void		vk_submit_image_barrier( VkCommandBuffer cb, const VkImageMemoryBarrier2 *barrier );
void vk_record_mip_layout_transition( VkCommandBuffer cmdBuf, VkImage image, uint32_t level,
	VkImageLayout old_layout, VkImageLayout new_layout );
void vk_record_image_layout_transition( VkCommandBuffer cmdBuf, VkImage image, 
	VkImageAspectFlags image_aspect_flags, 
	VkImageLayout old_layout, VkImageLayout new_layout, uint32_t src_stage_override, uint32_t dst_stage_override );


// memory
uint32_t	vk_find_memory_type( uint32_t memory_type_bits, VkMemoryPropertyFlags properties );
uint32_t	vk_find_memory_type_lazy( uint32_t memory_type_bits, VkMemoryPropertyFlags properties, VkMemoryPropertyFlags *outprops );

// attachment
void		vk_create_attachments( void );
void		vk_destroy_attachments( void );
void		vk_update_attachment_descriptors( void );
void		vk_clear_color_attachments( const vec4_t color );
void		vk_clear_depthstencil_attachments( qboolean clear_stencil );

// shade geometry
void		vk_set_2d( void );
void		vk_set_2d_scissor( float x, float y, float w, float h );
void		vk_get_2d_viewport( float *x, float *y, float *w, float *h, float *virtualW );
float		vk_ui_scale( void );
void		vk_clear_2d_scissor( void );
void		vk_set_depthrange( const Vk_Depth_Range depthRange );


pushConst	*vk_get_push_constant();

void		vk_update_mvp( const float *m );

void		vk_create_render_passes( void );
void		vk_destroy_render_passes( void );
void		vk_select_texture( const int index );
uint32_t	vk_tess_index( uint32_t numIndexes, const void *src );
#ifdef USE_VBO
void		vk_draw_indexed( uint32_t indexCount, uint32_t firstIndex );
#endif
void		vk_bind_index_buffer( VkBuffer buffer, uint32_t offset, VkIndexType type = VK_INDEX_TYPE_UINT32 );
void		vk_bind_index( void );
void		vk_bind_index_ext( const int numIndexes, const uint32_t *indexes );
void		vk_bind_pipeline( uint32_t pipeline );
void		vk_draw_geometry( Vk_Depth_Range depth_range, qboolean indexed );
void		vk_draw_dot( uint32_t storage_offset );
void		vk_bind_geometry( uint32_t flags );
void		vk_bind_geometry_buffer( void );
void		vk_bind_lighting( int stage, int bundle );
void		vk_reset_descriptor( int index);
void		vk_update_uniform_descriptor( VkDescriptorSet descriptor, VkBuffer buffer );
void		vk_create_storage_buffer( vk_storage_buffer_t *out, uint32_t size, const char *name );
void		vk_update_descriptor_offset( int index, uint32_t offset );
void		vk_init_descriptors( void );
void		vk_create_vertex_buffer( VkDeviceSize size );
void		vk_create_indirect_buffer( VkDeviceSize size );
VkBuffer	vk_get_vertex_buffer( void );
void		vk_update_descriptor( int tmu, VkDescriptorSet curDesSet );
#ifdef USE_VK_PBR
void		vk_update_pbr_descriptor( int binding, const struct image_s *image );
void		vk_update_pbr_descriptor_raw( int binding, const VkDescriptorImageInfo *info );
#endif
uint32_t	vk_find_pipeline_ext( uint32_t base, const Vk_Pipeline_Def *def, qboolean use );
VkPipeline	vk_gen_pipeline( uint32_t index );
void		vk_end_render_pass( void );
void		vk_begin_main_render_pass( void );
void		vk_get_pipeline_def( uint32_t pipeline, Vk_Pipeline_Def *def );
void		*vk_reserve_uniform( size_t size, uint32_t *offset );
uint32_t	vk_append_uniform( const void *uniform, size_t size, uint32_t min_offset );

// image process
void		R_SetColorMappings( void );
void		R_LightScaleTexture( byte *in, int inwidth, int inheight, qboolean only_gamma );
void		ResampleTexture( unsigned *in, int inwidth, int inheight, unsigned *out, int outwidth, int outheight );
byte		*R_ResampleRowScratch( size_t size );
void		R_BlendOverTexture( unsigned char *data, const uint32_t pixelCount, const uint32_t l );
void		R_MipMap( byte *out, byte *in, int width, int height );
void		R_MipMap2( unsigned* const out, unsigned* const in, int inWidth, int inHeight );

// image
void		vk_texture_mode( const char *string, const qboolean init );
void		R_LoadHDRImage( const char* filename, byte** data, int* width, int* height );
void		vk_destroy_samplers( void );
VkSampler	vk_find_sampler( const Vk_Sampler_Def *def );
void		vk_delete_textures( void );
#if 0
void		vk_record_buffer_memory_barrier( VkCommandBuffer cb, VkBuffer buffer,
				VkDeviceSize size, VkDeviceSize offset,
				VkPipelineStageFlags2 src_stages, VkPipelineStageFlags2 dst_stages,
				VkAccessFlags2 src_access, VkAccessFlags2 dst_access );
#endif
// post-processing
void		vk_begin_post_blend_render_pass( VkRenderPass renderpass, qboolean clearValues );

// bloom
void		vk_begin_bloom_extract_render_pass( void );
void		vk_begin_bloom_blur_render_pass( uint32_t index );
qboolean	vk_bloom( void );

// refraction
void		vk_refraction_extract( void );
void		vk_begin_post_refraction_extract_render_pass( void );

// dynamic glow
void		vk_begin_dglow_extract_render_pass( void );
void		vk_begin_dglow_blur_render_pass( uint32_t index );
qboolean	vk_begin_dglow_blur( void );

// cubemap
#ifdef VK_CUBEMAP
void		vk_begin_cubemap_render_pass( void );
void		vk_create_cubemap_prefilter( void );
void		vk_destroy_cubemap_prefilter( void );
#endif

#ifdef VK_PBR_BRDFLUT
void		vk_create_brfdlut( void );
#endif

// info
const char	*vk_format_string( VkFormat format );
const char	*vk_result_string( VkResult code );

// vk_allocator.cpp
void		vk_create_allocator( void );
void		vk_destroy_allocator( void );
qboolean	vk_create_image_memory( const VkImageCreateInfo *desc, VkImage *image, VmaAllocation *allocation, const char *name );
void		vk_destroy_image_memory( VkImage *image, VmaAllocation *allocation );
qboolean	vk_alloc_image_memory( VkImage image, qboolean transient, VmaAllocation *allocation, const char *name );
qboolean	vk_create_buffer_memory( const VkBufferCreateInfo *desc, vk_buffer_memory_t kind, VkBuffer *buffer,
				VmaAllocation *allocation, void **mapped, const char *name );
void		vk_destroy_buffer_memory( VkBuffer *buffer, VmaAllocation *allocation );
void		vk_free_image_memory( VmaAllocation *allocation );
void		vk_print_memory_usage( void );

// vk_pipeline_cache.cpp
void		vk_create_pipeline_cache( void );
void		vk_save_pipeline_cache( void );
void		vk_destroy_pipeline_cache( void );
const char	*vk_shadertype_string( Vk_Shader_Type code );
const char	*renderer_name( const VkPhysicalDeviceProperties *props );
void		vk_get_vulkan_properties( VkPhysicalDeviceProperties *props );
void		vk_info_f( void );
void		GfxInfo_f( void );

// debug
void		vk_set_object_name( uint64_t obj, const char *objName, VkDebugReportObjectTypeEXT objType );
#define		VK_SET_OBJECT_NAME( obj, objName, objType) vk_set_object_name( (uint64_t)( obj ), ( objName ), ( objType ) );

#define VK_CHECK( function_call ) { \
	VkResult result = function_call; \
	if ( result < 0 ) \
		vk_debug( "Vulkan: error %s returned by %s \n", vk_result_string( result ), #function_call ); \
}

void		vk_debug( const char *msg, ... );
void		R_DebugGraphics( void );

#ifdef USE_VK_VALIDATION
	void	vk_create_debug_callback( void );

#ifdef USE_DEBUG_UTILS
	void	vk_create_debug_utils( VkDebugUtilsMessengerCreateInfoEXT &desc );
#endif
#endif

#ifdef USE_VK_OBJECT_TRACKER
	void		vk_dump_tracked_objects( void );

	void		vk_create_tracked_buffer( VkDevice device, const VkBufferCreateInfo* createInfo, VkBuffer* outBuffer, const char* debugName, const char* file, int line, const char* function );
	void		vk_destroy_tracked_buffer( VkDevice device, VkBuffer buffer );

	VkResult	vk_allocate_tracked_memory( VkDevice device, const VkMemoryAllocateInfo* allocInfo, VkDeviceMemory* outMemory, const char* debugName, const char* file, int line, const char* function );
	void		vk_allocate_tracked_memory_checked( VkDevice device, const VkMemoryAllocateInfo* allocInfo, VkDeviceMemory* outMemory, const char* debugName, const char* file, int line, const char* function );
	void		vk_free_tracked_memory( VkDevice device, VkDeviceMemory memory );
	
	#define VK_CREATE_BUFFER( device, info, outBuffer, name )				vk_create_tracked_buffer( device, info, outBuffer, name, __FILE__, __LINE__, __func__ )
	#define	VK_DESTROY_BUFFER( device, buffer )								vk_destroy_tracked_buffer( device, buffer )

	#define	VK_ALLOCATE_MEMORY( device, info, outMemory, name )				vk_allocate_tracked_memory( device, info, outMemory, name, __FILE__, __LINE__, __func__ )
	#define	VK_ALLOCATE_MEMORY_CHECK( device, info, outMemory, name )		vk_allocate_tracked_memory_checked( device, info, outMemory, name, __FILE__, __LINE__, __func__ )

	#define	VK_FREE_MEMORY( device, memory )								vk_free_tracked_memory( device, memory )
#else
	#define VK_CREATE_BUFFER(device, info, outBuffer, name)				VK_CHECK( vkCreateBuffer( device, info, NULL, outBuffer ) )
	#define VK_DESTROY_BUFFER(device, buffer)							vkDestroyBuffer( device, buffer, NULL )
	#define VK_ALLOCATE_MEMORY(device, info, outMemory, name)			vkAllocateMemory( device, info, NULL, outMemory )
	#define VK_ALLOCATE_MEMORY_CHECK(device, info, outMemory, name)		VK_CHECK( vkAllocateMemory( device, info, NULL, outMemory ) )
	#define VK_FREE_MEMORY(device, memory)								vkFreeMemory( device, memory, NULL )
#endif // USE_VK_OBJECT_TRACKER
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

static VkBuffer shade_bufs[12];	// +4 qtangents, lightdir, bones, weight

static int bind_base;
static int bind_count;

void vk_select_texture( const int index ) 
{
	if (vk.ctmu == index)
		return;

	if ( index >= glConfig.maxActiveTextures )
		Com_Error(ERR_DROP, "%s: texture unit overflow = %i", __func__, index);

	vk.ctmu = index;
}

void vk_set_depthrange( const Vk_Depth_Range depthRange ) 
{
	tess.depthRange = depthRange;
}

VkBuffer vk_get_vertex_buffer( void )
{
	return vk.cmd->vertex_buffer;
}
 
/*
=================
vk_get_2d_viewport

Where the 640x480 virtual screen lands in the render target, in texels.

SPACE2D_SCREEN covers the whole of it: the caller is working in a space that is
already the window's shape, because the client widened it before drawing.

SPACE2D_FRAME keeps the virtual screen's own proportions - it fits by whichever
axis runs out first and centres what is left. That is the difference between an
interface designed at one shape and shown at another, and the same interface
stretched to fit: on 21:9 the stretch is seventy-eight per cent wider than it
was drawn.
=================
*/
/*
=================
vk_ui_scale

r_uiScale, clamped and never zero. Read here rather than at the cvar because
this is on the path of every 2D draw and a division by nothing is not the place
to find out the cvar was unregistered.
=================
*/
float vk_ui_scale( void )
{
	extern cvar_t *r_uiScale;

	if ( !r_uiScale || r_uiScale->value <= 0.0f ) {
		return 1.0f;
	}
	if ( r_uiScale->value < 0.5f ) {
		return 0.5f;
	}
	if ( r_uiScale->value > 2.0f ) {
		return 2.0f;
	}
	return r_uiScale->value;
}

void vk_get_2d_viewport( float *x, float *y, float *w, float *h, float *virtualW )
{
	const float targetW = (float)vk.renderWidth;
	const float targetH = (float)vk.renderHeight;

	if ( backEnd.space2D == SPACE2D_SCREEN ) {
		// The whole target, and a virtual space as wide as the window is. This
		// is the one that has no margins by construction: the space is the
		// shape of the screen, so it covers it exactly.
		*x = 0.0f;
		*y = 0.0f;
		*w = targetW;
		*h = targetH;
		// r_uiScale applies to both axes or the picture is not magnified, it is
		// stretched. Only the vertical extent was divided by it below, so at
		// r_uiScale 2 the head-up display came out twice as tall as it was wide
		// - and the client's own idea of the width (CG_ScreenWidth) already
		// divided, so the two disagreed about where the right-hand edge was.
		*virtualW = ( ( glConfig.virtualWidth > 0.0f )
			? glConfig.virtualWidth : (float)SCREEN_WIDTH ) / vk_ui_scale();
		return;
	}

	*virtualW = (float)SCREEN_WIDTH;

	const float want = (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT;
	const float have = targetW / targetH;

	if ( have > want ) {			// window is wider: bars at the sides
		*h = targetH;
		*w = targetH * want;
		*x = ( targetW - *w ) * 0.5f;
		*y = 0.0f;
	} else {						// window is taller: bars top and bottom
		*w = targetW;
		*h = targetW / want;
		*x = 0.0f;
		*y = ( targetH - *h ) * 0.5f;
	}
}

static void get_mvp_transform( float *mvp )
{
	if (backEnd.projection2D)
	{
		// The virtual screen onto its rectangle of the target, and that
		// rectangle onto normalised device coordinates.
		float vx, vy, vw, vh, virtualW;
		vk_get_2d_viewport( &vx, &vy, &vw, &vh, &virtualW );

		const float targetW = (float)vk.renderWidth;
		const float targetH = (float)vk.renderHeight;

		// r_uiScale magnifies by shrinking the space: half as many units across
		// the same pixels is twice the size on screen. It applies to the
		// head-up display and not to a fitted picture, which is already as big
		// as the window it was fitted to.
		const float virtualH = ( backEnd.space2D == SPACE2D_SCREEN )
			? (float)SCREEN_HEIGHT / vk_ui_scale() : (float)SCREEN_HEIGHT;

		const float mvp0 = 2.0f * ( vw / targetW ) / virtualW;
		const float mvp5 = 2.0f * ( vh / targetH ) / virtualH;

		// The anchoring shift is in virtual units, so it goes through the same
		// scale the coordinates do.
		const float ox = -1.0f + 2.0f * ( vx / targetW ) + mvp0 * backEnd.space2DOffsetX;
		const float oy = -1.0f + 2.0f * ( vy / targetH );

		mvp[0] = mvp0; mvp[1] = 0.0f; mvp[2] = 0.0f; mvp[3] = 0.0f;
		mvp[4] = 0.0f; mvp[5] = mvp5; mvp[6] = 0.0f; mvp[7] = 0.0f;
#ifdef USE_REVERSED_DEPTH
		mvp[8] = 0.0f; mvp[9] = 0.0f; mvp[10] = 0.0f; mvp[11] = 0.0f;
		mvp[12] = ox; mvp[13] = oy; mvp[14] = 1.0f; mvp[15] = 1.0f;
#else
		mvp[8] = 0.0f; mvp[9] = 0.0f; mvp[10] = 1.0f; mvp[11] = 0.0f;
		mvp[12] = ox; mvp[13] = oy; mvp[14] = 0.0f; mvp[15] = 1.0f;
#endif
	}
	else
	{
		const float* p = backEnd.viewParms.projectionMatrix;
		float proj[16];
		Com_Memcpy(proj, p, 64);

		proj[5] = -p[5];
		myGlMultMatrix(vk_world.modelview_transform, proj, mvp);
	}
}

static pushConst push_constants = { 0 };

pushConst *vk_get_push_constant() {
	return &push_constants;
}

void vk_update_mvp( const float *m ) {
	//pushConst push_constants;

	// Specify push constants.
	//
	// sizeof(push_constants) here, not sizeof(the struct): the copy is into the
	// matrix, and the block has more in it than the matrix now. It was written
	// the other way and was harmless only for as long as the two were the same
	// size.
	if (m)
		Com_Memcpy(push_constants.mvp, m, sizeof(push_constants.mvp));
	else
		get_mvp_transform(push_constants.mvp);

	// Every draw carries it, because a debug view that only applied to some of
	// the scene would be worse than none.
	push_constants.renderMode = (float)r_debugView->integer;

	vkCmdPushConstants(vk.cmd->command_buffer, vk.pipeline_layout,
		VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants), &push_constants);

#ifdef USE_VK_STATS
	vk.stats.push_size += sizeof(push_constants);
#endif
}

void vk_set_2d( void ) 
{
	if ( backEnd.projection2D ) {
		return;
	}

	backEnd.projection2D = qtrue;

	// A scissor set by the client belongs to the 2D block it was set in and
	// does not survive into the next one.
	vk_clear_2d_scissor();

	vk_update_mvp(NULL);

	// force depth range and viewport/scissor updates
	vk.cmd->depth_range = DEPTH_RANGE_COUNT;

	// set 2D virtual screen size
	// set time for 2D shaders
	backEnd.refdef.time = Sys_Milliseconds2() * Cvar_VariableValue("timescale");
	backEnd.refdef.floatTime = (double)backEnd.refdef.time * 0.001; // -EC-: cast to double

	return;
}

static void vk_bind_index_attr( int index )
{
	if (bind_base == -1) {
		bind_base = index;
		bind_count = 1;
	}
	else {
		bind_count = index - bind_base + 1;
	}
}

static void vk_bind_attr( int index, unsigned int item_size, const void *src ) {
	const uint32_t offset = PAD(vk.cmd->vertex_buffer_offset, 32);
	const uint32_t size = tess.numVertexes * item_size;

	if (offset + size > vk.geometry_buffer_size) {
		// schedule geometry buffer resize
		vk.geometry_buffer_size_new = log2pad(offset + size, 1);
	}
	else {
		vk.cmd->buf_offset[index] = offset;
		Com_Memcpy(vk.cmd->vertex_buffer_ptr + offset, src, size);
		vk.cmd->vertex_buffer_offset = (VkDeviceSize)offset + size;
	}

	vk_bind_index_attr(index);
}

uint32_t vk_tess_index( uint32_t numIndexes, const void *src ) {
	const uint32_t offset = vk.cmd->vertex_buffer_offset;
	const uint32_t size = numIndexes * sizeof(tess.indexes[0]);

	if (offset + size > vk.geometry_buffer_size) {
		// schedule geometry buffer resize
		vk.geometry_buffer_size_new = log2pad(offset + size, 1);
		return ~0U;
	}
	else {
		Com_Memcpy(vk.cmd->vertex_buffer_ptr + offset, src, size);
		vk.cmd->vertex_buffer_offset = (VkDeviceSize)offset + size;
		return offset;
	}
}

void vk_bind_index_buffer( VkBuffer buffer, uint32_t offset, VkIndexType type )
{
	if ( vk.cmd->curr_index_buffer != buffer || vk.cmd->curr_index_offset != offset )
		vkCmdBindIndexBuffer( vk.cmd->command_buffer, buffer, offset, type );

	vk.cmd->curr_index_buffer = buffer;
	vk.cmd->curr_index_offset = offset;
}

#ifdef USE_VBO
void vk_draw_indexed( uint32_t indexCount, uint32_t firstIndex )
{
	vkCmdDrawIndexed( vk.cmd->command_buffer, indexCount, 1, firstIndex, 0, 0 );
}
#endif

void vk_bind_index( void )
{
#ifdef USE_VBO
	if ( tess.vbo_world_index ) {
		vk.cmd->num_indexes = 0;
		//vkCmdBindIndexBuffer( vk.cmd->command_buffer, vk.vbo.index_buffer, tess.shader->iboOffset, VK_INDEX_TYPE_UINT32 );
		return;
	}
#endif

	//vk_bind_index_ext(tess.numIndexes, tess.indexes);
}

void vk_bind_index_ext( const int numIndexes, const uint32_t *indexes )
{
	uint32_t offset	= vk_tess_index( numIndexes, indexes );

	if ( offset != ~0U ) {
		vk_bind_index_buffer( vk.cmd->vertex_buffer, offset );
		vk.cmd->num_indexes = numIndexes;
	} else {
		// overflowed
		vk.cmd->num_indexes = 0;
	}
}

static void vk_vbo_bind_geometry_mdv( int32_t flags )
{
	VBO_t *vbo = tess.vbo_model;

	shade_bufs[0] = shade_bufs[1] = shade_bufs[2] = shade_bufs[3] = shade_bufs[4] = shade_bufs[5] = shade_bufs[6] = shade_bufs[7] = shade_bufs[8] = shade_bufs[9] = shade_bufs[10] = shade_bufs[11] = vbo->buffer;
	
	Com_Memset( vk.cmd->vbo_offset, 0, sizeof(vk.cmd->vbo_offset) );

	vk.cmd->vbo_offset[0] = vbo->offsets[0];	// xyz
	vk.cmd->vbo_offset[2] = vbo->offsets[2];	// texture coords
	vk.cmd->vbo_offset[5] = vbo->offsets[5];	// normals

	if (flags & TESS_ST1)
		vk.cmd->vbo_offset[3] = vbo->offsets[2];

	if (flags & TESS_ST2)
		vk.cmd->vbo_offset[4] = vbo->offsets[2];

	if (flags & TESS_TANGENT)
		vk.cmd->vbo_offset[8] = vbo->offsets[8];

	bind_base = 0;
	bind_count = 12;	// shouldn't 9 suffice for mdv
}

static void vk_vbo_bind_geometry_ghoul2( uint32_t flags )
{
	VBO_t *vbo = tess.vbo_model;

	shade_bufs[0] = shade_bufs[1] = shade_bufs[2] = shade_bufs[3] = shade_bufs[4] = shade_bufs[5] = shade_bufs[6] = shade_bufs[7] = shade_bufs[8] = shade_bufs[9] = shade_bufs[10] = shade_bufs[11] = vbo->buffer;

	Com_Memset( vk.cmd->vbo_offset, 0, sizeof(vk.cmd->vbo_offset) );

	vk.cmd->vbo_offset[0] = vbo->offsets[0];	// xyz
	vk.cmd->vbo_offset[2] = vbo->offsets[2];	// texture coords
	vk.cmd->vbo_offset[5] = vbo->offsets[5];	// normals

	// use flag for this?
	vk.cmd->vbo_offset[10] = vbo->offsets[10];	// bones
	vk.cmd->vbo_offset[11] = vbo->offsets[11];	// weight
	
	if (flags & TESS_ST1)
		vk.cmd->vbo_offset[3] = vbo->offsets[2];

	if (flags & TESS_ST2)
		vk.cmd->vbo_offset[4] = vbo->offsets[2];

	if (flags & TESS_TANGENT)
		vk.cmd->vbo_offset[8] = vbo->offsets[8];

	bind_base = 0;
	bind_count = 12;
}

#ifdef USE_VBO_SS
static void vk_vbo_bind_geometry_surface_sprites ( uint32_t flags )
{
	VBO_t *vbo = tess.vbo_model;
	shade_bufs[0] = shade_bufs[1] = shade_bufs[2] = shade_bufs[3] = shade_bufs[4] = shade_bufs[5] = vbo->buffer;


	shade_bufs[0] = tr.ss.vbo->buffer;
	
	vk.cmd->vbo_offset[0] = 0;	// xyz
	vk.cmd->vbo_offset[1] = vbo->offsets[0];	// xyz
	vk.cmd->vbo_offset[2] = vbo->offsets[1];	// normal
	vk.cmd->vbo_offset[3] = vbo->offsets[2];	// color
	vk.cmd->vbo_offset[4] = vbo->offsets[3];	// width height
	vk.cmd->vbo_offset[5] = vbo->offsets[4];	// skew
	
	bind_count = 6;
	bind_base = 0;

	vkCmdBindVertexBuffers(vk.cmd->command_buffer, bind_base, bind_count, shade_bufs, vk.cmd->vbo_offset + bind_base);
}
#endif

void vk_bind_geometry( uint32_t flags )
{
	bind_base = -1;
	bind_count = 0;

	if ((flags & (TESS_XYZ | TESS_RGBA0 | TESS_ST0 | TESS_ST1 | TESS_ST2 | TESS_NNN | TESS_RGBA1 | TESS_RGBA2)) == 0)
		return;

#ifdef USE_VBO
	if ( tess.vbo_model ) {
		Com_Memset( vk.cmd->vbo_offset, 0, sizeof(vk.cmd->vbo_offset) );

		switch (tess.surfType) {
			case SF_MDX:		return vk_vbo_bind_geometry_ghoul2( flags );
			case SF_VBO_MDVMESH:return vk_vbo_bind_geometry_mdv( flags );
			case SF_SPRITES:	return vk_vbo_bind_geometry_surface_sprites( flags );
			default:			break;
		}
	}

	if ( tess.vbo_world_index ) {
		shade_bufs[0] = shade_bufs[1] = shade_bufs[2] = shade_bufs[3] = shade_bufs[4] = shade_bufs[5] = shade_bufs[6] = shade_bufs[7] = vk.vbo.vertex_buffer;

#ifdef USE_VK_PBR
		shade_bufs[8] = vk.vbo.vertex_buffer;
		shade_bufs[9] = vk.vbo.vertex_buffer;
#endif

		if (flags & TESS_XYZ) {  // 0
			vk.cmd->vbo_offset[0] = tess.shader->vboOffset + 0;
			vk_bind_index_attr(0);
		}

		if (flags & TESS_RGBA0) { // 1
			vk.cmd->vbo_offset[1] = tess.shader->stages[tess.vboStage]->rgb_offset[0];
			vk_bind_index_attr(1);
		}

		if (flags & TESS_ST0) {  // 2
			vk.cmd->vbo_offset[2] = tess.shader->stages[tess.vboStage]->tex_offset[0];
			vk_bind_index_attr(2);
		}

		if (flags & TESS_ST1) {  // 3
			vk.cmd->vbo_offset[3] = tess.shader->stages[tess.vboStage]->tex_offset[1];
			vk_bind_index_attr(3);
		}

		if (flags & TESS_ST2) {  // 4
			vk.cmd->vbo_offset[4] = tess.shader->stages[tess.vboStage]->tex_offset[2];
			vk_bind_index_attr(4);
		}

		if (flags & TESS_NNN) { // 5
			vk.cmd->vbo_offset[5] = tess.shader->normalOffset;
			vk_bind_index_attr(5);
		}

		if (flags & TESS_RGBA1) { // 6
			vk.cmd->vbo_offset[6] = tess.shader->stages[tess.vboStage]->rgb_offset[1];
			vk_bind_index_attr(6);
		}

		if (flags & TESS_RGBA2) { // 7
			vk.cmd->vbo_offset[7] = tess.shader->stages[tess.vboStage]->rgb_offset[2];
			vk_bind_index_attr(7);
		}

		if (flags & TESS_TANGENT) {
			vk.cmd->vbo_offset[8] = tess.shader->qtangentOffset;
			vk_bind_index_attr(8);
		}

		if (flags & TESS_LIGHTDIR) {
			vk.cmd->vbo_offset[9] = tess.shader->lightdirOffset;
			vk_bind_index_attr(9);
		}

		//vkCmdBindVertexBuffers(vk.cmd->command_buffer, bind_base, bind_count, shade_bufs, vk.cmd->vbo_offset + bind_base);
	}
	else
#endif // USE_VBO
	{
		shade_bufs[0] = shade_bufs[1] = shade_bufs[2] = shade_bufs[3] = shade_bufs[4] = shade_bufs[5] = shade_bufs[6] = shade_bufs[7] = vk.cmd->vertex_buffer;
#ifdef USE_VK_PBR
		shade_bufs[8] = vk.cmd->vertex_buffer;
		shade_bufs[9] = vk.cmd->vertex_buffer;
#endif

		if (flags & TESS_XYZ)
			vk_bind_attr(0, sizeof(tess.xyz[0]), &tess.xyz[0]);

		if (flags & TESS_RGBA0)
			vk_bind_attr(1, sizeof(color4ub_t), tess.svars.colors[0]);

		if (flags & TESS_ST0)
			vk_bind_attr(2, sizeof(vec2_t), tess.svars.texcoordPtr[0]);

		if (flags & TESS_ST1)
			vk_bind_attr(3, sizeof(vec2_t), tess.svars.texcoordPtr[1]);

		if (flags & TESS_ST2)
			vk_bind_attr(4, sizeof(vec2_t), tess.svars.texcoordPtr[2]);

		if (flags & TESS_NNN)
			vk_bind_attr(5, sizeof(tess.normal[0]), tess.normal);

		if (flags & TESS_RGBA1)
			vk_bind_attr(6, sizeof(color4ub_t), tess.svars.colors[1]);

		if (flags & TESS_RGBA2)
			vk_bind_attr(7, sizeof(color4ub_t), tess.svars.colors[2]);

		if (flags & TESS_TANGENT)
			vk_bind_attr(8, sizeof(tess.qtangent[0]), tess.qtangent);

		if (flags & TESS_LIGHTDIR)
			vk_bind_attr(9, sizeof(tess.lightdir[0]), tess.lightdir);

		//vkCmdBindVertexBuffers(vk.cmd->command_buffer, bind_base, bind_count, shade_bufs, vk.cmd->buf_offset + bind_base);
	}
}

void vk_bind_geometry_buffer( void ) {

	if ( tess.vbo_world_index ) {
		vkCmdBindVertexBuffers(vk.cmd->command_buffer, bind_base, bind_count, shade_bufs, vk.cmd->vbo_offset + bind_base);
		return;
	}

	vkCmdBindVertexBuffers(vk.cmd->command_buffer, bind_base, bind_count, shade_bufs, vk.cmd->buf_offset + bind_base);
}


void vk_bind_lighting( int stage, int bundle )
{
	bind_base = -1;
	bind_count = 0;

#ifdef USE_VBO
	if ( tess.vbo_world_index ) {
		Com_Memset( vk.cmd->vbo_offset, 0, sizeof(vk.cmd->vbo_offset) );

		shade_bufs[0] = shade_bufs[1] = shade_bufs[2] = vk.vbo.vertex_buffer;

		vk.cmd->vbo_offset[0] = tess.shader->vboOffset + 0;
		vk.cmd->vbo_offset[1] = tess.shader->stages[stage]->tex_offset[bundle];
		vk.cmd->vbo_offset[2] = tess.shader->normalOffset;

		bind_base = 0;
		bind_count = 3;
	}
	else
#endif // USE_VBO
	{
		shade_bufs[0] = shade_bufs[1] = shade_bufs[2] = vk.cmd->vertex_buffer;

		vk_bind_attr(0, sizeof(tess.xyz[0]), &tess.xyz[0]);
		vk_bind_attr(1, sizeof(vec2_t), tess.svars.texcoordPtr[bundle]);
		vk_bind_attr(2, sizeof(tess.normal[0]), tess.normal);
	}
}

static void vk_write_uniform_descriptor( VkWriteDescriptorSet *desc, VkDescriptorBufferInfo *info, 
	VkBuffer buffer, VkDescriptorSet descriptor, const uint32_t binding, const size_t size )
{
	info[binding].buffer = buffer;
	info[binding].offset = 0;
	info[binding].range = size;

	desc[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	desc[binding].dstSet = descriptor;
	desc[binding].dstBinding = binding;
	desc[binding].dstArrayElement = 0;
	desc[binding].descriptorCount = 1;
	desc[binding].pNext = NULL;
	desc[binding].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	desc[binding].pImageInfo = NULL;
	desc[binding].pBufferInfo = &info[binding];
	desc[binding].pTexelBufferView = NULL;
}

void vk_update_uniform_descriptor( VkDescriptorSet descriptor, VkBuffer buffer )
{
	VkDescriptorBufferInfo info[VK_DESC_UNIFORM_COUNT];
	VkWriteDescriptorSet desc[VK_DESC_UNIFORM_COUNT];

	vk_write_uniform_descriptor( desc, info, buffer, descriptor, VK_DESC_UNIFORM_MAIN_BINDING, sizeof(vkUniform_t) );
	vk_write_uniform_descriptor( desc, info, buffer, descriptor, VK_DESC_UNIFORM_CAMERA_BINDING, sizeof(vkUniformCamera_t) );
	vk_write_uniform_descriptor( desc, info, buffer, descriptor, VK_DESC_UNIFORM_LIGHT_BINDING, sizeof(vkUniformLight_t) );
	vk_write_uniform_descriptor( desc, info, buffer, descriptor, VK_DESC_UNIFORM_ENTITY_BINDING, sizeof(vkUniformEntity_t) );
	vk_write_uniform_descriptor( desc, info, buffer, descriptor, VK_DESC_UNIFORM_BONES_BINDING, sizeof(vkUniformBones_t) );
	vk_write_uniform_descriptor( desc, info, buffer, descriptor, VK_DESC_UNIFORM_FOGS_BINDING, sizeof(vkUniformFog_t) );
	vk_write_uniform_descriptor( desc, info, buffer, descriptor, VK_DESC_UNIFORM_GLOBAL_BINDING, sizeof(vkUniformGlobal_t) );

	vkUpdateDescriptorSets(vk.device, VK_DESC_UNIFORM_COUNT, desc, 0, NULL);
}

void vk_create_storage_buffer( vk_storage_buffer_t *out, uint32_t size, const char *name )
{
	VkMemoryRequirements memory_requirements;
	VkMemoryAllocateInfo alloc_info;
	VkBufferCreateInfo desc;
	uint32_t memory_type_bits;
	uint32_t memory_type;

	desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.queueFamilyIndexCount = 0;
	desc.pQueueFamilyIndices = NULL;
	
	Com_Memset( &memory_requirements, 0, sizeof(memory_requirements) );
	
	desc.size = size;
	desc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	{
		void *mapped = NULL;
		if ( !vk_create_buffer_memory( &desc, VK_BUFFER_MEMORY_HOST_WRITE, &out->buffer,
				&out->allocation, &mapped, name ) ) {
			Com_Error( ERR_DROP, "Vulkan: could not allocate storage buffer '%s'", name );
			return;
		}
		out->buffer_ptr = (byte*)mapped;
		Com_Memset( out->buffer_ptr, 0, size );
	}

	VK_SET_OBJECT_NAME( out->buffer, va( "%s buffer", name ), VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );
	VK_SET_OBJECT_NAME( out->descriptor, va( "%s buffer descriptor", name ), VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
}

void vk_update_attachment_descriptors( void ) {

	if ( vk.color_image_view )
	{
		VkDescriptorImageInfo info;
		VkWriteDescriptorSet desc;
		Vk_Sampler_Def sd;

		Com_Memset( &sd, 0, sizeof(sd) );
		sd.gl_mag_filter = sd.gl_min_filter = vk.blitFilter;
		sd.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sd.max_lod_1_0 = qtrue;
		sd.noAnisotropy = qtrue;

		info.sampler = vk_find_sampler( &sd );
		info.imageView = vk.color_image_view;
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		desc.dstSet = vk.color_descriptor;
		desc.dstBinding = 0;
		desc.dstArrayElement = 0;
		desc.descriptorCount = 1;
		desc.pNext = NULL;
		desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		desc.pImageInfo = &info;
		desc.pBufferInfo = NULL;
		desc.pTexelBufferView = NULL;

		vkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );
		
		// refraction
		//
		// Its own sampler, not the blit one above: this is the only attachment
		// sampled with a level of detail, and the blit sampler is pinned to the
		// top level, which would quietly make every roughness look mirror-sharp.
		if ( vk.refractionActive )
		{
			Vk_Sampler_Def rd;

			Com_Memset( &rd, 0, sizeof( rd ) );
			rd.gl_mag_filter = GL_LINEAR;
			rd.gl_min_filter = GL_LINEAR_MIPMAP_LINEAR;
			rd.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			rd.max_lod_1_0 = qfalse;
			rd.noAnisotropy = qtrue;

			info.sampler = vk_find_sampler( &rd );
			info.imageView = vk.refraction_extract_image_view;
			desc.dstSet = vk.refraction_extract_descriptor;
			vkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );

			info.sampler = vk_find_sampler( &sd );
		}

		// screenmap
		sd.gl_mag_filter = sd.gl_min_filter = GL_LINEAR;
		sd.max_lod_1_0 = qfalse;
		sd.noAnisotropy = qtrue;

		info.sampler = vk_find_sampler( &sd );

		info.imageView = vk.screenMap.color_image_view;
		desc.dstSet = vk.screenMap.color_descriptor;

		vkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );

		// bloom images
		if ( vk.bloomActive )
		{
			uint32_t i;
			for (i = 0; i < ARRAY_LEN( vk.bloom_image_descriptor ); i++)
			{
				info.imageView = vk.bloom_image_view[i];
				desc.dstSet = vk.bloom_image_descriptor[i];

				vkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );
			}
		}

		// dglow images
		if ( vk.dglowActive )
		{
			uint32_t i;
			for ( i = 0; i < ARRAY_LEN( vk.dglow_image_descriptor ); i++ )
			{
				info.imageView = vk.dglow_image_view[i];
				desc.dstSet = vk.dglow_image_descriptor[i];

				vkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );
			}
		}

#ifdef VK_PBR_BRDFLUT
		if( vk.cubemapActive )
		{
			// brdf
			info.imageView = vk.brdflut_image_view;
			desc.dstSet = vk.brdflut_image_descriptor;

			vkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );

			// The contents as well as the set: the PBR path pushes this one
			// into the command buffer rather than binding it.
			vk.brdflut_descriptor_info = info;

			// cubemap
			info.imageView = vk.cubeMap.color_image_view[0];
			desc.dstSet = vk.cubeMap.color_descriptor;
			vkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );	
		}
#endif
	}
}

void vk_init_descriptors( void ) {
	VkDescriptorSetAllocateInfo alloc;
	VkDescriptorBufferInfo info;
	VkWriteDescriptorSet desc;
	uint32_t i;

	alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc.pNext = NULL;
	alloc.descriptorPool = vk.descriptor_pool;
	alloc.descriptorSetCount = 1;
	alloc.pSetLayouts = &vk.set_layout_storage;
	VK_CHECK( vkAllocateDescriptorSets( vk.device, &alloc, &vk.storage.descriptor ) );

	info.buffer = vk.storage.buffer;
	info.offset = 0;
	info.range = sizeof(uint32_t);

	desc.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	desc.dstSet = vk.storage.descriptor;
	desc.dstBinding = 0;
	desc.dstArrayElement = 0;
	desc.descriptorCount = 1;
	desc.pNext = NULL;
	desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
	desc.pImageInfo = NULL;
	desc.pBufferInfo = &info;
	desc.pTexelBufferView = NULL;

	vkUpdateDescriptorSets( vk.device, 1, &desc, 0, NULL );

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
	{
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.pNext = NULL;
		alloc.descriptorPool = vk.descriptor_pool;
		alloc.descriptorSetCount = 1;
		alloc.pSetLayouts = &vk.set_layout_uniform;
		VK_CHECK( vkAllocateDescriptorSets( vk.device, &alloc, &vk.tess[i].uniform_descriptor ) );

		vk_update_uniform_descriptor( vk.tess[i].uniform_descriptor, vk.tess[i].vertex_buffer );
		VK_SET_OBJECT_NAME( vk.tess[i].uniform_descriptor, "uniform descriptor", VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT );
	}

	if ( vk.color_image_view )
	{
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.pNext = NULL;
		alloc.descriptorPool = vk.descriptor_pool;
		alloc.descriptorSetCount = 1;
		alloc.pSetLayouts = &vk.set_layout_sampler;
		VK_CHECK( vkAllocateDescriptorSets( vk.device, &alloc, &vk.color_descriptor ) );
		
		// refraction
		if ( vk.refractionActive )
			VK_CHECK( vkAllocateDescriptorSets( vk.device, &alloc, &vk.refraction_extract_descriptor ) );

		// bloom images
		if ( vk.bloomActive ) {
			for ( i = 0; i < ARRAY_LEN( vk.bloom_image_descriptor ); i++ )
				VK_CHECK( vkAllocateDescriptorSets( vk.device, &alloc, &vk.bloom_image_descriptor[i] ) );
		}

		// dglow images - ALLOCATED WHETHER OR NOT IT IS ON.
		//
		// This function runs once, from vk_initialize. vk_restart_swapchain does
		// not call it, which is correct - a descriptor set outlives the
		// attachments it points at and only needs rewriting. But it means the
		// answer to "is dynamic glow on" was frozen at start-up: turning it on
		// afterwards left vk.dglow_image_descriptor[] full of VK_NULL_HANDLE,
		// and vk_update_attachment_descriptors duly wrote to a null set. That is
		// a segfault inside the validation layer, and it is what the dglow lane
		// found on its first run.
		//
		// A descriptor set is a handful of bytes out of a pool that is already
		// sized for the worst case - VK_NUM_BLUR_PASSES * 4 combined image
		// samplers, allocated regardless of which effects are active. Paying for
		// them always is cheaper than the alternative, which is a set that can
		// only be created from a path that has already run.
		//
		// Bloom, refraction and the cubemap are allocated conditionally just
		// above and below, and have the same shape: the day one of them gets a
		// rung of its own, it needs this same line.
		for ( i = 0; i < ARRAY_LEN( vk.dglow_image_descriptor ); i++ )
			VK_CHECK( vkAllocateDescriptorSets( vk.device, &alloc, &vk.dglow_image_descriptor[i] ) );

		alloc.descriptorSetCount = 1;
		VK_CHECK( vkAllocateDescriptorSets( vk.device, &alloc, &vk.screenMap.color_descriptor ) ); // screenmap

#ifdef VK_PBR_BRDFLUT
		if( vk.cubemapActive )
			VK_CHECK( vkAllocateDescriptorSets( vk.device, &alloc, &vk.brdflut_image_descriptor ) );
#endif

		// cubemap
		VK_CHECK( vkAllocateDescriptorSets( vk.device, &alloc, &vk.cubeMap.color_descriptor ) );

		vk_update_attachment_descriptors();
	}
}

// One VMA allocation per command buffer, mapped for the lifetime of the buffer.
// Upstream created all NUM_COMMAND_BUFFERS buffers, took the requirements of the
// last one, allocated a single block for all of them and bound them at manual
// offsets - which is only correct while every buffer has identical requirements.
void vk_create_vertex_buffer( VkDeviceSize size )
{
	VkBufferCreateInfo desc;
	int i;

	vk_debug("Create geometry buffer: vk.cmd->vertex_buffer \n");

	desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.queueFamilyIndexCount = 0;
	desc.pQueueFamilyIndices = NULL;

	for (i = 0; i < NUM_COMMAND_BUFFERS; i++) {
		void *data = NULL;

		desc.size = size;
		desc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

		if ( !vk_create_buffer_memory( &desc, VK_BUFFER_MEMORY_HOST_WRITE, &vk.tess[i].vertex_buffer,
				&vk.tess[i].vertex_buffer_allocation, &data, "geometry buffer" ) ) {
			Com_Error( ERR_DROP, "Vulkan: could not allocate a %i KiB geometry buffer", (int)( size / 1024 ) );
			return;
		}

		vk.tess[i].vertex_buffer_ptr = (byte*)data;
		vk.tess[i].vertex_buffer_offset = 0;
	}

	vk.geometry_buffer_size = size;
	vk.geometry_buffer_size_new = 0;

	Com_Memset(&vk.stats, 0, sizeof(vk.stats));
}

void vk_create_indirect_buffer( VkDeviceSize size )
{
	VkBufferCreateInfo desc;
	int i;

	vk_debug("Create indirect buffer: vk.cmd->indirect_buffer \n");

	desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	desc.pNext = NULL;
	desc.flags = 0;
	desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	desc.queueFamilyIndexCount = 0;
	desc.pQueueFamilyIndices = NULL;

	for (i = 0; i < NUM_COMMAND_BUFFERS; i++) {
		void *data = NULL;

		desc.size = size;
		desc.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

		if ( !vk_create_buffer_memory( &desc, VK_BUFFER_MEMORY_HOST_WRITE, &vk.tess[i].indirect_buffer,
				&vk.tess[i].indirect_buffer_allocation, &data, "indirect buffer" ) ) {
			Com_Error( ERR_DROP, "Vulkan: could not allocate a %i KiB indirect buffer", (int)( size / 1024 ) );
			return;
		}

		vk.tess[i].indirect_buffer_ptr = (byte*)data;
		vk.tess[i].indirect_buffer_offset = 0;
	}

	vk.indirect_buffer_size = size;
	vk.indirect_buffer_size_new = 0;
}

void vk_reset_descriptor( int index )
{
	vk.cmd->descriptor_set.current[index] = VK_NULL_HANDLE;
}

void vk_update_descriptor( int tmu, VkDescriptorSet curDesSet )
{
	// A set the pipeline layout does not have cannot be bound. On a device that
	// reports the Vulkan minimum of eight bound sets the layout has four, and
	// the PBR descriptors above that were being handed to vkCmdBindDescriptorSets
	// anyway - twenty-one spec violations per frame, invisible on a desktop GPU
	// with a limit of 32 and found by running headless under the validation
	// layers. Nothing reads them in that configuration either: the shaders that
	// would are not the ones built when the sets are missing.
	if ( tmu >= (int)vk.descriptorSetCount ) {
		return;
	}

	if (vk.cmd->descriptor_set.current[tmu] != curDesSet) {
		vk.cmd->descriptor_set.start = 
			(tmu < vk.cmd->descriptor_set.start) ? tmu : vk.cmd->descriptor_set.start;
		vk.cmd->descriptor_set.end = 
			(tmu > vk.cmd->descriptor_set.end) ? tmu : vk.cmd->descriptor_set.end;
	}

	vk.cmd->descriptor_set.current[tmu] = curDesSet;
}

#ifdef USE_VK_PBR
// One of the five physically-based textures for the next draw.
//
// The five used to be five descriptor SETS, one image apiece, which is what put
// the layout at ten sets and switched the whole path off on every device that
// reports the common limit of eight - including the software rasteriser this
// project runs headless, so nothing in this branch had ever been through the
// validation layer. They are one set with five bindings now, pushed into the
// command buffer at draw time.
//
// Pushed rather than allocated because no set could be cached anyway: four of
// the five belong to the material and the fifth, the cubemap, changes per
// surface.
void vk_update_pbr_descriptor( int binding, const image_t *image )
{
	if ( !vk.pushDescriptorAvailable || vk.useFastLight ) {
		return;
	}
	if ( binding < 0 || binding >= VK_DESC_PBR_BINDING_COUNT || image == NULL ) {
		return;
	}

	// The image, and nothing copied out of it. An image_t outlives its
	// VkImageView - the view is remade whenever the texture is reloaded and
	// again on every vid_restart - so anything cached here goes stale without
	// saying so, and a stale VkImageView is indistinguishable from a good one
	// until a driver reads it. Everything the push needs is read from the image
	// at push time.
	vk.cmd->pbr_source[binding] = image;
	vk.cmd->pbr_raw_set[binding] = qfalse;
	vk.cmd->pbr_dirty = qtrue;
}

// The BRDF lookup table is not an image_t - it is created with the attachments
// and has no entry in the texture list - so it arrives as a descriptor and has
// no view to read back. It is also created once per renderer life and never
// reloaded, which is the property the image case does not have.
void vk_update_pbr_descriptor_raw( int binding, const VkDescriptorImageInfo *info )
{
	if ( !vk.pushDescriptorAvailable || vk.useFastLight ) {
		return;
	}
	if ( binding < 0 || binding >= VK_DESC_PBR_BINDING_COUNT ) {
		return;
	}

	vk.cmd->pbr_raw[binding] = *info;
	vk.cmd->pbr_raw_set[binding] = qtrue;
	vk.cmd->pbr_source[binding] = NULL;
	vk.cmd->pbr_dirty = qtrue;
}
#endif

void vk_update_descriptor_offset( int index, uint32_t offset )
{
	vk.cmd->descriptor_set.offset[index] = offset;
}

void vk_bind_descriptor_sets( void ) 
{
	uint32_t offsets[VK_DESC_UNIFORM_COUNT], offset_count;
	uint32_t start, end, count, i;

	start = vk.cmd->descriptor_set.start;
	if (start == ~0U)
		return;

	end = vk.cmd->descriptor_set.end;

	offset_count = 0;
	if ( /*start == VK_DESC_STORAGE ||*/ start == VK_DESC_UNIFORM ) { // uniform offset or storage offset
		offsets[offset_count++] = vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_MAIN_BINDING];
		offsets[offset_count++] = vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_CAMERA_BINDING];
		offsets[offset_count++] = vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_LIGHT_BINDING];	
		offsets[offset_count++] = vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_ENTITY_BINDING];
		offsets[offset_count++] = vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_BONES_BINDING];	
		offsets[offset_count++] = vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_FOGS_BINDING];	
		offsets[offset_count++] = vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_GLOBAL_BINDING];
	}

	count = end - start + 1;

	// fill NULL descriptor gaps
	for ( i = start + 1; i < end; i++ ) {
		if ( vk.cmd->descriptor_set.current[i] == VK_NULL_HANDLE ) {
			vk.cmd->descriptor_set.current[i] = tr.whiteImage->descriptor_set;
		}
	}

	vkCmdBindDescriptorSets(vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		vk.pipeline_layout, start, count, vk.cmd->descriptor_set.current + start, offset_count, offsets);

	vk.cmd->descriptor_set.end = 0;
	vk.cmd->descriptor_set.start = ~0U;

#ifdef USE_VK_PBR
	// And the physically-based set, which is not bound but written straight
	// into the command buffer.
	if ( vk.cmd->pbr_dirty ) {
		vk_push_pbr_descriptor( vk.pipeline_layout,
			vk.cmd->pbr_source, vk.cmd->pbr_raw, vk.cmd->pbr_raw_set );

		vk.cmd->pbr_dirty = qfalse;
	}
#endif
}

#ifdef USE_VK_PBR
// The physically-based set, written straight into the command buffer.
//
// It takes its five images as arguments rather than reading vk.cmd, and that is
// the whole point of it existing separately: there are TWO paths to a draw in
// this back end and only one of them was pushing this set.
//
// vk_bind_descriptor_sets is the immediate path. The other is DrawItem -
// RB_AddDrawItemUniformBinding records the bound sets into the item and
// RB_BindDescriptorSets binds them again later - and it recorded sets zero to
// four, because those are the ones vk.cmd->descriptor_set holds. A pushed set
// is not in there. So every Ghoul2 mesh drawn through the item path ran a
// pipeline that statically uses set five with nothing bound to set five.
//
// What that costs: on lavapipe, a segmentation fault in a rasteriser worker
// thread, with the main thread sitting in vk_present_frame - which is the same
// signature the vid_restart crash has. The validation layer names it exactly:
//
//   VUID-vkCmdDrawIndexedIndirect-None-08600: the VkPipeline statically uses
//   descriptor set (index #5) which is not compatible with the currently bound
//   descriptor set's pipeline layout
//
// It needed real assets to show. The bench's own model has no normal map, so
// the shader permutation that reads set five was never generated here until a
// model with normalMap and rmoMap was put in front of it.
//
// Every binding is filled every time: a push descriptor set has no state
// between draws, so a binding left out is a binding the shader reads and
// nothing wrote.
void vk_push_pbr_descriptor( VkPipelineLayout layout,
	const image_t * const *sources, const VkDescriptorImageInfo *raw,
	const qboolean *raw_set )
{
	VkWriteDescriptorSet	writes[VK_DESC_PBR_BINDING_COUNT];
	VkDescriptorImageInfo	images[VK_DESC_PBR_BINDING_COUNT];
	uint32_t				i;

	if ( !vk.pushDescriptorAvailable || vk.useFastLight ) {
		return;
	}

	// Only the layout that has the set. The surface-sprite layout is a
	// different one and pushing set five into it is a spec violation of its
	// own.
	if ( layout != vk.pipeline_layout ) {
		return;
	}

	{
		for ( i = 0; i < VK_DESC_PBR_BINDING_COUNT; i++ ) {
			// Every binding, built here, from something that is current.
			//
			// A pushed set holds no state between draws, so "the caller only
			// sets the ones its material has" - which is what the five-set
			// arrangement allowed, because a set left alone kept whatever was
			// bound before - becomes an image info full of zeroes and a null
			// handle handed to the driver. The validation layer said so nine
			// hundred and twenty-four times on the first run.
			//
			// The obvious repair was to carry the last image info per binding
			// and fill in only the ones that were never set. That is wrong in a
			// way that took a segmentation fault to see: "never set" was being
			// asked as imageView == VK_NULL_HANDLE, and a view destroyed by a
			// vid_restart is not null, it is a handle to something that is
			// gone. The first screen wipe after a restart pushed one and the
			// validation layer died reading it.
			//
			// So nothing is carried. A binding is an image whose view is read
			// now, or the raw descriptor for the BRDF table, or the fallback -
			// white for the flat maps, the empty cubemap for the cube one,
			// which is what the call sites use for their own "not present".
			// A SAMPLER as well as a view, on every branch.
			//
			// A combined image sampler needs both, and only the view was ever
			// checked. An image_t carries the sampler it ended up with in
			// descriptor_info, filled by vk_update_descriptor_set - and an
			// image that has not been through that function yet, or a cubemap
			// created with no data, has a view and a null sampler. Pushing one
			// is VUID-VkWriteDescriptorSet-descriptorType-02996 and, on
			// lavapipe, a segmentation fault a few draws later.
			//
			// Nothing was ever checking it because nothing ever pushed this set
			// outside the immediate path, where the images always came from a
			// material that had finished loading.
			const image_t *fallback = ( i == VK_DESC_PBR_CUBEMAP_BINDING )
				? tr.emptyCubemap : tr.whiteImage;

			if ( sources[i] != NULL && sources[i]->view != VK_NULL_HANDLE
				&& sources[i]->descriptor_info.sampler != VK_NULL_HANDLE ) {
				images[i] = sources[i]->descriptor_info;
				images[i].imageView = sources[i]->view;
			} else if ( raw_set[i] && raw[i].imageView != VK_NULL_HANDLE
				&& raw[i].sampler != VK_NULL_HANDLE ) {
				images[i] = raw[i];
			} else if ( fallback != NULL && fallback->view != VK_NULL_HANDLE
				&& fallback->descriptor_info.sampler != VK_NULL_HANDLE ) {
				images[i] = fallback->descriptor_info;
				images[i].imageView = fallback->view;
			} else {
				// Nothing usable for this binding, so push nothing at all. A
				// set with a hole in it is worse than no push: the driver reads
				// the hole. The draw that wanted it will be wrong, and it will
				// be wrong visibly rather than fatally.
				return;
			}

			writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[i].pNext = NULL;
			writes[i].dstSet = VK_NULL_HANDLE;	// ignored for a pushed set
			writes[i].dstBinding = i;
			writes[i].dstArrayElement = 0;
			writes[i].descriptorCount = 1;
			writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[i].pImageInfo = &images[i];
			writes[i].pBufferInfo = NULL;
			writes[i].pTexelBufferView = NULL;
		}

		vkCmdPushDescriptorSetKHR( vk.cmd->command_buffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
			VK_DESC_PBR, VK_DESC_PBR_BINDING_COUNT, writes );
	}
}
#endif

void vk_bind_pipeline( uint32_t pipeline ) {
	VkPipeline vkpipe;

	vkpipe = vk_gen_pipeline(pipeline);

	// A pipeline that is not there is not a pipeline to bind. vk_gen_pipeline
	// answers VK_NULL_HANDLE for an index past the end of the table and for a
	// pipeline whose creation did not produce one, and vkCmdBindPipeline with
	// a null handle is undefined behaviour that the NVIDIA driver takes
	// literally: it dereferences it and the process is gone, inside the driver,
	// with a stack that says nothing about which surface asked for it.
	//
	// Found on hardware, twice in one session and from two directions:
	// r_depthPrepass 1 crashed on the first frame of a map, and so did
	// vid_restart. Both stacks were identical - vk_bind_pipeline, then
	// nvoglv64 reading address zero.
	//
	// Skipping the draw is not a fix for whatever produced no pipeline. It is
	// the difference between a missing surface with a line in the console and
	// a crash to the desktop, and it makes the thing that IS wrong findable:
	// the message names the index, which is what a bisect needs.
	//
	// And skipping the BIND is not skipping the draw, which is what the first
	// version of this did. The caller goes on and issues its vkCmdDrawIndexed
	// with whatever pipeline was bound last, or with none at all - which is the
	// same undefined behaviour one step further along, and the validation layer
	// says so: VUID-vkCmdDrawIndexed-None-08606, a draw with no graphics
	// pipeline bound. The flag below is what the draw reads.
	if ( vkpipe == VK_NULL_HANDLE ) {
		static uint32_t	complainedAbout = ~0U;

		if ( complainedAbout != pipeline ) {
			complainedAbout = pipeline;
			Com_Printf( S_COLOR_YELLOW "vk_bind_pipeline: pipeline %u has no handle "
				"for render pass %d - the surface using it will not be drawn\n",
				pipeline, (int)vk.renderPassIndex );
		}
		vk.cmd->pipeline_missing = qtrue;
		return;
	}

	vk.cmd->pipeline_missing = qfalse;

	if (vkpipe != vk.cmd->last_pipeline) {
		vkCmdBindPipeline(vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkpipe);
		vk.cmd->last_pipeline = vkpipe;
	}

	vk_world.dirty_depth_attachment |= (vk.pipelines[pipeline].def.state_bits & GLS_DEPTHMASK_TRUE);
}

static void vk_update_depth_range( Vk_Depth_Range depth_range )
{
	if ( vk.cmd->depth_range == depth_range )
		return;

	// configure pipeline's dynamic state
	VkViewport viewport;
	VkRect2D scissor_rect;

	vk.cmd->depth_range = depth_range;

	get_scissor_rect( &scissor_rect );

	if ( memcmp( &vk.cmd->scissor_rect, &scissor_rect, sizeof( scissor_rect ) ) != 0 ) {
		vkCmdSetScissor( vk.cmd->command_buffer, 0, 1, &scissor_rect );
		vk.cmd->scissor_rect = scissor_rect;
	}

	get_viewport( &viewport, depth_range);
	vkCmdSetViewport( vk.cmd->command_buffer, 0, 1, &viewport );
}

void vk_draw_geometry( Vk_Depth_Range depth_range, qboolean indexed )
{
	// geometry buffer overflow happened this frame
	if ( vk.geometry_buffer_size_new )
		return;

	vk_bind_descriptor_sets();

	// configure pipeline's dynamic state
	vk_update_depth_range( depth_range );

	if ( tess.shader->polygonOffset ) {
		vkCmdSetDepthBias( vk.cmd->command_buffer, r_offsetUnits->value, 0.0f, r_offsetFactor->value );
	}

	// issue draw call(s)
#ifdef USE_VBO
	if ( tess.vbo_world_index )
		VBO_RenderIBOItems();
	else
#endif
	{
		if (indexed)
			vkCmdDrawIndexed( vk.cmd->command_buffer, vk.cmd->num_indexes, 1, 0, 0, 0 );
		else
			vkCmdDraw( vk.cmd->command_buffer, tess.numVertexes, 1, 0, 0 );
	}
}

void vk_draw_dot( uint32_t storage_offset )
{
	// geometry buffer overflow happened this frame
	if ( vk.geometry_buffer_size_new )
		return;

	vkCmdBindDescriptorSets( vk.cmd->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout_storage, VK_DESC_STORAGE, 1, &vk.storage.descriptor, 1, &storage_offset );

	// configure pipeline's dynamic state
	vk_update_depth_range( DEPTH_RANGE_NORMAL );

	vkCmdDraw( vk.cmd->command_buffer, tess.numVertexes, 1, 0, 0 );
}

void ComputeColors( const int b, color4ub_t *dest, const shaderStage_t *pStage, int forceRGBGen )
{
	int			i;
	qboolean killGen = qfalse;
	alphaGen_t forceAlphaGen = pStage->bundle[b].alphaGen;//set this up so we can override below

	if (!tess.numVertexes)
		return;

#ifdef RF_ALPHA_FADE
	// The other half of RF_ALPHA_FADE: the blend override in vk_tess_end is
	// useless unless the alpha it blends with comes from the entity rather than
	// from whatever the shader computes.
	if ( backEnd.currentEntity != NULL &&
		( backEnd.currentEntity->e.renderfx & RF_ALPHA_FADE ) &&
		backEnd.currentEntity->e.shaderRGBA[3] < 255 )
	{
		forceAlphaGen = AGEN_ENTITY;
	}
#endif

	if (tess.shader != tr.projectionShadowShader && tess.shader != tr.shadowShader &&
		(backEnd.currentEntity->e.renderfx & (RF_DISINTEGRATE1 | RF_DISINTEGRATE2)))
	{
		RB_CalcDisintegrateColors( (unsigned char*) dest );
		RB_CalcDisintegrateVertDeform();

		// We've done some custom alpha and color stuff, so we can skip the rest.  Let it do fog though
		killGen = qtrue;
	}

	if ( pStage->bundle[0].rgbGen == CGEN_LIGHTMAPSTYLE )
		forceRGBGen = CGEN_LIGHTMAPSTYLE;

	//
	// rgbGen
	//
	if (!forceRGBGen)
	{
		forceRGBGen = pStage->bundle[b].rgbGen;
	}

	// does not work for rotated models, technically, this should also be a CGEN type.
	// But that would entail adding new shader commands....which is too much work for one thing
	if (backEnd.currentEntity->e.renderfx & RF_VOLUMETRIC) 
	{
		int			i;
		float* normal, dot;
		unsigned char* color;
		int			numVertexes;

		normal = tess.normal[0];
		color = tess.svars.colors[0][0];

		numVertexes = tess.numVertexes;

		for (i = 0; i < numVertexes; i++, normal += 4, color += 4)
		{
			dot = DotProduct(normal, backEnd.refdef.viewaxis[0]);

			dot *= dot * dot * dot;

			if (dot < 0.2f) // so low, so just clamp it
			{
				dot = 0.0f;
			}

			color[0] = color[1] = color[2] = color[3] = Q_ftol(backEnd.currentEntity->e.shaderRGBA[0] * (1 - dot));
		}

		killGen = qtrue;
	}

	if (killGen)
	{
		goto avoidGen;
	}

	//
	// rgbGen
	//
	switch (forceRGBGen)
	{
	case CGEN_IDENTITY:
		Com_Memset(dest, 0xff, tess.numVertexes * 4);
		break;
	default:
	case CGEN_IDENTITY_LIGHTING:
		Com_Memset(dest, tr.identityLightByte, tess.numVertexes * 4);
		break;
	case CGEN_LIGHTING_DIFFUSE:
		RB_CalcDiffuseColor( (unsigned char*) dest );
		break;
	case CGEN_LIGHTING_DIFFUSE_ENTITY:
		RB_CalcDiffuseEntityColor( (unsigned char*) dest );
		if (forceAlphaGen == AGEN_IDENTITY &&
			backEnd.currentEntity->e.shaderRGBA[3] == 0xff
			)
		{
			forceAlphaGen = AGEN_SKIP;	//already got it in this set since it does all 4 components
		}
		break;
	case CGEN_EXACT_VERTEX:
		Com_Memcpy(dest, tess.vertexColors, tess.numVertexes * sizeof(tess.vertexColors[0]));
		break;
	case CGEN_CONST:
		for (i = 0; i < tess.numVertexes; i++) {
			*(int *)dest[i] = *(int *)pStage->bundle[b].constantColor;
		}
		break;
	case CGEN_VERTEX:
		if (tr.identityLight == 1)
		{
			Com_Memcpy(dest, tess.vertexColors, tess.numVertexes * sizeof(tess.vertexColors[0]));
		}
		else
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				dest[i][0] = tess.vertexColors[i][0] * tr.identityLight;
				dest[i][1] = tess.vertexColors[i][1] * tr.identityLight;
				dest[i][2] = tess.vertexColors[i][2] * tr.identityLight;
				dest[i][3] = tess.vertexColors[i][3];
			}
		}
		break;
	case CGEN_ONE_MINUS_VERTEX:
		if (tr.identityLight == 1)
		{
			for (i = 0; i < tess.numVertexes; i++)
			{
				dest[i][0] = 255 - tess.vertexColors[i][0];
				dest[i][1] = 255 - tess.vertexColors[i][1];
				dest[i][2] = 255 - tess.vertexColors[i][2];
			}
		}
		else
		{
			for (i = 0; i < tess.numVertexes; i++)
			{
				dest[i][0] = (255 - tess.vertexColors[i][0]) * tr.identityLight;
				dest[i][1] = (255 - tess.vertexColors[i][1]) * tr.identityLight;
				dest[i][2] = (255 - tess.vertexColors[i][2]) * tr.identityLight;
			}
		}
		break;
	case CGEN_FOG:
	{
		const fog_t *fog;

		fog = tr.world->fogs + tess.fogNum;

		for (i = 0; i < tess.numVertexes; i++) {
			*(int *)&dest[i] = fog->colorInt;
		}
	}
	break;
	case CGEN_WAVEFORM:
		RB_CalcWaveColor(&pStage->bundle[b].rgbWave, (unsigned char*) dest);
		break;
	case CGEN_ENTITY:
		RB_CalcColorFromEntity( (unsigned char*) dest );
		if (forceAlphaGen == AGEN_IDENTITY && backEnd.currentEntity->e.shaderRGBA[3] == 0xff)
		{
			forceAlphaGen = AGEN_SKIP;	//already got it in this set since it does all 4 components
		}
		break;
	case CGEN_ONE_MINUS_ENTITY:
		RB_CalcColorFromOneMinusEntity( (unsigned char*) dest );
		break;
	case CGEN_LIGHTMAPSTYLE:
		for (i = 0; i < tess.numVertexes; i++)
		{
			*(int *)dest[i] = *(int *)styleColors[pStage->lightmapStyle[b%2]]; 
		}
		break;
	}

	//
	// alphaGen
	//
	switch ( forceAlphaGen )
	{
	case AGEN_SKIP:
		break;
	case AGEN_IDENTITY:
		if (forceRGBGen != CGEN_IDENTITY) {
			if ((forceRGBGen == CGEN_VERTEX && tr.identityLight != 1) ||
				forceRGBGen != CGEN_VERTEX) {
				for (i = 0; i < tess.numVertexes; i++) {
					dest[i][3] = 0xff;
				}
			}
		}
		break;
	case AGEN_CONST:
		if (forceRGBGen != CGEN_CONST) {
			for (i = 0; i < tess.numVertexes; i++) {
				dest[i][3] = pStage->bundle[b].constantColor[3];
			}
		}
		break;
	case AGEN_WAVEFORM:
		RB_CalcWaveAlpha(&pStage->bundle[b].alphaWave, (unsigned char*) dest );
		break;
	case AGEN_LIGHTING_SPECULAR:
		RB_CalcSpecularAlpha( (unsigned char*) dest );
		break;
	case AGEN_ENTITY:
		RB_CalcAlphaFromEntity( (unsigned char*) dest );
		break;
	case AGEN_ONE_MINUS_ENTITY:
		RB_CalcAlphaFromOneMinusEntity( (unsigned char*) dest );
		break;
	case AGEN_VERTEX:
		if (forceRGBGen != CGEN_VERTEX) {
			for (i = 0; i < tess.numVertexes; i++) {
				dest[i][3] = tess.vertexColors[i][3];
			}
		}
		break;
	case AGEN_ONE_MINUS_VERTEX:
		for (i = 0; i < tess.numVertexes; i++)
		{
			dest[i][3] = 255 - tess.vertexColors[i][3];
		}
		break;
	case AGEN_PORTAL:
	{
		for (i = 0; i < tess.numVertexes; i++)
		{
			unsigned char alpha;
			float len;
			vec3_t v;

			VectorSubtract(tess.xyz[i], backEnd.viewParms.ori.origin, v);
			len = VectorLength( v ) * tess.shader->portalRangeR;

			if ( len > 1 )
			{
				alpha = 0xff;
			}
			else
			{
				alpha = len * 0xff;
			}

			dest[i][3] = alpha;
		}
	}
	break;
	case AGEN_BLEND:
		if (forceRGBGen != CGEN_VERTEX)
		{
			for (i = 0; i < tess.numVertexes; i++)
			{
				dest[i][3] = tess.vertexAlphas[i][pStage->index]; //rwwRMG - added support
			}
		}
		break;
	default:
		break;
	}
avoidGen:
	//
	// fog adjustment for colors to fade out as fog increases
	//
	if (tess.fogNum)
	{
		switch (pStage->bundle[b].adjustColorsForFog)
		{
		case ACFF_MODULATE_RGB:
			RB_CalcModulateColorsByFog( (unsigned char*) dest );
			break;
		case ACFF_MODULATE_ALPHA:
			RB_CalcModulateAlphasByFog( (unsigned char*) dest );
			break;
		case ACFF_MODULATE_RGBA:
			RB_CalcModulateRGBAsByFog( (unsigned char*) dest );
			break;
		case ACFF_NONE:
			break;
		}
	}
}

void *vk_reserve_uniform( size_t size, uint32_t *offset ) {
	*offset = PAD(vk.cmd->vertex_buffer_offset, (VkDeviceSize)vk.uniform_alignment);

	if (*offset + size > vk.geometry_buffer_size)
		return NULL;

	vk.cmd->vertex_buffer_offset = *offset + size;

	return (void *)(vk.cmd->vertex_buffer_ptr + *offset);
}

uint32_t vk_append_uniform( const void *uniform, size_t size, uint32_t min_offset ) {
	const uint32_t offset = PAD(vk.cmd->vertex_buffer_offset, (VkDeviceSize)vk.uniform_alignment);

	if ( offset + min_offset > vk.geometry_buffer_size )
		return ~0U;

	Com_Memcpy( vk.cmd->vertex_buffer_ptr + offset, uniform, size );
	vk.cmd->vertex_buffer_offset = offset + min_offset;

	return offset;
}

static uint32_t vk_push_uniform( const vkUniform_t *uniform ) 
{
	const uint32_t offset = vk_append_uniform( uniform, sizeof(*uniform), (VkDeviceSize)vk.uniform_item_size );

	vk_reset_descriptor( VK_DESC_UNIFORM );
	vk_update_descriptor( VK_DESC_UNIFORM, vk.cmd->uniform_descriptor );
	vk_update_descriptor_offset( VK_DESC_UNIFORM_MAIN_BINDING, offset );

	return offset;
}

static uint32_t vk_push_uniform_global( const vkUniformGlobal_t *uniform ) {	
	const uint32_t offset = PAD(vk.cmd->vertex_buffer_offset, (VkDeviceSize)vk.uniform_alignment);

	if ( offset + vk.uniform_global_item_size > vk.geometry_buffer_size )
		return ~0U;

	Com_Memcpy( vk.cmd->vertex_buffer_ptr + offset, uniform, sizeof(*uniform) );
	vk.cmd->vertex_buffer_offset = offset + vk.uniform_global_item_size;

	//vk_reset_descriptor( VK_DESC_UNIFORM );
	//vk_update_descriptor( VK_DESC_UNIFORM, vk.cmd->uniform_descriptor );
	//vk_update_descriptor_offset( VK_DESC_UNIFORM, 0 );
	vk_update_descriptor_offset( VK_DESC_UNIFORM_GLOBAL_BINDING, offset );

	return 0;
}

VkDrawIndexedIndirectCommand *vk_reserve_draw_indexed_indirect( uint32_t count, uint32_t *offset )
{
	uint32_t size = count * sizeof(VkDrawIndexedIndirectCommand);

	*offset = vk.cmd->indirect_buffer_offset;

	if (*offset + size > vk.indirect_buffer_size)
		return NULL;

	vk.cmd->indirect_buffer_offset += size;

	return (VkDrawIndexedIndirectCommand *)(vk.cmd->indirect_buffer_ptr + *offset);
}

#if 0
uint32_t vk_push_indirect( int count, const void *data ) 
{
	const uint32_t offset = vk.cmd->indirect_buffer_offset;	// no alignment for indirect buffer?
	const uint32_t size = count * sizeof(VkDrawIndexedIndirectCommand);

	if (offset + size > vk.indirect_buffer_size) {
		// schedule geometry buffer resize
		vk.indirect_buffer_size_new = log2pad(offset + size, 1);
		Com_Printf("resize"); //hmmm
	}
	else {
		Com_Memcpy(vk.cmd->indirect_buffer_ptr + offset, data, size);
		vk.cmd->indirect_buffer_offset = (VkDeviceSize)offset + size;
	}

	return offset;
}
#endif

/*
========================
RB_CalcFogProgramParms
========================
*/
const fogProgramParms_t *RB_CalcFogProgramParms( void )
{
	static fogProgramParms_t parm;
	const fog_t* fog;
	vec3_t		local;

	Com_Memset(parm.fogDepthVector, 0, sizeof(parm.fogDepthVector));

	fog = tr.world->fogs + tess.fogNum;

	// all fogging distance is based on world Z units
	VectorSubtract(backEnd.ori.origin, backEnd.viewParms.ori.origin, local);
	parm.fogDistanceVector[0] = -backEnd.ori.modelViewMatrix[2];
	parm.fogDistanceVector[1] = -backEnd.ori.modelViewMatrix[6];
	parm.fogDistanceVector[2] = -backEnd.ori.modelViewMatrix[10];
	parm.fogDistanceVector[3] = DotProduct(local, backEnd.viewParms.ori.axis[0]);

	// scale the fog vectors based on the fog's thickness
	parm.fogDistanceVector[0] *= fog->tcScale;
	parm.fogDistanceVector[1] *= fog->tcScale;
	parm.fogDistanceVector[2] *= fog->tcScale;
	parm.fogDistanceVector[3] *= fog->tcScale;

	// rotate the gradient vector for this orientation
	if (fog->hasSurface) {
		parm.fogDepthVector[0] = fog->surface[0] * backEnd.ori.axis[0][0] +
			fog->surface[1] * backEnd.ori.axis[0][1] + fog->surface[2] * backEnd.ori.axis[0][2];
		parm.fogDepthVector[1] = fog->surface[0] * backEnd.ori.axis[1][0] +
			fog->surface[1] * backEnd.ori.axis[1][1] + fog->surface[2] * backEnd.ori.axis[1][2];
		parm.fogDepthVector[2] = fog->surface[0] * backEnd.ori.axis[2][0] +
			fog->surface[1] * backEnd.ori.axis[2][1] + fog->surface[2] * backEnd.ori.axis[2][2];
		parm.fogDepthVector[3] = -fog->surface[3] + DotProduct(backEnd.ori.origin, fog->surface);

		parm.eyeT = DotProduct(backEnd.ori.viewOrigin, parm.fogDepthVector) + parm.fogDepthVector[3];
	}
	else {
		parm.eyeT = 1.0f; // non-surface fog always has eye inside
	}

	// see if the viewpoint is outside
	// this is needed for clipping distance even for constant fog
	if (parm.eyeT < 0) {
		parm.eyeOutside = qtrue;
	}
	else {
		parm.eyeOutside = qfalse;
	}

	parm.fogDistanceVector[3] += 1.0 / 512;
	parm.fogColor = fog->color;

	return &parm;
}

#ifdef USE_PMLIGHT
static void vk_set_light_params( vkUniform_t *uniform, const dlight_t *dl ) {
	float radius;

	if (!glConfig.deviceSupportsGamma && !vk.fboActive)
		VectorScale(dl->color, 2 * powf(r_intensity->value, r_gamma->value), uniform->lightColor);
	else
		VectorCopy(dl->color, uniform->lightColor);

	radius = dl->radius;

	// vertex data
	VectorCopy(backEnd.ori.viewOrigin, uniform->eyePos); uniform->eyePos[3] = 0.0f;
	VectorCopy(dl->transformed, uniform->lightPos); uniform->lightPos[3] = 0.0f;

	// fragment data
	uniform->lightColor[3] = 1.0f / Square(radius);

	if (dl->linear)
	{
		vec4_t ab;
		VectorSubtract(dl->transformed2, dl->transformed, ab);
		ab[3] = 1.0f / DotProduct(ab, ab);
		VectorCopy4(ab, uniform->lightVector);
	}
}
#endif

static void vk_set_fog_params( vkUniform_t *uniform, int *fogStage )
{
	if (tess.fogNum && tess.shader->fogPass) {
		if ( vk.hw_fog ) {
			// re-use these bits
			uniform->fog.fogDistanceVector[0] = tr.world ? (tr.world->globalFog - 1) : -1;
			uniform->fog.fogDistanceVector[1] = tess.fogNum ? (tess.fogNum - 1) : -1;
			uniform->fog.fogDistanceVector[2] = backEnd.isGlowPass ? 0.0f : 1.0f;
			//Com_Memcpy( uniform->fog.fogEyeT, backEnd.refdef.vieworg, sizeof( vec3_t) );
			*fogStage = 1;
			return;
		}
		
		const fogProgramParms_t *fp = RB_CalcFogProgramParms();
		// vertex data
		VectorCopy4(fp->fogDistanceVector, uniform->fog.fogDistanceVector);
		VectorCopy4(fp->fogDepthVector, uniform->fog.fogDepthVector);
		uniform->fog.fogEyeT[0] = fp->eyeT;
		if (fp->eyeOutside) {
			uniform->fog.fogEyeT[1] = 0.0; // fog eye out
		}
		else {
			uniform->fog.fogEyeT[1] = 1.0; // fog eye in
		}
		// fragment data
		if ( backEnd.isGlowPass )
			VectorCopy4( colorBlack, uniform->fog.fogColor );
		else
			VectorCopy4( fp->fogColor, uniform->fog.fogColor );

		*fogStage = 1;
	}
	else {
		*fogStage = 0;
	}
}

/*
===================
RB_FogPass
Blends a fog texture on top of everything else
===================
*/
static vkUniform_t			uniform;
static vkUniformGlobal_t	uniform_global;

static void RB_FogPass( void ) 
{
	const int sh = ( tess.vbo_model ) ? ( tess.surfType == SF_MDX ? 1 : 2 ) : 0;

	uint32_t pipeline = vk.std_pipeline.fog_pipelines[sh][tess.shader->fogPass - 1][tess.shader->cullType][tess.shader->polygonOffset];

#ifdef USE_FOG_ONLY
	int fog_stage;
	
	//vk_bind_pipeline( pipeline );
	vk_set_fog_params( &uniform, &fog_stage );
	vk_push_uniform( &uniform );
	vk_update_descriptor( VK_DESC_FOG_ONLY, tr.fogImage->descriptor_set );
	//vk_draw_geometry( DEPTH_RANGE_NORMAL, qtrue );

	// create draw item
	{
		DrawItem item = {};
		item.pipeline = pipeline;
		item.pipeline_layout = vk.pipeline_layout;
		item.depthRange = DEPTH_RANGE_NORMAL;
		item.polygonOffset = tess.shader->polygonOffset;
		item.identifier = 18;
		item.reset_uniform = qfalse;

		RB_AddDrawItemIndexBinding( item );
		RB_AddDrawItemVertexBinding( item );
		RB_AddDrawItemUniformBinding( item, backEnd.currentEntity );

		RB_AddDrawItem( backEndData->currentPass, item );
	}

#else
	const fog_t *fog;
	int			i;

	fog = tr.world->fogs + tess.fogNum;

	for (i = 0; i < tess.numVertexes; i++) {
		*(int*)&tess.svars.colors[0][i] = fog->colorInt;
	}

	RB_CalcFogTexCoords((float*)tess.svars.texcoords[0]);
	tess.svars.texcoordPtr[0] = tess.svars.texcoords[0];
	vk_bind(tr.fogImage);

	vk_bind_pipeline(pipeline);
	vk_bind_geometry(TESS_ST0 | TESS_RGBA0);
	vk_draw_geometry(DEPTH_RANGE_NORMAL, qtrue);
#endif
}

void vk_bind( image_t *image ) {
	if (!image) {
		vk_debug("vk_bind: NULL image\n");
		image = tr.defaultImage;
	}

	image->frameUsed = tr.frameCount;

	vk_update_descriptor( vk.ctmu + VK_DESC_TEXTURE_BASE, image->descriptor_set );
}

void R_BindAnimatedImage( const textureBundle_t *bundle ) {

	int64_t index;

	if ( bundle->isVideoMap ) {
		CIN_RunCinematic( bundle->videoMapHandle );
		CIN_UploadCinematic( bundle->videoMapHandle );
		return;
	}
	if ( bundle->isScreenMap ) {
		if ( !backEnd.screenMapDone ) {
			vk_bind( tr.blackImage );
		}
		else {

			vk_update_descriptor( vk.ctmu + VK_DESC_TEXTURE_BASE, vk.screenMap.color_descriptor );
		}
		return;
	}

	if ( ( r_fullbright->value || ( tr.refdef.rdflags & RDF_doFullbright ) ) && bundle->isLightmap )
	{
		vk_bind( tr.whiteImage );
		return;
	}

	if ( bundle->numImageAnimations <= 1 ) {
		vk_bind(bundle->image[0]);
		return;
	}

#ifdef RF_SETANIMINDEX
	if ( backEnd.currentEntity->e.renderfx & RF_SETANIMINDEX )
	{
		index = backEnd.currentEntity->e.skinNum;
	}
	else
#endif
	{
		// it is necessary to do this messy calc to make sure animations line up
		// exactly with waveforms of the same frequency
		index = Q_ftol( tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE );
		index >>= FUNCTABLE_SIZE2;

		if ( index < 0 ) {
			index = 0;	// may happen with shader time offsets
		}
	}

	if ( bundle->oneShotAnimMap )
	{
		if ( index >= bundle->numImageAnimations )
		{
			// stick on last frame
			index = bundle->numImageAnimations - 1;
		}
	}
	else
	{
		// loop
		index %= bundle->numImageAnimations;
	}

	vk_bind( bundle->image[index] );
}

void ComputeTexCoords( const int b, const textureBundle_t *bundle ) {
	int	i;
	int tm;
	vec2_t* src, * dst;

	if (!tess.numVertexes)
		return;

	src = dst = tess.svars.texcoords[b];

	//
	// generate the texture coordinates
	//
	switch (bundle->tcGen)
	{
	case TCGEN_IDENTITY:
		src = tess.texCoords00;
		break;
	case TCGEN_TEXTURE:
		src = tess.texCoords[0];
		break;
	case TCGEN_LIGHTMAP:
		src = tess.texCoords[1];
		break;
	case TCGEN_LIGHTMAP1:
		src = tess.texCoords[2];
		break;
	case TCGEN_LIGHTMAP2:
		src = tess.texCoords[3];
		break;
	case TCGEN_LIGHTMAP3:
		src = tess.texCoords[4];
		break;
	case TCGEN_VECTOR:
		for (i = 0; i < tess.numVertexes; i++) {
			dst[i][0] = DotProduct(tess.xyz[i], bundle->tcGenVectors[0]);
			dst[i][1] = DotProduct(tess.xyz[i], bundle->tcGenVectors[1]);
		}
		break;
	case TCGEN_FOG:
		RB_CalcFogTexCoords((float*)dst);
		break;
	case TCGEN_ENVIRONMENT_MAPPED:
		RB_CalcEnvironmentTexCoords((float*)dst);
		break;
	//case TCGEN_ENVIRONMENT_MAPPED_FP:
	//	RB_CalcEnvironmentTexCoordsFP((float*)dst, bundle->isScreenMap);
	//	break;
	case TCGEN_BAD:
		return;
	}

	//
	// alter texture coordinates
	//
	for (tm = 0; tm < bundle->numTexMods; tm++) {
		switch (bundle->texMods[tm].type)
		{
		case TMOD_NONE:
			tm = TR_MAX_TEXMODS; // break out of for loop
			break;

		case TMOD_TURBULENT:
			RB_CalcTurbulentTexCoords(&bundle->texMods[tm].wave, (float*)src, (float*)dst);
			src = dst;
			break;

		case TMOD_ENTITY_TRANSLATE:
			RB_CalcScrollTexCoords(backEnd.currentEntity->e.shaderTexCoord, (float*)src, (float*)dst);
			src = dst;
			break;

		case TMOD_SCROLL:
			RB_CalcScrollTexCoords(bundle->texMods[tm].translate, (float*)src, (float*)dst);
			src = dst;
			break;

		case TMOD_SCALE:
			RB_CalcScaleTexCoords(bundle->texMods[tm].translate, (float*)src, (float*)dst);
			src = dst;
			break;

		case TMOD_STRETCH:
			RB_CalcStretchTexCoords(&bundle->texMods[tm].wave, (float*)src, (float*)dst);
			src = dst;
			break;

		case TMOD_TRANSFORM:
			RB_CalcTransformTexCoords(&bundle->texMods[tm], (float*)src, (float*)dst);
			src = dst;
			break;

		case TMOD_ROTATE:
			RB_CalcRotateTexCoords(bundle->texMods[tm].translate[0], (float*)src, (float*)dst);
			src = dst;
			break;

		default:
			Com_Error(ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'", bundle->texMods[tm].type, tess.shader->name);
			break;
		}
	}

	/*if (r_mergeLightmaps->integer && bundle->isLightmap && bundle->tcGen != TCGEN_LIGHTMAP) {
		// adjust texture coordinates to map on proper lightmap
		for (i = 0; i < tess.numVertexes; i++) {
			dst[i][0] = (src[i][0] * tr.lightmapScale[0]) + tess.shader->lightmapOffset[0];
			dst[i][1] = (src[i][1] * tr.lightmapScale[1]) + tess.shader->lightmapOffset[1];
		}
		src = dst;
	}*/

	tess.svars.texcoordPtr[b] = src;
}

static void vk_compute_tex_mods( const textureBundle_t *bundle, float *outMatrix, float *outOffTurb ) {
	int tm;
	float matrix[6], currentmatrix[6];

	matrix[0] = 1.0f; matrix[2] = 0.0f; matrix[4] = 0.0f;
	matrix[1] = 0.0f; matrix[3] = 1.0f; matrix[5] = 0.0f;

	currentmatrix[0] = 1.0f; currentmatrix[2] = 0.0f; currentmatrix[4] = 0.0f;
	currentmatrix[1] = 0.0f; currentmatrix[3] = 1.0f; currentmatrix[5] = 0.0f;

	outMatrix[0] = 1.0f; outMatrix[2] = 0.0f;
	outMatrix[1] = 0.0f; outMatrix[3] = 1.0f;

	outOffTurb[0] = 0.0f; outOffTurb[1] = 0.0f; outOffTurb[2] = 0.0f; outOffTurb[3] = 0.0f;

	for ( tm = 0; tm < bundle->numTexMods ; tm++ ) {
		switch ( bundle->texMods[tm].type )
		{
			
		case TMOD_NONE:
			tm = TR_MAX_TEXMODS;		// break out of for loop
			break;

		case TMOD_TURBULENT:
			RB_CalcTurbulentFactors(&bundle->texMods[tm].wave, &outOffTurb[2], &outOffTurb[3]);
			break;

		case TMOD_ENTITY_TRANSLATE:
			RB_CalcScrollTexMatrix( backEnd.currentEntity->e.shaderTexCoord, matrix );
			break;

		case TMOD_SCROLL:
			RB_CalcScrollTexMatrix( bundle->texMods[tm].translate, matrix );
			break;

		case TMOD_SCALE:
			RB_CalcScaleTexMatrix( bundle->texMods[tm].translate, matrix );
			break;
		
		case TMOD_STRETCH:
			RB_CalcStretchTexMatrix( &bundle->texMods[tm].wave,  matrix );
			break;

		case TMOD_TRANSFORM:
			RB_CalcTransformTexMatrix( &bundle->texMods[tm], matrix );
			break;

		case TMOD_ROTATE:
			RB_CalcRotateTexMatrix( bundle->texMods[tm].translate[0], matrix );
			break;

		default:
			Com_Error( ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'", bundle->texMods[tm].type, tess.shader->name );
			break;
		}

		switch ( bundle->texMods[tm].type )
		{	
		case TMOD_NONE:
		case TMOD_TURBULENT:
		default:
			break;

		case TMOD_ENTITY_TRANSLATE:
		case TMOD_SCROLL:
		case TMOD_SCALE:
		case TMOD_STRETCH:
		case TMOD_TRANSFORM:
		case TMOD_ROTATE:
			outMatrix[0] = matrix[0] * currentmatrix[0] + matrix[2] * currentmatrix[1];
			outMatrix[1] = matrix[1] * currentmatrix[0] + matrix[3] * currentmatrix[1];

			outMatrix[2] = matrix[0] * currentmatrix[2] + matrix[2] * currentmatrix[3];
			outMatrix[3] = matrix[1] * currentmatrix[2] + matrix[3] * currentmatrix[3];

			outOffTurb[0] = matrix[0] * currentmatrix[4] + matrix[2] * currentmatrix[5] + matrix[4];
			outOffTurb[1] = matrix[1] * currentmatrix[4] + matrix[3] * currentmatrix[5] + matrix[5];

			currentmatrix[0] = outMatrix[0];
			currentmatrix[1] = outMatrix[1];
			currentmatrix[2] = outMatrix[2];
			currentmatrix[3] = outMatrix[3];
			currentmatrix[4] = outOffTurb[0];
			currentmatrix[5] = outOffTurb[1];
			break;
		}
	}
}

#ifdef USE_VBO_GHOUL2
#if 0 // skip ghoul2 vbo glsl in_colors for now
static void vk_set_attr_color( color4ub_t *dest, const qboolean skip ){
	uint32_t i;
	int numVerts;

	numVerts = ( tess.vbo_world_index && tess.surfType == SF_MDX ) ? 
		tess.mesh_ptr->numVertexes : tess.numVertexes;

	if ( skip ) {
		Com_Memset( dest, 0, numVerts * sizeof(color4ub_t) );
		return;
	}

	for ( i = 0; i < numVerts; i++ ) {
		dest[i][0] = tess.vertexColors[i][0];
		dest[i][1] = tess.vertexColors[i][1];
		dest[i][2] = tess.vertexColors[i][2];
		dest[i][3] = tess.vertexColors[i][3];
	}
}
#endif

static void vk_compute_tex_coords( const textureBundle_t *bundle, vktcMod_t *tcMod, vktcGen_t *tcGen ) {
	vk_compute_tex_mods( bundle, tcMod->matrix, tcMod->offTurb ); 

	tcGen->type = bundle->tcGen;
	
	if ( bundle->tcGen == TCGEN_VECTOR )
	{
		VectorCopy( bundle->tcGenVectors[0], tcGen->vector0 );
		VectorCopy( bundle->tcGenVectors[1], tcGen->vector1 );
	}
}

static void vk_compute_colors( const int b, const shaderStage_t *pStage, int forceRGBGen ){	
	if ( backEnd.currentEntity->e.renderfx & RF_VOLUMETRIC ) 
		return;

	float *baseColor, *vertColor;

	int rgbGen = forceRGBGen;
	int alphaGen = pStage->bundle[b].alphaGen;

	baseColor = (float*)uniform_global.bundle[b].baseColor;
	vertColor = (float*)uniform_global.bundle[b].vertColor;

	baseColor[0] = baseColor[1] = baseColor[2] = baseColor[3] = 1.0f;  	
   	vertColor[0] = vertColor[1] = vertColor[2] = vertColor[3] = 0.0f;

	if ( !forceRGBGen )
		rgbGen = pStage->bundle[b].rgbGen;

	switch ( rgbGen) {
		case CGEN_IDENTITY_LIGHTING: 
			baseColor[0] = baseColor[1] = baseColor[2] = tr.identityLight;
			break;
		case CGEN_EXACT_VERTEX:
			baseColor[0] = baseColor[1] = baseColor[2] = baseColor[3] = 0.0f;
			vertColor[0] = vertColor[1] = vertColor[2] = vertColor[3] = 1.0f;
			break;
		case CGEN_CONST:
			baseColor[0] = pStage->bundle[b].constantColor[0] / 255.0f;
			baseColor[1] = pStage->bundle[b].constantColor[1] / 255.0f;
			baseColor[2] = pStage->bundle[b].constantColor[2] / 255.0f;
			baseColor[3] = pStage->bundle[b].constantColor[3] / 255.0f;
			break;
		case CGEN_VERTEX:
			baseColor[0] = baseColor[1] = baseColor[2] = baseColor[3] = 0.0f;
			vertColor[0] = vertColor[1] = vertColor[2] = tr.identityLight;
			vertColor[3] = 1.0f;
			break;
		case CGEN_ONE_MINUS_VERTEX:
			baseColor[0] = baseColor[1] = baseColor[2] = tr.identityLight;
			vertColor[0] = vertColor[1] = vertColor[2] = -tr.identityLight;
			break;
		case CGEN_FOG:
			{
				fog_t *fog = tr.world->fogs + tess.fogNum;
				VectorCopy4(fog->color, baseColor);
			}
			break;
		case CGEN_WAVEFORM:
			baseColor[0] = baseColor[1] = baseColor[2] = RB_CalcWaveColorSingle( &pStage->bundle[b].rgbWave );
			break;
		case CGEN_ENTITY:
		case CGEN_LIGHTING_DIFFUSE_ENTITY:
			if ( backEnd.currentEntity )
			{
				baseColor[0] = ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[0] / 255.0f;
				baseColor[1] = ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[1] / 255.0f;
				baseColor[2] = ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[2] / 255.0f;
				baseColor[3] = ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[3] / 255.0f;

				//vertColor[0] = vertColor[1] = vertColor[2] = tr.identityLight;
				//vertColor[3] = 1.0f;

				if ( alphaGen == AGEN_IDENTITY && backEnd.currentEntity->e.shaderRGBA[3] == 255 )
					alphaGen = AGEN_SKIP;
			}
			break;
		case CGEN_ONE_MINUS_ENTITY:
			if ( backEnd.currentEntity )
			{
				baseColor[0] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[0] / 255.0f;
				baseColor[1] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[1] / 255.0f;
				baseColor[2] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[2] / 255.0f;
				baseColor[3] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[3] / 255.0f;
			}
			break;
		case CGEN_LIGHTMAPSTYLE:
			VectorScale4 (styleColors[pStage->lightmapStyle[b%2]], 1.0f / 255.0f, baseColor);
			break;
		case CGEN_IDENTITY:
		case CGEN_LIGHTING_DIFFUSE:
		case CGEN_BAD:
			break;
		default:
			break;
	}

	switch ( alphaGen ) {
		case AGEN_SKIP:
			break;
		case AGEN_CONST:
			if ( rgbGen != CGEN_CONST ) {
				baseColor[3] = pStage->bundle[b].constantColor[3] / 255.0f;
				vertColor[3] = 0.0f;
			}
			break;
		case AGEN_WAVEFORM:
			baseColor[3] = RB_CalcWaveAlphaSingle( &pStage->bundle[b].alphaWave );
			vertColor[3] = 0.0f;
			break;
		case AGEN_ENTITY:
			if ( backEnd.currentEntity )
				baseColor[3] = ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[3] / 255.0f;

			vertColor[3] = 0.0f;
			break;
		case AGEN_ONE_MINUS_ENTITY:
			if ( backEnd.currentEntity )
				baseColor[3] = 1.0f - ((unsigned char *)backEnd.currentEntity->e.shaderRGBA)[3] / 255.0f;

			vertColor[3] = 0.0f;
			break;
		case AGEN_VERTEX:
			if ( rgbGen != CGEN_VERTEX ) {
				baseColor[3] = 0.0f;
				vertColor[3] = 1.0f;			
			}
			break;
		case AGEN_ONE_MINUS_VERTEX:
			baseColor[3] = 1.0f;
			vertColor[3] = -1.0f;
			break;
		case AGEN_IDENTITY:
		case AGEN_LIGHTING_SPECULAR:
		case AGEN_PORTAL:
			// done entirely in vertex program
			baseColor[3] = 1.0f;
			vertColor[3] = 0.0f;
			break;
		default:
			break;
	}

	if ( backEnd.currentEntity && backEnd.currentEntity->e.renderfx & RF_FORCE_ENT_ALPHA ) {
		baseColor[3] = backEnd.currentEntity->e.shaderRGBA[3] / 255.0f; 
		vertColor[3] = 0.0f;
	}

	// multiply color by overbrightbits if this isn't a blend
	if ( tr.overbrightBits 
	 && !( ( pStage->stateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_DST_COLOR )
	 && !( ( pStage->stateBits & GLS_SRCBLEND_BITS ) == GLS_SRCBLEND_ONE_MINUS_DST_COLOR )
	 && !( ( pStage->stateBits & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_SRC_COLOR )
	 && !( ( pStage->stateBits & GLS_DSTBLEND_BITS ) == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR ) )
	{
		float scale = 1 << tr.overbrightBits;

		baseColor[0] *= scale;
		baseColor[1] *= scale;
		baseColor[2] *= scale;
		vertColor[0] *= scale;
		vertColor[1] *= scale;
		vertColor[2] *= scale;
	}

	uniform_global.bundle[b].rgbGen = (uint32_t)rgbGen;
	uniform_global.bundle[b].alphaGen = (uint32_t)alphaGen;

	if ( alphaGen == AGEN_PORTAL )
		uniform_global.portalRange = tess.shader->portalRange;
}

static void vk_compute_deform( void ) {
	int		type = DEFORM_NONE;
	int		waveFunc = GF_NONE;
	vkDeform_t	*info;

	info = &uniform_global.deform;

	Com_Memset( info + 0, 0, sizeof(float) * 12 );

	if ( backEnd.currentEntity->e.renderfx & RF_DISINTEGRATE2 ) {
		info->type = (float)DEFORM_DISINTEGRATION;
		return;
	}

	if ( tess.shader->numDeforms && !ShaderRequiresCPUDeforms( tess.shader ) ) {
		// only support the first one
		deformStage_t *ds = tess.shader->deforms[ 0 ];

		switch ( ds->deformation ) {
			case DEFORM_WAVE:
				type = DEFORM_WAVE;
				waveFunc = ds->deformationWave.func;

				info->base = ds->deformationWave.base;
				info->amplitude = ds->deformationWave.amplitude;
				info->phase = ds->deformationWave.phase;
				info->frequency = ds->deformationWave.frequency;
				info->vector[0] = ds->deformationSpread;
				info->vector[1] = 0.0f;
				info->vector[2] = 0.0f;
				break;
			case DEFORM_BULGE:
				type = DEFORM_BULGE;

				info->base = 0.0f;
				info->amplitude = ds->bulgeHeight; // amplitude
				info->phase = ds->bulgeWidth;  // phase
				info->frequency = ds->bulgeSpeed;  // frequency
				info->vector[0] = 0.0f;
				info->vector[1] = 0.0f;
				info->vector[2] = 0.0f;

				if ( ds->bulgeSpeed == 0.0f && ds->bulgeWidth == 0.0f )
					type = DEFORM_BULGE_UNIFORM;

				break;
			case DEFORM_MOVE:
				type = DEFORM_MOVE;
				waveFunc = ds->deformationWave.func;

				info->base = ds->deformationWave.base;
				info->amplitude = ds->deformationWave.amplitude;
				info->phase = ds->deformationWave.phase;
				info->frequency = ds->deformationWave.frequency;
				info->vector[0] = ds->moveVector[0];
				info->vector[1] = ds->moveVector[1];
				info->vector[2] = ds->moveVector[2];
				break;
			case DEFORM_NORMALS:
				type = DEFORM_NORMALS;

				info->base = 0.0f;
				info->amplitude = ds->deformationWave.amplitude; // amplitude
				info->phase = 0.0f;  // phase
				info->frequency = ds->deformationWave.frequency;  // frequency
				info->vector[0] = 0.0f;
				info->vector[1] = 0.0f;
				info->vector[2] = 0.0f;
				break;
			case DEFORM_PROJECTION_SHADOW:
				type = DEFORM_PROJECTION_SHADOW;

				info->base = backEnd.ori.axis[0][2];
				info->amplitude = backEnd.ori.axis[1][2];
				info->phase = backEnd.ori.axis[2][2];
				info->frequency = backEnd.ori.origin[2] - backEnd.currentEntity->e.shadowPlane;

				vec3_t lightDir;
				VectorCopy( backEnd.currentEntity->modelLightDir, lightDir );
				lightDir[2] = 0.0f;
				VectorNormalize( lightDir );
				VectorSet( lightDir, lightDir[0] * 0.3f, lightDir[1] * 0.3f, 1.0f );

				info->vector[0] = lightDir[0];
				info->vector[1] = lightDir[1];
				info->vector[2] = lightDir[2];
				break;
			default:
				break;
		}
	}

	if ( type != DEFORM_NONE ) {
		info->time = tess.shaderTime;
		info->type = type;
		info->func = waveFunc;	
	}
}

static void vk_compute_disintegration( int *forceRGBGen )
{
	vkDisintegration_t	*info;

	if ( backEnd.currentEntity->e.renderfx & RF_DISINTEGRATE1 )
		*forceRGBGen = (int)CGEN_DISINTEGRATION_1;
	else
		*forceRGBGen = (int)CGEN_DISINTEGRATION_2;

	info = &uniform_global.disintegration;

	info->origin[0] = backEnd.currentEntity->e.oldorigin[0];
	info->origin[1] = backEnd.currentEntity->e.oldorigin[1];
	info->origin[2] = backEnd.currentEntity->e.oldorigin[2];
	info->threshold = ( backEnd.refdef.time - backEnd.currentEntity->e.endTime ) * 0.045f;
	info->threshold *= info->threshold;
}
#endif

#ifdef USE_PMLIGHT
void vk_lighting_pass( void )
{
	static uint32_t uniform_offset;
	static int fog_stage;
	uint32_t pipeline;
	const shaderStage_t *pStage;
	cullType_t cull;
	int abs_light;

	if (tess.shader->lightingStage < 0)
		return;

	pStage = tess.xstages[tess.shader->lightingStage];

	// we may need to update programs for fog transitions
	if (tess.dlightUpdateParams) {
		vk_set_fog_params(&uniform, &fog_stage);
		vk_set_light_params(&uniform, tess.light);

		uniform_offset = vk_push_uniform(&uniform);

		tess.dlightUpdateParams = qfalse;
	}

	if (uniform_offset == ~0)
		return; // no space left...

	cull = tess.shader->cullType;
	if (backEnd.viewParms.portalView == PV_MIRROR) {
		switch (cull) {
		case CT_FRONT_SIDED: cull = CT_BACK_SIDED; break;
		case CT_BACK_SIDED: cull = CT_FRONT_SIDED; break;
		default: break;
		}
	}

	abs_light = /* (pStage->stateBits & GLS_ATEST_BITS) && */ (cull == CT_TWO_SIDED) ? 1 : 0;

	if (fog_stage)
		vk_update_descriptor( VK_DESC_FOG_DLIGHT, tr.fogImage->descriptor_set );

	if (tess.light->linear)
		pipeline = vk.std_pipeline.dlight1_pipelines_x[cull][tess.shader->polygonOffset][fog_stage][abs_light];
	else
		pipeline = vk.std_pipeline.dlight_pipelines_x[cull][tess.shader->polygonOffset][fog_stage][abs_light];

	vk_select_texture(0);
	R_BindAnimatedImage(&pStage->bundle[tess.shader->lightingBundle]);

#ifdef USE_VBO
	if (tess.vbo_world_index == 0)
#endif
	{
		ComputeTexCoords(tess.shader->lightingBundle, &pStage->bundle[tess.shader->lightingBundle]);
	}
	
	vk_bind_pipeline(pipeline);
	vk_bind_index();
	vk_bind_lighting( tess.shader->lightingStage, tess.shader->lightingBundle );
	vk_bind_geometry_buffer();
	vk_draw_geometry( tess.depthRange, qtrue );
}
#endif // USE_PMLIGHT

void ForceAlpha(unsigned char *dstColors, int TR_ForceEntAlpha)
{
	int	i;

	dstColors += 3;

	for ( i = 0; i < tess.numVertexes; i++, dstColors += 4 )
	{
		*dstColors = TR_ForceEntAlpha;
	}
}

static int compare_cmds(const void *a, const void *b)
{
    const vk_ss_group_cmd_t *cmd_a = (const vk_ss_group_cmd_t *)a;
    const vk_ss_group_cmd_t *cmd_b = (const vk_ss_group_cmd_t *)b;

    return cmd_a->firstInstance - cmd_b->firstInstance;
}

void vk_merge_surface_sprite_commands(vk_ss_group_t *group)
{
    // sort by firstInstance
    qsort(group->cmd, group->num_commands, sizeof(vk_ss_group_cmd_t), compare_cmds);

    int write = 0;

    for ( int read = 1; read < group->num_commands; read++ ) 
	{
        vk_ss_group_cmd_t *a = &group->cmd[write];
        vk_ss_group_cmd_t *b = &group->cmd[read];

        int a_start = a->firstInstance;
        int a_end   = a->firstInstance + a->numInstances;

        int b_start = b->firstInstance;
        int b_end   = b->firstInstance + b->numInstances;
		
		// merge b into a when adjacent
        if ( b_start <= a_end ) 
		{ 
			a->firstInstance = MIN( b_start, a_start );
			a->numInstances = MAX( b_end, a_end ) - a->firstInstance;
        }
		else 
		{ 
            if ( ++write != read )
                group->cmd[write] = *b;
        }
    }

    group->num_commands = write + 1;
}

#ifdef USE_VBO_SS
void RB_SurfaceSpritesVBO( srfSprites_t *surf )
{
	if ( !r_surfaceSprites->integer )
		return;

	if ( !tr.ss.groups_count )
		return;

	RB_EndSurface();

	int current_ent = -1;
	uint32_t i, j;
	float push_constants[16] = { 0 };

	for ( i = 0; i < tr.ss.groups_count; i++ )
	{
		vk_ss_group_t *group = &tr.ss.groups[i];

		if ( !group->num_commands )
			continue;

		tess.shader = group->def.shader;
		shaderStage_t *firstStage = tess.shader->stages[0];	
		static int fog_stage;

		tess.surfType = SF_SPRITES;
		tess.vbo_model = tr.vbos[SS_UNPACK_VBO(group->def.surf_bits)];
		tess.fogNum = SS_UNPACK_FOG(group->def.surf_bits);

		vk_set_fog_params( &uniform, &fog_stage );
		
		DrawItem item = { 0 };

		if ( backEnd.viewParms.portalView == PV_MIRROR ) {
			item.pipeline = firstStage->vk_mirror_pipeline[fog_stage];
		}
		else {
			item.pipeline = firstStage->vk_pipeline[fog_stage];
		}
		item.pipeline_layout = vk.pipeline_layout_surface_sprite;

		vk_select_texture(0);
		R_BindAnimatedImage( &firstStage->bundle[0] );
		vk_bind_geometry( TESS_XYZ | TESS_NNN | TESS_RGBA0 | TESS_RGBA1 );
		RB_AddDrawItemVertexBinding( item );

		const int entity_num = (int)SS_UNPACK_ENT(group->def.surf_bits);

		if ( entity_num != current_ent )
		{
			current_ent = entity_num;

			if ( entity_num == REFENTITYNUM_WORLD )
			{
				get_mvp_transform( push_constants );
			}
			else 
			{
				orientationr_t ori;
				trRefEntity_t *ent = &tr.refdef.entities[entity_num];
	
				R_RotateForEntity( ent, &backEnd.viewParms, &ori );

				const float* p = backEnd.viewParms.projectionMatrix;
				float proj[16];
				Com_Memcpy(proj, p, 64);
				proj[5] = -p[5];

				myGlMultMatrix( ori.modelViewMatrix, proj, push_constants );
			}
		}
		Com_Memcpy( item.mvp, push_constants, sizeof(float)*16);

		item.depthRange = DEPTH_RANGE_NORMAL;
		item.polygonOffset = qfalse;
		item.identifier = 25;

		item.descriptor_set.offset[0] = vk_push_uniform(&uniform);
		item.descriptor_set.offset[1] = vk.cmd->camera_ubo_offset;
		item.descriptor_set.offset[2] = 0; // light
		item.descriptor_set.offset[3] = 0; // entity
		item.descriptor_set.offset[4] = 0; // bones
		item.descriptor_set.offset[5] = vk.cmd->fogs_ubo_offset;
		item.descriptor_set.offset[6] = 0; // global (drawcall)
		item.descriptor_set.offset[7] = SS_UNPACK_SSBO_OFFSET(group->def.ssbo_bits);

		item.descriptor_set.current[0] = vk.cmd->uniform_descriptor;
		item.descriptor_set.current[1] = vk.surface_sprites_ssbo[SS_UNPACK_SSBO_INDEX(group->def.ssbo_bits)].descriptor;
		item.descriptor_set.current[2] = vk.cmd->descriptor_set.current[VK_DESC_TEXTURE0];
		item.descriptor_set.current[3] = fog_stage ? tr.fogImage->descriptor_set : VK_NULL_HANDLE;

		uint32_t set_count = fog_stage ? 4: 3;
		item.descriptor_set.start = 0;
		item.descriptor_set.end = (set_count - 1);

		item.ibo = tr.ss.ibo;
		item.indexedIndirect = qtrue;		// change type
		item.draw.params.indexedIndirect.numDraws = group->num_commands;

		// ~sunny, this worth cpu cycles? 
		// eg. t2_dpred issues alot of tiny ss drawsurfs
		if ( group->num_commands > 10 && r_surfaceSprites->integer == 2 ) 
			vk_merge_surface_sprite_commands( group ); 

		VkDrawIndexedIndirectCommand *cmd = vk_reserve_draw_indexed_indirect( group->num_commands, &item.draw.params.indexedIndirect.offset );

		if ( !cmd )
			break;

		for ( j = 0; j < group->num_commands; j++ )
		{
			const vk_ss_group_cmd_t* group_cmd = &group->cmd[j];

			cmd[j].indexCount		= 6;
			cmd[j].instanceCount	= group_cmd->numInstances;
			cmd[j].firstIndex		= 0;
			cmd[j].vertexOffset		= 0;
			cmd[j].firstInstance	= group_cmd->firstInstance;
		}

		RB_AddDrawItem( backEndData->currentPass, item );
	}

	tess.numVertexes = 0;
	tess.numIndexes = 0;
	tess.multiDrawPrimitives = 0;

	tess.vbo_world_index = 0;
	tess.vbo_model = nullptr;
	tess.ibo_model = nullptr;

	for ( uint32_t i = 0; i < VK_DESC_COUNT; i++ ) {
		vk_reset_descriptor( i );
	}

	vk.cmd->descriptor_set.end = 0;
	vk.cmd->descriptor_set.start = ~0U;

	if ( backEnd.viewParms.portalView == PV_NONE )
		tr.ss.groups_count = 0;
}
#endif

void RB_AddDrawItemUniformBinding( DrawItem &item, const trRefEntity_t *refEntity ) 
{
	uint32_t i;

	// fog or env will have this slot bound
	if ( item.reset_uniform ) 
	{
		vk_reset_descriptor( VK_DESC_UNIFORM );	// to set start/end
		vk_update_descriptor( VK_DESC_UNIFORM, vk.cmd->uniform_descriptor );
		vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM] = 0;
	}

	vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_CAMERA_BINDING] = vk.cmd->camera_ubo_offset;
	vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_LIGHT_BINDING] = vk.cmd->light_ubo_offset;
	vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_FOGS_BINDING] = vk.cmd->fogs_ubo_offset;

	if ( backEnd.currentEntity ) 
	{
		if ( backEnd.currentEntity == &backEnd.entity2D ) 
		{
			vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_ENTITY_BINDING] = vk.cmd->entity_ubo_offset[REFENTITYNUM_WORLD];
			vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_BONES_BINDING] = 0;
		}
		else if ( backEnd.currentEntity == &tr.worldEntity ) 
		{
			vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_ENTITY_BINDING] = vk.cmd->entity_ubo_offset[REFENTITYNUM_WORLD];
			vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_BONES_BINDING] = 0;
		}
		else 
		{
			const int refEntityNum = backEnd.currentEntity - backEnd.refdef.entities;

			vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_ENTITY_BINDING] = vk.cmd->entity_ubo_offset[refEntityNum];
			vk.cmd->descriptor_set.offset[VK_DESC_UNIFORM_BONES_BINDING] = vk.cmd->bones_ubo_offset;
		}
	}
		
	{
		// fill NULL descriptor gaps
		if ( vk.cmd->descriptor_set.start != ~0U )
		{
			for ( i = vk.cmd->descriptor_set.start + 1; i < vk.cmd->descriptor_set.end; i++ ) {
				if ( vk.cmd->descriptor_set.current[i] == VK_NULL_HANDLE ) {
					vk.cmd->descriptor_set.current[i] = tr.whiteImage->descriptor_set;
				}
			}
		}

		Com_Memcpy( &item.descriptor_set, &vk.cmd->descriptor_set, sizeof(vk.cmd->descriptor_set));

#ifdef USE_VK_PBR
		// The pushed set travels with the item, because the item is executed
		// after every other item has been recorded and vk.cmd holds whatever
		// the LAST of them set. Copying it here is what makes each mesh get its
		// own normal and physical maps rather than the final mesh's.
		Com_Memcpy( item.pbr_source, vk.cmd->pbr_source, sizeof(item.pbr_source) );
		Com_Memcpy( item.pbr_raw, vk.cmd->pbr_raw, sizeof(item.pbr_raw) );
		Com_Memcpy( item.pbr_raw_set, vk.cmd->pbr_raw_set, sizeof(item.pbr_raw_set) );

		item.pbr_used = qfalse;
		for ( i = 0; i < VK_DESC_PBR_BINDING_COUNT; i++ ) {
			if ( vk.cmd->pbr_source[i] != NULL || vk.cmd->pbr_raw_set[i] ) {
				item.pbr_used = qtrue;
				break;
			}
		}
#endif
	}

	vk.cmd->descriptor_set.end = 0;
	vk.cmd->descriptor_set.start = ~0U;
}

void RB_AddDrawItemVertexBinding( DrawItem &item ) 
{
	// only set appropiate data please, fix this. acceptable for now
	const pushConst *constants = vk_get_push_constant();
	Com_Memcpy( item.mvp, constants->mvp, sizeof(float)*16);

	Com_Memcpy( &item.shade_buffers, shade_bufs, sizeof(shade_bufs));

	if ( tess.vbo_world_index || tess.vbo_model ) 
		Com_Memcpy( &item.shade_offset, &vk.cmd->vbo_offset, sizeof(vk.cmd->vbo_offset));
	else
		Com_Memcpy( &item.shade_offset, &vk.cmd->buf_offset, sizeof(vk.cmd->buf_offset));

	item.bind_base = bind_base;
	item.bind_count = bind_count;
}

//static std::vector<VkDrawIndexedIndirectCommand> indirectCommands;

void RB_AddDrawItemIndexBinding( DrawItem &item ) 
{
	// model vbo
	if ( tess.vbo_model ) 
	{
		item.ibo = tess.ibo_model;

		if ( tess.multiDrawPrimitives ) 
		{
			// draw indexed indirect
			if ( tess.multiDrawPrimitives > 1 ) 
			{
				uint32_t i, offset;
				size_t *index;

				item.indexedIndirect = qtrue;		// change type
				item.draw.params.indexedIndirect.numDraws = tess.multiDrawPrimitives;

				VkDrawIndexedIndirectCommand *cmd = vk_reserve_draw_indexed_indirect( tess.multiDrawPrimitives, &item.draw.params.indexedIndirect.offset );

				for ( i = 0; i < tess.multiDrawPrimitives; i++ ) 
				{
					index = (size_t*)tess.multiDrawFirstIndex + i;

					cmd[i].indexCount		= tess.multiDrawNumIndexes[i];
					cmd[i].instanceCount	= 1;
					cmd[i].firstIndex		= (uint32_t)(*index);
					cmd[i].vertexOffset		= 0;
					cmd[i].firstInstance	= 0;
				}

				return;
			}

			// draw regular indexed
			item.indexed = qtrue;
			item.draw.params.indexed.num_indexes = tess.multiDrawNumIndexes[0];
			item.draw.params.indexed.index_offset = (glIndex_t)(size_t)(tess.multiDrawFirstIndex[0]) * sizeof(uint32_t);
		}
	}

	// world vbo
	else if ( tess.vbo_world_index ) 
	{
		item.indexed = qtrue;
		item.vbo_world_index = tess.vbo_world_index;
		item.draw.params.indexed.world.ibo_offset = tess.shader->iboOffset;

		VBO_GetDeviceBuffer( item );
		VBO_GetSoftbuffer( item );

		return;
	}

	else 
	{
		uint32_t offset	= vk_tess_index( tess.numIndexes, tess.indexes );

		if ( offset != ~0U ) {
			item.indexed = qtrue;
			item.draw.params.indexed.index_offset = offset;
			item.draw.params.indexed.num_indexes = tess.numIndexes;

			return;
		}

		// overflowed
		item.draw.params.indexed.num_indexes = 0;
	}
}

static ss_input ssInput;
void RB_StageIteratorGeneric( void )
{
	const shaderStage_t		*pStage;
	Vk_Pipeline_Def			def;
	uint32_t				stage = 0;
	uint32_t				pipeline;
	int						tess_flags, i;
	int						fog_stage = 0;
	qboolean				fogCollapse;
	qboolean				is_ghoul2_vbo;
	qboolean				is_mdv_vbo;

	is_ghoul2_vbo = qfalse;
	is_mdv_vbo = qfalse;

	// A surface that does not own the depth of the pixels it covers is not in
	// the pre-pass at all. Decided once, when the shader was finished; see
	// ComputeDepthPrepass.
	if ( backEnd.depthPrepass && tess.shader->depthPrepass == DEPTHPREPASS_SKIP ) {
		return;
	}

	if ( tess.vbo_model ) {
		is_ghoul2_vbo = (qboolean)( tess.surfType == SF_MDX );
		is_mdv_vbo = (qboolean)( tess.surfType == SF_VBO_MDVMESH );
	}

#ifdef USE_VBO
	if ( tess.vbo_world_index != 0 ) {
		VBO_PrepareQueues();
		tess.vboStage = 0;
	} 
	else
#endif
	{
		// if !tess.vbo_model_index .. 
		RB_DeformTessGeometry();
	}

#ifdef USE_PMLIGHT
	if ( tess.dlightPass ) {
		vk_lighting_pass();
		return;
	}
#endif

	vk_bind_index();

	tess_flags = tess.shader->tessFlags;

	fogCollapse = qfalse;

#ifdef USE_FOG_COLLAPSE
	// Fog is colour. It neither moves a vertex nor decides whether one is there,
	// so in the pre-pass it would only cost a second set of pipelines that draw
	// the same depth.
	if ( !backEnd.depthPrepass &&
		tess.fogNum && tess.shader->fogPass && tess.shader->fogCollapse && r_drawfog->value >= 2 ) {
		vk_set_fog_params( &uniform, &fog_stage );

		fogCollapse = qtrue;
	}
#endif

	Com_Memset( &uniform_global, 0, sizeof(uniform_global) );
	
	if ( backEnd.currentEntity != &tr.worldEntity ) 
		vk_compute_deform();

	for ( stage = 0; stage < MAX_SHADER_STAGES; stage++ )
	{
		int			forceRGBGen = 0;
		qboolean	is_refraction = qfalse;

		pStage = tess.xstages[stage];

		if ( !pStage || !pStage->active )
			break;

#ifdef USE_VBO
		tess.vboStage = stage;
#endif

		// we check for surfacesprites AFTER drawing everything else
		if ( pStage->ss && pStage->ss->type )
			continue;

		// vertexLightmap isnt used rn
		if ( stage && r_lightmap->integer && !( pStage->bundle[0].isLightmap || pStage->bundle[1].isLightmap || pStage->bundle[0].vertexLightmap ) )
			break;

		if ( backEnd.currentEntity ) {
			assert( backEnd.currentEntity->e.renderfx >= 0 );

			if ( is_ghoul2_vbo && backEnd.currentEntity->e.renderfx & ( RF_DISINTEGRATE1 | RF_DISINTEGRATE2 ) )
				vk_compute_disintegration( &forceRGBGen );

			//want to use RGBGen from ent
			else if ( backEnd.currentEntity->e.renderfx & RF_RGB_TINT )
				forceRGBGen = CGEN_ENTITY;
		}

		tess_flags |= pStage->tessFlags;

		// refraction
#ifdef RF_DISTORTION
		if ( tess.shader->useDistortion == qtrue || backEnd.currentEntity->e.renderfx & RF_DISTORTION )
#else
		if ( tess.shader->useDistortion == qtrue )
#endif
		{
			is_refraction = qtrue;
		}

		for ( i = 0; i < pStage->numTexBundles; i++ ) {
			if ( pStage->bundle[i].image[0] != NULL )  {
				vk_select_texture( i );

				if ( backEnd.isGlowPass ) 
				{
					// use blackimage for non glow bundles during a glowPass
					if ( !pStage->bundle[i].glow ) 
					{
						vk_bind( tr.blackImage );
						Com_Memset( tess.svars.colors[i], 0xff, tess.numVertexes * 4 );
						continue;
					}

					// edge case: ensure tessflags bits are set, could be optimized out if equalTC or equalRGB in
					// tr_shader: try to avoid redundant per-stage computations.
					// could result in stale tc or rgb data.
					if ( stage && !tess.xstages[stage -1]->bundle[i].glow && !(tess_flags & TESS_ENV) )
						tess_flags |= TESS_RGBA0 | TESS_ST0;
				}

				R_BindAnimatedImage( &pStage->bundle[i] );

				if ( tess.vbo_model ) {
					vk_compute_colors( i, pStage, forceRGBGen );

					if ( is_refraction && i >= 1 )
						continue;

					vk_compute_tex_coords( &pStage->bundle[i], &uniform_global.bundle[i].tcMod, &uniform_global.bundle[i].tcGen );
					uniform_global.bundle[i].numTexMods = pStage->bundle[i].numTexMods;

					continue;
				}

				if ( tess_flags & (TESS_ST0 << i) )
					ComputeTexCoords(i, &pStage->bundle[i]);

				if ( (tess_flags & (TESS_RGBA0 << i)) || forceRGBGen )
					ComputeColors( i, tess.svars.colors[i], pStage, forceRGBGen );
			}
		}
	
		// reject this stage if it's not a glow stage but we are doing a glow pass.
		if ( backEnd.isGlowPass && !pStage->glow )
			continue;

		vk_select_texture( 0 );

		if ( r_lightmap->integer && pStage->bundle[1].isLightmap ) {
			//vk_select_texture(0);
			vk_bind( tr.whiteImage ); // replace diffuse texture with a white one thus effectively render only lightmap
		}

		if ( backEnd.viewParms.portalView == PV_MIRROR ) {
			pipeline = pStage->vk_mirror_pipeline[fog_stage];
		}
		else {
			pipeline = pStage->vk_pipeline[fog_stage];
		}

		Com_Memset( &def, 0, sizeof(Vk_Pipeline_Def) );

		// for 2D flipped images
		if ( backEnd.projection2D ) {
			if ( !pStage->vk_2d_pipeline ) {
				vk_get_pipeline_def(pStage->vk_pipeline[0], &def);

				// use an excisting pipeline with the same def or create a new one.
				def.face_culling = CT_TWO_SIDED;
				def.vk_light_flags = 0;
				tess.xstages[stage]->vk_2d_pipeline = vk_find_pipeline_ext(0, &def, qfalse);
			}

			pipeline = pStage->vk_2d_pipeline;
		}
		else if ( backEnd.currentEntity ) {
			if ( backEnd.viewParms.portalView == PV_MIRROR )
				vk_get_pipeline_def(pStage->vk_mirror_pipeline[fog_stage], &def);
			else{
				vk_get_pipeline_def(pStage->vk_pipeline[fog_stage], &def);
			}
			// we want to be able to rip a hole in the thing being disintegrated,
			// and by doing the depth-testing it avoids some kinds of artefacts, but will probably introduce others?
			if ( backEnd.currentEntity->e.renderfx & RF_DISINTEGRATE1 )
				def.state_bits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHMASK_TRUE | GLS_ATEST_GE_C0;

#ifdef RF_ALPHA_FADE
			// Single-player: fade the model out using the entity alpha,
			// whatever blend the shader asked for. Only while it is actually
			// translucent - at full alpha the shader's own blend is correct and
			// swapping it would cost a pipeline for nothing. The surfaces are
			// already sorted last by the post-render bit in the sort key, which
			// is what makes fading over the scene behind them work.
			if ( ( backEnd.currentEntity->e.renderfx & RF_ALPHA_FADE ) &&
				backEnd.currentEntity->e.shaderRGBA[3] < 255 )
			{
				def.state_bits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
			}
#endif

			// only force blend on the internal distortion shader
			if ( tess.shader == tr.distortionShader )
				def.state_bits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHMASK_TRUE;
			
			//want to use RGBGen from ent
			// "forceRGBGen override" requires +cl glsl shader, substitute if identity shader is set.
			if ( forceRGBGen && !(tess_flags & TESS_RGBA0) )
			{
				tess_flags |= TESS_RGBA0;
				def.shader_type = !pStage->mtEnv ? TYPE_SINGLE_TEXTURE : 
					( pStage->mtEnv3 ? TYPE_MULTI_TEXTURE_ADD3 : TYPE_MULTI_TEXTURE_ADD2);
			}

			// refraction
			if ( is_refraction ) 
			{
				def.shader_type = TYPE_REFRACTION;
				def.face_culling = CT_TWO_SIDED;
				tess_flags |= TESS_NNN;

				// The client's inverse blend. It was written for the alternate
				// saber trail, and it is the one thing in the old distortion
				// that is a blend mode rather than a number, so it stays one.
				if ( tr_distortionNegate )
					def.state_bits = GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE_MINUS_SRC_COLOR;
			}
			
			if ( backEnd.currentEntity->e.renderfx & RF_FORCE_ENT_ALPHA ) {
				ForceAlpha( (unsigned char *) tess.svars.colors, backEnd.currentEntity->e.shaderRGBA[3] );
				
				def.state_bits = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
				// depth write, so faces through the model will be stomped over by nearer ones. this works because
				// we draw RF_FORCE_ENT_ALPHA stuff after everything else, including standard alpha surfs.
#ifdef RF_ALPHA_DEPTH
				if ( backEnd.currentEntity->e.renderfx & RF_ALPHA_DEPTH ) 
					def.state_bits |= GLS_DEPTHMASK_TRUE;
#endif
			}

			def.vbo_ghoul2 = is_ghoul2_vbo;
			def.vbo_mdv = is_mdv_vbo;

			// sunny forgot why this is here.. @!&!%!
			if ( backEnd.currentEntity != &tr.worldEntity && !tess.vbo_model )
				def.vk_light_flags = 0;
		
			pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
		}

		// Whatever pipeline the stage arrived at, the pre-pass wants that exact
		// one with colour switched off. Pulling the def back out and re-resolving
		// - rather than building a depth pipeline from scratch - is what keeps
		// the vertex side identical: same shader type, same bindings, same
		// skinning, so vk_bind_geometry below feeds it what it expects and the
		// depth it writes is the depth the main pass will find.
		if ( backEnd.depthPrepass ) {
			vk_get_pipeline_def( pipeline, &def );
			def.depth_only = qtrue;
			pipeline = vk_find_pipeline_ext( 0, &def, qfalse );
		}

		if ( is_refraction )
		{
			// bind extracted color image copy / blit
			vk_update_descriptor( VK_DESC_TEXTURE0, vk.refraction_extract_descriptor );

			Com_Memset( &uniform.refraction, 0, sizeof(uniform.refraction) );

			if ( !tess.vbo_model )
				vk_compute_tex_coords( &pStage->bundle[0], &uniform.refraction.tcMod, &uniform.refraction.tcGen ); 

			// How far the ray is bent, in the units the surface is modelled in.
			// The base is what the shader used to arrive at on its own, so a
			// scale of one is the effect the renderer was already trying to
			// draw; the client's stretch multiplies it, and zero from the client
			// means it has no opinion.
			const float stretch = ( tr_distortionStretch != 0.0f ) ? tr_distortionStretch : 1.0f;

			uniform_global.refraction[0] = REFRACTION_BASE_THICKNESS * r_refractionScale->value * stretch;
			uniform_global.refraction[1] = tr_distortionAlpha;
			uniform_global.refraction[3] = r_refractionChromatic->value;

			// How blurred a rough surface may transmit. Only a surface that has
			// a physical map has a roughness worth reading - without one the map
			// is white, which reads as fully rough and would blur every pane of
			// glass in the game to nothing. Zero here is the shader's own switch
			// for "sample the sharp level and do not touch the physical map".
			// This is the PBR half of the two rendering modes; the non-PBR half
			// refracts sharply on purpose.
			if ( pStage->vk_pbr_flags & ( PBR_HAS_PHYSICALMAP | PBR_HAS_SPECULARMAP ) )
				uniform_global.refraction[2] = (float)( vk.refraction_extract_mips - 1 );
			else
				uniform_global.refraction[2] = 0.0f;
		}

		VectorCopy4( pStage->normalScale, uniform_global.normalScale );
		VectorCopy4( pStage->specularScale, uniform_global.specularScale );

		// removed glow handling statement, this results in unnecessary glow pass binds?
		{
			qboolean has_cubemap = ( !vk.useFastLight && tr.numCubemaps && tess.cubemapIndex > 0) ? qtrue : qfalse;

			if ( def.vk_light_flags )
				vk_update_pbr_descriptor_raw( VK_DESC_PBR_BRDFLUT_BINDING, &vk.brdflut_descriptor_info );

			if ( pStage->vk_pbr_flags & PBR_HAS_NORMALMAP )
				vk_update_pbr_descriptor( VK_DESC_PBR_NORMAL_BINDING, pStage->normalMap );

			if ( pStage->vk_pbr_flags & PBR_HAS_PHYSICALMAP || pStage->vk_pbr_flags & PBR_HAS_SPECULARMAP )
				vk_update_pbr_descriptor( VK_DESC_PBR_PHYSICAL_BINDING, pStage->physicalMap );
			else
			{
				vk_update_pbr_descriptor( VK_DESC_PBR_PHYSICAL_BINDING, tr.whiteImage );
				
				uniform_global.specularScale[0] = 0.0f;
				uniform_global.specularScale[2] =
				uniform_global.specularScale[3] = 1.0f;
				uniform_global.specularScale[1] = 0.5f;
			}

			if ( !has_cubemap || backEnd.viewParms.targetCube != nullptr )
				vk_update_pbr_descriptor( VK_DESC_PBR_CUBEMAP_BINDING, tr.emptyCubemap );
			else 	
				vk_update_pbr_descriptor( VK_DESC_PBR_CUBEMAP_BINDING, tr.cubemaps[tess.cubemapIndex-1].prefiltered_image );
		
			if ( def.vk_light_flags & LIGHTDEF_USE_LIGHTMAP && def.vk_pbr_flags & PBR_HAS_DELUXEMAP )
				vk_update_pbr_descriptor( VK_DESC_PBR_DELUXE_BINDING, pStage->bundle[1].deluxeMap );
			else
				vk_update_pbr_descriptor( VK_DESC_PBR_DELUXE_BINDING, tr.whiteImage );
		}

		Vk_Depth_Range depthRange = tess.depthRange;

#ifdef USE_VK_IMGUI
		// ImGui outline surface/shader 
		if ( tess.shader == tr.outlineShader ) {
			pipeline = vk.std_pipeline.inspector_object_debug_pipeline;
			depthRange = DEPTH_RANGE_ZERO;
		}
#endif

		vk_bind_geometry( tess_flags );

		// create draw item
		{
			DrawItem item = {};
			item.pipeline = pipeline;
			item.pipeline_layout = vk.pipeline_layout;
			item.depthRange = depthRange;
			item.polygonOffset = tess.shader->polygonOffset;
			item.identifier = 23;
			item.reset_uniform = qtrue;

			{
				if ( fogCollapse ) {
					VectorCopy( backEnd.ori.viewOrigin, uniform.eyePos );
					vk_push_uniform( &uniform );
					vk_update_descriptor( VK_DESC_FOG_COLLAPSE, tr.fogImage->descriptor_set );

					item.reset_uniform = qfalse;
				}
				else if ( tess_flags & TESS_VPOS ) {
					VectorCopy( backEnd.ori.viewOrigin, uniform.eyePos );
					vk_push_uniform( &uniform );
					tess_flags &= ~TESS_VPOS;

					item.reset_uniform = qfalse;
				}
				else if ( is_refraction ) {
					vk_push_uniform( &uniform );
					item.reset_uniform = qfalse;
				}

				vk_push_uniform_global( &uniform_global );
			}

			RB_AddDrawItemIndexBinding( item );
			RB_AddDrawItemVertexBinding( item );
			RB_AddDrawItemUniformBinding( item, backEnd.currentEntity );

			RB_AddDrawItem( backEndData->currentPass, item );
		}

		// allow skipping out to show just lightmaps during development
		if ( r_lightmap->integer && ( pStage->bundle[0].isLightmap || pStage->bundle[1].isLightmap ) )
			break;

		tess_flags = 0;

		// One stage is the whole of the pre-pass. Later stages add colour to a
		// surface whose extent the first one already fixed, so drawing them
		// would write the same depth a second time.
		if ( backEnd.depthPrepass ) {
			break;
		}
	}

	if (tess_flags) // fog-only shaders?
		vk_bind_geometry(tess_flags);

	// now do fog
	if ( !backEnd.depthPrepass &&
		tr.world && r_drawfog->value && tess.fogNum && tess.shader->fogPass && !fogCollapse ) {
		RB_FogPass();
	}

	// skip surfacesprites, not rendered anyway
	return;

	// Now check for surfacesprites.
	if ( r_surfaceSprites->integer && !vk.vboWorldActive )
	{
		qboolean ssFound = qfalse;

		for (stage = 1; stage < tess.shader->numUnfoggedPasses; stage++)
		{
			pStage = tess.xstages[stage];

			if ( !pStage || !pStage->ss || !pStage->ss->type )
				continue;

			if (!ssFound) {
				// don't cringe, this is a temporary solution. but slow..
				// we are still reading from tess.xyz while also writing a group of surfacesprites to it.
				// which means the next group will read from garbaged surface data.
				// we duplicate the necessary tess data to ssInput and use that to read from.
				// yeah ..
				// surfacesprites currently don't work with vbo enabled.
				// need to look at the the methods from OpenJK repo

				ssInput.numIndexes = tess.numIndexes;
				ssInput.numVertexes = tess.numVertexes;

				memcpy(ssInput.indexes, tess.indexes, sizeof(tess.indexes));
				memcpy(ssInput.xyz, tess.xyz, sizeof(tess.xyz));
				memcpy(ssInput.normal, tess.normal, sizeof(tess.normal));
				memcpy(ssInput.vertexColors, tess.vertexColors, sizeof(tess.vertexColors));

				ssFound = qtrue;
			}

			// Draw the surfacesprite
			RB_DrawSurfaceSprites( pStage, &ssInput );
		}
	}
}
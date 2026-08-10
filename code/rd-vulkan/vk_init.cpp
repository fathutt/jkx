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

#include <stdio.h>
#include "tr_local.h"
#include <algorithm>
#include "tr_common.h"
#include "tr_WorldEffects.h"
#include "qcommon/MiniHeap.h"
#include "ghoul2/g2_local.h"

int vkSamples = VK_SAMPLE_COUNT_1_BIT;
int vkMaxSamples = VK_SAMPLE_COUNT_1_BIT;

static void vk_set_render_scale( void )
{
	if (gls.windowWidth != glConfig.vidWidth || gls.windowHeight != glConfig.vidHeight)
	{
		if (r_renderScale->integer > 0)
		{
			int scaleMode = r_renderScale->integer - 1;
			if (scaleMode & 1)
			{
				// preserve aspect ratio (black bars on sides)
				float windowAspect = (float)gls.windowWidth / (float)gls.windowHeight;
				float renderAspect = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
				if (windowAspect >= renderAspect)
				{
					float scale = (float)gls.windowHeight / (float)glConfig.vidHeight;
					int bias = (gls.windowWidth - scale * (float)glConfig.vidWidth) / 2;
					vk.blitX0 += bias;
				}
				else
				{
					float scale = (float)gls.windowWidth / (float)glConfig.vidWidth;
					int bias = (gls.windowHeight - scale * (float)glConfig.vidHeight) / 2;
					vk.blitY0 += bias;
				}
			}
			// linear filtering
			if (scaleMode & 2)
				vk.blitFilter = GL_LINEAR;
			else
				vk.blitFilter = GL_NEAREST;
		}

		vk.windowAdjusted = qtrue;
	}

	if (r_ext_supersample->integer && !r_renderScale->integer)
	{
		vk.blitFilter = GL_LINEAR;
	}
}

void get_viewport_rect( VkRect2D *r )
{
	if (backEnd.projection2D)
	{
		r->offset.x = 0;
		r->offset.y = 0;
		r->extent.width = vk.renderWidth;
		r->extent.height = vk.renderHeight;
	}
	else
	{
		r->offset.x = backEnd.viewParms.viewportX * vk.renderScaleX;
		r->offset.y = vk.renderHeight - (backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight) * vk.renderScaleY;
		r->extent.width = (float)backEnd.viewParms.viewportWidth * vk.renderScaleX;
		r->extent.height = (float)backEnd.viewParms.viewportHeight * vk.renderScaleY;
	}
}

void get_viewport( VkViewport *viewport, Vk_Depth_Range depth_range ) {
	VkRect2D r;

	get_viewport_rect(&r);

	viewport->x = (float)r.offset.x;
	viewport->y = (float)r.offset.y;
	viewport->width = (float)r.extent.width;
	viewport->height = (float)r.extent.height;

	switch (depth_range) {
		default:
#ifdef USE_REVERSED_DEPTH
		//case DEPTH_RANGE_NORMAL:
			viewport->minDepth = 0.0f;
			viewport->maxDepth = 1.0f;
			break;
		case DEPTH_RANGE_ZERO:
			viewport->minDepth = 1.0f;
			viewport->maxDepth = 1.0f;
			break;
		case DEPTH_RANGE_ONE:
			viewport->minDepth = 0.0f;
			viewport->maxDepth = 0.0f;
			break;
		case DEPTH_RANGE_WEAPON:
			viewport->minDepth = 0.6f;
			viewport->maxDepth = 1.0f;
			break;
#else
		//case DEPTH_RANGE_NORMAL:
			viewport->minDepth = 0.0f;
			viewport->maxDepth = 1.0f;
			break;
		case DEPTH_RANGE_ZERO:
			viewport->minDepth = 0.0f;
			viewport->maxDepth = 0.0f;
			break;
		case DEPTH_RANGE_ONE:
			viewport->minDepth = 1.0f;
			viewport->maxDepth = 1.0f;
			break;
		case DEPTH_RANGE_WEAPON:
			viewport->minDepth = 0.0f;
			viewport->maxDepth = 0.3f;
			break;
#endif
	}
}

// The single-player scissor rectangle, in render-target pixels, or inactive.
// It lives here because get_scissor_rect below is its only reader, and it is
// cleared on every entry into 2D - see vk_set_2d.
static qboolean	vk_scissor2DActive = qfalse;
static VkRect2D	vk_scissor2D;

void vk_clear_2d_scissor( void ) {
	vk_scissor2DActive = qfalse;
}

// Virtual 640x480 in, render-target pixels out: the same mapping the 2D
// projection applies to everything else the client draws with these numbers.
void vk_set_2d_scissor( float x, float y, float w, float h ) {
	if ( x < 0.0f ) {
		vk_scissor2DActive = qfalse;
		return;
	}

	// The same mapping the 2D projection uses, or the scissor clips a different
	// rectangle from the one the caller can see - which on a wide screen means
	// the menu is cut off inside its own frame.
	float vx, vy, vw, vh, virtualW;
	vk_get_2d_viewport( &vx, &vy, &vw, &vh, &virtualW );

	const float sx = vw / virtualW;
	const float sy = vh / (float)SCREEN_HEIGHT;

	int x0 = (int)( vx + x * sx );
	int y0 = (int)( vy + y * sy );
	int x1 = (int)( vx + ( x + w ) * sx );
	int y1 = (int)( vy + ( y + h ) * sy );

	const int maxX = (int)vk.renderWidth;
	const int maxY = (int)vk.renderHeight;

	x0 = ( x0 < 0 ) ? 0 : ( ( x0 > maxX ) ? maxX : x0 );
	x1 = ( x1 < x0 ) ? x0 : ( ( x1 > maxX ) ? maxX : x1 );
	y0 = ( y0 < 0 ) ? 0 : ( ( y0 > maxY ) ? maxY : y0 );
	y1 = ( y1 < y0 ) ? y0 : ( ( y1 > maxY ) ? maxY : y1 );

	vk_scissor2D.offset.x = x0;
	vk_scissor2D.offset.y = y0;
	vk_scissor2D.extent.width = (uint32_t)( x1 - x0 );
	vk_scissor2D.extent.height = (uint32_t)( y1 - y0 );
	vk_scissor2DActive = qtrue;
}

void get_scissor_rect( VkRect2D *r ) {

	// Only 2D drawing is clipped: the client sets this between pictures, and a
	// 3D view arriving in the middle of a 2D block has its own scissor.
	if (backEnd.projection2D && vk_scissor2DActive)
	{
		*r = vk_scissor2D;
		return;
	}

	if (backEnd.viewParms.portalView != PV_NONE)
	{
		r->offset.x = backEnd.viewParms.scissorX;
		r->offset.y = glConfig.vidHeight - backEnd.viewParms.scissorY - backEnd.viewParms.scissorHeight;
		r->extent.width = backEnd.viewParms.scissorWidth;
		r->extent.height = backEnd.viewParms.scissorHeight;
	}
	else
	{
		get_viewport_rect(r);

		if (r->offset.x < 0)
			r->offset.x = 0;
		if (r->offset.y < 0)
			r->offset.y = 0;

		if (r->offset.x + r->extent.width > glConfig.vidWidth)
			r->extent.width = glConfig.vidWidth - r->offset.x;
		if (r->offset.y + r->extent.height > glConfig.vidHeight)
			r->extent.height = glConfig.vidHeight - r->offset.y;
	}
}

static void vk_render_splash( void )
{
	VkCommandBufferBeginInfo	begin_info;
	VkSubmitInfo				submit_info;
	VkPresentInfoKHR			present_info;
	VkPipelineStageFlags		wait_dst_stage_mask;
	VkImage						imageBuffer;
	image_t						*splashImage;
	VkImageBlit					imageBlit;
	float						ratio;

	ratio = ( (float)( SCREEN_WIDTH * glConfig.vidHeight ) / (float)( SCREEN_HEIGHT * glConfig.vidWidth ) );

	if ( cl_ratioFix->integer && ratio >= 0.74f && ratio <= 0.76f ){
		splashImage = R_FindImageFile( "menu/splash_16_9", IMGFLAG_CLAMPTOEDGE, 0 );

		if ( !splashImage ){
			splashImage = R_FindImageFile( "menu/splash", IMGFLAG_CLAMPTOEDGE, 0 );
		}
	}
	else{
		splashImage = R_FindImageFile( "menu/splash", IMGFLAG_CLAMPTOEDGE, 0 );
	}

	if( !splashImage ){
		return;
	}

	//VK_CHECK( vkWaitForFences( vk.device, 1, &vk.cmd->rendering_finished_fence, VK_TRUE, 1e10 ) );
	//VK_CHECK( vkResetFences( vk.device, 1, &vk.cmd->rendering_finished_fence ) );

#ifdef USE_UPLOAD_QUEUE
	vk_flush_staging_buffer( qfalse );
#endif

	vkAcquireNextImageKHR( vk.device, vk.swapchain, 1 * 1000000000ULL, vk.cmd->image_acquired, VK_NULL_HANDLE, &vk.cmd->swapchain_image_index );
	imageBuffer = vk.swapchain_images[vk.cmd->swapchain_image_index];

	// begin the command buffer
	Com_Memset( &begin_info, 0, sizeof(begin_info) );
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.pNext = NULL;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	begin_info.pInheritanceInfo = NULL;
	VK_CHECK( vkBeginCommandBuffer( vk.cmd->command_buffer, &begin_info ) );

	vk_record_image_layout_transition( vk.cmd->command_buffer, splashImage->handle, VK_IMAGE_ASPECT_COLOR_BIT, 
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		0, 0 );

	vk_record_image_layout_transition( vk.cmd->command_buffer, imageBuffer, VK_IMAGE_ASPECT_COLOR_BIT, 
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		0, 0 );

	Com_Memset( &imageBlit, 0, sizeof(imageBlit) );
	imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageBlit.srcSubresource.mipLevel = 0;
	imageBlit.srcSubresource.baseArrayLayer = 0;
	imageBlit.srcSubresource.layerCount = 1;
	imageBlit.srcOffsets[0] = { 0, 0, 0 };
	imageBlit.srcOffsets[1] = { splashImage->width, splashImage->height, 1 };
	imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageBlit.dstSubresource.mipLevel = 0;
	imageBlit.dstSubresource.baseArrayLayer = 0;
	imageBlit.dstSubresource.layerCount = 1;
	imageBlit.dstOffsets[0] = { vk.blitX0, vk.blitY0, 0 };
	imageBlit.dstOffsets[1] = { ( gls.windowWidth - vk.blitX0 ), ( gls.windowHeight - vk.blitY0 ), 1 };

	vkCmdBlitImage( vk.cmd->command_buffer, splashImage->handle,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, imageBuffer,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
		&imageBlit, VK_FILTER_LINEAR );

	vk_record_image_layout_transition( vk.cmd->command_buffer, imageBuffer, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		0, 0 );

	// we can end the command buffer now
	VK_CHECK( vkEndCommandBuffer( vk.cmd->command_buffer ) );

	wait_dst_stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT;

	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pNext = NULL;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &vk.cmd->command_buffer;
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = &vk.cmd->image_acquired;
	submit_info.pWaitDstStageMask = &wait_dst_stage_mask;
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = &vk.swapchain_rendering_finished[ vk.cmd->swapchain_image_index ];
	VK_CHECK( vkQueueSubmit( vk.queue, 1, &submit_info, VK_NULL_HANDLE ) );

	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.pNext = NULL;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = &vk.swapchain_rendering_finished[ vk.cmd->swapchain_image_index ];
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &vk.swapchain;
	present_info.pImageIndices = &vk.cmd->swapchain_image_index;
	present_info.pResults = NULL;
	vkQueuePresentKHR( vk.queue, &present_info );
	//VK_CHECK( vkResetFences( vk.device, 1, &vk.cmd->rendering_finished_fence ) );
	return;
}

void vk_set_clearcolor( void ) {
	vec4_t clr = { 0.75, 0.75, 0.75, 1.0 };

	if ( r_fastsky->integer ) 
	{
		vec4_t *out;

		switch( r_fastsky->integer ){
			case 1: out = &colorBlack; break;
			case 2: out = &colorRed; break;
			case 3: out = &colorGreen; break;
			case 4: out = &colorBlue; break;
			case 5: out = &colorYellow; break;
			case 6: out = &colorOrange; break;
			case 7: out = &colorMagenta; break;
			case 8: out = &colorCyan; break;
			case 9: out = &colorWhite; break;
			case 10: out = &colorLtGrey; break;
			case 11: out = &colorMdGrey; break;
			case 12: out = &colorDkGrey; break;
			case 13: out = &colorLtBlue; break;
			case 14: out = &colorDkBlue; break;
			default: out = &colorBlack;
		}

		Com_Memcpy(  tr.clearColor, *out, sizeof( vec4_t ) );
		return;
	}

	if ( tr.world && tr.world->globalFog != -1 ) 
	{
		const fog_t	*fog = &tr.world->fogs[tr.world->globalFog];
		Com_Memcpy(clr, (float*)fog->color, sizeof(vec3_t));
	}

	Com_Memcpy( tr.clearColor, clr, sizeof( vec4_t ) );
}

void vk_create_window( void ) {
	//R_Set2DRatio();

	if (glConfig.vidWidth == 0)
	{
		windowDesc_t windowDesc = { GRAPHICS_API_VULKAN };

		glConfig.deviceSupportsGamma = qfalse;
		vidWindow = WIN_Init(&windowDesc, &glConfig);

		if (r_ignorehwgamma->integer)
			glConfig.deviceSupportsGamma = qfalse;

		gls.windowWidth = glConfig.vidWidth;
		gls.windowHeight = glConfig.vidHeight;

		// The width of the head-up display's 2D space, in the same units its
		// height has always been measured in. Taken from the window and not
		// from the render target: r_renderScale can make those different
		// shapes, and what an element pinned to the right edge is pinned to is
		// the edge of the monitor.
		glConfig.virtualWidth = ( gls.windowHeight > 0 )
			? (float)SCREEN_HEIGHT * (float)gls.windowWidth / (float)gls.windowHeight
			: (float)SCREEN_WIDTH;

		gls.captureWidth = glConfig.vidWidth;
		gls.captureHeight = glConfig.vidHeight;

		//CL_SetScaling(1.0, glConfig.vidWidth, glConfig.vidHeight);	// consolefont and avi capture

		{
			if (r_renderScale->integer){
				glConfig.vidWidth = r_renderWidth->integer;
				glConfig.vidHeight = r_renderHeight->integer;
			}

			gls.captureWidth = glConfig.vidWidth;
			gls.captureHeight = glConfig.vidHeight;
		
			//CL_SetScaling(1.0, gls.captureWidth, gls.captureHeight);	// consolefont and avi capture

			if (r_ext_supersample->integer){
				glConfig.vidWidth *= 2;
				glConfig.vidHeight *= 2;

				//CL_SetScaling(2.0, gls.captureWidth, gls.captureHeight);	// consolefont and avi capture
			}
		}

		vk_initialize();

		gls.initTime = Sys_Milliseconds2();
	}

	if ( !vk.active && vk.instance ){
		// might happen after REF_KEEP_WINDOW
		vk_initialize();
		gls.initTime = Sys_Milliseconds2();
	}
	if ( vk.active ) {
		vk_init_descriptors();
	}
	else {
		Com_Error( ERR_FATAL, "Recursive error during Vulkan initialization" );
	}

	glState.glStateBits = GLS_DEPTHTEST_DISABLE | GLS_DEPTHMASK_TRUE;

	tr.inited = qtrue;
}

static void vk_initTextureCompression( void )
{
	if ( r_ext_compressed_textures->integer )
	{
		VkFormatProperties formatProps;
		vkGetPhysicalDeviceFormatProperties( vk.physical_device, VK_FORMAT_BC3_UNORM_BLOCK, &formatProps );
		if ( formatProps.linearTilingFeatures && formatProps.optimalTilingFeatures )
		{
			vk.compressed_format = VK_FORMAT_BC3_UNORM_BLOCK; //GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
		}
	}
}

void vk_initialize( void )
{
	VkPhysicalDeviceProperties props;
	uint32_t i;

	// First, so a fault anywhere below is reported rather than guessed at.
	vk_install_crash_handler();

	vk_init_library();

	vkGetDeviceQueue( vk.device, vk.queue_family_index, 0, &vk.queue );

	vk_get_vulkan_properties(&props);

	// Command buffer
	vk.cmd_index = 0;
	//vk.cmd = vk.tess + vk.cmd_index;
	vk.cmd = &vk.tess[vk.cmd_index];

	// Memory alignment
	vk.uniform_alignment = props.limits.minUniformBufferOffsetAlignment;
	vk.uniform_item_size		= PAD( sizeof(vkUniform_t),			(size_t)vk.uniform_alignment );
	vk.uniform_camera_item_size = PAD( sizeof(vkUniformCamera_t),	(size_t)vk.uniform_alignment );
	vk.uniform_light_item_size	= PAD( sizeof(vkUniformLight_t),	(size_t)vk.uniform_alignment );
	vk.uniform_entity_item_size = PAD( sizeof(vkUniformEntity_t),	(size_t)vk.uniform_alignment );
	vk.uniform_global_item_size = PAD( sizeof(vkUniformGlobal_t),	(size_t)vk.uniform_alignment );
	vk.uniform_fogs_item_size	= PAD( sizeof(vkUniformFog_t),		(size_t)vk.uniform_alignment );

	vk.storage_alignment = MAX( props.limits.minStorageBufferOffsetAlignment, sizeof(uint32_t) ); //for flare visibility tests
	vk.surface_sprites_ssbo_item_size = PAD( sizeof(SurfaceSpriteBlock), (size_t)props.limits.minStorageBufferOffsetAlignment );

	vk.defaults.geometry_size = VERTEX_BUFFER_SIZE;
	vk.defaults.staging_size = STAGING_BUFFER_SIZE;

	// get memory size & defaults
	{
		VkPhysicalDeviceMemoryProperties props;
		VkDeviceSize maxDedicatedSize = 0;
		VkDeviceSize maxBARSize = 0;
		vkGetPhysicalDeviceMemoryProperties( vk.physical_device, &props );
		for ( i = 0; i < props.memoryTypeCount; i++ ) {
			if ( props.memoryTypes[i].propertyFlags == VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ) {
				maxDedicatedSize = props.memoryHeaps[props.memoryTypes[i].heapIndex].size;
			}
			else if ( props.memoryTypes[i].propertyFlags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ) {
				if ( maxDedicatedSize == 0 || props.memoryHeaps[props.memoryTypes[i].heapIndex].size > maxDedicatedSize ) {
					maxDedicatedSize = props.memoryHeaps[props.memoryTypes[i].heapIndex].size;
				}
			}
			if ( props.memoryTypes[i].propertyFlags == (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ) {
				maxBARSize = props.memoryHeaps[props.memoryTypes[i].heapIndex].size;
			}
			else if ( (props.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) == (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ) {
				if ( maxBARSize == 0 ) {
					maxBARSize = props.memoryHeaps[props.memoryTypes[i].heapIndex].size;
				}
			}
		}

		if ( maxDedicatedSize != 0 ) {
			CL_RefPrintf( PRINT_ALL, "...device memory size: %iMB\n", (int)((maxDedicatedSize + (1024 * 1024) - 1) / (1024 * 1024)) );
		}
		if ( maxBARSize != 0 ) {
			if ( maxBARSize >= 128 * 1024 * 1024 ) {
				// user larger buffers to avoid potential reallocations
				vk.defaults.geometry_size = VERTEX_BUFFER_SIZE_HI;
				vk.defaults.staging_size = STAGING_BUFFER_SIZE_HI;
			}
#ifdef _DEBUG
			CL_RefPrintf( PRINT_ALL, "...BAR memory size: %iMB\n", (int)((maxBARSize + (1024 * 1024) - 1) / (1024 * 1024)) );
#endif
		}
	}

	// maxTextureSize must not exceed IMAGE_CHUNK_SIZE
	glConfig.maxTextureSize = MIN( props.limits.maxImageDimension2D, log2pad( sqrtf( IMAGE_CHUNK_SIZE / 4 ), 0 ) );
	if ( glConfig.maxTextureSize > MAX_TEXTURE_SIZE )
		glConfig.maxTextureSize = MAX_TEXTURE_SIZE; // ResampleTexture() relies on that maximum

	// default chunk size, may be doubled on demand
	vk.image_chunk_size = IMAGE_CHUNK_SIZE; 

	// maxActiveTextures must not exceed MAX_TEXTURE_UNITS
	if ( props.limits.maxPerStageDescriptorSamplers != 0xFFFFFFFF )
		glConfig.maxActiveTextures = props.limits.maxPerStageDescriptorSamplers;
	else
		glConfig.maxActiveTextures = props.limits.maxBoundDescriptorSets;

	if ( glConfig.maxActiveTextures > MAX_TEXTURE_UNITS )
		glConfig.maxActiveTextures = MAX_TEXTURE_UNITS;

	vk.maxBoundDescriptorSets = props.limits.maxBoundDescriptorSets;

	// How many descriptor sets the main pipeline layout will actually have. The
	// Vulkan minimum is eight and the full set is ten, so on a device at the
	// minimum - lavapipe, and some integrated parts - the layout is built with
	// four and everything past the third set does not exist. Recorded here, at
	// the limit it comes from, because two other places have to agree with it.
	vk.descriptorSetCount = ( vk.maxBoundDescriptorSets >= VK_DESC_COUNT ) ? VK_DESC_COUNT : 4;

	if ( r_ext_texture_env_add->integer != 0 )
		glConfig.textureEnvAddAvailable = qtrue;
	else
		glConfig.textureEnvAddAvailable = qfalse;

	vk.maxAnisotropy = props.limits.maxSamplerAnisotropy;
	vk.maxLod = 1 + Q_log2( glConfig.maxTextureSize );

	// Two fields single-player's glconfig_t has and multiplayer's does not.
	// Nothing outside the renderer reads either one today, but glconfig is a
	// struct the engine and the gamecode share: leaving fields uninitialised in
	// it is how a value that is never wrong becomes a value that is wrong once.
	glConfig.textureFilterAnisotropicAvailable = vk.samplerAnisotropy;
	// Vulkan has separate front and back stencil state in core, so the
	// two-sided path the single-player renderer probes for glStencilOpSeparate
	// to enable is simply always available here.
	glConfig.doStencilShadowsInOneDrawcall = qtrue;

	CL_RefPrintf( PRINT_ALL, "\nVK_MAX_TEXTURE_SIZE: %d\n", glConfig.maxTextureSize );
	CL_RefPrintf( PRINT_ALL, "VK_MAX_TEXTURE_UNITS: %d\n", glConfig.maxActiveTextures );

	R_InitImageScratch();
	vk_initTextureCompression();

	vk.xscale2D = glConfig.vidWidth * ( 1.0 / 640.0 );
	vk.yscale2D = glConfig.vidHeight * ( 1.0 / 480.0 );

	vk.windowAdjusted = qfalse;
	vk.blitFilter = GL_NEAREST;
	vk.blitX0 = vk.blitY0 = 0;

	vk_set_render_scale();

	vk.fboActive = qtrue;

	// Latched: turning it off frees the extract attachment, and the internal
	// distortion shader is built differently without it.
	vk.refractionActive = r_refraction->integer ? qtrue : qfalse;

	vk.vboWorldActive = qtrue;
	vk.vboGhoul2Active = qtrue;
	vk.vboMdvActive = qtrue; 
	
	if ( r_normalMapping->integer )
		vk.normalMappingActive = qtrue;

	if ( r_specularMapping->integer )
		vk.specularMappingActive = qtrue;

	if ( ( !vk.normalMappingActive && !vk.specularMappingActive ) || vk.maxBoundDescriptorSets < 11 ) {
		vk.useFastLight = qtrue;
	}

#if defined(JKX_VK_TRACE)
	// So a report can state which package produced it. A trace build and a
	// plain release build are otherwise indistinguishable from the outside,
	// and one of them answers questions the other cannot.
	CL_RefPrintf( PRINT_ALL, "JKX: trace build, writing vk_log.log next to the executable\n" );
#endif

	// Say which lighting path is actually running. This used to be silent: on a
	// device reporting maxBoundDescriptorSets < 11 the whole PBR path switched
	// itself off without a word, so the renderer looked like it was doing PBR
	// while it was not. Silent degradation is exactly what
	// docs/CODING-STANDARDS.md section 8.2 forbids, and it is also what
	// tools/verify/verify.py keys on to tell you which path was measured.
	if ( vk.useFastLight ) {
		if ( vk.maxBoundDescriptorSets < 11 ) {
			CL_RefPrintf( PRINT_WARNING,
				"JKX: lighting path = fastlight (PBR OFF): this device reports maxBoundDescriptorSets %i, "
				"and the PBR path needs 11. Bindless removes this limit.\n", vk.maxBoundDescriptorSets );
		} else {
			CL_RefPrintf( PRINT_ALL,
				"JKX: lighting path = fastlight (PBR OFF): r_normalMapping and r_specularMapping are both off.\n" );
		}
	} else {
		CL_RefPrintf( PRINT_ALL, "JKX: lighting path = PBR (maxBoundDescriptorSets %i)\n",
			vk.maxBoundDescriptorSets );
	}

#ifdef VK_DLIGHT_GPU
	if ( !vk.useFastLight && r_dlightMethod->integer )
		vk.useGPUDLight = qtrue;
#endif

#ifdef VK_CUBEMAP
	if ( r_cubeMapping->integer )
		vk.cubemapActive = qtrue;
#endif

	//if (r_ext_multisample->integer && !r_ext_supersample->integer)
	if ( r_ext_multisample->integer )
		vk.msaaActive = qtrue;

	// MSAA
	vkMaxSamples = MIN( props.limits.sampledImageColorSampleCounts, props.limits.sampledImageDepthSampleCounts);

	if ( vk.msaaActive ) {
		VkSampleCountFlags mask = vkMaxSamples;
		vkSamples = MAX( log2pad( r_ext_multisample->integer, 1 ), VK_SAMPLE_COUNT_2_BIT );
		while ( vkSamples > mask )
			vkSamples >>= 1;
	}
	else {
		vkSamples = VK_SAMPLE_COUNT_1_BIT;
	}

	CL_RefPrintf( PRINT_ALL, "MSAA max: %dx, using %dx\n", vkMaxSamples, vkSamples );

	// Anisotropy
	CL_RefPrintf( PRINT_ALL, "Anisotropy max: %dx, using %dx\n\n", r_ext_max_anisotropy->integer, r_ext_texture_filter_anisotropic->integer );
		
	// Bloom
	if ( r_bloom->integer )
		vk.bloomActive = qtrue;

	// Dynamic glow
	if ( glConfig.maxActiveTextures >= 4 && r_DynamicGlow->integer )
		vk.dglowActive = qtrue;

	// "Hardware" fog mode
	vk.hw_fog = r_drawfog->integer == 2 ? 1 : 0;

	// Screenmap
	vk.screenMapSamples = MIN(vkMaxSamples, VK_SAMPLE_COUNT_4_BIT);
	vk.screenMapWidth = (float)glConfig.vidWidth / 16.0;
	vk.screenMapHeight = (float)glConfig.vidHeight / 16.0;	

	if ( vk.screenMapWidth < 4 )
		vk.screenMapWidth = 4;	
	
	if ( vk.screenMapHeight < 4 )
		vk.screenMapHeight = 4;

	// do early texture mode setup to avoid redundant descriptor updates in GL_SetDefaultState()
	vk.samplers.filter_min = -1;
	vk.samplers.filter_max = -1;
	vk_texture_mode( r_textureMode->string, qtrue );
	r_textureMode->modified = qfalse;

	// Before anything allocates. The geometry, indirect and storage buffers
	// below all go through VMA, so an allocator created after them is an
	// allocator that does not exist when they run - which cost an access
	// violation on the first hardware launch of this renderer.
	vk_create_allocator();

	vk_create_sync_primitives();
	vk_create_command_pool();
	vk_create_command_buffer();
	vk_create_descriptor_layout();
	vk_create_pipeline_layout();

	vk.geometry_buffer_size_new = vk.defaults.geometry_size;
	vk.indirect_buffer_size_new = sizeof(VkDrawIndexedIndirectCommand) * 1024 * 1024;
	vk_create_vertex_buffer( vk.geometry_buffer_size_new );
	vk_create_indirect_buffer( vk.indirect_buffer_size_new );
	vk_create_storage_buffer( &vk.storage, MAX_FLARES * vk.storage_alignment, "storage (flares)" );
	vk_create_shader_modules();

	vk_create_pipeline_cache();

#ifdef VK_COMPUTE_NORMALMAP
	vk_create_compute_normalmap_pipelines();
#endif
	vk.renderPassIndex = RENDER_PASS_MAIN; // default render pass
	vk.initSwapchainLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	vk_create_swapchain( vk.physical_device, vk.device, vk.surface, vk.present_format, &vk.swapchain );
	//vk_texture_mode( r_textureMode->string, qtrue );
	vk_render_splash();
	vk_create_attachments();
	vk_create_render_passes();
	vk_create_framebuffers();

#ifdef VK_CUBEMAP
	vk_create_cubemap_prefilter();
#endif

	// preallocate staging buffer?
	if ( vk.defaults.staging_size == STAGING_BUFFER_SIZE_HI ) {
		vk_alloc_staging_buffer( vk.defaults.staging_size );
	}

	vk.active = qtrue;
}

// Shutdown vulkan subsystem by releasing resources acquired by Vk_Instance.
void vk_shutdown( void )
{
    CL_RefPrintf( PRINT_ALL, "vk_shutdown()\n" );

	if (!vkQueuePresentKHR) {// not fully initialized
		goto __cleanup;
	}

	vk_destroy_framebuffers();
	vk_destroy_pipelines( qtrue ); // reset counter
	vk_destroy_render_passes();
	vk_destroy_attachments();
	vk_destroy_swapchain();
#ifdef VK_CUBEMAP	
	vk_destroy_cubemap_prefilter();
#endif

	vk_destroy_pipeline_cache();

	vkDestroyCommandPool(vk.device, vk.command_pool, NULL);

	vkDestroyDescriptorPool(vk.device, vk.descriptor_pool, NULL);

	vkDestroyDescriptorSetLayout(vk.device, vk.set_layout_sampler, NULL);
	vkDestroyDescriptorSetLayout(vk.device, vk.set_layout_uniform, NULL);
	vkDestroyDescriptorSetLayout(vk.device, vk.set_layout_storage, NULL);

	vkDestroyPipelineLayout(vk.device, vk.pipeline_layout, NULL);
	vkDestroyPipelineLayout(vk.device, vk.pipeline_layout_storage, NULL);
#ifdef USE_VBO_SS
	vkDestroyPipelineLayout(vk.device, vk.pipeline_layout_surface_sprite, NULL);
#endif
	vkDestroyPipelineLayout(vk.device, vk.pipeline_layout_post_process, NULL);
	vkDestroyPipelineLayout(vk.device, vk.pipeline_layout_blend, NULL);
#ifdef USE_VK_PBR
	vkDestroyPipelineLayout(vk.device, vk.pipeline_layout_brdflut, NULL);
#endif

#ifdef USE_VBO	
	vk_release_vbo();
	vk_release_model_vbo();
#endif

	vk_clean_staging_buffer();

	vk_release_geometry_buffers();
	vk_release_indirect_buffers();

	vk_destroy_samplers();

    vk_destroy_sync_primitives();
   
	// storage buffer
	vk_destroy_buffer_memory(&vk.storage.buffer, &vk.storage.allocation);

#ifdef USE_VBO_SS
	vk_clean_surface_sprites();
#endif

    vk_destroy_shader_modules();

	R_DestroyImageScratch();

	// Last, and it is the mirror of vk_create_allocator() being first. Every
	// release above goes through VMA, so an allocator destroyed before them is
	// an allocator they use after it is gone: the whole block of buffer
	// releases ran against freed memory and the process died on exit with an
	// access violation, after a clean run, with the log ending mid-shutdown.
	vk_destroy_allocator();

__cleanup:
	// Before the engine unloads this module, which it does on every
	// vid_restart and not only at exit.
	vk_remove_crash_handler();

	if (vk.device != VK_NULL_HANDLE) {
#ifdef USE_VK_OBJECT_TRACKER
		vk_dump_tracked_objects();
#endif // USE_VK_OBJECT_TRACKER
		vkDestroyDevice(vk.device, NULL);
	}
	if (vk.surface != VK_NULL_HANDLE)
		vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);

#ifdef USE_VK_VALIDATION
	#ifdef USE_DEBUG_REPORT
		if (vkDestroyDebugReportCallbackEXT && vk.debug_callback)
			vkDestroyDebugReportCallbackEXT(vk.instance, vk.debug_callback, NULL);
	#endif
	#ifdef USE_DEBUG_UTILS
		if (vkDestroyDebugUtilsMessengerEXT && vk.debug_utils_messenger)
			vkDestroyDebugUtilsMessengerEXT(vk.instance, vk.debug_utils_messenger, NULL);
	#endif
#endif

	if (vk.instance != VK_NULL_HANDLE)
		vkDestroyInstance(vk.instance, NULL);

	Com_Memset(&vk, 0, sizeof(vk));
	Com_Memset(&vk_world, 0, sizeof(vk_world));

	vk_deinit_library();
}
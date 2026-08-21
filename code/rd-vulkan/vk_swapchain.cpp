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

void vk_restart_swapchain( const char *funcname )
{
    uint32_t i;
    CL_RefPrintf( PRINT_WARNING, "%s(): restarting swapchain...\n", funcname );
    vk_debug( "Restarting swapchain \n" );

    vk_wait_idle();

    for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
        vkResetCommandBuffer( vk.tess[i].command_buffer, 0 );
    }

	if ( vk.useUploadQueue ) {
		vkResetCommandBuffer( vk.staging_command_buffer, 0 );
	}


    vk_destroy_pipelines(qfalse);
    vk_destroy_framebuffers();
    vk_destroy_render_passes();
    vk_destroy_attachments();
    vk_destroy_swapchain();
    vk_destroy_sync_primitives();
#ifdef VK_CUBEMAP	
    vk_destroy_cubemap_prefilter();
#endif

    vk_select_surface_format( vk.physical_device, vk.surface );
    vk_setup_surface_formats( vk.physical_device );

    vk_create_sync_primitives();
    vk_create_swapchain( vk.physical_device, vk.device, vk.surface, vk.present_format, &vk.swapchain );
    vk_create_attachments();
    vk_create_render_passes();
    vk_create_framebuffers();
#ifdef VK_CUBEMAP
    vk_create_cubemap_prefilter();
#endif
    vk_update_attachment_descriptors();
    vk_update_post_process_pipelines();

#ifdef VK_PBR_BRDFLUT
	vk_create_brfdlut();
#endif
}

/*
================
vk_restart_presentation

The swapchain and the things that point straight at it, and NOTHING ELSE.

vk_restart_swapchain above rebuilds every pipeline as well, and it has to: it is
the answer to a surface that went out of date, where the format or the extent
may have changed under everything.

A present-mode change is not that. The format is the same and the extent is the
same, so the render passes, the attachments and every pipeline built against
them are still valid. What has to go is the swapchain itself and the
framebuffers that name its image views. Measured on a software rasteriser, two
changes in one run: both inside the same second.

Kept separate from the general restart rather than added to it as a flag,
because the two answer different questions - "the surface is gone" and "the
player moved a setting". Anything that changes the format or the size belongs in
the other one.
================
*/
void vk_restart_presentation( const char *funcname )
{
    uint32_t i;

    CL_RefPrintf( PRINT_ALL, "%s(): rebuilding the swapchain only\n", funcname );
    vk_debug( "Rebuilding presentation\n" );

    vk_wait_idle();

    for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
        vkResetCommandBuffer( vk.tess[i].command_buffer, 0 );
        vk.tess[i].swapchain_image_acquired = qfalse;
    }

    // The sync primitives go with it, and leaving them behind is a HANG rather
    // than a subtle wrongness: a semaphore left signalled by an acquire whose
    // image is now gone gets signalled again by the next acquire, and a
    // per-slot fence that nothing will ever signal is waited on for ten seconds
    // a frame. vk_restart_swapchain destroys and recreates them for the same
    // reason. They are cheap; it is the pipelines that are not.
    vk_destroy_framebuffers();
    vk_destroy_swapchain();
    vk_destroy_sync_primitives();

    vk_create_sync_primitives();
    vk_create_swapchain( vk.physical_device, vk.device, vk.surface,
        vk.present_format, &vk.swapchain );
    vk_create_framebuffers();
}

static const char *vk_pmode_to_str( VkPresentModeKHR mode )
{
    static char buf[32];

    switch ( mode ) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:     return "IMMEDIATE";
        case VK_PRESENT_MODE_MAILBOX_KHR:       return "MAILBOX";
        case VK_PRESENT_MODE_FIFO_KHR:          return "FIFO";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:  return "FIFO_RELAXED";
        case VK_PRESENT_MODE_FIFO_LATEST_READY_EXT: return "FIFO_LATEST_READY";
        default: Com_sprintf(buf, sizeof(buf), "mode#%x", mode); return buf;
    };
}

void vk_create_swapchain( VkPhysicalDevice physical_device, VkDevice device, 
    VkSurfaceKHR surface, VkSurfaceFormatKHR surface_format, VkSwapchainKHR *swapchain ) 
{
    int                         v;
    VkImageViewCreateInfo       view;
    VkSurfaceCapabilitiesKHR    surface_caps;
    VkExtent2D                  image_extent;
    uint32_t                    present_mode_count, i, image_count;
    VkPresentModeKHR            present_mode, *present_modes;
    VkSwapchainCreateInfoKHR    desc;
    qboolean                    mailbox_supported = qfalse;
    qboolean                    immediate_supported = qfalse;
    qboolean                    fifo_relaxed_supported = qfalse;
 

    VK_CHECK( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( physical_device, surface, &surface_caps ) );

    image_extent = surface_caps.currentExtent;
    if ( image_extent.width == 0xffffffff && image_extent.height == 0xffffffff ) {
        image_extent.width = MIN( surface_caps.maxImageExtent.width, MAX( surface_caps.minImageExtent.width, (uint32_t)glConfig.vidWidth ) );
        image_extent.height = MIN( surface_caps.maxImageExtent.height, MAX( surface_caps.minImageExtent.height, (uint32_t)glConfig.vidHeight ) );
    }

    // Minimization can set the window size to 0 when a swapchain restart is triggered, which results in a GPU crash later.
	// Window resizing below the gls window size also results in the same issue, though of course that's not normally possible.
	// With this clamping, new frames still aren't displayed while the window is too small, but that shouldn't matter while
	// minimized. If windowed mode resizing is ever implemented later then something more dynamic needs to be setup anyway.
	if ( image_extent.width < gls.windowWidth) image_extent.width = gls.windowWidth;
	if ( image_extent.height < gls.windowHeight) image_extent.height = gls.windowHeight;

    vk.clearAttachment = qtrue;

    // determine present mode and swapchain image count
    VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( physical_device, surface, &present_mode_count, NULL ) );

    present_modes = (VkPresentModeKHR*)malloc( present_mode_count * sizeof( VkPresentModeKHR ) );
    //present_modes = (VkPresentModeKHR*)R_Z_Malloc(present_mode_count * sizeof(VkPresentModeKHR));
    VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( physical_device, surface, &present_mode_count, present_modes ) );

    CL_RefPrintf( PRINT_ALL, "----- Presentation modes -----\n" );

    for ( i = 0; i < present_mode_count; i++ ) {
        CL_RefPrintf( PRINT_ALL, " %s\n", vk_pmode_to_str( present_modes[i] ) );
        
        switch ( present_modes[i] ) {
            case VK_PRESENT_MODE_MAILBOX_KHR: mailbox_supported = qtrue; break;
            case VK_PRESENT_MODE_IMMEDIATE_KHR: immediate_supported = qtrue; break;
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR: fifo_relaxed_supported = qtrue; break;
            default: break;
        }
    }

    free( present_modes );

    if ( ( v = Cvar_VariableIntegerValue( "r_swapInterval" ) ) != 0 ) {
        if ( v == 3 && mailbox_supported )
            present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        else if ( v == 2 && fifo_relaxed_supported )
            present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        else
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
        image_count = MAX( MIN_SWAPCHAIN_IMAGES_FIFO, surface_caps.minImageCount );
    }
    else {
        if ( immediate_supported ) {
            present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            image_count = MAX( MIN_SWAPCHAIN_IMAGES_IMM, surface_caps.minImageCount );
        }
        else if ( mailbox_supported ) {
            present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            image_count = MAX( MIN_SWAPCHAIN_IMAGES_MAILBOX, surface_caps.minImageCount );
        }
        else if ( fifo_relaxed_supported ) {
            present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
            image_count = MAX( MIN_SWAPCHAIN_IMAGES_FIFO, surface_caps.minImageCount );
        }
        else {
            present_mode = VK_PRESENT_MODE_FIFO_KHR;
            image_count = MAX( MIN_SWAPCHAIN_IMAGES_FIFO, surface_caps.minImageCount );
        }
    }

    if (image_count < 2) {
        image_count = 2;
    }

    if ( surface_caps.maxImageCount == 0 && present_mode == VK_PRESENT_MODE_FIFO_KHR ) {
		image_count = MAX( MIN_SWAPCHAIN_IMAGES_FIFO_0, surface_caps.minImageCount );
	} else if ( surface_caps.maxImageCount > 0 ) {
        image_count = MIN( MIN( image_count, surface_caps.maxImageCount ), MAX_SWAPCHAIN_IMAGES );
    }

    CL_RefPrintf( PRINT_ALL, "selected presentation mode: %s, image count: %i\n", vk_pmode_to_str( present_mode ), image_count );

    // create swap chain
    desc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    desc.pNext = NULL;
    desc.flags = 0;
    desc.surface = surface;
    desc.minImageCount = image_count;
    desc.imageFormat = surface_format.format;
    desc.imageColorSpace = surface_format.colorSpace;
    desc.imageExtent = image_extent;
    desc.imageArrayLayers = 1;
    desc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    // The splash screen and the final present both blit into the swapchain
    // image, and a blit destination needs TRANSFER_DST. Without it validation
    // rejects every one of those transitions - correctly; the driver happens to
    // tolerate it here, which is exactly why it went unnoticed. Guarded because
    // the flag is optional per surface, though no desktop driver omits it.
    if ( surface_caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) {
        desc.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    desc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    desc.queueFamilyIndexCount = 0;
    desc.pQueueFamilyIndices = NULL;
    desc.preTransform = surface_caps.currentTransform;
    //desc.compositeAlpha = get_composite_alpha( surface_caps.supportedCompositeAlpha );
    desc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    desc.presentMode = present_mode;
    desc.clipped = VK_TRUE;
    desc.oldSwapchain = VK_NULL_HANDLE;

    VK_CHECK( vkCreateSwapchainKHR( device, &desc, NULL, swapchain ) );

    VK_CHECK( vkGetSwapchainImagesKHR( vk.device, vk.swapchain, &vk.swapchain_image_count, NULL ) );
    vk.swapchain_image_count = MIN(vk.swapchain_image_count, MAX_SWAPCHAIN_IMAGES );
    VK_CHECK( vkGetSwapchainImagesKHR( vk.device, vk.swapchain, &vk.swapchain_image_count, vk.swapchain_images ) );

    for ( i = 0; i < vk.swapchain_image_count; i++ ) {

        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.pNext = NULL;
        view.flags = 0;
        view.image = vk.swapchain_images[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = vk.present_format.format;
        view.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;

        VK_CHECK( vkCreateImageView( vk.device, &view, NULL, &vk.swapchain_image_views[i] ) );

        VK_SET_OBJECT_NAME( vk.swapchain_images[i], va( "swapchain image %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );
        VK_SET_OBJECT_NAME( vk.swapchain_image_views[i], va( "swapchain image %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT );
    }

	for ( i = 0; i < vk.swapchain_image_count; i++ ) {
		VkSemaphoreCreateInfo s;
		s.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		s.pNext = NULL;
		s.flags = 0;
		VK_CHECK( vkCreateSemaphore( vk.device, &s, NULL, &vk.swapchain_rendering_finished[i] ) );
		VK_SET_OBJECT_NAME( vk.swapchain_rendering_finished[i], va( "swapchain_rendering_finished semaphore %i", i ), VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT );
	}
#if 0
    if ( vk.initSwapchainLayout != VK_IMAGE_LAYOUT_UNDEFINED ) {
        VkCommandBuffer command_buffer = vk_begin_command_buffer();

        for (i = 0; i < vk.swapchain_image_count; i++) {
            vk_record_image_layout_transition( command_buffer, vk.swapchain_images[i], VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED,
                vk.initSwapchainLayout, 0, 0 );
        }
        
        vk_end_command_buffer( command_buffer, __func__ );
    }
#endif
	for ( i = 0; i < vk.swapchain_image_count; i++ ) {
		if ( vk.initSwapchainLayout != VK_IMAGE_LAYOUT_UNDEFINED ) {
			// The Vulkan spec states : Use of a presentable image must occur only after the image is returned by vkAcquireNextImageKHR,
			// and before it is released by vkQueuePresentKHR.
			// This includes transitioning the image layout and rendering commands(https ://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainKHR.html#_description)
			vk.swapchain_images_inited[i] = qfalse;
		} else {
			vk.swapchain_images_inited[i] = qtrue; // assume undefined layout
		}
	}
}

void vk_destroy_swapchain ( void ) {
    uint32_t i;

    // Views before the swapchain, because they are views ONTO its images and
    // the swapchain owns those.
    for ( i = 0; i < vk.swapchain_image_count; i++ ) {
        if ( vk.swapchain_image_views[i] != VK_NULL_HANDLE ) {
            vkDestroyImageView( vk.device, vk.swapchain_image_views[i], NULL );
            vk.swapchain_image_views[i] = VK_NULL_HANDLE;
        }
    }

    vkDestroySwapchainKHR( vk.device, vk.swapchain, NULL );

    // AND THE SEMAPHORES AFTER IT, WHICH IS THE WHOLE POINT OF SPLITTING THE
    // LOOP. These are the ones vkQueuePresentKHR waits on. A wait handed to the
    // presentation engine is not finished when the queue goes idle - vkDeviceWaitIdle
    // covers submitted work, not the display side - so between the last present
    // and the destruction of the swapchain there is a semaphore with a wait
    // outstanding on it. Destroying a semaphore in that state is undefined, and
    // destroying the swapchain is what retires the wait.
    //
    // Nothing here can see it: a software rasteriser has no presentation engine
    // to be behind, the validation layer models the present as complete, and the
    // local lanes shut down clean either way. The hardware dies inside
    // vkDestroyDevice, in the driver's presentation code, on quit and on
    // vid_restart alike. Whether this is that fault is not settled; that the
    // order was wrong is not in question, and it costs one loop to have it right.
    for ( i = 0; i < vk.swapchain_image_count; i++ ) {
        if ( vk.swapchain_rendering_finished[i] != VK_NULL_HANDLE ) {
            vkDestroySemaphore( vk.device, vk.swapchain_rendering_finished[i], NULL );
            vk.swapchain_rendering_finished[i] = VK_NULL_HANDLE;
        }
    }

    // And forgotten, which it was not. vk_restart_swapchain calls this and then
    // builds a new one; a handle left behind is one that a second teardown -
    // shutdown after a restart - hands to the driver again, and destroying a
    // swapchain twice is the driver walking memory it has already released.
    // Whether that is what the hardware is dying of is not yet known; that it is
    // wrong is not in question.
    vk.swapchain = VK_NULL_HANDLE;

    // The images belong to the swapchain and died with it. They were never ours
    // to destroy, which is why nothing above touches them - but leaving the
    // handles and the count in place leaves a loop bound that says there are
    // still images and an array of handles into a released object for it to
    // read. The count is what every loop in this file trusts.
    Com_Memset( vk.swapchain_images, 0, sizeof( vk.swapchain_images ) );
    Com_Memset( vk.swapchain_images_inited, 0, sizeof( vk.swapchain_images_inited ) );
    vk.swapchain_image_count = 0;
}
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

#include <string.h>
#include <stdlib.h>

#include "tr_local.h"
#include "rd-common/tr_public.h"

unsigned char s_intensitytable[256];
unsigned char s_gammatable[256];
unsigned char s_gammatable_linear[256];

// Vulkan API functions used by the renderer.





#ifdef USE_VK_VALIDATION
	#ifdef USE_DEBUG_REPORT
	#endif
	#ifdef USE_DEBUG_UTILS
	#endif
#endif


// These two used to be defined here as well as in volk, which owns every other
// entry point in this renderer. Two definitions of the same global: whichever
// one volkLoadDevice filled, the other stayed null, and which one a translation
// unit saw depended on its include order. The linker only complained once the
// single-player target linked volk.c into the same module.



#ifdef USE_VK_IMGUI
#endif


static char *Q_stradd( char *dst, const char *src )
{
    char c;
    while ((c = *src++) != '\0')
        *dst++ = c;
    *dst = '\0';
    return dst;
}

static qboolean vk_used_instance_extension( const char *ext )
{
    const char *u;

    // allow all VK_*_surface extensions
    u = strrchr(ext, '_');
    if (u && Q_stricmp(u + 1, "surface") == 0)
        return qtrue;

	if ( Q_stricmp( ext, VK_KHR_DISPLAY_EXTENSION_NAME ) == 0 )
		return qtrue; // needed for KMSDRM instances/devices?

    if (Q_stricmp(ext, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
        return qtrue;

#ifdef USE_VK_VALIDATION
	#ifdef USE_DEBUG_REPORT
		if (Q_stricmp(ext, VK_EXT_DEBUG_REPORT_EXTENSION_NAME) == 0)
			return qtrue;
	#endif
	#ifdef USE_DEBUG_UTILS
		if (Q_stricmp(ext, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
			return qtrue;
	#endif
#endif

    if (Q_stricmp(ext, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
        return qtrue;

	if ( Q_stricmp( ext, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME ) == 0 )
		return qtrue;

	if ( Q_stricmp( ext, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME ) == 0 )
		return qtrue;

    return qfalse;

}

static void vk_create_instance( void )
{
#ifdef USE_VK_VALIDATION
    const char *validation_layer_name_lunarg = "VK_LAYER_LUNARG_standard_validation";
    const char *validation_layer_name_khronos = "VK_LAYER_KHRONOS_validation"; // causes lower fps?
#endif
    VkResult result;
    VkApplicationInfo appInfo;
    VkInstanceCreateInfo desc;
	VkInstanceCreateFlags flags;
    VkExtensionProperties *extension_properties;
    uint32_t i, n, len, count, extension_count;
    const char **extension_names, *end;
	char *str;

	vk_debug("----- Create Instance -----\n");

    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext = NULL;
    appInfo.pApplicationName = "EternalJK";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Quake3";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	// JKX targets Vulkan 1.3: dynamic rendering, synchronization2, descriptor
	// indexing and timeline semaphores are all core there, so none of them needs
	// an extension dance. See the project's engine design roadmap section 3.
	appInfo.apiVersion = VK_API_VERSION_1_3;
	flags = 0;
    count = 0;
    extension_count = 0;

    VK_CHECK(vkEnumerateInstanceExtensionProperties(NULL, &count, NULL));
    assert(count > 0);

    extension_properties = (VkExtensionProperties*)malloc(sizeof(VkExtensionProperties) * count);
    extension_names = (const char**)malloc(sizeof(char*) * (count));

    VK_CHECK(vkEnumerateInstanceExtensionProperties(NULL, &count, extension_properties));

	// fill vk.instance_extensions_string
	str = vk.instance_extensions_string; *str = '\0';
	end = &vk.instance_extensions_string[sizeof(vk.instance_extensions_string) - 1];

    for (i = 0; i < count; i++) {
        const char *ext = extension_properties[i].extensionName;

		// add the device extension to vk.instance_extensions_string
		{
			if (i != 0) {
				if (str + 1 >= end)
					continue;
				str = Q_stradd(str, " ");
			}
			len = (uint32_t)strlen(ext);
			if (str + len >= end)
				continue;
			str = Q_stradd(str, ext);
		}

        if (!vk_used_instance_extension(ext)) {
            continue;
        }

		// search for duplicates
        for (n = 0; n < extension_count; n++) {
            if (Q_stricmp(ext, extension_names[n]) == 0) {
                break;
            }
        }

        if (n != extension_count) {
            continue;
        }

        extension_names[extension_count++] = ext;

		if ( Q_stricmp( ext, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME ) == 0 ) {
			flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
		}

		CL_RefPrintf(PRINT_DEVELOPER, "instance extension: %s\n", ext);
    }

    // create instance
    desc.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    desc.pNext = NULL;
    desc.flags = flags;
    desc.pApplicationInfo = &appInfo;
    desc.enabledExtensionCount = extension_count;
    desc.ppEnabledExtensionNames = extension_names;

#ifdef USE_VK_VALIDATION
    desc.enabledLayerCount = 1;
    desc.ppEnabledLayerNames = &validation_layer_name_lunarg;

#ifdef USE_DEBUG_UTILS
	VkDebugUtilsMessengerCreateInfoEXT debug_utils_create_info;
	Com_Memset( &debug_utils_create_info, 0, sizeof(VkDebugUtilsMessengerCreateInfoEXT) );
	vk_create_debug_utils( debug_utils_create_info );

	desc.pNext = &debug_utils_create_info;
#endif

    result = vkCreateInstance(&desc, NULL, &vk.instance);

    if (result == VK_ERROR_LAYER_NOT_PRESENT) {

        desc.enabledLayerCount = 1;
        desc.ppEnabledLayerNames = &validation_layer_name_khronos;

        result = vkCreateInstance(&desc, NULL, &vk.instance);

        if (result == VK_ERROR_LAYER_NOT_PRESENT) {

            vk_debug("...validation layer is not available\n");

            // try without validation layer
            desc.enabledLayerCount = 0;
            desc.ppEnabledLayerNames = NULL;
#ifdef USE_DEBUG_UTILS
			desc.pNext = NULL;
#endif

            result = vkCreateInstance(&desc, NULL, &vk.instance);
        }
    }
#else
    desc.enabledLayerCount = 0;
    desc.ppEnabledLayerNames = NULL;

    result = vkCreateInstance(&desc, NULL, &vk.instance);
#endif

	// hotfix: reintroduce duplicate instance creation. 
	// mysterious x64-linux configuration causing a crash after vid_restart.
	result = vkCreateInstance(&desc, NULL, &vk.instance);

    switch (result) {
        case VK_SUCCESS:
            vk_debug("--- Vulkan create instance success! ---\n\n"); break;
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            vk_debug("The requested version of Vulkan is not supported by the driver.\n"); break;
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            vk_debug("Cannot find a specified extension library.\n"); break;
        default:
            vk_debug("%d, returned by vkCreateInstance.\n", result); break;
    }

    free((void*)extension_names);
    free(extension_properties);
}

static VkFormat get_hdr_format( VkFormat base_format )
{
    switch (r_hdr->integer) {
        case -1: 
            return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
        case 1: 
            return VK_FORMAT_R16G16B16A16_UNORM;
        default: 
            return base_format;
    }
}

static VkFormat get_depth_format( VkPhysicalDevice physical_device ) {
    VkFormatProperties props;
    VkFormat formats[2];
    uint32_t i;

    if ( glConfig.stencilBits > 0 ) {
        formats[0] = glConfig.depthBits == 16 ? VK_FORMAT_D16_UNORM_S8_UINT : VK_FORMAT_D24_UNORM_S8_UINT;
        formats[1] = VK_FORMAT_D32_SFLOAT_S8_UINT;
    }
    else {
        formats[0] = glConfig.depthBits == 16 ? VK_FORMAT_D16_UNORM : VK_FORMAT_X8_D24_UNORM_PACK32;
        formats[1] = VK_FORMAT_D32_SFLOAT;
    }

    for (i = 0; i < ARRAY_LEN(formats); i++) {
        vkGetPhysicalDeviceFormatProperties(physical_device, formats[i], &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return formats[i];
        }
    }

    Com_Error(ERR_FATAL, "get_depth_format: failed to find depth attachment format");
    return VK_FORMAT_UNDEFINED; // never get here
}

static qboolean vk_blit_enabled( VkPhysicalDevice physical_device, const VkFormat srcFormat, const VkFormat dstFormat )
{
    VkFormatProperties formatProps;

    vkGetPhysicalDeviceFormatProperties(physical_device, srcFormat, &formatProps);
	if ((formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) == 0) {
		return qfalse;
	}

    vkGetPhysicalDeviceFormatProperties(physical_device, dstFormat, &formatProps);
	if ((formatProps.linearTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0) {
		return qfalse;
	}

	return qtrue;
}

typedef struct {
	int bits;
	VkFormat rgb;
	VkFormat bgr;
} present_format_t;

static const present_format_t present_formats[] = {
	//{12, VK_FORMAT_B4G4R4A4_UNORM_PACK16, VK_FORMAT_R4G4B4A4_UNORM_PACK16},
	//{15, VK_FORMAT_B5G5R5A1_UNORM_PACK16, VK_FORMAT_R5G5B5A1_UNORM_PACK16},
	{16, VK_FORMAT_B5G6R5_UNORM_PACK16, VK_FORMAT_R5G6B5_UNORM_PACK16},
	{24, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM},
	{30, VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_A2R10G10B10_UNORM_PACK32},
	//{32, VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_FORMAT_B10G11R11_UFLOAT_PACK32}
};

static void get_present_format( int present_bits, VkFormat *bgr, VkFormat *rgb ) {
	const present_format_t *pf, *sel;
	int i;

	sel = NULL;
	pf = present_formats;
	for (i = 0; i < ARRAY_LEN(present_formats); i++, pf++) {
		if (pf->bits <= present_bits) {
			sel = pf;
		}
	}
	if (!sel) {
		*bgr = VK_FORMAT_B8G8R8A8_UNORM;
		*rgb = VK_FORMAT_R8G8B8A8_UNORM;
	}
	else {
		*bgr = sel->bgr;
		*rgb = sel->rgb;
	}
}

qboolean vk_select_surface_format( VkPhysicalDevice physical_device, VkSurfaceKHR surface )
{
	VkFormat base_bgr, base_rgb;
	VkFormat ext_bgr, ext_rgb;
    VkSurfaceFormatKHR *candidates;
    uint32_t format_count;
    VkResult result;

    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, NULL);
    if (result < 0) {
        CL_RefPrintf(PRINT_ERROR, "vkGetPhysicalDeviceSurfaceFormatsKHR returned error %i\n", result);
        return qfalse;
    }
 
    if (format_count == 0) {
        CL_RefPrintf(PRINT_ERROR, "...no surface formats found\n");
        return qfalse;
    }

    candidates = (VkSurfaceFormatKHR*)malloc(format_count * sizeof(VkSurfaceFormatKHR));

    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, candidates));

	get_present_format(24, &base_bgr, &base_rgb);
	get_present_format(r_presentBits->integer, &ext_bgr, &ext_rgb);

	if (format_count == 1 && candidates[0].format == VK_FORMAT_UNDEFINED) {
		// special case that means we can choose any format
		vk.base_format.format = base_bgr;
		vk.base_format.colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
		vk.present_format.format = ext_bgr;
		vk.present_format.colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
	}
	else {
		uint32_t i;
		for (i = 0; i < format_count; i++) {
			if ((candidates[i].format == base_bgr || candidates[i].format == base_rgb) && candidates[i].colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
				vk.base_format = candidates[i];
				break;
			}
		}
		if (i == format_count) {
			vk.base_format = candidates[0];
		}
		for (i = 0; i < format_count; i++) {
			if ((candidates[i].format == ext_bgr || candidates[i].format == ext_rgb) && candidates[i].colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
				vk.present_format = candidates[i];
				break;
			}
		}
		if (i == format_count) {
			vk.present_format = vk.base_format;
		}
	}

    free(candidates);

    return qtrue;
}

 void vk_setup_surface_formats( VkPhysicalDevice physical_device )
{
    vk.color_format		= get_hdr_format(vk.base_format.format);
    vk.depth_format		= get_depth_format(physical_device);
    vk.bloom_format		= vk.base_format.format;
    vk.capture_format	= VK_FORMAT_R8G8B8A8_UNORM;
    vk.blitEnabled		= vk_blit_enabled(physical_device, vk.color_format, vk.capture_format);

    if (!vk.blitEnabled)
        vk.capture_format = vk.color_format;
}

static qboolean vk_create_device( VkPhysicalDevice physical_device, int device_index ) {

#ifdef _DEBUG
	VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore;
	VkPhysicalDeviceVulkanMemoryModelFeatures memory_model;
	VkPhysicalDeviceBufferDeviceAddressFeatures devaddr_features;
	VkPhysicalDevice8BitStorageFeatures storage_8bit_features;
#endif

	CL_RefPrintf(PRINT_ALL, "selected physical device: %i\n\n", device_index);

	// The renderer records synchronization2 barriers and uses dynamic
	// rendering, both core in 1.3. Requesting VkPhysicalDeviceVulkan13Features
	// does not enforce anything on its own: a pre-1.3 driver walks the pNext
	// chain, does not recognise the sType, skips it, and returns VK_SUCCESS.
	// volk then resolves vkCmdPipelineBarrier2 to NULL and the first barrier
	// jumps to address zero. Reject the device here, where the reason can be
	// stated, instead of there, where it cannot.
	{
		VkPhysicalDeviceProperties device_props;
		vkGetPhysicalDeviceProperties( physical_device, &device_props );
		if ( device_props.apiVersion < VK_API_VERSION_1_3 ) {
			CL_RefPrintf( PRINT_ERROR, "...device %i reports Vulkan %i.%i, and this renderer needs 1.3\n",
				device_index,
				(int)VK_API_VERSION_MAJOR( device_props.apiVersion ),
				(int)VK_API_VERSION_MINOR( device_props.apiVersion ) );
			return qfalse;
		}
	}

	// select surface format
	if (!vk_select_surface_format(physical_device, vk.surface)) {
		return qfalse;
	}

	vk_setup_surface_formats(physical_device);

	// select queue family
	{
		VkQueueFamilyProperties* queue_families;
		uint32_t queue_family_count;
		uint32_t i;

		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, NULL);
		queue_families = (VkQueueFamilyProperties*)malloc(queue_family_count * sizeof(VkQueueFamilyProperties));
		vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families);

		// select queue family with presentation and graphics support
		vk.queue_family_index = ~0U;
		for (i = 0; i < queue_family_count; i++) {
			VkBool32 presentation_supported;
			VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, vk.surface, &presentation_supported));

			if (presentation_supported && (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
				vk.queue_family_index = i;
				break;
			}
		}

		free(queue_families);

		if (vk.queue_family_index == ~0U) {
			CL_RefPrintf(PRINT_ERROR, "...failed to find graphics queue family\n");

			return qfalse;
		}
	}

	// create VkDevice
	{
		char *str;
		const char *device_extension_list[9];
		uint32_t device_extension_count;
		const char *ext, *end;
		const float priority = 1.0;
		VkExtensionProperties *extension_properties;
		VkDeviceQueueCreateInfo queue_desc;
		VkPhysicalDeviceFeatures device_features;
		VkPhysicalDeviceFeatures features;
		VkDeviceCreateInfo device_desc;
		VkResult result;
		qboolean swapchainSupported = qfalse;
		qboolean dedicatedAllocation = qfalse;
		qboolean memoryRequirements2 = qfalse;
		qboolean debugMarker = qfalse;
		qboolean toolingInfo = qfalse;
	qboolean pushDescriptor = qfalse;
#ifdef _DEBUG
		qboolean timelineSemaphore = qfalse;
		qboolean memoryModel = qfalse;
		qboolean devAddrFeat = qfalse;
		qboolean storage8bit = qfalse;
		const void** pNextPtr;
#endif
		uint32_t i, len, count = 0;

		VK_CHECK(vkEnumerateDeviceExtensionProperties(physical_device, NULL, &count, NULL));
		extension_properties = (VkExtensionProperties*)malloc(count * sizeof(VkExtensionProperties));
		VK_CHECK(vkEnumerateDeviceExtensionProperties(physical_device, NULL, &count, extension_properties));

		// fill glConfig.extensions_string
		str = vk.device_extensions_string; *str = '\0';
		end = &vk.device_extensions_string[sizeof(vk.device_extensions_string) - 1];
		glConfig.extensions_string = (const char*)vk.device_extensions_string;

		for (i = 0; i < count; i++) {
			ext = extension_properties[i].extensionName;
			if ( strcmp( ext, VK_KHR_SWAPCHAIN_EXTENSION_NAME ) == 0 ) {
				swapchainSupported = qtrue;
			}
			else if (strcmp( ext, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME ) == 0 ) {
				dedicatedAllocation = qtrue;
			}
			else if (strcmp( ext, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME ) == 0 ) {
				memoryRequirements2 = qtrue;
			}
			else if (strcmp( ext, VK_EXT_DEBUG_MARKER_EXTENSION_NAME ) == 0 ) {
				debugMarker = qtrue; 
			}
			else if ( strcmp( ext, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME ) == 0 ) {
				pushDescriptor = qtrue;
			}
			else if ( strcmp( ext, VK_EXT_TOOLING_INFO_EXTENSION_NAME ) == 0 ) {
				toolingInfo = qtrue;
#ifdef _DEBUG
			} else if ( strcmp( ext, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME ) == 0 ) {
				timelineSemaphore = qtrue;
			} else if ( strcmp( ext, VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME ) == 0 ) {
				memoryModel = qtrue;
			} else if ( strcmp( ext, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME ) == 0 ) {
				devAddrFeat = qtrue;
			} else if ( strcmp( ext, VK_KHR_8BIT_STORAGE_EXTENSION_NAME ) == 0 ) {
				storage8bit = qtrue;
#endif
			}

			// add this device extension to glConfig
			if (i != 0) {
				if (str + 1 >= end)
					continue;
				str = Q_stradd(str, " ");
			}
			len = (uint32_t)strlen(ext);
			if (str + len >= end)
				continue;
			str = Q_stradd(str, ext);
		}

		free(extension_properties);

		device_extension_count = 0;

		if (!swapchainSupported) {
			CL_RefPrintf(PRINT_ERROR, "...required device extension is not available: %s\n", VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			return qfalse;
		}

		if (!memoryRequirements2)
			dedicatedAllocation = qfalse;
		else
			vk.dedicatedAllocation = dedicatedAllocation;

#ifndef USE_DEDICATED_ALLOCATION
		vk.dedicatedAllocation = qfalse;
#endif

		device_extension_list[device_extension_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

		if (vk.dedicatedAllocation) {
			device_extension_list[device_extension_count++] = VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME;
			device_extension_list[device_extension_count++] = VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME;
		}

		if (debugMarker) {
			device_extension_list[device_extension_count++] = VK_EXT_DEBUG_MARKER_EXTENSION_NAME;
			vk.debugMarkers = qtrue;
		}

		if ( toolingInfo )
			device_extension_list[device_extension_count++] = VK_EXT_TOOLING_INFO_EXTENSION_NAME;

#ifdef USE_VK_PBR
		// What lets the five physically-based textures live in one descriptor
		// set instead of five, and so what decides whether the PBR path can run
		// on a device that reports the common limit of eight bound sets. Every
		// desktop driver has had it for years and it is core in Vulkan 1.4;
		// where it is missing, vk_initialize says so and falls back to
		// fastlight rather than binding sets that are not in the layout.
		if ( pushDescriptor ) {
			device_extension_list[device_extension_count++] = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
			vk.pushDescriptorAvailable = qtrue;
		}
#endif

#ifdef _DEBUG
		if ( timelineSemaphore ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;
		}

		if ( memoryModel ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME;
		}
		if ( devAddrFeat ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME;
		}
		if ( storage8bit ) {
			device_extension_list[ device_extension_count++ ] = VK_KHR_8BIT_STORAGE_EXTENSION_NAME;
		}
#endif // _DEBUG

		vkGetPhysicalDeviceFeatures(physical_device, &device_features);

		if (device_features.fillModeNonSolid == VK_FALSE) {
			CL_RefPrintf(PRINT_ERROR, "...fillModeNonSolid feature is not supported\n");
			return qfalse;
		}

		queue_desc.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_desc.pNext = NULL;
		queue_desc.flags = 0;
		queue_desc.queueFamilyIndex = vk.queue_family_index;
		queue_desc.queueCount = 1;
		queue_desc.pQueuePriorities = &priority;

		Com_Memset(&features, 0, sizeof(features));
		features.fillModeNonSolid = VK_TRUE;

#ifdef _DEBUG
		if ( device_features.shaderInt64 ) {
			features.shaderInt64 = VK_TRUE;
		}
#endif
		if (device_features.wideLines) { // needed for RB_SurfaceAxis
			features.wideLines = VK_TRUE;
			//vk.wideLines = qtrue;
		}

		if (device_features.shaderStorageImageMultisample) {
			features.shaderStorageImageMultisample = VK_TRUE;
			vk.shaderStorageImageMultisample = qtrue;
		}

		if ( device_features.fragmentStoresAndAtomics && device_features.vertexPipelineStoresAndAtomics ) {
			features.vertexPipelineStoresAndAtomics = VK_TRUE;
			features.fragmentStoresAndAtomics = VK_TRUE;
			vk.fragmentStores = qtrue;
		}

		if(device_features.multiDrawIndirect) {
			features.multiDrawIndirect = VK_TRUE;
		}

#ifdef USE_VK_PBR
		if ( device_features.geometryShader )
			features.geometryShader = VK_TRUE;
#endif
		if (r_ext_texture_filter_anisotropic->integer && device_features.samplerAnisotropy) {
			features.samplerAnisotropy = VK_TRUE;
			vk.samplerAnisotropy = qtrue;
		}

		device_desc.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		device_desc.pNext = NULL;
		device_desc.flags = 0;
		device_desc.queueCreateInfoCount = 1;
		device_desc.pQueueCreateInfos = &queue_desc;
		device_desc.enabledLayerCount = 0;
		device_desc.ppEnabledLayerNames = NULL;
		device_desc.enabledExtensionCount = device_extension_count;
		device_desc.ppEnabledExtensionNames = device_extension_list;
		device_desc.pEnabledFeatures = &features;

		// Core 1.3 features the renderer relies on. Requested unconditionally:
		// if the driver cannot provide them it is not a device we support, and
		// failing at device creation is far better than failing later in a way
		// that looks like a rendering bug.
		static VkPhysicalDeviceVulkan13Features features13;
		Com_Memset( &features13, 0, sizeof( features13 ) );
		features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		features13.synchronization2 = VK_TRUE;
		features13.dynamicRendering = VK_TRUE;
		features13.maintenance4 = VK_TRUE;
		features13.pNext = (void *)device_desc.pNext;
		device_desc.pNext = &features13;

#ifdef _DEBUG
		// From features13's own pNext, not from the head of the chain: seeding
		// it at the head makes the first assignment below overwrite features13
		// and quietly disable synchronization2 while the renderer keeps
		// recording synchronization2 barriers.
		pNextPtr = (const void **)&features13.pNext;

		if ( timelineSemaphore ) {
			*pNextPtr = &timeline_semaphore;
			timeline_semaphore.pNext = NULL;
			timeline_semaphore.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
			timeline_semaphore.timelineSemaphore = VK_TRUE;
			pNextPtr = (const void **)&timeline_semaphore.pNext;
		}
		if ( memoryModel ) {
			*pNextPtr = &memory_model;
			memory_model.pNext = NULL;
			memory_model.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES;
			memory_model.vulkanMemoryModel = VK_TRUE;
			memory_model.vulkanMemoryModelAvailabilityVisibilityChains = VK_FALSE;
			memory_model.vulkanMemoryModelDeviceScope = VK_TRUE;
			pNextPtr = (const void **)&memory_model.pNext;
		}
		if ( devAddrFeat ) {
			*pNextPtr = &devaddr_features;
			devaddr_features.pNext = NULL;
			devaddr_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
			devaddr_features.bufferDeviceAddress = VK_TRUE;
			devaddr_features.bufferDeviceAddressCaptureReplay = VK_FALSE;
			devaddr_features.bufferDeviceAddressMultiDevice = VK_FALSE;
			pNextPtr = (const void **)&devaddr_features.pNext;
		}
		if ( storage8bit ) {
			*pNextPtr = &storage_8bit_features;
			storage_8bit_features.pNext = NULL;
			storage_8bit_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES;
			storage_8bit_features.storageBuffer8BitAccess = VK_TRUE;
			storage_8bit_features.storagePushConstant8 = VK_FALSE;
			storage_8bit_features.uniformAndStorageBuffer8BitAccess = VK_TRUE;
			pNextPtr = (const void **)&storage_8bit_features.pNext;
		}
#endif

		result = vkCreateDevice(physical_device, &device_desc, NULL, &vk.device);
		if (result == VK_SUCCESS) {
			volkLoadDevice(vk.device);
		}
		if (result < 0) {
			CL_RefPrintf(PRINT_ERROR, "vkCreateDevice returned %s\n", vk_result_string(result));
			return qfalse;
		}
	}

	return qtrue;
}






void vk_init_library( void )
{
	VkPhysicalDeviceProperties props;
	VkPhysicalDevice *physical_devices;
	uint32_t device_count;
	int device_index, i;
	VkResult res;
#ifdef _WIN32
	qboolean deviceCountRetried = qfalse;
__initStart:
#endif

	Com_Memset(&vk, 0, sizeof(vk));

	// The loader comes from SDL, not from a linked libvulkan, so volk is
	// initialised with that address rather than by volkInitialize().
	{
		PFN_vkGetInstanceProcAddr getProcAddr = (PFN_vkGetInstanceProcAddr)WIN_VK_GetInstanceProcAddress();
		if (getProcAddr == NULL) {
			Com_Error(ERR_FATAL, "Vulkan: failed to find entrypoint vkGetInstanceProcAddr");
			return;
		}
		volkInitializeCustom(getProcAddr);
	}

	vk_create_instance();

	// Instance-level entry points only; device-level ones are loaded once the
	// device exists, so they resolve directly instead of going through the
	// loader trampoline.
	volkLoadInstanceOnly(vk.instance);


#ifdef USE_VK_VALIDATION
	#ifdef USE_DEBUG_REPORT
	#endif
	#ifdef USE_DEBUG_UTILS
	#endif

	vk_create_debug_callback();
#endif

	// create surface
	if (!WIN_VK_CreateSurface(vk.instance, &vk.surface))
	{
		Com_Error(ERR_FATAL, "Error creating Vulkan surface");
		return;
	}

	res = vkEnumeratePhysicalDevices(vk.instance, &device_count, NULL);
	if (device_count == 0) {
#ifdef _WIN32
		if (!deviceCountRetried) {
			// May be a conflict between VK_LAYER_AMD_swichable_graphics and VK_LAYER_NV_optimus on laptops with AMD + Nvidia GPUs:
			// https://stackoverflow.com/questions/68109171/vkenumeratephysicaldevices-not-finding-all-gpus/68631366#68631366
			CL_RefPrintf(PRINT_WARNING, "Vulkan: No physical devices found. Retrying with AMD_SWITCHABLE_GRAPHICS disabled.\n");

			// Clear instance with a subset of vk_shutdown
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
			vkDestroyInstance(vk.instance, NULL);
			vk_deinit_library();

			// Disable the AMD layer and try again.
			SetEnvironmentVariable("DISABLE_LAYER_AMD_SWITCHABLE_GRAPHICS_1", "1");
			deviceCountRetried = qtrue;
			goto __initStart;
		}
#endif
		Com_Error(ERR_FATAL, "Vulkan: no physical devices found");
		return;
	}
	else if (res < 0) {
		Com_Error(ERR_FATAL, "vkEnumeratePhysicalDevices returned %s", vk_result_string(res));
		return;
	}

	physical_devices = (VkPhysicalDevice*)malloc(device_count * sizeof(VkPhysicalDevice));
	VK_CHECK(vkEnumeratePhysicalDevices(vk.instance, &device_count, physical_devices));

	// initial physical device index
	device_index = r_device->integer;

	CL_RefPrintf(PRINT_ALL, "\n\n----- Available physical devices -----\n");
	for (i = 0; i < device_count; i++) {
		vkGetPhysicalDeviceProperties(physical_devices[i], &props);
		CL_RefPrintf(PRINT_ALL, " %i: %s\n", i, renderer_name(&props));
		if (device_index == -1 && props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
			device_index = i;
		}
		else if (device_index == -2 && props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
			device_index = i;
		}
	}


	vk.physical_device = VK_NULL_HANDLE;
	for (i = 0; i < device_count; i++, device_index++) {
		if (device_index >= device_count || device_index < 0) {
			device_index = 0;
		}
		if (vk_create_device(physical_devices[device_index], device_index)) {
			vk.physical_device = physical_devices[device_index];
			break;
		}
	}

	free(physical_devices);

	if (vk.physical_device == VK_NULL_HANDLE) {
		Com_Error(ERR_FATAL, "Vulkan: unable to find any suitable physical device");
		return;
	}

	//
	// Get device level functions.
	//

	if (vk.dedicatedAllocation) {
		if (!vkGetBufferMemoryRequirements2KHR || !vkGetImageMemoryRequirements2KHR) {
			vk.dedicatedAllocation = qfalse;
		}
	}

	if (vk.debugMarkers) {
	}



#ifdef USE_VK_IMGUI
#endif
}


void vk_deinit_library( void )
{
	// volk holds the loaded entry points; clearing them by hand used to be ~110
	// assignments that had to be kept in step with the load side.
	volkFinalize();
}

#define FORMAT_DEPTH(format, r_bits, g_bits, b_bits) case(VK_FORMAT_##format): *r = r_bits; *b = b_bits; *g = g_bits; return qtrue;
qboolean vk_surface_format_color_depth( VkFormat format, int *r, int *g, int *b ) {
	switch (format) {
		// Common formats from https://vulkan.gpuinfo.org/listsurfaceformats.php
		FORMAT_DEPTH(B8G8R8A8_UNORM, 255, 255, 255)
		FORMAT_DEPTH(B8G8R8A8_SRGB, 255, 255, 255)
		FORMAT_DEPTH(A2B10G10R10_UNORM_PACK32, 1023, 1023, 1023)
		FORMAT_DEPTH(R8G8B8A8_UNORM, 255, 255, 255)
		FORMAT_DEPTH(R8G8B8A8_SRGB, 255, 255, 255)
		FORMAT_DEPTH(A2R10G10B10_UNORM_PACK32, 1023, 1023, 1023)
		FORMAT_DEPTH(R5G6B5_UNORM_PACK16, 31, 63, 31)
		FORMAT_DEPTH(R8G8B8A8_SNORM, 255, 255, 255)
		FORMAT_DEPTH(A8B8G8R8_UNORM_PACK32, 255, 255, 255)
		FORMAT_DEPTH(A8B8G8R8_SNORM_PACK32, 255, 255, 255)
		FORMAT_DEPTH(A8B8G8R8_SRGB_PACK32, 255, 255, 255)
		FORMAT_DEPTH(R16G16B16A16_UNORM, 65535, 65535, 65535)
		FORMAT_DEPTH(R16G16B16A16_SNORM, 65535, 65535, 65535)
		FORMAT_DEPTH(B5G6R5_UNORM_PACK16, 31, 63, 31)
		FORMAT_DEPTH(B8G8R8A8_SNORM, 255, 255, 255)
		FORMAT_DEPTH(R4G4B4A4_UNORM_PACK16, 15, 15, 15)
		FORMAT_DEPTH(B4G4R4A4_UNORM_PACK16, 15, 15, 15)
		FORMAT_DEPTH(A1R5G5B5_UNORM_PACK16, 31, 31, 31)
		FORMAT_DEPTH(R5G5B5A1_UNORM_PACK16, 31, 31, 31)
		FORMAT_DEPTH(B5G5R5A1_UNORM_PACK16, 31, 31, 31)
		default:
			*r = 255; *g = 255; *b = 255; return qfalse;
	}
}
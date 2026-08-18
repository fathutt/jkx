/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// What is still alive when the device is destroyed.
//
// The hardware dies inside vkDestroyDevice. Three rounds of reports say so: the
// last line in the log is "vk_shutdown: destroying the device", the frames
// resolve into the NVIDIA ICD, and the faulting read is a small offset from
// nothing - the shape of a driver walking a list whose entries have already
// been released. It happens on quit and on vid_restart, at the same
// instruction, so it is not about what the player was doing.
//
// THE BENCH CANNOT SEE IT. The lanes run under the validation layer, which
// tracks every object and complains at vkDestroyDevice about any that survive,
// and it has never complained once - the local runs reach "vk_shutdown: done"
// clean. Either nothing leaks on lavapipe and something leaks on the hardware,
// or what leaks is not an object at all. Either way the answer is on a machine
// with a real driver, and that machine has no validation layer installed.
//
// So the renderer counts for itself. volk resolves every entry point into a
// writable global function pointer, which makes this a hook rather than an
// audit: the pointers are swapped once, every create and destroy the renderer
// issues goes through a thunk that adds or removes a row, and the call sites
// change not at all. That matters, because a census maintained by hand at
// forty-odd call sites is a census that goes stale the first time somebody
// adds a forty-first.
//
// Each row keeps the caller's return address, so a survivor prints the line
// that made it. On Windows that resolves through the same dbghelp session the
// crash reporter opens; elsewhere it prints an address, which is enough
// alongside a map file.
//
// What this deliberately does NOT cover: anything VMA creates for itself. VMA
// takes its pointers from vkGetDeviceProcAddr and so bypasses the hook, and its
// blocks are its own to free. The images and buffers we ask VMA to make for us
// are counted, in vk_allocator.cpp, because those are ours.

#include "tr_local.h"

#ifdef USE_VK_CENSUS

#define VK_CENSUS_MAX 32768

typedef struct {
	uint64_t	handle;
	uint64_t	site;		// return address of the creating call
	const char	*kind;
	uint32_t	serial;
} vk_census_row_t;

static vk_census_row_t	s_rows[VK_CENSUS_MAX];
static uint32_t			s_count;
static uint32_t			s_serial;
static qboolean			s_overflowed;
static qboolean			s_active;

// Nothing here is thread safe and nothing here needs to be: every call comes
// from the render thread, and the renderer has one.
void vk_census_add( const char *kind, uint64_t handle, uint64_t site )
{
	if ( !s_active || handle == 0 ) {
		return;
	}

	if ( s_count >= VK_CENSUS_MAX ) {
		// Said once. A census that reports its own overflow is still worth
		// reading; one that reports it every frame is not.
		if ( !s_overflowed ) {
			s_overflowed = qtrue;
			CL_RefPrintf( PRINT_WARNING, "census: more than %i live objects, "
				"the count below is a floor and not a total\n", VK_CENSUS_MAX );
		}
		return;
	}

	s_rows[s_count].handle = handle;
	s_rows[s_count].site = site;
	s_rows[s_count].kind = kind;
	s_rows[s_count].serial = ++s_serial;
	s_count++;
}

void vk_census_remove( const char *kind, uint64_t handle )
{
	uint32_t i;

	if ( !s_active || handle == 0 ) {
		return;
	}

	// Backwards, because handles are reused: the driver hands the same address
	// back after a free, and the row that matches most recently is the one this
	// destroy belongs to. Front to back would retire a row for an object that
	// is still alive and leave the dead one in the report.
	for ( i = s_count; i > 0; i-- ) {
		if ( s_rows[i - 1].handle == handle && s_rows[i - 1].kind == kind ) {
			s_rows[i - 1] = s_rows[s_count - 1];
			s_count--;
			return;
		}
	}
}

/*
================
vk_census_report

Called with the device still alive, immediately before vkDestroyDevice. Prints
one line per surviving object; prints nothing but a clean bill if there are
none, so a report from a machine that is fine reads as clearly as one from a
machine that is not.
================
*/
void vk_census_report( void )
{
	uint32_t i, j;
	char     where[512];

	if ( !s_active ) {
		return;
	}

	// The total is printed either way, and it is not decoration: a hook that
	// never fired reports "nothing left alive" in exactly the same words as a
	// clean shutdown. One number tells the two apart, and a report from a
	// machine nobody here can run has to be readable without a second run.
	if ( s_count == 0 ) {
		CL_RefPrintf( PRINT_ALL, "census: nothing left alive at vkDestroyDevice "
			"(%u object(s) tracked)\n", (unsigned)s_serial );
		return;
	}

	CL_RefPrintf( PRINT_ALL, "census: %u object(s) still alive at vkDestroyDevice "
		"(of %u tracked)\n", (unsigned)s_count, (unsigned)s_serial );

	// Grouped by creation site rather than listed one per object, because a
	// leak that happens once a frame produces hundreds of identical rows and
	// the count is the interesting part of all of them.
	for ( i = 0; i < s_count; i++ ) {
		uint32_t same = 0;
		uint32_t first = s_rows[i].serial;

		if ( s_rows[i].kind == NULL ) {
			continue; // already folded into an earlier group
		}

		for ( j = i; j < s_count; j++ ) {
			if ( s_rows[j].kind == s_rows[i].kind && s_rows[j].site == s_rows[i].site ) {
				same++;
				if ( s_rows[j].serial < first ) {
					first = s_rows[j].serial;
				}
				if ( j != i ) {
					s_rows[j].kind = NULL;
				}
			}
		}

		vk_symbolise_address( s_rows[i].site, where, sizeof( where ) );
		CL_RefPrintf( PRINT_ALL, "census:   %-24s x%-4u  first #%u  from %s\n",
			s_rows[i].kind, (unsigned)same, (unsigned)first, where );
	}
}

// ---------------------------------------------------------------------------
// The hook itself.
//
// One thunk per entry point, generated so that adding a type is one line. The
// saved pointer is the real one; the thunk is what volk's global holds after
// vk_census_install.
// ---------------------------------------------------------------------------

#if defined( _MSC_VER )
	#include <intrin.h>
	#define VK_CENSUS_SITE()	( (uint64_t)(uintptr_t)_ReturnAddress() )
#elif defined( __GNUC__ ) || defined( __clang__ )
	#define VK_CENSUS_SITE()	( (uint64_t)(uintptr_t)__builtin_return_address( 0 ) )
#else
	#define VK_CENSUS_SITE()	( (uint64_t)0 )
#endif

// A create that produces one handle and returns VkResult.
#define VK_CENSUS_CREATE( name, infoType, handleType )                         \
	static PFN_vkCreate##name real_vkCreate##name;                             \
	static VKAPI_ATTR VkResult VKAPI_CALL census_vkCreate##name(               \
		VkDevice device, const infoType *info,                                 \
		const VkAllocationCallbacks *alloc, handleType *out )                  \
	{                                                                          \
		const uint64_t site = VK_CENSUS_SITE();                                \
		const VkResult result = real_vkCreate##name( device, info, alloc, out );\
		if ( result == VK_SUCCESS ) {                                          \
			vk_census_add( "Vk" #name, (uint64_t)*out, site );                  \
		}                                                                      \
		return result;                                                         \
	}

#define VK_CENSUS_DESTROY( name, handleType )                                  \
	static PFN_vkDestroy##name real_vkDestroy##name;                           \
	static VKAPI_ATTR void VKAPI_CALL census_vkDestroy##name(                   \
		VkDevice device, handleType handle,                                    \
		const VkAllocationCallbacks *alloc )                                   \
	{                                                                          \
		vk_census_remove( "Vk" #name, (uint64_t)handle );                       \
		real_vkDestroy##name( device, handle, alloc );                          \
	}

#define VK_CENSUS_PAIR( name, infoType, handleType )                           \
	VK_CENSUS_CREATE( name, infoType, handleType )                             \
	VK_CENSUS_DESTROY( name, handleType )

VK_CENSUS_PAIR( Image,               VkImageCreateInfo,               VkImage )
VK_CENSUS_PAIR( ImageView,           VkImageViewCreateInfo,           VkImageView )
VK_CENSUS_PAIR( Buffer,              VkBufferCreateInfo,              VkBuffer )
VK_CENSUS_PAIR( Sampler,             VkSamplerCreateInfo,             VkSampler )
VK_CENSUS_PAIR( Framebuffer,         VkFramebufferCreateInfo,         VkFramebuffer )
VK_CENSUS_PAIR( RenderPass,          VkRenderPassCreateInfo,          VkRenderPass )
VK_CENSUS_PAIR( PipelineLayout,      VkPipelineLayoutCreateInfo,      VkPipelineLayout )
VK_CENSUS_PAIR( DescriptorSetLayout, VkDescriptorSetLayoutCreateInfo, VkDescriptorSetLayout )
VK_CENSUS_PAIR( DescriptorPool,      VkDescriptorPoolCreateInfo,      VkDescriptorPool )
VK_CENSUS_PAIR( CommandPool,         VkCommandPoolCreateInfo,         VkCommandPool )
VK_CENSUS_PAIR( Semaphore,           VkSemaphoreCreateInfo,           VkSemaphore )
VK_CENSUS_PAIR( Fence,               VkFenceCreateInfo,               VkFence )
VK_CENSUS_PAIR( ShaderModule,        VkShaderModuleCreateInfo,        VkShaderModule )
VK_CENSUS_PAIR( PipelineCache,       VkPipelineCacheCreateInfo,       VkPipelineCache )
VK_CENSUS_PAIR( QueryPool,           VkQueryPoolCreateInfo,           VkQueryPool )
VK_CENSUS_PAIR( Event,               VkEventCreateInfo,               VkEvent )

// The swapchain does not fit the pattern: KHR suffix on both halves.
static PFN_vkCreateSwapchainKHR real_vkCreateSwapchainKHR;
static VKAPI_ATTR VkResult VKAPI_CALL census_vkCreateSwapchainKHR( VkDevice device,
	const VkSwapchainCreateInfoKHR *info, const VkAllocationCallbacks *alloc,
	VkSwapchainKHR *out )
{
	const uint64_t site = VK_CENSUS_SITE();
	const VkResult result = real_vkCreateSwapchainKHR( device, info, alloc, out );
	if ( result == VK_SUCCESS ) {
		vk_census_add( "VkSwapchainKHR", (uint64_t)*out, site );
	}
	return result;
}

static PFN_vkDestroySwapchainKHR real_vkDestroySwapchainKHR;
static VKAPI_ATTR void VKAPI_CALL census_vkDestroySwapchainKHR( VkDevice device,
	VkSwapchainKHR handle, const VkAllocationCallbacks *alloc )
{
	vk_census_remove( "VkSwapchainKHR", (uint64_t)handle );
	real_vkDestroySwapchainKHR( device, handle, alloc );
}

// Pipelines come in batches and share one destroy.
static PFN_vkCreateGraphicsPipelines real_vkCreateGraphicsPipelines;
static VKAPI_ATTR VkResult VKAPI_CALL census_vkCreateGraphicsPipelines( VkDevice device,
	VkPipelineCache cache, uint32_t count, const VkGraphicsPipelineCreateInfo *infos,
	const VkAllocationCallbacks *alloc, VkPipeline *out )
{
	const uint64_t site = VK_CENSUS_SITE();
	const VkResult result = real_vkCreateGraphicsPipelines( device, cache, count,
		infos, alloc, out );
	if ( result == VK_SUCCESS ) {
		uint32_t i;
		for ( i = 0; i < count; i++ ) {
			vk_census_add( "VkPipeline", (uint64_t)out[i], site );
		}
	}
	return result;
}

static PFN_vkCreateComputePipelines real_vkCreateComputePipelines;
static VKAPI_ATTR VkResult VKAPI_CALL census_vkCreateComputePipelines( VkDevice device,
	VkPipelineCache cache, uint32_t count, const VkComputePipelineCreateInfo *infos,
	const VkAllocationCallbacks *alloc, VkPipeline *out )
{
	const uint64_t site = VK_CENSUS_SITE();
	const VkResult result = real_vkCreateComputePipelines( device, cache, count,
		infos, alloc, out );
	if ( result == VK_SUCCESS ) {
		uint32_t i;
		for ( i = 0; i < count; i++ ) {
			vk_census_add( "VkPipeline", (uint64_t)out[i], site );
		}
	}
	return result;
}

static PFN_vkDestroyPipeline real_vkDestroyPipeline;
static VKAPI_ATTR void VKAPI_CALL census_vkDestroyPipeline( VkDevice device,
	VkPipeline handle, const VkAllocationCallbacks *alloc )
{
	vk_census_remove( "VkPipeline", (uint64_t)handle );
	real_vkDestroyPipeline( device, handle, alloc );
}

// Memory is allocated and freed rather than created and destroyed, and the
// renderer still has hand-written allocations alongside VMA's.
static PFN_vkAllocateMemory real_vkAllocateMemory;
static VKAPI_ATTR VkResult VKAPI_CALL census_vkAllocateMemory( VkDevice device,
	const VkMemoryAllocateInfo *info, const VkAllocationCallbacks *alloc,
	VkDeviceMemory *out )
{
	const uint64_t site = VK_CENSUS_SITE();
	const VkResult result = real_vkAllocateMemory( device, info, alloc, out );
	if ( result == VK_SUCCESS ) {
		vk_census_add( "VkDeviceMemory", (uint64_t)*out, site );
	}
	return result;
}

static PFN_vkFreeMemory real_vkFreeMemory;
static VKAPI_ATTR void VKAPI_CALL census_vkFreeMemory( VkDevice device,
	VkDeviceMemory handle, const VkAllocationCallbacks *alloc )
{
	vk_census_remove( "VkDeviceMemory", (uint64_t)handle );
	real_vkFreeMemory( device, handle, alloc );
}

#define VK_CENSUS_HOOK( name )                     \
	real_vk##name = vk##name;                      \
	vk##name = census_vk##name;

#define VK_CENSUS_UNHOOK( name )                   \
	if ( real_vk##name ) {                         \
		vk##name = real_vk##name;                  \
		real_vk##name = NULL;                      \
	}

#define VK_CENSUS_HOOK_PAIR( name )                \
	VK_CENSUS_HOOK( Create##name )                 \
	VK_CENSUS_HOOK( Destroy##name )

#define VK_CENSUS_UNHOOK_PAIR( name )              \
	VK_CENSUS_UNHOOK( Create##name )               \
	VK_CENSUS_UNHOOK( Destroy##name )

/*
================
vk_census_install

Called once the device functions are resolved and before anything is created
with them. Ordering matters: a pointer swapped after the first create means an
object with no row, which reads as a destroy of something that was never made
and quietly does nothing.
================
*/
void vk_census_install( void )
{
	if ( s_active ) {
		return;
	}

	VK_CENSUS_HOOK_PAIR( Image )
	VK_CENSUS_HOOK_PAIR( ImageView )
	VK_CENSUS_HOOK_PAIR( Buffer )
	VK_CENSUS_HOOK_PAIR( Sampler )
	VK_CENSUS_HOOK_PAIR( Framebuffer )
	VK_CENSUS_HOOK_PAIR( RenderPass )
	VK_CENSUS_HOOK_PAIR( PipelineLayout )
	VK_CENSUS_HOOK_PAIR( DescriptorSetLayout )
	VK_CENSUS_HOOK_PAIR( DescriptorPool )
	VK_CENSUS_HOOK_PAIR( CommandPool )
	VK_CENSUS_HOOK_PAIR( Semaphore )
	VK_CENSUS_HOOK_PAIR( Fence )
	VK_CENSUS_HOOK_PAIR( ShaderModule )
	VK_CENSUS_HOOK_PAIR( PipelineCache )
	VK_CENSUS_HOOK_PAIR( QueryPool )
	VK_CENSUS_HOOK_PAIR( Event )
	VK_CENSUS_HOOK_PAIR( SwapchainKHR )

	VK_CENSUS_HOOK( CreateGraphicsPipelines )
	VK_CENSUS_HOOK( CreateComputePipelines )
	VK_CENSUS_HOOK( DestroyPipeline )

	VK_CENSUS_HOOK( AllocateMemory )
	VK_CENSUS_HOOK( FreeMemory )

	s_count = 0;
	s_serial = 0;
	s_overflowed = qfalse;
	s_active = qtrue;
}

/*
================
vk_census_shutdown

Puts volk's globals back and forgets every row. Called after the device is gone,
because a thunk left in place across a vid_restart points at a saved pointer
belonging to a device that no longer exists - and the next install would save
the thunk as the real one and call itself forever.
================
*/
void vk_census_shutdown( void )
{
	if ( !s_active ) {
		return;
	}

	VK_CENSUS_UNHOOK_PAIR( Image )
	VK_CENSUS_UNHOOK_PAIR( ImageView )
	VK_CENSUS_UNHOOK_PAIR( Buffer )
	VK_CENSUS_UNHOOK_PAIR( Sampler )
	VK_CENSUS_UNHOOK_PAIR( Framebuffer )
	VK_CENSUS_UNHOOK_PAIR( RenderPass )
	VK_CENSUS_UNHOOK_PAIR( PipelineLayout )
	VK_CENSUS_UNHOOK_PAIR( DescriptorSetLayout )
	VK_CENSUS_UNHOOK_PAIR( DescriptorPool )
	VK_CENSUS_UNHOOK_PAIR( CommandPool )
	VK_CENSUS_UNHOOK_PAIR( Semaphore )
	VK_CENSUS_UNHOOK_PAIR( Fence )
	VK_CENSUS_UNHOOK_PAIR( ShaderModule )
	VK_CENSUS_UNHOOK_PAIR( PipelineCache )
	VK_CENSUS_UNHOOK_PAIR( QueryPool )
	VK_CENSUS_UNHOOK_PAIR( Event )
	VK_CENSUS_UNHOOK_PAIR( SwapchainKHR )

	VK_CENSUS_UNHOOK( CreateGraphicsPipelines )
	VK_CENSUS_UNHOOK( CreateComputePipelines )
	VK_CENSUS_UNHOOK( DestroyPipeline )

	VK_CENSUS_UNHOOK( AllocateMemory )
	VK_CENSUS_UNHOOK( FreeMemory )

	s_count = 0;
	s_serial = 0;
	s_overflowed = qfalse;
	s_active = qfalse;
}

#endif // USE_VK_CENSUS

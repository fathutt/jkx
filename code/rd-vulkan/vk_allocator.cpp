/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Device memory allocation, via the Vulkan Memory Allocator.
//
// The renderer inherited three separate hand-written allocators:
//
//   textures      32 MiB chunks, bump-only. chunk->used never decreased, so a
//                 freed texture's memory was unreachable until the whole chunk
//                 was released at map change. Overflow of the 256-chunk array
//                 was a Com_Error telling the player to edit a constant.
//   attachments   one VkDeviceMemory with offsets computed by hand
//   buffers       ad hoc vkAllocateMemory at roughly fifteen call sites
//
// This file introduces VMA and moves textures onto it first, because that is
// the allocator with the real defect: sub-allocations were never reused.
// Attachments and buffers follow; they are correct today, just manual.
//
// VMA also gives us budget tracking, which replaces
// ri.Error("GPU memory heap overflow") with something a player can act on.

#include "tr_local.h"

#define VMA_IMPLEMENTATION
// volk owns the entry points, so VMA must be told to fetch them dynamically
// rather than expect linked prototypes.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wnullability-completeness"
#endif
#include "vk_mem_alloc.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

void vk_create_allocator(void)
{
    // Hand VMA the pointers volk already resolved instead of letting it load
    // its own; otherwise the two disagree about which device is current.
    VmaVulkanFunctions functions = {};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo desc = {};
    desc.instance = vk.instance;
    desc.physicalDevice = vk.physical_device;
    desc.device = vk.device;
    desc.pVulkanFunctions = &functions;
    desc.vulkanApiVersion = VK_API_VERSION_1_1;

    if (vk.dedicatedAllocation) {
        desc.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
    }

    VK_CHECK(vmaCreateAllocator(&desc, &vk.allocator));
}

void vk_destroy_allocator(void)
{
    if (vk.allocator == VK_NULL_HANDLE) {
        return;
    }
    vmaDestroyAllocator(vk.allocator);
    vk.allocator = VK_NULL_HANDLE;
}

qboolean vk_create_image_memory(const VkImageCreateInfo* desc, VkImage* image, VmaAllocation* allocation,
                                const char* name)
{
    // The allocator is infrastructure: nothing may allocate before it exists.
    // Without this check a null allocator reaches VMA and the process dies with
    // an access violation and no message at all, which is how an initialisation
    // order mistake cost a full round of hardware testing.
    if (vk.allocator == VK_NULL_HANDLE) {
        ri.Error(ERR_FATAL, "Vulkan: %s called before vk_create_allocator()", __func__);
        return qfalse;
    }

    VmaAllocationCreateInfo alloc = {};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    // Let VMA decide between sub-allocation and a dedicated block; it applies
    // the driver's own preference from VK_KHR_dedicated_allocation when the
    // extension is present.
    alloc.priority = 1.0f;

    const VkResult result = vmaCreateImage(vk.allocator, desc, &alloc, image, allocation, NULL);
    if (result != VK_SUCCESS) {
        // Report what is actually exhausted rather than a bare result code: the
        // old message ("GPU memory heap overflow") told nobody anything.
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {};
        vmaGetHeapBudgets(vk.allocator, budgets);

        VkDeviceSize used = 0;
        VkDeviceSize budget = 0;
        for (uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; ++i) {
            used += budgets[i].usage;
            budget += budgets[i].budget;
        }

        ri.Printf(PRINT_WARNING,
                  "Vulkan: image allocation failed (%s), %ux%u; %u MiB in use of %u MiB budget\n",
                  vk_result_string(result), desc->extent.width, desc->extent.height,
                  (unsigned)(used / (1024 * 1024)), (unsigned)(budget / (1024 * 1024)));
        return qfalse;
    }

    if (name != NULL) {
        vmaSetAllocationName(vk.allocator, *allocation, name);
        VK_SET_OBJECT_NAME(*image, name, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT);
    }
    return qtrue;
}

void vk_destroy_image_memory(VkImage* image, VmaAllocation* allocation)
{
    if (image == NULL || *image == VK_NULL_HANDLE) {
        return;
    }
    // Releasing through a destroyed allocator is a use-after-free that the
    // driver turns into an access violation with no message. Say so instead:
    // the fix is always an ordering one, and it is cheap to point straight at.
    if (vk.allocator == VK_NULL_HANDLE) {
        ri.Printf(PRINT_WARNING, "Vulkan: %s called after vk_destroy_allocator()\n", __func__);
        return;
    }
    // Unlike the chunk allocator this actually returns the memory, so a texture
    // freed mid-level is reusable immediately instead of at the next map load.
    vmaDestroyImage(vk.allocator, *image, allocation != NULL ? *allocation : VK_NULL_HANDLE);
    *image = VK_NULL_HANDLE;
    if (allocation != NULL) {
        *allocation = VK_NULL_HANDLE;
    }
}

void vk_print_memory_usage(void)
{
    if (vk.allocator == VK_NULL_HANDLE) {
        return;
    }

    VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {};
    vmaGetHeapBudgets(vk.allocator, budgets);

    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(vk.physical_device, &props);

    ri.Printf(PRINT_ALL, "Vulkan memory:\n");
    for (uint32_t i = 0; i < props.memoryHeapCount; ++i) {
        const VmaBudget& b = budgets[i];
        if (b.budget == 0) {
            continue;
        }
        ri.Printf(PRINT_ALL, "  heap %u%s: %5u MiB used, %5u MiB allocated, %5u MiB budget (%u block(s))\n", i,
                  (props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? " [device]" : "         ",
                  (unsigned)(b.statistics.allocationBytes / (1024 * 1024)),
                  (unsigned)(b.statistics.blockBytes / (1024 * 1024)),
                  (unsigned)(b.budget / (1024 * 1024)), b.statistics.blockCount);
    }
}

qboolean vk_alloc_image_memory(VkImage image, qboolean transient, VmaAllocation* allocation, const char* name)
{
    // The allocator is infrastructure: nothing may allocate before it exists.
    // Without this check a null allocator reaches VMA and the process dies with
    // an access violation and no message at all, which is how an initialisation
    // order mistake cost a full round of hardware testing.
    if (vk.allocator == VK_NULL_HANDLE) {
        ri.Error(ERR_FATAL, "Vulkan: %s called before vk_create_allocator()", __func__);
        return qfalse;
    }

    // Not VMA_MEMORY_USAGE_AUTO. AUTO decides the memory type by reading the
    // buffer or image creation info, which vmaAllocateMemoryForImage does not
    // have - it is handed a VkImage that already exists - so VMA rejects the
    // combination outright with VK_ERROR_FEATURE_NOT_PRESENT. That is what
    // "out of device memory allocating attachment 0" was on an 8 GiB card with
    // nothing allocated yet. The requirements are stated directly instead.
    VmaAllocationCreateInfo alloc = {};
    alloc.usage = VMA_MEMORY_USAGE_UNKNOWN;
    alloc.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    alloc.priority = 1.0f;

    if (transient) {
        // Tile-based GPUs can back a transient attachment with memory that is
        // never written to RAM at all. The old pool asked for this explicitly
        // and fell back by hand; here it is a preference, so the fallback is
        // VMA's problem rather than ours.
        alloc.preferredFlags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
    }

    const VkResult result = vmaAllocateMemoryForImage(vk.allocator, image, &alloc, allocation, NULL);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "Vulkan: attachment allocation failed (%s) for '%s'\n", vk_result_string(result),
                  name != NULL ? name : "?");
        return qfalse;
    }

    if (vmaBindImageMemory(vk.allocator, *allocation, image) != VK_SUCCESS) {
        vmaFreeMemory(vk.allocator, *allocation);
        *allocation = VK_NULL_HANDLE;
        return qfalse;
    }

    if (name != NULL) {
        vmaSetAllocationName(vk.allocator, *allocation, name);
    }
    return qtrue;
}

void vk_free_image_memory(VmaAllocation* allocation)
{
    if (allocation == NULL || *allocation == VK_NULL_HANDLE) {
        return;
    }
    // Releasing through a destroyed allocator is a use-after-free that the
    // driver turns into an access violation with no message. Say so instead:
    // the fix is always an ordering one, and it is cheap to point straight at.
    if (vk.allocator == VK_NULL_HANDLE) {
        ri.Printf(PRINT_WARNING, "Vulkan: %s called after vk_destroy_allocator()\n", __func__);
        return;
    }
    vmaFreeMemory(vk.allocator, *allocation);
    *allocation = VK_NULL_HANDLE;
}

qboolean vk_create_buffer_memory(const VkBufferCreateInfo* desc, vk_buffer_memory_t kind, VkBuffer* buffer,
                                 VmaAllocation* allocation, void** mapped, const char* name)
{
    // The allocator is infrastructure: nothing may allocate before it exists.
    // Without this check a null allocator reaches VMA and the process dies with
    // an access violation and no message at all, which is how an initialisation
    // order mistake cost a full round of hardware testing.
    if (vk.allocator == VK_NULL_HANDLE) {
        ri.Error(ERR_FATAL, "Vulkan: %s called before vk_create_allocator()", __func__);
        return qfalse;
    }

    VmaAllocationCreateInfo alloc = {};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    alloc.priority = 1.0f;

    switch (kind) {
    case VK_BUFFER_MEMORY_DEVICE:
        alloc.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        break;

    case VK_BUFFER_MEMORY_HOST_WRITE:
        // Written by the CPU, read by the GPU: vertex, index and uniform
        // streams, and staging. Persistently mapped, which is what every one of
        // these call sites did by hand with a vkMapMemory that was never
        // unmapped. VMA also prefers BAR memory here when the device exposes it.
        alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        // Required, not preferred. HOST_ACCESS_* only promises HOST_VISIBLE,
        // and every call site converted to this helper previously asked for
        // HOST_COHERENT by hand and writes through the mapped pointer without
        // ever calling vmaFlushAllocation. On a device whose first host-visible
        // type is non-coherent, dropping it would make those writes silently
        // invisible to the GPU - corruption with no error anywhere.
        alloc.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;

    case VK_BUFFER_MEMORY_HOST_READ:
        // Read back by the CPU: screenshots and AVI capture.
        alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        alloc.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    }

    VmaAllocationInfo info = {};
    const VkResult result = vmaCreateBuffer(vk.allocator, desc, &alloc, buffer, allocation, &info);
    if (result != VK_SUCCESS) {
        ri.Printf(PRINT_WARNING, "Vulkan: buffer allocation failed (%s) for '%s', %u KiB\n",
                  vk_result_string(result), name != NULL ? name : "?",
                  (unsigned)(desc->size / 1024));
        return qfalse;
    }

    if (mapped != NULL) {
        // Non-null only for the mapped kinds; VMA filled it in when it created
        // the allocation, so there is no separate map call to keep balanced.
        *mapped = info.pMappedData;
    }

    if (name != NULL) {
        vmaSetAllocationName(vk.allocator, *allocation, name);
        VK_SET_OBJECT_NAME(*buffer, name, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT);
    }
    return qtrue;
}

void vk_destroy_buffer_memory(VkBuffer* buffer, VmaAllocation* allocation)
{
    if (buffer == NULL || *buffer == VK_NULL_HANDLE) {
        return;
    }
    // Releasing through a destroyed allocator is a use-after-free that the
    // driver turns into an access violation with no message. Say so instead:
    // the fix is always an ordering one, and it is cheap to point straight at.
    if (vk.allocator == VK_NULL_HANDLE) {
        ri.Printf(PRINT_WARNING, "Vulkan: %s called after vk_destroy_allocator()\n", __func__);
        return;
    }
    vmaDestroyBuffer(vk.allocator, *buffer, allocation != NULL ? *allocation : VK_NULL_HANDLE);
    *buffer = VK_NULL_HANDLE;
    if (allocation != NULL) {
        *allocation = VK_NULL_HANDLE;
    }
}

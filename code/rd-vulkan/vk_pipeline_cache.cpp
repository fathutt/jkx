/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Persistent VkPipelineCache.
//
// Upstream created the cache with initialDataSize = 0 and destroyed it without
// ever calling vkGetPipelineCacheData, so every launch and every vid_restart
// recompiled the lot from scratch. With up to 2304 pipeline definitions, each
// instantiated across several render passes, that is seconds of stall on a warm
// machine and considerably worse on Mesa.
//
// Cache data is driver-specific and explicitly not portable. A blob from
// another GPU, another driver build, or another Vulkan implementation must not
// be handed back: some drivers validate the header and reject it, others do
// not. So the header is checked here against the current device before the blob
// is offered, and mismatches drop the file silently and start clean.

#include "tr_local.h"

#include <cstdint>
#include <cstring>

namespace
{

constexpr const char* kCacheFile = "vkpipelines.bin";

// VkPipelineCacheHeaderVersionOne, as laid down by the specification. Read
// field by field rather than by struct overlay: the file comes off disk and is
// therefore untrusted (docs/CODING-STANDARDS.md section 5.2).
constexpr uint32_t kHeaderLength = 32;
constexpr uint32_t kHeaderVersionOne = 1;

uint32_t readU32(const uint8_t* p)
{
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

bool headerMatchesDevice(const uint8_t* data, size_t size, const VkPhysicalDeviceProperties& props)
{
    if (size <= kHeaderLength) {
        return false;
    }
    if (readU32(data + 0) != kHeaderLength) {
        return false;
    }
    if (readU32(data + 4) != kHeaderVersionOne) {
        return false;
    }
    if (readU32(data + 8) != props.vendorID) {
        return false;
    }
    if (readU32(data + 12) != props.deviceID) {
        return false;
    }
    return std::memcmp(data + 16, props.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}

}  // namespace

void vk_create_pipeline_cache(void)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vk.physical_device, &props);

    void* fileData = nullptr;
    const long fileSize = ri.FS_ReadFile(kCacheFile, &fileData);

    VkPipelineCacheCreateInfo desc;
    Com_Memset(&desc, 0, sizeof(desc));
    desc.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

    bool reused = false;
    if (fileData != nullptr && fileSize > 0) {
        const uint8_t* bytes = static_cast<const uint8_t*>(fileData);
        if (headerMatchesDevice(bytes, static_cast<size_t>(fileSize), props)) {
            desc.initialDataSize = static_cast<size_t>(fileSize);
            desc.pInitialData = fileData;
            reused = true;
        } else {
            // Expected after a driver update or when the config directory is
            // shared between machines. Say so; a silent reset looks like a
            // performance regression nobody can explain.
            ri.Printf(PRINT_ALL, "Vulkan: pipeline cache is for a different device or driver, rebuilding\n");
        }
    }

    const VkResult result = vkCreatePipelineCache(vk.device, &desc, VK_NULL_HANDLE, &vk.pipelineCache);

    if (fileData != nullptr) {
        ri.FS_FreeFile(fileData);
    }

    if (result != VK_SUCCESS) {
        // A rejected blob is recoverable: retry empty rather than take the
        // whole renderer down over a cache file.
        ri.Printf(PRINT_WARNING, "Vulkan: pipeline cache rejected (%s), rebuilding\n", vk_result_string(result));
        Com_Memset(&desc, 0, sizeof(desc));
        desc.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VK_CHECK(vkCreatePipelineCache(vk.device, &desc, VK_NULL_HANDLE, &vk.pipelineCache));
        reused = false;
    }

    if (reused) {
        ri.Printf(PRINT_ALL, "Vulkan: reusing pipeline cache (%li KiB)\n", fileSize / 1024);
    }
}

void vk_save_pipeline_cache(void)
{
    if (vk.pipelineCache == VK_NULL_HANDLE) {
        return;
    }

    size_t size = 0;
    if (vkGetPipelineCacheData(vk.device, vk.pipelineCache, &size, nullptr) != VK_SUCCESS || size == 0) {
        return;
    }

    void* data = ri.Hunk_AllocateTempMemory(static_cast<int>(size));
    if (data == nullptr) {
        return;
    }

    // The cache can grow between the two calls, so trust the size the second
    // call reports rather than the first.
    size_t written = size;
    if (vkGetPipelineCacheData(vk.device, vk.pipelineCache, &written, data) == VK_SUCCESS && written > 0) {
        ri.FS_WriteFile(kCacheFile, data, static_cast<int>(written));
        ri.Printf(PRINT_ALL, "Vulkan: saved pipeline cache (%u KiB)\n", static_cast<unsigned>(written / 1024));
    }

    ri.Hunk_FreeTempMemory(data);
}

void vk_destroy_pipeline_cache(void)
{
    if (vk.pipelineCache == VK_NULL_HANDLE) {
        return;
    }
    vk_save_pipeline_cache();
    vkDestroyPipelineCache(vk.device, vk.pipelineCache, nullptr);
    vk.pipelineCache = VK_NULL_HANDLE;
}

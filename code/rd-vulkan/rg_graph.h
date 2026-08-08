/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Render graph: pass ordering, resource lifetimes, barriers and aliasing.
//
// The renderer inherited 29 VkRenderPass objects and 44 framebuffers, all
// created up front and all destroyed and rebuilt on every swapchain restart,
// plus 32 hand-written call sites that transition image layouts by passing in
// the old layout the caller is expected to remember. That last part is the real
// problem: nothing checks the caller is right, and being wrong produces
// corruption or a validation error far from the mistake.
//
// Moving to VK_KHR_dynamic_rendering removes the render pass objects but makes
// layout tracking the caller's job outright, so the tracking has to become
// automatic in the same step. That tracker, plus lifetimes, is most of a render
// graph, which is why this exists rather than a smaller fix.
//
// Deliberately free of device calls. Everything here is bookkeeping over
// declarations, so it is unit-testable without Vulkan or a GPU, and the tests
// run in every CI build (docs/CODING-STANDARDS.md section 11). Recording is a
// separate layer that consumes what compile() produces.

#ifndef RG_GRAPH_H
#define RG_GRAPH_H

#include "volk.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rg
{

constexpr uint32_t kInvalidHandle = 0xFFFFFFFFu;

using ResourceHandle = uint32_t;
using PassHandle = uint32_t;

enum class PassKind : uint8_t
{
    Graphics,
    Compute,
    AsyncCompute,
    Transfer,
};

enum class ResourceKind : uint8_t
{
    Texture,
    Buffer,
};

struct TextureDesc
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t layers = 1;
    uint32_t mips = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = 0;

    [[nodiscard]] bool aliasableWith(const TextureDesc& other) const
    {
        return width == other.width && height == other.height && layers == other.layers && mips == other.mips
               && format == other.format && samples == other.samples;
    }
};

// How a pass touches a resource. Layout is ignored for buffers.
struct Access
{
    ResourceHandle resource = kInvalidHandle;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool write = false;
};

// One barrier as derived by compile(), to be recorded before its pass.
struct Barrier
{
    ResourceHandle resource = kInvalidHandle;
    VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_NONE;
    VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE;
    VkAccessFlags2 dstAccess = VK_ACCESS_2_NONE;
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct Resource
{
    std::string name;
    ResourceKind kind = ResourceKind::Texture;
    TextureDesc texture;
    bool imported = false;   // backed by an existing image the graph does not own
    bool transient = false;  // may share memory with another resource

    // Filled in by compile().
    uint32_t firstUse = kInvalidHandle;
    uint32_t lastUse = kInvalidHandle;
    ResourceHandle aliasOf = kInvalidHandle;
    VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct Pass
{
    std::string name;
    PassKind kind = PassKind::Graphics;
    std::vector<Access> accesses;

    // Filled in by compile().
    bool culled = false;
    std::vector<Barrier> barriers;
};

class Graph
{
public:
    void reset();

    // Declaration -----------------------------------------------------------

    ResourceHandle createTexture(std::string_view name, const TextureDesc& desc, bool transient = true);
    ResourceHandle importTexture(std::string_view name, const TextureDesc& desc, VkImageLayout currentLayout);
    ResourceHandle createBuffer(std::string_view name);

    PassHandle addPass(std::string_view name, PassKind kind = PassKind::Graphics);

    void read(PassHandle pass, ResourceHandle resource, VkPipelineStageFlags2 stage, VkAccessFlags2 access,
              VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    void write(PassHandle pass, ResourceHandle resource, VkPipelineStageFlags2 stage, VkAccessFlags2 access,
               VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // A resource the frame must actually produce; keeps its producers alive
    // when unused passes are culled.
    void markOutput(ResourceHandle resource);

    // Compilation -----------------------------------------------------------

    // Orders passes, culls those nothing depends on, computes lifetimes,
    // assigns aliases and derives barriers. Returns false and fills lastError()
    // if the declarations are inconsistent.
    bool compile();

    [[nodiscard]] const std::string& lastError() const { return m_error; }

    [[nodiscard]] const std::vector<PassHandle>& order() const { return m_order; }
    [[nodiscard]] const Pass& pass(PassHandle handle) const { return m_passes[handle]; }
    [[nodiscard]] const Resource& resource(ResourceHandle handle) const { return m_resources[handle]; }
    [[nodiscard]] uint32_t passCount() const { return static_cast<uint32_t>(m_passes.size()); }
    [[nodiscard]] uint32_t resourceCount() const { return static_cast<uint32_t>(m_resources.size()); }

    // Number of distinct memory backings after aliasing, for tests and /vkinfo.
    [[nodiscard]] uint32_t distinctAllocations() const;

private:
    bool sortPasses();
    void cullPasses();
    void computeLifetimes();
    void assignAliases();
    void deriveBarriers();

    std::vector<Resource> m_resources;
    std::vector<Pass> m_passes;
    std::vector<ResourceHandle> m_outputs;
    std::vector<PassHandle> m_order;
    std::string m_error;
};

}  // namespace rg

#endif  // RG_GRAPH_H

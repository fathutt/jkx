/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#include "rg_graph.h"

#include <algorithm>

namespace rg
{

void Graph::reset()
{
    m_resources.clear();
    m_passes.clear();
    m_outputs.clear();
    m_order.clear();
    m_error.clear();
}

ResourceHandle Graph::createTexture(std::string_view name, const TextureDesc& desc, bool transient)
{
    Resource r;
    r.name = name;
    r.kind = ResourceKind::Texture;
    r.texture = desc;
    r.transient = transient;
    m_resources.push_back(std::move(r));
    return static_cast<ResourceHandle>(m_resources.size() - 1);
}

ResourceHandle Graph::importTexture(std::string_view name, const TextureDesc& desc, VkImageLayout currentLayout)
{
    Resource r;
    r.name = name;
    r.kind = ResourceKind::Texture;
    r.texture = desc;
    r.imported = true;
    r.transient = false;  // we do not own it, so its memory cannot be reused
    r.initialLayout = currentLayout;
    m_resources.push_back(std::move(r));
    return static_cast<ResourceHandle>(m_resources.size() - 1);
}

ResourceHandle Graph::createBuffer(std::string_view name)
{
    Resource r;
    r.name = name;
    r.kind = ResourceKind::Buffer;
    r.transient = false;
    m_resources.push_back(std::move(r));
    return static_cast<ResourceHandle>(m_resources.size() - 1);
}

PassHandle Graph::addPass(std::string_view name, PassKind kind)
{
    Pass p;
    p.name = name;
    p.kind = kind;
    m_passes.push_back(std::move(p));
    return static_cast<PassHandle>(m_passes.size() - 1);
}

void Graph::read(PassHandle pass, ResourceHandle resource, VkPipelineStageFlags2 stage, VkAccessFlags2 access,
                 VkImageLayout layout)
{
    Access a;
    a.resource = resource;
    a.stage = stage;
    a.access = access;
    a.layout = layout;
    a.write = false;
    m_passes[pass].accesses.push_back(a);
}

void Graph::write(PassHandle pass, ResourceHandle resource, VkPipelineStageFlags2 stage, VkAccessFlags2 access,
                  VkImageLayout layout)
{
    Access a;
    a.resource = resource;
    a.stage = stage;
    a.access = access;
    a.layout = layout;
    a.write = true;
    m_passes[pass].accesses.push_back(a);
}

void Graph::markOutput(ResourceHandle resource)
{
    m_outputs.push_back(resource);
}

bool Graph::compile()
{
    m_error.clear();
    m_order.clear();

    for (Pass& p : m_passes) {
        p.culled = false;
        p.barriers.clear();
    }
    for (Resource& r : m_resources) {
        r.firstUse = kInvalidHandle;
        r.lastUse = kInvalidHandle;
        r.aliasOf = kInvalidHandle;
        if (!r.imported) {
            r.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        r.finalLayout = r.initialLayout;
    }

    if (!sortPasses()) {
        return false;
    }
    cullPasses();
    computeLifetimes();
    assignAliases();
    deriveBarriers();
    return true;
}

// Passes are declared in submission order, and a pass may only read what an
// earlier pass wrote. That makes the declared order a valid topological order
// already; what this checks is that nobody violated it, because the alternative
// is a cycle silently turning into a read of undefined contents.
bool Graph::sortPasses()
{
    std::vector<PassHandle> lastWriter(m_resources.size(), kInvalidHandle);

    for (uint32_t i = 0; i < m_passes.size(); ++i) {
        const Pass& p = m_passes[i];

        for (const Access& a : p.accesses) {
            if (a.resource >= m_resources.size()) {
                m_error = "pass '" + p.name + "' references an unknown resource";
                return false;
            }
            if (!a.write && lastWriter[a.resource] == kInvalidHandle && !m_resources[a.resource].imported) {
                m_error = "pass '" + p.name + "' reads '" + m_resources[a.resource].name
                          + "' before anything writes it";
                return false;
            }
        }

        for (const Access& a : p.accesses) {
            if (a.write) {
                lastWriter[a.resource] = i;
            }
        }

        m_order.push_back(i);
    }
    return true;
}

// Anything that does not contribute to a declared output is dropped. Without
// this the graph would keep executing passes whose results a later change
// stopped consuming, which is exactly the sort of dead work that accumulates
// unnoticed in a hand-written frame.
void Graph::cullPasses()
{
    if (m_outputs.empty()) {
        return;  // no outputs declared: keep everything
    }

    std::vector<bool> needed(m_resources.size(), false);
    for (ResourceHandle h : m_outputs) {
        if (h < needed.size()) {
            needed[h] = true;
        }
    }

    // Walk backwards: a pass survives if it writes something needed, and then
    // everything it reads becomes needed too.
    for (auto it = m_order.rbegin(); it != m_order.rend(); ++it) {
        Pass& p = m_passes[*it];

        bool contributes = false;
        for (const Access& a : p.accesses) {
            if (a.write && needed[a.resource]) {
                contributes = true;
                break;
            }
        }

        if (!contributes) {
            p.culled = true;
            continue;
        }

        for (const Access& a : p.accesses) {
            if (!a.write) {
                needed[a.resource] = true;
            }
        }
    }

    m_order.erase(std::remove_if(m_order.begin(), m_order.end(),
                                 [this](PassHandle h) { return m_passes[h].culled; }),
                  m_order.end());
}

void Graph::computeLifetimes()
{
    for (uint32_t position = 0; position < m_order.size(); ++position) {
        for (const Access& a : m_passes[m_order[position]].accesses) {
            Resource& r = m_resources[a.resource];
            if (r.firstUse == kInvalidHandle) {
                r.firstUse = position;
            }
            r.lastUse = position;
        }
    }
}

// Two transient textures with identical descriptions and non-overlapping
// lifetimes can share memory. In this renderer that is the bloom and dynamic
// glow chains, the refraction extract target and the screenmap: all the same
// format, all alive for two or three passes out of the frame, and all resident
// for the whole frame today.
void Graph::assignAliases()
{
    for (uint32_t i = 0; i < m_resources.size(); ++i) {
        Resource& candidate = m_resources[i];

        if (!candidate.transient || candidate.imported || candidate.kind != ResourceKind::Texture) {
            continue;
        }
        if (candidate.firstUse == kInvalidHandle) {
            continue;  // unused after culling
        }

        for (uint32_t j = 0; j < i; ++j) {
            Resource& existing = m_resources[j];

            if (!existing.transient || existing.imported || existing.kind != ResourceKind::Texture) {
                continue;
            }
            if (existing.firstUse == kInvalidHandle || existing.aliasOf != kInvalidHandle) {
                continue;
            }
            if (!existing.texture.aliasableWith(candidate.texture)) {
                continue;
            }
            // Usage flags must be compatible: the shared image has to be
            // creatable with the union of both.
            if (existing.lastUse >= candidate.firstUse) {
                continue;  // lifetimes overlap
            }

            candidate.aliasOf = j;
            existing.texture.usage |= candidate.texture.usage;
            break;
        }
    }
}

// Derives one barrier per resource per pass that needs it, from the difference
// between how the previous pass left the resource and how this one wants it.
// This is the part that replaces 32 call sites where the caller passed in the
// old layout from memory.
void Graph::deriveBarriers()
{
    struct State
    {
        VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2 access = VK_ACCESS_2_NONE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool written = false;
    };

    std::vector<State> state(m_resources.size());
    for (uint32_t i = 0; i < m_resources.size(); ++i) {
        state[i].layout = m_resources[i].initialLayout;
    }

    for (PassHandle handle : m_order) {
        Pass& p = m_passes[handle];

        for (const Access& a : p.accesses) {
            State& s = state[a.resource];
            const bool isTexture = m_resources[a.resource].kind == ResourceKind::Texture;
            const VkImageLayout wanted = isTexture ? a.layout : VK_IMAGE_LAYOUT_UNDEFINED;

            const bool layoutChanges = isTexture && wanted != s.layout;
            const bool hazard = s.written || a.write;

            // Read after read in the same layout needs nothing.
            if (!layoutChanges && !hazard) {
                s.stage |= a.stage;
                s.access |= a.access;
                continue;
            }

            Barrier b;
            b.resource = a.resource;
            b.srcStage = s.stage != VK_PIPELINE_STAGE_2_NONE ? s.stage : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            b.dstStage = a.stage;
            b.srcAccess = s.access;
            b.dstAccess = a.access;
            b.oldLayout = s.layout;
            b.newLayout = wanted;

            // A write to a resource nobody has read yet can discard the old
            // contents, which lets the driver skip a decompress on tilers.
            if (a.write && !s.written && s.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
                b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            }

            p.barriers.push_back(b);

            s.stage = a.stage;
            s.access = a.access;
            s.layout = wanted;
            s.written = a.write;
        }
    }

    for (uint32_t i = 0; i < m_resources.size(); ++i) {
        m_resources[i].finalLayout = state[i].layout;
    }
}

uint32_t Graph::distinctAllocations() const
{
    uint32_t count = 0;
    for (const Resource& r : m_resources) {
        if (r.firstUse == kInvalidHandle) {
            continue;  // culled
        }
        if (r.imported) {
            continue;  // not ours
        }
        if (r.aliasOf == kInvalidHandle) {
            ++count;
        }
    }
    return count;
}

}  // namespace rg

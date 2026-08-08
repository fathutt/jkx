/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Tests for the render graph core: ordering, culling, lifetimes, aliasing and
// barrier derivation. No device, no GPU, so this runs in every CI build
// (docs/CODING-STANDARDS.md section 11).
//
// These are the tests that matter most in the whole renderer. A wrong barrier
// does not crash: it produces corruption on one vendor, at one resolution,
// three passes away from the mistake. Checking the derivation here is enormously
// cheaper than finding it in a frame capture.

#include "../code/rd-vulkan/rg_graph.h"

#include <cstdio>
#include <string>

namespace
{

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    }
}

rg::TextureDesc colorTarget(uint32_t w = 1920, uint32_t h = 1080)
{
    rg::TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    d.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    return d;
}

constexpr VkPipelineStageFlags2 kColorOut = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
constexpr VkPipelineStageFlags2 kFragment = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
constexpr VkAccessFlags2 kColorWrite = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
constexpr VkAccessFlags2 kShaderRead = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

void testWriteThenReadInsertsOneBarrier()
{
    std::printf("write then read\n");
    rg::Graph g;

    const auto scene = g.createTexture("scene", colorTarget());
    const auto out = g.importTexture("swapchain", colorTarget(), VK_IMAGE_LAYOUT_UNDEFINED);

    const auto opaque = g.addPass("opaque");
    g.write(opaque, scene, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const auto tonemap = g.addPass("tonemap");
    g.read(tonemap, scene, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.write(tonemap, out, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    g.markOutput(out);
    check(g.compile(), "compiles");
    check(g.order().size() == 2, "keeps both passes");

    // The write discards: nothing has read the resource, so the old contents
    // need not be preserved.
    const rg::Pass& first = g.pass(g.order()[0]);
    check(first.barriers.size() == 1, "one barrier before the first write");
    if (first.barriers.size() == 1) {
        check(first.barriers[0].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED, "discards undefined contents");
        check(first.barriers[0].newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, "moves to attachment layout");
    }

    const rg::Pass& second = g.pass(g.order()[1]);
    check(second.barriers.size() == 2, "barriers for the read and the new write");
    if (second.barriers.size() == 2) {
        const rg::Barrier& b = second.barriers[0];
        check(b.resource == scene, "first barrier is for the sampled texture");
        check(b.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, "from the layout the writer left");
        check(b.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, "to the layout the reader wants");
        check(b.srcStage == kColorOut, "waits on the producing stage");
        check(b.dstStage == kFragment, "blocks the consuming stage");
        check(b.srcAccess == kColorWrite, "makes the write available");
        check(b.dstAccess == kShaderRead, "makes it visible to the read");
    }
}

void testReadAfterReadNeedsNoBarrier()
{
    std::printf("read after read\n");
    rg::Graph g;

    const auto tex = g.createTexture("gbuffer", colorTarget());
    const auto out = g.importTexture("swapchain", colorTarget(), VK_IMAGE_LAYOUT_UNDEFINED);

    const auto produce = g.addPass("produce");
    g.write(produce, tex, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const auto a = g.addPass("consumer a");
    g.read(a, tex, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.write(a, out, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const auto b = g.addPass("consumer b");
    g.read(b, tex, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.write(b, out, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    g.markOutput(out);
    check(g.compile(), "compiles");

    // Same layout, no write in between: the second reader synchronises against
    // nothing. Emitting a barrier here would be a real cost on a hot path.
    uint32_t barriersForTex = 0;
    for (rg::PassHandle h : g.order()) {
        for (const rg::Barrier& bar : g.pass(h).barriers) {
            if (bar.resource == tex) {
                ++barriersForTex;
            }
        }
    }
    check(barriersForTex == 2, "one to write, one to first read, none for the second");
}

void testCullsPassesNothingConsumes()
{
    std::printf("culling\n");
    rg::Graph g;

    const auto used = g.createTexture("used", colorTarget());
    const auto orphan = g.createTexture("orphan", colorTarget());
    const auto out = g.importTexture("swapchain", colorTarget(), VK_IMAGE_LAYOUT_UNDEFINED);

    const auto produce = g.addPass("produce");
    g.write(produce, used, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const auto dead = g.addPass("dead end");
    g.write(dead, orphan, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const auto present = g.addPass("present");
    g.read(present, used, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.write(present, out, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    g.markOutput(out);
    check(g.compile(), "compiles");
    check(g.order().size() == 2, "drops the pass whose output nobody reads");
    check(g.pass(dead).culled, "the dead end is the one dropped");
    check(g.resource(orphan).firstUse == rg::kInvalidHandle, "its resource is never allocated");
}

void testAliasesNonOverlappingTransients()
{
    std::printf("transient aliasing\n");
    rg::Graph g;

    const rg::TextureDesc half = colorTarget(960, 540);
    const auto bloomA = g.createTexture("bloom a", half);
    const auto bloomB = g.createTexture("bloom b", half);
    const auto glow = g.createTexture("glow", half);
    const auto out = g.importTexture("swapchain", colorTarget(), VK_IMAGE_LAYOUT_UNDEFINED);

    const auto extract = g.addPass("bloom extract");
    g.write(extract, bloomA, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const auto blur = g.addPass("bloom blur");
    g.read(blur, bloomA, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.write(blur, bloomB, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // bloomA is dead from here on, so glow can take its memory.
    const auto glowPass = g.addPass("glow");
    g.read(glowPass, bloomB, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.write(glowPass, glow, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const auto composite = g.addPass("composite");
    g.read(composite, glow, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.write(composite, out, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    g.markOutput(out);
    check(g.compile(), "compiles");

    check(g.resource(glow).aliasOf == bloomA, "glow reuses the first bloom target");
    check(g.resource(bloomB).aliasOf == rg::kInvalidHandle, "the overlapping one is not aliased");
    check(g.distinctAllocations() == 2, "three transients need two allocations");
}

void testDoesNotAliasAcrossOverlappingLifetimes()
{
    std::printf("no aliasing while both are live\n");
    rg::Graph g;

    const rg::TextureDesc d = colorTarget(512, 512);
    const auto a = g.createTexture("a", d);
    const auto b = g.createTexture("b", d);
    const auto out = g.importTexture("swapchain", colorTarget(), VK_IMAGE_LAYOUT_UNDEFINED);

    const auto produce = g.addPass("produce both");
    g.write(produce, a, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    g.write(produce, b, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const auto consume = g.addPass("consume both");
    g.read(consume, a, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.read(consume, b, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.write(consume, out, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    g.markOutput(out);
    check(g.compile(), "compiles");
    check(g.resource(b).aliasOf == rg::kInvalidHandle, "both stay live, so neither is aliased");
    check(g.distinctAllocations() == 2, "two allocations");
}

void testDoesNotAliasDifferentDescriptions()
{
    std::printf("no aliasing across descriptions\n");
    rg::Graph g;

    const auto a = g.createTexture("a", colorTarget(512, 512));
    rg::TextureDesc other = colorTarget(512, 512);
    other.format = VK_FORMAT_R8G8B8A8_UNORM;
    const auto b = g.createTexture("b", other);
    const auto out = g.importTexture("swapchain", colorTarget(), VK_IMAGE_LAYOUT_UNDEFINED);

    const auto first = g.addPass("first");
    g.write(first, a, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const auto second = g.addPass("second");
    g.read(second, a, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.write(second, b, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const auto third = g.addPass("third");
    g.read(third, b, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.write(third, out, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    g.markOutput(out);
    check(g.compile(), "compiles");
    check(g.resource(b).aliasOf == rg::kInvalidHandle, "different formats are not aliased");
}

void testImportedResourcesKeepTheirLayout()
{
    std::printf("imported resources\n");
    rg::Graph g;

    // A texture the graph did not create, already in a known layout.
    const auto existing = g.importTexture("lightmap", colorTarget(256, 256),
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const auto out = g.importTexture("swapchain", colorTarget(), VK_IMAGE_LAYOUT_UNDEFINED);

    const auto use = g.addPass("use");
    g.read(use, existing, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.write(use, out, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    g.markOutput(out);
    check(g.compile(), "compiles");

    uint32_t barriersForExisting = 0;
    for (const rg::Barrier& b : g.pass(use).barriers) {
        if (b.resource == existing) {
            ++barriersForExisting;
        }
    }
    check(barriersForExisting == 0, "no barrier when the layout already matches");
    check(g.distinctAllocations() == 0, "imported resources are not allocated by the graph");
}

void testRejectsReadBeforeWrite()
{
    std::printf("rejects reading undefined contents\n");
    rg::Graph g;

    const auto tex = g.createTexture("never written", colorTarget());
    const auto use = g.addPass("use");
    g.read(use, tex, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    check(!g.compile(), "compilation fails");
    check(g.lastError().find("never written") != std::string::npos, "names the offending resource");
    check(g.lastError().find("use") != std::string::npos, "names the offending pass");
}

void testRejectsUnknownResource()
{
    std::printf("rejects unknown resources\n");
    rg::Graph g;

    const auto pass = g.addPass("bad");
    g.write(pass, 42, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    check(!g.compile(), "compilation fails");
    check(g.lastError().find("unknown resource") != std::string::npos, "says what is wrong");
}

void testFrameShapedGraph()
{
    std::printf("frame-shaped graph\n");
    rg::Graph g;

    const rg::TextureDesc full = colorTarget();
    rg::TextureDesc depthDesc = colorTarget();
    depthDesc.format = VK_FORMAT_D32_SFLOAT;
    depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    const auto depth = g.createTexture("depth", depthDesc);
    const auto scene = g.createTexture("scene", full);
    const auto glow = g.createTexture("glow", full);
    const auto swapchain = g.importTexture("swapchain", full, VK_IMAGE_LAYOUT_UNDEFINED);

    const auto prepass = g.addPass("depth prepass");
    g.write(prepass, depth, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    const auto opaque = g.addPass("opaque");
    g.read(opaque, depth, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    g.write(opaque, scene, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    g.write(opaque, glow, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const auto post = g.addPass("post");
    g.read(post, scene, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.read(post, glow, kFragment, kShaderRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g.write(post, swapchain, kColorOut, kColorWrite, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    g.markOutput(swapchain);
    check(g.compile(), "compiles");
    check(g.order().size() == 3, "keeps all three passes");

    // The depth prepass result is read by the opaque pass, which is the whole
    // point of having one; the transition must be derived, not assumed.
    bool depthTransition = false;
    for (const rg::Barrier& b : g.pass(opaque).barriers) {
        if (b.resource == depth && b.oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            && b.newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
            depthTransition = true;
        }
    }
    check(depthTransition, "depth moves from attachment to read-only between the passes");
    check(g.resource(swapchain).finalLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          "tracks the final layout for the present transition");
}

}  // namespace

int main()
{
    testWriteThenReadInsertsOneBarrier();
    testReadAfterReadNeedsNoBarrier();
    testCullsPassesNothingConsumes();
    testAliasesNonOverlappingTransients();
    testDoesNotAliasAcrossOverlappingLifetimes();
    testDoesNotAliasDifferentDescriptions();
    testImportedResourcesKeepTheirLayout();
    testRejectsReadBeforeWrite();
    testRejectsUnknownResource();
    testFrameShapedGraph();

    std::printf("\n%d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

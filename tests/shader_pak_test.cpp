/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// Tests for the SPIR-V pak reader.
//
// The pak is external data, so per docs/CODING-STANDARDS.md section 5.2 every
// field is validated on read and every rejection path needs a test. Runs
// without Vulkan and without a GPU, so it is cheap enough to sit in every CI
// build.
//
// Optionally point it at a real pak:
//     shader_pak_test [path/to/shaders.pak]

#include "../code/rd-vulkan/vk_shader_pak.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

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

// Minimal packer mirroring shadergen.py, so the tests do not need the tool.
std::vector<uint8_t> buildPak(const std::vector<std::pair<std::string, std::vector<uint32_t>>>& modules)
{
    struct Record
    {
        uint64_t hash;
        std::vector<uint32_t> words;
    };

    std::vector<Record> records;
    records.reserve(modules.size());
    for (const auto& [name, words] : modules) {
        records.push_back({ShaderPak_HashName(name.c_str()), words});
    }
    std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) { return a.hash < b.hash; });

    const uint32_t count = static_cast<uint32_t>(records.size());
    const size_t headerSize = 16;
    const size_t tableSize = static_cast<size_t>(count) * 16;

    std::vector<uint8_t> out(headerSize + tableSize);
    const uint32_t magic = kShaderPakMagic;
    const uint32_t version = kShaderPakVersion;
    const uint32_t reserved = 0;
    std::memcpy(out.data() + 0, &magic, 4);
    std::memcpy(out.data() + 4, &version, 4);
    std::memcpy(out.data() + 8, &count, 4);
    std::memcpy(out.data() + 12, &reserved, 4);

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t offset = static_cast<uint32_t>(out.size());
        const uint32_t size = static_cast<uint32_t>(records[i].words.size() * sizeof(uint32_t));
        uint8_t* record = out.data() + headerSize + static_cast<size_t>(i) * 16;
        std::memcpy(record + 0, &records[i].hash, 8);
        std::memcpy(record + 8, &offset, 4);
        std::memcpy(record + 12, &size, 4);
        const uint8_t* blob = reinterpret_cast<const uint8_t*>(records[i].words.data());
        out.insert(out.end(), blob, blob + size);
    }
    return out;
}

void testRoundTrip()
{
    std::printf("round trip\n");
    const std::vector<std::pair<std::string, std::vector<uint32_t>>> modules = {
        {"vert_lightmap_tx1_fog", {0x07230203, 1, 2, 3}},
        {"frag_tx0_ident", {0x07230203, 9}},
        {"normalmap_comp", {0x07230203, 4, 5}},
    };
    const std::vector<uint8_t> pakData = buildPak(modules);

    ShaderPak pak;
    check(pak.open(pakData.data(), pakData.size()), "opens a well formed pak");
    check(pak.count() == modules.size(), "reports the right module count");

    for (const auto& [name, words] : modules) {
        size_t size = 0;
        const uint32_t* found = pak.find(name.c_str(), &size);
        check(found != nullptr, "finds every packed module");
        if (found != nullptr) {
            check(size == words.size() * sizeof(uint32_t), "reports the right size");
            check(std::memcmp(found, words.data(), size) == 0, "returns the right bytes");
            check((reinterpret_cast<uintptr_t>(found) % 4) == 0, "returns 4 byte aligned words");
        }
    }

    size_t size = 0;
    check(pak.find("does_not_exist", &size) == nullptr, "misses an absent module");
}

void testRejectsMalformed()
{
    std::printf("rejects malformed input\n");
    const std::vector<uint8_t> good = buildPak({{"a", {0x07230203, 1}}, {"b", {0x07230203, 2}}});

    ShaderPak pak;
    check(!pak.open(nullptr, 0), "rejects a null pointer");
    check(!pak.open(good.data(), 4), "rejects a truncated header");

    std::vector<uint8_t> badMagic = good;
    badMagic[0] ^= 0xFF;
    check(!pak.open(badMagic.data(), badMagic.size()), "rejects a bad magic");

    std::vector<uint8_t> badVersion = good;
    badVersion[4] = 0xFE;
    check(!pak.open(badVersion.data(), badVersion.size()), "rejects an unknown version");

    // Count large enough that the entry table cannot fit; this is the field an
    // attacker would inflate to walk off the end of the mapping.
    std::vector<uint8_t> hugeCount = good;
    const uint32_t huge = 0xFFFFFFFFu;
    std::memcpy(hugeCount.data() + 8, &huge, 4);
    check(!pak.open(hugeCount.data(), hugeCount.size()), "rejects a count that overflows the table");

    // Offset pointing past the end of the file.
    std::vector<uint8_t> badOffset = good;
    const uint32_t past = static_cast<uint32_t>(good.size() + 4096);
    std::memcpy(badOffset.data() + 16 + 8, &past, 4);
    check(!pak.open(badOffset.data(), badOffset.size()), "rejects an out of range offset");

    // Size that runs past the end from a valid offset.
    std::vector<uint8_t> badSize = good;
    const uint32_t big = 0xFFFFFF00u;
    std::memcpy(badSize.data() + 16 + 12, &big, 4);
    check(!pak.open(badSize.data(), badSize.size()), "rejects a size that runs past the end");

    // Unsorted table: lookup is a binary search, so order is a correctness
    // requirement rather than an optimisation.
    std::vector<uint8_t> unsorted = good;
    uint64_t first = 0;
    uint64_t second = 0;
    std::memcpy(&first, unsorted.data() + 16, 8);
    std::memcpy(&second, unsorted.data() + 32, 8);
    std::memcpy(unsorted.data() + 16, &second, 8);
    std::memcpy(unsorted.data() + 32, &first, 8);
    check(!pak.open(unsorted.data(), unsorted.size()), "rejects an unsorted table");

    // Misaligned blob: SPIR-V must be readable as a word stream.
    std::vector<uint8_t> misaligned = good;
    uint32_t offset = 0;
    std::memcpy(&offset, misaligned.data() + 16 + 8, 4);
    offset += 1;
    std::memcpy(misaligned.data() + 16 + 8, &offset, 4);
    check(!pak.open(misaligned.data(), misaligned.size()), "rejects a misaligned blob");
}

void testHashMatchesGenerator()
{
    std::printf("hash agreement with shadergen.py\n");
    // Reference values printed by tools/shadergen/shadergen.py name_hash(). If
    // the two implementations ever disagree, every lookup silently misses and
    // the renderer ends up with no modules at all, so these are pinned rather
    // than recomputed by the algorithm under test.
    check(ShaderPak_HashName("") == 0xCBF29CE484222325ULL, "empty string");
    check(ShaderPak_HashName("frag_tx0_ident") == 0x028574BC7B30A583ULL, "frag_tx0_ident");
    check(ShaderPak_HashName("vert_lightmap_tx1_fog") == 0x7FD9014ED5682712ULL, "vert_lightmap_tx1_fog");
    check(ShaderPak_HashName("normalmap_comp") == 0x9B6AFD74659B173EULL, "normalmap_comp");
    check(ShaderPak_HashName("a") != ShaderPak_HashName("b"), "distinct names differ");
}

void testRealPak(const char* path)
{
    std::printf("real pak: %s\n", path);
    std::FILE* fh = std::fopen(path, "rb");
    if (fh == nullptr) {
        std::printf("  skipped (not found)\n");
        return;
    }
    std::fseek(fh, 0, SEEK_END);
    const long size = std::ftell(fh);
    std::fseek(fh, 0, SEEK_SET);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    const size_t read = std::fread(data.data(), 1, data.size(), fh);
    std::fclose(fh);
    check(read == data.size(), "reads the whole file");

    ShaderPak pak;
    check(pak.open(data.data(), data.size()), "opens the generated pak");
    std::printf("  %u module(s), %.0f KiB\n", pak.count(), size / 1024.0);

    size_t moduleSize = 0;
    const uint32_t* words = pak.find("frag_tx0_ident", &moduleSize);
    check(words != nullptr, "finds a known variant by name");
    if (words != nullptr) {
        check(words[0] == 0x07230203u, "blob starts with the SPIR-V magic number");
    }
}

}  // namespace

int main(int argc, char** argv)
{
    testRoundTrip();
    testRejectsMalformed();
    testHashMatchesGenerator();
    if (argc > 1) {
        testRealPak(argv[1]);
    }

    std::printf("\n%d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

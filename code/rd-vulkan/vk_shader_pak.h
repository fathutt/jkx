/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

// SPIR-V module storage.
//
// The upstream renderer compiled every shader offline into shaders/spirv/
// shader_data.c: 61.7 MB and 1.9 million lines of C, committed to git, and
// 572 vkCreateShaderModule calls issued at startup whether or not the modules
// were ever used.
//
// Here the same blobs live in a single shaders.pak built by
// tools/shadergen/shadergen.py, and modules are created on first use.
//
// Pak layout, little-endian throughout:
//
//   struct Header {
//       char     magic[4];   // "JKSP"
//       uint32_t version;    // kShaderPakVersion
//       uint32_t count;
//       uint32_t reserved;   // 0
//   };
//   struct Entry {           // count entries, sorted ascending by hash
//       uint64_t hash;       // FNV-1a of the variant name, 64 bit
//       uint32_t offset;     // from the start of the pak
//       uint32_t size;       // bytes, unpadded
//   };
//   // blobs, each padded to a 4 byte boundary so SPIR-V stays aligned
//
// Entries are sorted so lookup is a binary search with no allocation and no
// hash table to build at load time. shadergen.py rejects a collision at pack
// time rather than leaving it to be discovered at runtime.

#ifndef VK_SHADER_PAK_H
#define VK_SHADER_PAK_H

#include <cstddef>
#include <cstdint>

constexpr uint32_t kShaderPakMagic = 0x50534B4A;  // 'JKSP' little-endian
constexpr uint32_t kShaderPakVersion = 1;

// FNV-1a, 64 bit. Must stay identical to name_hash() in shadergen.py.
constexpr uint64_t ShaderPak_HashName(const char* name)
{
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (const char* p = name; *p != '\0'; ++p) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(*p));
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

class ShaderPak
{
public:
    // Takes a borrowed view of an already loaded pak; the caller keeps ownership
    // of the memory and must outlive this object. Returns false and leaves the
    // object empty if the data is not a pak this build understands.
    bool open(const void* data, size_t size);

    void close();

    [[nodiscard]] bool isOpen() const { return m_entries != nullptr; }

    [[nodiscard]] uint32_t count() const { return m_count; }

    // Returns the SPIR-V words for a variant, or nullptr if absent.
    // outSizeBytes is written only on success.
    [[nodiscard]] const uint32_t* find(uint64_t hash, size_t* outSizeBytes) const;

    [[nodiscard]] const uint32_t* find(const char* name, size_t* outSizeBytes) const
    {
        return find(ShaderPak_HashName(name), outSizeBytes);
    }

private:
    struct Entry
    {
        uint64_t hash;
        uint32_t offset;
        uint32_t size;
    };

    const uint8_t* m_base = nullptr;
    const Entry* m_entries = nullptr;
    uint32_t m_count = 0;
    size_t m_size = 0;
};

#endif  // VK_SHADER_PAK_H

/*
===========================================================================
Copyright (C) 2026 JKX contributors

This file is part of JKX.

JKX is free software; you can redistribute it and/or modify it under the terms
of the GNU General Public License version 2 as published by the Free Software
Foundation.
===========================================================================
*/

#include "vk_shader_pak.h"

#include <cstring>

namespace
{

// Everything read out of the pak is treated as hostile until proven otherwise;
// a truncated or crafted file must fail the open, not fault later during a draw
// (see docs/CODING-STANDARDS.md section 5.2).
struct PakHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t reserved;
};

constexpr size_t kHeaderSize = sizeof(PakHeader);
constexpr size_t kEntrySize = 16;  // uint64 + uint32 + uint32, no padding

uint32_t readU32(const uint8_t* p)
{
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

uint64_t readU64(const uint8_t* p)
{
    uint64_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

}  // namespace

bool ShaderPak::open(const void* data, size_t size)
{
    close();

    if (data == nullptr || size < kHeaderSize) {
        return false;
    }

    const uint8_t* base = static_cast<const uint8_t*>(data);

    if (readU32(base + 0) != kShaderPakMagic) {
        return false;
    }
    if (readU32(base + 4) != kShaderPakVersion) {
        return false;
    }

    const uint32_t count = readU32(base + 8);

    // Table must fit, without the multiplication wrapping.
    if (count > (size - kHeaderSize) / kEntrySize) {
        return false;
    }

    // The pak stores entries unaligned relative to the mapping, so parse into a
    // typed view only after validating each record.
    const uint8_t* table = base + kHeaderSize;
    const size_t blobsBegin = kHeaderSize + static_cast<size_t>(count) * kEntrySize;

    uint64_t previousHash = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* record = table + static_cast<size_t>(i) * kEntrySize;
        const uint64_t hash = readU64(record + 0);
        const uint32_t offset = readU32(record + 8);
        const uint32_t length = readU32(record + 12);

        // Sorted and unique: lookup depends on it, and a duplicate would mean
        // the packer let a hash collision through.
        if (i != 0 && hash <= previousHash) {
            return false;
        }
        previousHash = hash;

        if (offset < blobsBegin || length == 0) {
            return false;
        }
        if (offset > size || length > size - offset) {
            return false;
        }
        // SPIR-V is a word stream; anything else means a corrupt pak.
        if ((offset % 4) != 0 || (length % 4) != 0) {
            return false;
        }
    }

    m_base = base;
    m_entries = reinterpret_cast<const Entry*>(table);
    m_count = count;
    m_size = size;
    return true;
}

void ShaderPak::close()
{
    m_base = nullptr;
    m_entries = nullptr;
    m_count = 0;
    m_size = 0;
}

const uint32_t* ShaderPak::find(uint64_t hash, size_t* outSizeBytes) const
{
    if (m_entries == nullptr) {
        return nullptr;
    }

    uint32_t low = 0;
    uint32_t high = m_count;
    while (low < high) {
        const uint32_t mid = low + (high - low) / 2;
        const uint8_t* record = reinterpret_cast<const uint8_t*>(m_entries) + static_cast<size_t>(mid) * kEntrySize;
        const uint64_t candidate = readU64(record);
        if (candidate < hash) {
            low = mid + 1;
        } else if (candidate > hash) {
            high = mid;
        } else {
            if (outSizeBytes != nullptr) {
                *outSizeBytes = readU32(record + 12);
            }
            return reinterpret_cast<const uint32_t*>(m_base + readU32(record + 8));
        }
    }
    return nullptr;
}

// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// Kodachi
#include <kodachi/attribute/Attribute.h>

namespace kodachi
{
    std::vector<char> convertToBinary(const kodachi::GroupAttribute& attr);
    kodachi::GroupAttribute readFromBinary(const char* bin, std::size_t size);

    void convertToBinary_directDiskWrite(const kodachi::GroupAttribute& attr, const std::string& filename);
    kodachi::GroupAttribute readFromBinary_directDiskRead(const std::string& filename);
} // namespace kodachi


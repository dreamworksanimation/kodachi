// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// Foundry
#include <internal/FnPlatform/StringView.h>

namespace kodachi {

using string_view = FnPlatform::StringView;

struct StringViewHash
{
    size_t operator()(const kodachi::string_view& key) const
    {
        return std::_Hash_impl::hash(key.data(), key.length());
    }

    size_t hash(const kodachi::string_view& key) const
    {
        return operator()(key);
    }

    bool equal(const kodachi::string_view& key, const kodachi::string_view& other) const
    {
        return key == other;
    }
};

/**
 * Concatenation helpers.
 *
 * TODO: Performance profiling. Can probably be improved by copying
 * data ourselves instead of multiple calls to append.
 */
inline std::string
concat(const string_view& a, const string_view& b)
{
    std::string result;
    result.reserve(a.size() + b.size());

    result.append(a.data(), a.size())
          .append(b.data(), b.size());

    return result;
}

inline std::string
concat(const string_view& a, const string_view& b, const string_view& c)
{
    std::string result;
    result.reserve(a.size() + b.size() + c.size());

    result.append(a.data(), a.size())
          .append(b.data(), b.size())
          .append(c.data(), c.size());

    return result;
}

inline std::string
_concat(std::initializer_list<string_view> svs)
{
    std::size_t resultSize = 0;
    for (const string_view& sv : svs) {
        resultSize += sv.size();
    }

    std::string result;
    result.reserve(resultSize);
    for (const string_view& sv : svs) {
        result.append(sv.data(), sv.size());
    }

    return result;
}

template <typename... SV>
inline std::string
concat(const string_view& a, const string_view& b,
       const string_view& c, const string_view& d,
       const SV&... args)
{
    return _concat({a, b, c, d, static_cast<const string_view&>(args)...});
}

} // namespace kodachi


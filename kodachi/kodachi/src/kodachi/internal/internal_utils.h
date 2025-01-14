// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// Linux
#include <limits.h>
#include <sys/stat.h>

// C++
#include <string>
#include <vector>

#ifndef POSIX_RET_SUCCESS
#define POSIX_RET_SUCCESS 0
#endif

#ifndef POSIX_RET_FAILURE
#define POSIX_RET_FAILURE -1
#endif

namespace kodachi
{
    namespace internal
    {
        inline bool
        fileOrDirExists(const std::string& dir)
        {
            struct stat buffer;
            return (::stat(dir.c_str(), &buffer) == POSIX_RET_SUCCESS);
        }

        inline std::string
        absolutePath(const std::string& path)
        {
            char resolved_path[PATH_MAX + 1];
            const char* returned_resolved_path = ::realpath(path.c_str(), resolved_path);
            return { (returned_resolved_path != nullptr ? returned_resolved_path : "") };
        }

        inline std::vector<std::string>
        splitString(const std::string& str, char delim)
        {
            if (str.empty()) {
                return {};
            }

            std::vector<std::string> result;
            const std::size_t strSize = str.size();
            std::size_t i = 0;
            std::size_t substrStartIdx = 0;
            for (; i < strSize; ++i) {
                if (str[i] == delim) {
                    if (i != substrStartIdx) {
                        result.emplace_back(
                                str.begin() + substrStartIdx,
                                str.begin() + i);
                    }
                    substrStartIdx = i + 1;
                }
            }

            // In case no delimiter at the end of the input string
            if (substrStartIdx != i) {
                result.emplace_back(str.begin() + substrStartIdx, str.end());
            }

            // In case no delimiter found when input string is not empty,
            // add the whole input string to the result.
            if (result.empty() && !str.empty()) {
                result.emplace_back(str);
            }

            return result;
        }
    } // namespace internal

    // concatenating paths
    inline std::string
    operator/(const std::string& lhs, const std::string& rhs)
    {
        if (lhs.back() == '/') {
            return lhs + rhs;
        }

        return (lhs + "/" + rhs);
    }

} // namespace kodachi


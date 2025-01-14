// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// C/C++
#include <cstring>
#include <stdlib.h>
#include <type_traits>
#include <vector>

#define KODACHI_CACHE_BLOCK_ALIGNMENT 512u

namespace kodachi
{
    template <typename T>
    class block_aligned_allocator
    {
    public:
        using value_type = T;

        block_aligned_allocator() noexcept {}  // not required, unless used
        template <typename U> block_aligned_allocator(block_aligned_allocator<U> const&) noexcept {}

        value_type*  // Use pointer if pointer is not a value_type*
        allocate(std::size_t n)
        {
            value_type* data_ptr = nullptr;
            const int err = ::posix_memalign(reinterpret_cast<void**>( &data_ptr ),
                                             KODACHI_CACHE_BLOCK_ALIGNMENT,
                                             (n * sizeof(value_type)));
            // Allocation failed if (err != 0)
            if (err != 0 || data_ptr == nullptr) {
                throw std::bad_alloc();
            }

            return data_ptr;
        }

        void
        deallocate(value_type* p, std::size_t) noexcept  // Use pointer if pointer is not a value_type*
        {
            ::free(p);
        }
    };

    template <typename T, typename U>
    bool
    operator==(block_aligned_allocator<T> const&, block_aligned_allocator<U> const&) noexcept
    {
        return true;
    }

    template <typename T, typename U>
    bool
    operator!=(block_aligned_allocator<T> const& x, block_aligned_allocator<U> const& y) noexcept
    {
        return !(x == y);
    }
} // namespace kodachi


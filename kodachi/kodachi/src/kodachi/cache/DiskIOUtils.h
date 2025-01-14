// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// C/C++
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Local
#include "AlignedBufferAllocator.h"
#include "CacheUtils.h"

namespace kodachi
{
namespace cache_utils
{
    class StdDiskIO_C
    {
    public:
        using Buffer_t = std::vector<char>;

        static Buffer_t read(const std::string& path)
        {
            std::FILE* fhandle = std::fopen(path.c_str(), "r");
            if (fhandle == nullptr) {
                std::cout << "\n  [KodachiCache] failed to read file [" << path << "]." << std::endl;
                return { };
            }

            //-----------------------
            // First, find the file size in bytes
            //-----------------------

            // Move position indicator to the end of file
            if (std::fseek(fhandle, 0, SEEK_END) != 0) {
                std::cout << "\n  [KodachiCache] failed to read file [" << path << "] (std::fseek(SEEK_END) failed): "
                          << std::strerror(errno)
                          << "." << std::endl;

                std::fclose(fhandle);
                return { };
            }

            // Get character count at the end of file (1 char == 1 byte)
            const long int fileSizeBytes = std::ftell(fhandle);
            if (fileSizeBytes == -1) {
                std::cout << "\n  [KodachiCache] failed to read file [" << path << "] (std::ftell() failed): "
                          << std::strerror(errno)
                          << "." << std::endl;

                std::fclose(fhandle);
                return { };
            }

            // Move position indicator back to the beginning of file
            if (std::fseek(fhandle, 0, SEEK_SET) != 0) {
                std::cout << "\n  [KodachiCache] failed to read file [" << path << "] (std::fseek(SEEK_SET) failed): "
                          << std::strerror(errno)
                          << "." << std::endl;

                std::fclose(fhandle);
                return { };
            }

            //-----------------------
            // Allocate a char buffer (same size as the file)
            //-----------------------

            Buffer_t buffer(fileSizeBytes);

            //-----------------------
            // Read the file
            //-----------------------

            if (std::fread(static_cast<void*>(buffer.data()),
                           sizeof(decltype(buffer)::value_type), // size of 1 object
                           buffer.size(), // number of objects to read
                           fhandle) != buffer.size()) {

                std::cout << "\n  [KodachiCache] failed to read file [" << path << "] (std::fread() failed): "
                          << std::strerror(errno)
                          << "." << std::endl;

                std::fclose(fhandle);
                return { };
            }

            std::fclose(fhandle);
            return buffer;
        }

        static bool write(const char* buffer, std::size_t buffer_size, const std::string& path)
        {
            static const std::string tmpFileExtension = ".tmp";

            // Write a *.tmp file first; after file successfully written to disk, remove the file extension.
            const std::string tmpFilePath = path + tmpFileExtension;

            std::FILE* fhandle = std::fopen(tmpFilePath.c_str(), "w");
            if (fhandle == nullptr) {
                std::cout << "\n  [KodachiCache] failed to write file [" << tmpFilePath << "] (invalid file handle)." << std::endl;
                return false;
            }

            if (std::fwrite(static_cast<const void*>(buffer),
                            // size of 1 object
                            sizeof(char),
                            // number of objects to read
                            buffer_size,
                            fhandle) != buffer_size) {

                std::cout << "\n  [KodachiCache] failed to write file [" << tmpFilePath << "] (std::fwrite() failed): "
                          << std::strerror(errno)
                          << "." << std::endl;

                std::fclose(fhandle);
                return false;
            }

            std::fclose(fhandle);

            // File successfully written to disk, remove the file extension (.tmp)
            if (std::rename(tmpFilePath.c_str(), path.c_str()) != 0 /* success == 0 */) {
                // If the file already exists (created and renamed by another thread),
                // then there is no need to print an error message.
                if (!kodachi::cache_utils::fileOrDirExists(path)) {
                    std::cout << "\n  [KodachiCache] failed to rename temp file [" << tmpFilePath << "] to ["
                              << path << "] (std::fwrite() failed): "
                              << std::strerror(errno)
                              << "." << std::endl;
                }
            }

            return true;
        }

        static bool write(const Buffer_t& buffer, const std::string& path)
        {
            return StdDiskIO_C::write(buffer.data(), buffer.size(), path);
        }
    };

    class StdDiskIO_Cpp
    {
    public:
        using Buffer_t = std::vector<char>;

        static Buffer_t read(const std::string& path)
        {
            std::fstream f_in(path, std::ios::in | std::ios::binary | std::ios::ate);
            if (f_in.fail()) {
                std::cout << "\n  [KodachiCache] failed to read file [" << path << "]." << std::endl;
                return { };
            }

            const std::size_t fileSize = f_in.tellg();
            f_in.seekg(0, std::ios::beg);

            Buffer_t buffer(fileSize);
            f_in.read(buffer.data(), fileSize);

            return buffer;
        }

        static bool write(const char* buffer, std::size_t buffer_size, const std::string& path)
        {
            static const std::string tmpFileExtension = ".tmp";

            // Write a *.tmp file first; after file successfully written to disk, remove the file extension.
            const std::string tmpFilePath = path + tmpFileExtension;

            std::fstream f_out(tmpFilePath, std::ios::out | std::ios::binary);
            if (f_out.fail()) {
                std::cout << "\n  [KodachiCache] failed to open/create file [" << tmpFilePath << "]." << std::endl;
                return false;
            }

            f_out.write(buffer, buffer_size);
            if (f_out.bad()) {
                std::cout << "\n  [KodachiCache] failed to write file [" << tmpFilePath << "]." << std::endl;
                return false;
            }

            // File successfully written to disk, remove the file extension (.tmp)
            if (std::rename(tmpFilePath.c_str(), path.c_str()) != 0 /* success == 0 */) {
                // If the file already exists (created and renamed by another thread),
                // then there is no need to print an error message.
                if (!kodachi::cache_utils::fileOrDirExists(path)) {
                    std::cout << "\n  [KodachiCache] failed to rename temp file [" << tmpFilePath << "] to ["
                              << path << "] (std::fwrite() failed): "
                              << std::strerror(errno)
                              << "." << std::endl;
                }
            }

            return true;
        }

        static bool write(const Buffer_t& buffer, const std::string& path)
        {
            return StdDiskIO_Cpp::write(buffer.data(), buffer.size(), path);
        }
    };

    class PosixDiskIO
    {
    public:
        using Buffer_t = std::vector<char>;

        static Buffer_t read(const std::string& path)
        {
            constexpr int posix_open_flags        = O_RDONLY /* | O_SYNC */;
            constexpr int posix_open_access_flags = S_IRUSR | S_IWUSR; // read and write access to all
            const int file_desc = ::open(path.c_str(), posix_open_flags, posix_open_access_flags);
            if (file_desc == -1) {
                std::cout << "\n  [KodachiCache] failed to read file [" << path << "] (::open() failed): "
                          << std::strerror(errno)
                          << "." << std::endl;
                return { };
            }

            const std::size_t fileSizeBytes = getFileSize(path);
            Buffer_t buffer(fileSizeBytes);

            constexpr std::size_t maxChunkSizeBytes = 2UL * 1024UL * 1024UL * 1024UL; // 2GB
            const std::size_t num_of_2gb_chunks  = fileSizeBytes / maxChunkSizeBytes;
            const std::size_t size_of_last_chunk = fileSizeBytes % maxChunkSizeBytes;

            std::size_t offset_bytes = 0u;
            for (std::size_t idx = 0; idx < num_of_2gb_chunks; ++idx) {
                if (::pread(file_desc,
                            buffer.data() + offset_bytes,
                            maxChunkSizeBytes,
                            offset_bytes) == -1) {

                    std::cout << "\n  [KodachiCache] failed to read file [" << path << "] (::pread() failed): "
                              << std::strerror(errno)
                              << "." << std::endl;

                    ::close(file_desc);
                    return { };
                }

                offset_bytes += maxChunkSizeBytes;
            }

            if (size_of_last_chunk > 0) {
                if (::pread(file_desc,
                            buffer.data() + offset_bytes,
                            size_of_last_chunk,
                            offset_bytes) == -1) {

                    std::cout << "\n  [KodachiCache] failed to read file [" << path << "] (::pread() failed): "
                              << std::strerror(errno)
                              << "." << std::endl;

                    ::close(file_desc);
                    return { };
                }
            }

            ::close(file_desc);
            return buffer;
        }

        static bool write(const char* buffer, std::size_t buffer_size, const std::string& path)
        {
            // If the file already exists, remove it from disk to avoid appending.
            // We can't overwrite old files since we need to open a handle with
            // (O_CREAT | O_APPEND) options to be able to handle 2GB+ files.
            //
            if (fileOrDirExists(path)) {
                std::remove(path.c_str());
            }

            constexpr int posix_open_flags        = O_CREAT | O_APPEND | O_WRONLY /* | O_SYNC */;
            constexpr int posix_open_access_flags = S_IRUSR | S_IWUSR; // read and write access to all
            const int file_desc = ::open(path.c_str(), posix_open_flags, posix_open_access_flags);
            if (file_desc == -1) {
                std::cout << "\n  [KodachiCache] failed to write file [" << path << "] (::open() failed): "
                          << std::strerror(errno)
                          << "." << std::endl;
                return false;
            }

            constexpr std::size_t maxChunkSizeBytes = 2UL * 1024UL * 1024UL * 1024UL; // 2GB
            const std::size_t num_of_2gb_chunks     = buffer_size / maxChunkSizeBytes;
            const std::size_t size_of_last_chunk    = buffer_size % maxChunkSizeBytes;

            std::size_t offset_bytes = 0u;
            for (std::size_t idx = 0; idx < num_of_2gb_chunks; ++idx) {
                if (::pwrite(file_desc,
                             buffer + offset_bytes,
                             maxChunkSizeBytes,
                             offset_bytes) == -1) {

                    std::cout << "\n  [KodachiCache] failed to write file [" << path << "] (::pread() failed): "
                              << std::strerror(errno)
                              << "." << std::endl;

                    ::close(file_desc);
                    return false;
                }

                offset_bytes += maxChunkSizeBytes;
            }

            if (size_of_last_chunk > 0) {
                if (::pwrite(file_desc,
                             buffer + offset_bytes,
                             size_of_last_chunk,
                             offset_bytes) == -1) {

                    std::cout << "\n  [KodachiCache] failed to write file [" << path << "] (::pread() failed): "
                              << std::strerror(errno)
                              << "." << std::endl;

                    ::close(file_desc);
                    return false;
                }
            }

            ::close(file_desc);
            return true;
        }

        static bool write(const Buffer_t& buffer, const std::string& path)
        {
            return PosixDiskIO::write(buffer.data(), buffer.size(), path);
        }
    };

    class PosixDirectDiskIO
    {
    public:
        using Buffer_t = std::vector<char, block_aligned_allocator<char>>;

        static Buffer_t read(const std::string& path)
        {
            constexpr int posix_open_flags        = O_RDONLY | O_DIRECT /* | O_SYNC */;
            constexpr int posix_open_access_flags = S_IRUSR | S_IWUSR; // read and write access to all
            const int file_desc = ::open(path.c_str(), posix_open_flags, posix_open_access_flags);
            if (file_desc == -1) {
                std::cout << "\n  [KodachiCache] failed to read file [" << path << "] (::open(O_DIRECT) failed): "
                          << std::strerror(errno)
                          << "." << std::endl;
                return { };
            }

            const std::size_t fileSizeBytes = getFileSize(path);
            Buffer_t buffer(fileSizeBytes);

            constexpr std::size_t maxChunkSizeBytes = 2UL * 1024UL * 1024UL * 1024UL; // 2GB
            const std::size_t num_of_2gb_chunks  = fileSizeBytes / maxChunkSizeBytes;
            const std::size_t size_of_last_chunk = fileSizeBytes % maxChunkSizeBytes;

            std::size_t offset_bytes = 0u;
            for (std::size_t idx = 0; idx < num_of_2gb_chunks; ++idx) {
                if (::pread(file_desc,
                            buffer.data() + offset_bytes,
                            maxChunkSizeBytes,
                            offset_bytes) == -1) {

                    std::cout << "\n  [KodachiCache] failed to read file [" << path << "] (::pread() failed, O_DIRECT): "
                              << std::strerror(errno)
                              << "." << std::endl;

                    ::close(file_desc);
                    return { };
                }

                offset_bytes += maxChunkSizeBytes;
            }

            if (size_of_last_chunk > 0) {
                if (::pread(file_desc,
                            buffer.data() + offset_bytes,
                            size_of_last_chunk,
                            offset_bytes) == -1) {

                    std::cout << "\n  [KodachiCache] failed to read file [" << path << "] (::pread() failed, O_DIRECT): "
                              << std::strerror(errno)
                              << "." << std::endl;

                    ::close(file_desc);
                    return { };
                }
            }

            ::close(file_desc);
            return buffer;
        }

        static bool write(const char* aligned_buffer, std::size_t aligned_buffer_size, const std::string& path)
        {
            // If the file already exists, remove it from disk to avoid appending.
            // We can't overwrite old files since we need to open a handle with
            // (O_CREAT | O_APPEND) options to be able to handle 2GB+ files.
            //
            if (fileOrDirExists(path)) {
                std::remove(path.c_str());
            }

            constexpr int posix_open_flags        = O_CREAT | O_APPEND | O_WRONLY | O_DIRECT /* | O_SYNC */;
            constexpr int posix_open_access_flags = S_IRUSR | S_IWUSR; // read and write access to all
            const int file_desc = ::open(path.c_str(), posix_open_flags, posix_open_access_flags);
            if (file_desc == -1) {
                std::cout << "\n  [KodachiCache] failed to write file [" << path << "] (::open() failed, O_DIRECT): "
                          << std::strerror(errno)
                          << "." << std::endl;
                return false;
            }

            constexpr std::size_t maxChunkSizeBytes = 2UL * 1024UL * 1024UL * 1024UL; // 2GB
            const std::size_t num_of_2gb_chunks     = aligned_buffer_size / maxChunkSizeBytes;
            const std::size_t size_of_last_chunk    = aligned_buffer_size % maxChunkSizeBytes;

            std::size_t offset_bytes = 0u;
            for (std::size_t idx = 0; idx < num_of_2gb_chunks; ++idx) {
                if (::pwrite(file_desc,
                             aligned_buffer + offset_bytes,
                             maxChunkSizeBytes,
                             offset_bytes) == -1) {

                    std::cout << "\n  [KodachiCache] failed to write file [" << path << "] (::pread() failed, O_DIRECT): "
                              << std::strerror(errno)
                              << "." << std::endl;

                    ::close(file_desc);
                    return false;
                }

                offset_bytes += maxChunkSizeBytes;
            }

            if (size_of_last_chunk > 0) {
                if (::pwrite(file_desc,
                             aligned_buffer + offset_bytes,
                             size_of_last_chunk,
                             offset_bytes) == -1) {

                    std::cout << "\n  [KodachiCache] failed to write file [" << path << "] (::pread() failed, O_DIRECT): "
                              << std::strerror(errno)
                              << "." << std::endl;

                    ::close(file_desc);
                    return false;
                }
            }

            ::close(file_desc);
            return true;
        }

        static bool write(const Buffer_t& aligned_buffer, const std::string& path)
        {
            return PosixDirectDiskIO::write(aligned_buffer.data(), aligned_buffer.size(), path);
        }
    };

    //-----------------------------------------------------------------


} // namespace cache_utils
} // namespace kodachi


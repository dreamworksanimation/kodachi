// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// C/C++
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include <system_error>

// Linux
#include <dirent.h>
#include <fcntl.h>
#include <fts.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef POSIX_RET_SUCCESS
#define POSIX_RET_SUCCESS 0
#endif

#ifndef POSIX_RET_FAILURE
#define POSIX_RET_FAILURE -1
#endif

namespace kodachi
{
namespace cache_utils
{
    template <typename DurationType>
    inline DurationType
    ctime_to_std_duration(const timespec& ctime)
    {
        // Use std::chrono::system_clock since input is probably
        // built by a C or POSIX function.
        std::chrono::system_clock::time_point cpptime(
                  std::chrono::seconds(ctime.tv_sec)
                + std::chrono::nanoseconds(ctime.tv_nsec));

        return std::chrono::time_point_cast<DurationType>(cpptime).time_since_epoch();
    }

    template <typename DurationType>
    inline std::uint64_t
    getTimeElapsed(const timespec& ctime)
    {
        auto prevTime = ctime_to_std_duration<std::chrono::nanoseconds>(ctime);
        auto currTime = std::chrono::time_point_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now()).time_since_epoch();

        return std::chrono::duration_cast<DurationType>(currTime - prevTime).count();
    }

    template <typename DurationType>
    inline std::uint64_t
    getTimeSinceLastAccess(const std::string& filePath)
    {
        struct stat fileAttributes;
        if (::stat(filePath.c_str(), &fileAttributes) == POSIX_RET_SUCCESS) {
            return cache_utils::getTimeElapsed<DurationType>(fileAttributes.st_atim);
        }

        return 0;
    }

    template <typename DurationType>
    inline std::uint64_t
    getTimeLastModified(const std::string& filePath)
    {
        struct stat fileAttributes;
        if (::stat(filePath.c_str(), &fileAttributes) == POSIX_RET_SUCCESS) {
            return ctime_to_std_duration<DurationType>(fileAttributes.st_mtim).count();
        }

        return 0;
    }

    template <typename DurationType>
    inline std::uint64_t
    getTimeThisProcessStarted(bool * success = nullptr)
    {
        const int mypid = ::getpid();
        const std::string limits_filepath = "/proc/" + std::to_string(mypid) + "/limits";

        struct stat fileAttributes;
        if (::stat(limits_filepath.c_str(), &fileAttributes) == POSIX_RET_SUCCESS) {
            if (success != nullptr) {
                *success = true;
            }

            return ctime_to_std_duration<DurationType>(fileAttributes.st_mtim).count();
        }

        if (success != nullptr) {
            *success = false;
        }

        return 0;
    }

    inline std::size_t
    getFileSize(const std::string& filePath)
    {
        struct stat fileAttributes;
        if (::stat(filePath.c_str(), &fileAttributes) == POSIX_RET_SUCCESS) {
            return fileAttributes.st_size;
        }

        return 0;
    }

    inline std::size_t
    getDirectorySize(const std::string& filePath)
    {
        char* paths[] = { const_cast<char*>(filePath.c_str()), NULL };
        FTS* fts = fts_open(&paths[0], FTS_NOCHDIR, NULL); // NOTE: https://www.freebsd.org/cgi/man.cgi?query=fts_open
        if (fts == NULL) {
            return 0;
        }

        std::size_t dirSize = 0;
        while (true) {
            FTSENT* ftsent = fts_read(fts);
            if (ftsent == NULL) {
                break;
            }

            if (ftsent->fts_info & FTS_F) {
                dirSize += ftsent->fts_statp->st_size;
            }
        }

        if (fts_close(fts) != POSIX_RET_SUCCESS) {
            return 0;
        }

        return dirSize;
    }

    //-----------------------------------------------------------------

    inline bool
    fileOrDirExists(const char* dir)
    {
        struct stat buffer;
        return (::stat(dir, &buffer) == POSIX_RET_SUCCESS);
    }

    inline bool
    fileOrDirExists(const std::string& dir)
    {
        return fileOrDirExists(dir.c_str());
    }

    //-----------------------------------------------------------------

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

    inline std::vector<std::string>
    getLocationStack(const std::string& path)
    {
        const std::size_t pathLength = path.size();
        if (pathLength < 2 || path[0] != '/') {
            return { };
        }

        std::vector<std::string> directories;
        std::string currPath;
        std::size_t startIdx = 1; // skip first '/'
        while (startIdx < pathLength) {
            std::size_t currIdx = path.find('/', startIdx);
            if (currIdx == std::string::npos) {
                currIdx = pathLength;
            }

            if (currIdx == startIdx) {
                ++startIdx;
                continue;
            }

            currPath += ("/" + path.substr(startIdx, (currIdx - startIdx)));
            directories.emplace_back(currPath);

            startIdx = currIdx + 1;
        }

        return directories;
    }

    //-----------------------------------------------------------------

    inline bool
    recursiveMkdir(const std::string& path, __mode_t mode = ACCESSPERMS)
    {
        // Nothing to do if directory already exists
        if (fileOrDirExists(path)) {
            return true;
        }

        const std::vector<std::string> dirs = getLocationStack(path);
        for (const auto& dir : dirs) {
            // Create directory if current 'dir' doesn't exist
            if (!fileOrDirExists(dir)) {
                if (::mkdir(dir.c_str(), mode) == POSIX_RET_FAILURE) {
                    std::cout << "\n  [KodachiCache] failed to create directory hierarchy [" << dir << "]: "
                              << std::strerror(errno)
                              << "." << std::endl;
                    return false;
                }
            }
        }

        return true;
    }

    //-----------------------------------------------------------------

    inline int
    removeFiles(const char* fpath,
                const struct stat* sb,
                int tflag,
                struct FTW* ftwbuf)
    {
        if (tflag == FTW_F) {
            int result = std::remove(fpath);
            if (result != 0) {
                std::cout << "\n  [KodachiCache] failed to remove file ["
                          << fpath << "]: "
                          << std::strerror(errno)
                          << "." << std::endl;
            }
        }

        return FTW_CONTINUE;
    }

    inline int
    removeDirectories(const char* fpath,
                      const struct stat* sb,
                      int tflag,
                      struct FTW* ftwbuf)
    {
        if (tflag == FTW_DP) {
            int result = std::remove(fpath);
            if (result != 0) {
                std::cout << "\n  [KodachiCache] failed to remove directory [" << fpath << "]: "
                          << std::strerror(errno)
                          << "." << std::endl;
            }
        }

        return FTW_CONTINUE;
    }

    inline bool
    removeDirectoryContents(const std::string& path)
    {
        if (path.empty() || !fileOrDirExists(path)) {
            return false;
        }

        if (::nftw(path.c_str(), removeFiles, 64, (FTW_DEPTH | FTW_PHYS)) != POSIX_RET_SUCCESS) {
            throw std::system_error(errno,
                                    std::system_category(),
                                    "[KodachiCache] failed to remove [" + path + "]");
        }

        return true;
    }

    template <std::uint64_t MaxLifeSeconds>
    inline int
    removeStaleFiles(const char* fpath,
                     const struct stat* sb,
                     int tflag,
                     struct FTW* ftwbuf)
    {
        if (tflag == FTW_F) {
            const std::uint64_t secondsSinceLastAccess =
                    getTimeElapsed<std::chrono::seconds>(sb->st_atim);

            if (secondsSinceLastAccess > MaxLifeSeconds) {
                int result = std::remove(fpath);
                if (result != 0) {
                    std::cout << "\n  [KodachiCache] failed to remove [" << fpath << "]: "
                              << std::strerror(errno)
                              << "." << std::endl;
                }
            }
        }

        return FTW_CONTINUE;
    }

    template <std::uint64_t MaxLifeSeconds>
    inline bool
    removeStaleDirectoryContents(const std::string& path)
    {
        if (path.empty() || !fileOrDirExists(path)) {
            return false;
        }

        if (::nftw(path.c_str(), removeStaleFiles<MaxLifeSeconds>, 64, (FTW_DEPTH | FTW_PHYS)) != POSIX_RET_SUCCESS) {
            throw std::system_error(errno,
                                    std::system_category(),
                                    "[KodachiCache] failed to remove [" + path + "]");
        }

        return true;
    }

    inline bool
    removeDirectory(const std::string& path)
    {
        if (path.empty() || !fileOrDirExists(path)) {
            return false;
        }

        if (!removeDirectoryContents(path)) {
            return false;
        }

        if (::nftw(path.c_str(), removeDirectories, 64, (FTW_DEPTH | FTW_PHYS)) != POSIX_RET_SUCCESS) {
            throw std::system_error(errno,
                                    std::system_category(),
                                    "[KodachiCache] failed to remove [" + path + "]");
        }

        return true;
    }

    //-------------------------------------------------------------------------

    class DirectoryStream
    {
    private:
        DIR* m_dir_ptr = nullptr;

    public:
        DirectoryStream() = delete;
        DirectoryStream(const std::string& path)
            : m_dir_ptr(::opendir(path.c_str()))
        {
        }

        ~DirectoryStream()
        {
            if (m_dir_ptr != nullptr) {
                ::closedir(m_dir_ptr);
                m_dir_ptr = nullptr;
            }
        }

        bool isValid() const
        {
            return (m_dir_ptr != nullptr);
        }

        bool
        contains(const std::string& dir_name) const
        {
            if (m_dir_ptr == nullptr) {
                return false;
            }

            struct dirent* dir_entry_ptr = nullptr;
            while ((dir_entry_ptr = ::readdir(m_dir_ptr)) != nullptr) {
                if (dir_name == dir_entry_ptr->d_name) {
                    return true;
                }
            }

            return false;
        }
    };

    //-----------------------------------------------------------------

    // Try to create the directory described by [path] on disk; if directory already
    // exists, try to clean up any files that are older than [cacheLifeSeconds].
    //
    inline bool
    initializeCacheOnDisk(const std::string& path, const std::string& rezResolve)
    {
        // If directory does not exist, *try* to create it.
        if (!cache_utils::fileOrDirExists(path)) {
            if (!cache_utils::recursiveMkdir(path)) {
                // Directory does not exist and we can't create it
                return false;
            }
        }

        if (!rezResolve.empty()) {
            // Write the text file to the parent directory
            const std::string txtFilePath =
                    path.substr(0, path.rfind('/')) + "/rez_packages.txt";

            if (!cache_utils::fileOrDirExists(txtFilePath)) {
                std::FILE* fhandle = std::fopen(txtFilePath.c_str(), "w");
                if (fhandle == nullptr) {
                    std::cout << "\n [KodachiCache] failed to write file [" << txtFilePath << "] (invalid file handle)." << std::endl;
                    return false;
                }

                const std::size_t buffer_size = rezResolve.size() * sizeof(std::string::value_type);
                if (std::fwrite(static_cast<const void*>(rezResolve.c_str()),
                                // size of 1 object
                                sizeof(char),
                                // number of objects to read
                                buffer_size,
                                fhandle) != buffer_size) {

                    std::cout << "\n [KodachiCache] failed to write file [" << txtFilePath << "] (std::fwrite() failed): "
                              << std::strerror(errno)
                              << "." << std::endl;

                    std::fclose(fhandle);
                }
            }
        }

        return true;
    }
} // namespace cache_utils
} // namespace kodachi


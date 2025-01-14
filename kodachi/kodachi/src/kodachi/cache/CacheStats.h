// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// C/C++
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace kodachi
{
namespace cache_utils
{
    //-----------------------------------------------------------------
    // To keep track of read/write performance
    class CacheStats {
    private:
        static constexpr float sBytesToMB               = 1.0f / (1024.0f * 1024.0f);
        static constexpr float sNanosecondsToSeconds    = 1.0f / 1000000000.0f;
        static constexpr float sDefaultDiskPerfMBperSec = 700.0f /* 700 MB/s */;

        mutable std::mutex mMutex;

        struct {
            std::atomic<std::uint32_t> mDiskHitCounter { 0 };
            std::atomic<std::uint32_t> mMemHitCounter  { 0 };

            std::atomic<std::uint32_t> mDiskMissCounter { 0 };
            std::atomic<std::uint32_t> mMemMissCounter  { 0 };

            std::uint64_t mReadTime = 0; // Nanoseconds
            std::uint64_t mReadSize = 0; // Bytes

            std::uint64_t mWriteTime = 0; // Nanoseconds
            std::uint64_t mWriteSize = 0; // Bytes

            std::uint64_t mValueCreationTime = 0; // Nanoseconds
            std::uint64_t mValueCreationSize = 0; // Bytes

            std::atomic<std::uint64_t> mTimeSpentInsideGetValCalls { 0 }; // Nanoseconds

            // MB/Second
            std::atomic<float> mDiskReadPerf      { sDefaultDiskPerfMBperSec };
            std::atomic<float> mDiskWritePerf     { sDefaultDiskPerfMBperSec };
            std::atomic<float> mValueCreationPerf { 0.0f };

        } mCurrent;

        struct {
            std::uint64_t mReadTime = 0; // Nanoseconds
            std::uint64_t mReadSize = 0; // Bytes

            std::uint64_t mWriteTime = 0; // Nanoseconds
            std::uint64_t mWriteSize = 0; // Bytes

            // MB/Second
            // NOTE: average of performance between runs.
            std::atomic<float> mDiskReadPerf  { sDefaultDiskPerfMBperSec };
            std::atomic<float> mDiskWritePerf { sDefaultDiskPerfMBperSec };
        } mHistory;

        //------------------------
    public:
        CacheStats() = default;

        CacheStats(std::uint64_t readTime,
                   std::uint64_t readSize,
                   float readPerf,

                   std::uint64_t writeTime,
                   std::uint64_t writeSize,
                   float writePerf)
        {
             mHistory.mReadTime     = readTime;
             mHistory.mReadSize     = readSize;
             mHistory.mDiskReadPerf = readPerf;

             mHistory.mWriteTime     = writeTime;
             mHistory.mWriteSize     = writeSize;
             mHistory.mDiskWritePerf = writePerf;
        }

        CacheStats(const CacheStats& other)
        {
            std::lock_guard<std::mutex> lock(other.mMutex);

            mCurrent.mDiskHitCounter.store( other.mCurrent.mDiskHitCounter );
            mCurrent.mMemHitCounter.store( other.mCurrent.mMemHitCounter );

            mCurrent.mDiskMissCounter.store( other.mCurrent.mDiskMissCounter );
            mCurrent.mMemMissCounter.store( other.mCurrent.mMemMissCounter );

            mCurrent.mReadTime = other.mCurrent.mReadTime;
            mCurrent.mReadSize = other.mCurrent.mReadSize;

            mCurrent.mWriteTime = other.mCurrent.mWriteTime;
            mCurrent.mWriteSize = other.mCurrent.mWriteSize;

            mCurrent.mValueCreationTime = other.mCurrent.mValueCreationTime;
            mCurrent.mValueCreationSize = other.mCurrent.mValueCreationSize;

            mCurrent.mTimeSpentInsideGetValCalls.store( other.mCurrent.mTimeSpentInsideGetValCalls );

            mCurrent.mDiskReadPerf.store( other.mCurrent.mDiskReadPerf );
            mCurrent.mDiskWritePerf.store( other.mCurrent.mDiskWritePerf );
            mCurrent.mValueCreationPerf.store( other.mCurrent.mValueCreationPerf );

            mHistory.mReadTime = other.mHistory.mReadTime;
            mHistory.mReadSize = other.mHistory.mReadSize;

            mHistory.mWriteTime = other.mHistory.mWriteTime;
            mHistory.mWriteSize = other.mHistory.mWriteSize;

            mHistory.mDiskReadPerf.store( other.mHistory.mDiskReadPerf );
            mHistory.mDiskWritePerf.store( other.mHistory.mDiskWritePerf );
        }

        CacheStats& operator=(const CacheStats& other)
        {
            std::lock_guard<std::mutex> lock(other.mMutex);

            mCurrent.mDiskHitCounter.store( other.mCurrent.mDiskHitCounter );
            mCurrent.mMemHitCounter.store( other.mCurrent.mMemHitCounter );

            mCurrent.mDiskMissCounter.store( other.mCurrent.mDiskMissCounter );
            mCurrent.mMemMissCounter.store( other.mCurrent.mMemMissCounter );

            mCurrent.mReadTime = other.mCurrent.mReadTime;
            mCurrent.mReadSize = other.mCurrent.mReadSize;

            mCurrent.mWriteTime = other.mCurrent.mWriteTime;
            mCurrent.mWriteSize = other.mCurrent.mWriteSize;

            mCurrent.mValueCreationTime = other.mCurrent.mValueCreationTime;
            mCurrent.mValueCreationSize = other.mCurrent.mValueCreationSize;

            mCurrent.mTimeSpentInsideGetValCalls.store( other.mCurrent.mTimeSpentInsideGetValCalls );

            mCurrent.mDiskReadPerf.store( other.mCurrent.mDiskReadPerf );
            mCurrent.mDiskWritePerf.store( other.mCurrent.mDiskWritePerf );
            mCurrent.mValueCreationPerf.store( other.mCurrent.mValueCreationPerf );

            mHistory.mReadTime = other.mHistory.mReadTime;
            mHistory.mReadSize = other.mHistory.mReadSize;

            mHistory.mWriteTime = other.mHistory.mWriteTime;
            mHistory.mWriteSize = other.mHistory.mWriteSize;

            mHistory.mDiskReadPerf.store( other.mHistory.mDiskReadPerf );
            mHistory.mDiskWritePerf.store( other.mHistory.mDiskWritePerf );

            return *this;
        }

        //------------------------

        void reset()
        {
            std::lock_guard<std::mutex> lock(mMutex);

            mCurrent.mDiskHitCounter.store( 0 );
            mCurrent.mMemHitCounter.store( 0 );

            mCurrent.mDiskMissCounter.store( 0 );
            mCurrent.mMemMissCounter.store( 0 );

            mCurrent.mReadTime = { };
            mCurrent.mReadSize = { };

            mCurrent.mWriteTime = { };
            mCurrent.mWriteSize = { };

            mCurrent.mValueCreationTime = { };
            mCurrent.mValueCreationSize = { };

            mCurrent.mTimeSpentInsideGetValCalls.store( 0 );

            mCurrent.mDiskReadPerf.store( 0.0f );
            mCurrent.mDiskWritePerf.store( 0.0f );
            mCurrent.mValueCreationPerf.store( 0.0f );

            mHistory.mReadTime = { };
            mHistory.mReadSize = { };

            mHistory.mWriteTime = { };
            mHistory.mWriteSize = { };

            mHistory.mDiskReadPerf.store( 0.0f );
            mHistory.mDiskWritePerf.store( 0.0f );
        }

        //------------------------

        void memoryHit()
        {
            ++mCurrent.mMemHitCounter;
        }

        void diskHit()
        {
            ++mCurrent.mDiskHitCounter;
        }

        void memoryMiss()
        {
            ++mCurrent.mMemMissCounter;
        }

        void diskMiss()
        {
            ++mCurrent.mDiskMissCounter;
        }

        void updateGetValTimer(std::uint64_t nanoseconds)
        {
            mCurrent.mTimeSpentInsideGetValCalls.fetch_add(nanoseconds);
        }

        //------------------------
        // Performance on creating a new value

        float updateValueCreationPerf(std::uint64_t valueSize_bytes, std::uint64_t valCreateTime_ns)
        {
            std::lock_guard<std::mutex> lock(mMutex);

            mCurrent.mValueCreationSize += valueSize_bytes;
            const float valCreateSizeMB = static_cast<float>(mCurrent.mValueCreationSize) * sBytesToMB;

            mCurrent.mValueCreationTime += valCreateTime_ns;
            const float valCreateTime = static_cast<float>(mCurrent.mValueCreationTime) * sNanosecondsToSeconds;

            mCurrent.mValueCreationPerf.store( valCreateSizeMB / valCreateTime );

            return mCurrent.mValueCreationPerf.load();
        }

        // Read performance in Bytes/seconds
        float getValueCreationPerf() const
        {
            return mCurrent.mValueCreationPerf.load();
        }

        //------------------------
        // Performance of reading from disk

        float updateDiskReadPerf(std::uint64_t readSize_bytes, std::uint64_t readTime_ns)
        {
            std::lock_guard<std::mutex> lock(mMutex);

            mCurrent.mReadSize += readSize_bytes;
            mHistory.mReadSize += readSize_bytes;
            const float totalReadSizeMB = static_cast<float>(mHistory.mReadSize) * sBytesToMB;

            mCurrent.mReadTime += readTime_ns;
            mHistory.mReadTime += readTime_ns;
            const float totalReadTime = static_cast<float>(mHistory.mReadTime) * sNanosecondsToSeconds;

            mHistory.mDiskReadPerf.store( totalReadSizeMB / totalReadTime );

            return mHistory.mDiskReadPerf.load();
        }

        // Read performance in Bytes/seconds
        float getDiskReadPerf() const
        {
            return mHistory.mDiskReadPerf.load();
        }

        //------------------------
        // Performance of writing to disk

        float updateDiskWritePerf(std::uint64_t writeSize_bytes, std::uint64_t writeTime_ns)
        {
            std::lock_guard<std::mutex> lock(mMutex);

            mCurrent.mWriteSize += writeSize_bytes;
            mHistory.mWriteSize += writeSize_bytes;
            const float totalWriteSizeMB = static_cast<float>(mHistory.mWriteSize) * sBytesToMB;

            mCurrent.mWriteTime += writeTime_ns;
            mHistory.mWriteTime += writeTime_ns;
            const float totalWriteTime = static_cast<float>(mHistory.mWriteTime) * sNanosecondsToSeconds;

            mHistory.mDiskWritePerf.store( totalWriteSizeMB / totalWriteTime );

            return mHistory.mDiskWritePerf.load();
        }

        // Write performance in Bytes/seconds
        float getDiskWritePerf() const
        {
            return mHistory.mDiskWritePerf.load();
        }

        //------------------------

        std::string getStatsStr(const std::string indent = "  ") const
        {
            std::uint32_t current_diskHitCounter = 0;
            std::uint32_t current_memHitCounter = 0;

            std::uint32_t current_diskMissCounter = 0;
            std::uint32_t current_memMissCounter = 0;

            std::uint64_t current_readTime = 0;
            std::uint64_t current_readSize = 0;

            std::uint64_t current_writeTime = 0;
            std::uint64_t current_writeSize = 0;

            std::uint64_t current_valCreationTime = 0;
            std::uint64_t current_valCreationSize = 0;

            std::uint64_t current_getValTimer = 0;

            float diskReadPerf = 0.0f;
            float diskWritePerf = 0.0f;
            float current_valCreationPerf = 0.0f;

            {
                std::lock_guard<std::mutex> lock(mMutex);

                current_diskHitCounter = mCurrent.mDiskHitCounter;
                current_memHitCounter  = mCurrent.mMemHitCounter;

                current_diskMissCounter = mCurrent.mDiskMissCounter;
                current_memMissCounter  = mCurrent.mMemMissCounter;

                current_readTime = mCurrent.mReadTime;
                current_readSize = mCurrent.mReadSize;

                current_writeTime = mCurrent.mWriteTime;
                current_writeSize = mCurrent.mWriteSize;

                current_valCreationTime = mCurrent.mValueCreationTime;
                current_valCreationSize = mCurrent.mValueCreationSize;

                current_getValTimer = mCurrent.mTimeSpentInsideGetValCalls;

                diskReadPerf  = mHistory.mDiskReadPerf;
                diskWritePerf = mHistory.mDiskWritePerf;
                current_valCreationPerf = mCurrent.mValueCreationPerf;
            }

            std::ostringstream oss;
            oss << "\n" << std::setprecision(std::numeric_limits<float>::digits10 + 1)

                << indent << "Cache-hits   (memory-only)   = " << current_memHitCounter << "\n"
                << indent << "             (disk-only)     = " << current_diskHitCounter << "\n"
                << "\n"
                << indent << "Cache-misses (memory-only)   = " << current_memMissCounter << "\n"
                << indent << "             (disk-only)     = " << current_diskMissCounter << "\n"
                << "\n"

                << indent << "Size of values created (on cache-misses)         ~ "
                          << static_cast<float>(current_valCreationSize) * sBytesToMB << " MB\n"
                << indent << "Time spent creating new values (on cache-misses) = "
                          << static_cast<float>(current_valCreationTime) * sNanosecondsToSeconds << " s\n"
                << indent << "Value creation performance                       = "
                          << current_valCreationPerf << " MB/s \n"
                << "\n"

                << indent << "Total time spent inside KodachiCache::getValue() calls = "
                          << static_cast<float>(current_getValTimer) * sNanosecondsToSeconds << " s\n"
                << "\n"

                << indent << "Size of cached values read from disk        = "
                          << static_cast<float>(current_readSize) * sBytesToMB << " MB\n"
                << indent << "Time spent fetching cached values from disk = "
                          << static_cast<float>(current_readTime) * sNanosecondsToSeconds << " s\n"
                << "\n"

                << indent << "Size of cached values written to disk = "
                          << static_cast<float>(current_writeSize) * sBytesToMB << " MB\n"
                << indent << "Time spent writing values to disk     = "
                          << static_cast<float>(current_writeTime) * sNanosecondsToSeconds << " s\n"
                << "\n"

                << indent << "Overall disk I/O performance (since on-disk cache directory created): \n"
                << indent << "    Read  = " << diskReadPerf << " MB/s \n"
                << indent << "    Write = " << diskWritePerf << " MB/s \n";

            return oss.str();
        }

        void print(const std::string& scope) const
        {
            std::cout << "\n ====================================="
                      << "\n  Kodachi Cache debug info: \n"
                      << "\n  Scope: " << scope
                      << "\n" << getStatsStr()
                      << "\n =====================================" << std::endl;
        }

        // Returns a 40-byte char array containing following information:
        //  [ 0,  7] Total read time (std::uint64_t, nanoseconds)
        //  [ 8, 15] Total read size (std::uint64_t, bytes)
        //  [16, 23] Total write time (std::uint64_t, nanoseconds)
        //  [24, 31] Total write size (std::uint64_t, bytes)
        //
        //  [32, 35] Avg disk read performance (float, MB/seconds)
        //  [36, 39] Avg disk write performance (float, MB/seconds)
        //
        std::vector<char> getBinary() const
        {
            std::uint64_t totalSizeAndTime [4] { };
            float avgPerf [2] { };

            {
                std::lock_guard<std::mutex> lock(mMutex);

                totalSizeAndTime[0] = mHistory.mReadTime;  // Total read time
                totalSizeAndTime[1] = mHistory.mReadSize;  // Total read size

                totalSizeAndTime[2] = mHistory.mWriteTime; // Total write time
                totalSizeAndTime[3] = mHistory.mWriteSize; // Total write size

                avgPerf[0] = mHistory.mDiskReadPerf;
                avgPerf[1] = mHistory.mDiskWritePerf;
            }

            std::vector<char> binary(sizeof(totalSizeAndTime) + sizeof(avgPerf));

            // 64-bit integers first
            std::memcpy(reinterpret_cast<void*>(binary.data()),
                        reinterpret_cast<const void*>(totalSizeAndTime),
                        sizeof(totalSizeAndTime));

            // floats at the end
            std::memcpy(reinterpret_cast<void*>(binary.data() + sizeof(totalSizeAndTime)),
                        reinterpret_cast<const void*>(avgPerf),
                        sizeof(avgPerf));

            return binary;
        }

        // Input is a 40-byte char array containing following information:
        //  [ 0,  7] Total read time (std::uint64_t, nanoseconds)
        //  [ 8, 15] Total read size (std::uint64_t, bytes)
        //  [16, 23] Total write time (std::uint64_t, nanoseconds)
        //  [24, 31] Total write size (std::uint64_t, bytes)
        //
        //  [32, 35] Avg disk read performance (float, MB/seconds)
        //  [36, 39] Avg disk write performance (float, MB/seconds)
        //
        static CacheStats fromBinary(const char* data_src, std::size_t data_size /* not used */)
        {
            std::uint64_t totalSizeAndTime[4] { };
            std::memcpy(reinterpret_cast<void*>(totalSizeAndTime),
                        reinterpret_cast<const void*>(data_src),
                        sizeof(totalSizeAndTime));

            float avgPerf[2] { };
            std::memcpy(reinterpret_cast<void*>(avgPerf),
                        reinterpret_cast<const void*>(data_src + sizeof(totalSizeAndTime)),
                        sizeof(avgPerf));

            return { totalSizeAndTime[0], // Total read time
                     totalSizeAndTime[1], // Total read size
                     avgPerf[0],          // Read performance
                     totalSizeAndTime[2], // Total write time
                     totalSizeAndTime[3], // Total write size
                     avgPerf[1]           // Write performance
                   };
        }

        static CacheStats fromBinary(const std::vector<char>& data_src)
        {
            return CacheStats::fromBinary(data_src.data(), data_src.size());
        }
    };
} // namespace cache_utils
} // namespace kodachi


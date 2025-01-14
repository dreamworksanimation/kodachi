// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "BinaryConversion.h"

// Kodachi
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/GroupBuilder.h>

// C++
#include <cstdio>
#include <queue>
#include <set>
#include <sstream>
#include <stack>
#include <unordered_set>
#include <unordered_map>
#include <vector>

// To check whether or not a binary file/buffer was created by this
// tool, an ID in form of a 13-character string "kodachi_cache" is
// added to the beginning of the file/buffer.
static const std::string kBinaryFileHeaderID { "kodachi_cache" };

static const std::string kGroupAttrRootName { "kodachi_cache_ga_root" };

constexpr std::uint64_t kInvalidParentIdx = std::numeric_limits<std::uint64_t>::max();

namespace kodachi
{
    /**
     * Write value of type T to the memory location pointed at by beginPtr.
     */
    template <typename T, std::size_t byte_count = sizeof(T)>
    inline char*
    write_to_char_array(T value, char* beginPtr)
    {
        static_assert(std::is_standard_layout<T>::value || std::is_trivial<T>::value,
                      "T can only be a POD.");

        std::memcpy(reinterpret_cast<void*>(beginPtr),
                    reinterpret_cast<const void*>(& value),
                    byte_count);

        return beginPtr + byte_count;
    }

    /**
     * Read value of type T from the memory location pointed at by beginPtr.
     */
    template <typename T, std::size_t byte_count = sizeof(T)>
    inline const char*
    read_from_char_array(const char* beginPtr, T & value)
    {
        static_assert(std::is_standard_layout<T>::value || std::is_trivial<T>::value,
                      "T can only be a POD.");

        std::memcpy(reinterpret_cast<void*>(& value),
                    reinterpret_cast<const void*>(beginPtr),
                    byte_count);

        return beginPtr + byte_count;
    }

    /**
     * Check if Attribute attr is not a GroupAttribute
     */
    bool
    isLeaf(kodachi::Attribute const& attr) { return attr.getType() != kFnKatAttributeTypeGroup; }

    /**
     * Return Attribute attr's 64-bit unsinged int hash value.
     */
    std::uint64_t
    getHash(kodachi::Attribute const& attr) { return attr.getHash().uint64(); }

    /**
     * Returns the number of Attributes held by GroupAttribute attr.
     */
    std::uint64_t
    countNodes(kodachi::GroupAttribute const& attr)
    {
        std::uint64_t nodeCount = 0;
        std::queue<kodachi::GroupAttribute> bfsQueue;
        bfsQueue.push(attr);

        while (!bfsQueue.empty()) {
            kodachi::GroupAttribute currAttr = std::move(bfsQueue.front());
            bfsQueue.pop();
            ++nodeCount;

            const std::uint64_t childCount = currAttr.getNumberOfChildren();
            for (std::uint64_t idx = 0; idx < childCount; ++idx) {
                kodachi::Attribute childAttr = currAttr.getChildByIndex(idx);
                if (kodachi::isLeaf(childAttr)) {
                    ++nodeCount;
                }
                else {
                    bfsQueue.emplace(std::move(childAttr));
                }
            }
        }

        return nodeCount;
    }

    /**
     * Intermediate representation of an Attribute held by the source GroupAttribute.
     */
    class Node {
    public:
        Node() = default;

        Node(std::uint64_t hash,
             std::uint64_t parent_idx,
             std::uint64_t str_offset)
                : m_hash(hash)
                , m_parent_idx(parent_idx)
                , m_str_offset(str_offset)
        {
        }

        Node(Node const&) = default;
        Node(Node&&) = default;

        Node& operator=(Node const&) = default;
        Node& operator=(Node&&) = default;

        char*
        writeToCharArray(char* begin) const
        {
            if (begin == nullptr) {
                return nullptr;
            }

            std::memcpy(reinterpret_cast<void*>(begin), // Destination
                        reinterpret_cast<const void*>(this), // Source
                        sizeof(Node)); // Size

            return begin + sizeof(Node); // return the end pointer
        }

        std::uint64_t m_hash       = 0;
        std::uint64_t m_parent_idx = 0;
        std::uint64_t m_str_offset = 0;
    };

    /**
     * To hold useful information about the source GroupAttribute.
     */
    class GroupAttrHeader {
    public:
        GroupAttrHeader() = default;

        GroupAttrHeader(std::uint64_t node_count,
               std::uint64_t string_sec_size,
               std::uint64_t data_sec_size,
               bool groupInherit)
            : m_node_count(node_count)
            , m_string_section_size(string_sec_size)
            , m_total_data_size(data_sec_size)
        {
            setGroupInherit(groupInherit);
        }

        GroupAttrHeader(std::uint64_t node_count,
               std::uint64_t string_sec_size,
               std::uint64_t data_sec_size,
               char flags[3])
            : m_node_count(node_count)
            , m_string_section_size(string_sec_size)
            , m_total_data_size(data_sec_size)
        {
            m_flags[0] = flags[0];
            m_flags[1] = flags[1];
            m_flags[2] = flags[2];
        }

        void setFlags(char flags[3])
        {
            m_flags[0] = flags[0];
            m_flags[1] = flags[1];
            m_flags[2] = flags[2];
        }

        void setGroupInherit(bool groupInherit)
        {
            // set if groupInherit is false
            if (groupInherit) {
                m_flags[0] |= 0x80;
            }
            // clear if groupInherit is false
            else {
                m_flags[0] &= (~(0x80));
            }
        }

        bool getGroupInherit() const { return (m_flags[0] == 0x80); }

        char*
        writeToCharArray(char* begin) const
        {
            if (begin == nullptr) {
                return nullptr;
            }

            char* _begin = begin;

            // Write out m_id
            std::memcpy(reinterpret_cast<void*>(_begin), // Destination
                        reinterpret_cast<const void*>(kBinaryFileHeaderID.data()), // Source
                        kBinaryFileHeaderID.size()); // Size
            _begin = _begin + kBinaryFileHeaderID.size();

            // Write out m_flags
            std::memcpy(reinterpret_cast<void*>(_begin), // Destination
                        reinterpret_cast<const void*>(m_flags), // Source
                        3); // Size
            _begin = _begin + 3;

            // Write out the rest
            _begin = write_to_char_array<std::uint64_t>(m_node_count, _begin);
            _begin = write_to_char_array<std::uint64_t>(m_string_section_size, _begin);
            _begin = write_to_char_array<std::uint64_t>(m_total_data_size, _begin);

            return _begin; // return the end pointer
        }

        bool
        readFromCharArray(const char* begin,
                          // Out:
                          const char* & begin_graph,
                          const char* & begin_strings,
                          const char* & begin_data)
        {
            if (begin == nullptr) {
                return false;
            }

            const char* _begin = begin;

            // Read m_id
            std::string id(13, '\0');
            std::memcpy(reinterpret_cast<void*>(&id[0]), // Destination
                        reinterpret_cast<const void*>(_begin), // Source
                        13); // Size
            _begin = _begin + 13;

            // Make sure this was written with kodachi_cache's custom toBinary()
            if (id != kBinaryFileHeaderID) {
                return false;
            }

            // Read m_flags
            std::memcpy(reinterpret_cast<void*>(m_flags), // Destination
                        reinterpret_cast<const void*>(_begin), // Source
                        3); // Size
            _begin = _begin + 3;


            _begin = read_from_char_array<std::uint64_t>(_begin, m_node_count);
            _begin = read_from_char_array<std::uint64_t>(_begin, m_string_section_size);
            _begin = read_from_char_array<std::uint64_t>(_begin, m_total_data_size);

            // Outputs
            begin_graph = _begin;

            _begin = _begin + (m_node_count * sizeof(Node));
            begin_strings = _begin;

            _begin = _begin + m_string_section_size;
            begin_data = _begin;

            return true;
        }

        bool
        readFromCharArray(const char* begin)
        {
            const char* _dummy;
            return readFromCharArray(begin, _dummy, _dummy, _dummy);
        }

        static constexpr std::size_t m_header_size = 40;

        char m_flags[3] { '\0', '\0', '\0' }; // 3 bytes

        std::uint64_t m_node_count          = 0;
        std::uint64_t m_string_section_size = 0;
        std::uint64_t m_total_data_size     = 0;
    };

   /* DataAttribute Header:
    *
    *  <----------- 56 bytes ----------->
    *  ----------------------------------
    *  | ID | Flags | H | N | S | V | T |
    *  ----------------------------------
    *
    *      1) ID   : char[13], "kodachi_cache"
    *      2) Flags: char[3] , 3 bytes, (up to) 24 bits to use for flags
    *          Initially going to use a single char, flag[0], to hold the
    *          the attribute type (a value from 1 to 4):
    *              - kFnKatAttributeTypeInt    1
    *              - kFnKatAttributeTypeFloat  2
    *              - kFnKatAttributeTypeDouble 3
    *              - kFnKatAttributeTypeString 4
    *      3) H    : uint64_t, this kodachi::Attribute's hash value
    *      4) N    : uint64_t, total size of this object in bytes; from &ID[0]
    *                to past-the-end memory location:
    *                     sizeof(header) + sizeof(sample times) + sizeof(values)
    *
    *      5) S    : uint64_t, number of time samples
    *      6) V    : uint64_t, number of values per time sample
    *      7) T    : uint64_t, number of tuples
    */
    class DataAttrHeader {
    public:
        DataAttrHeader() = default;

        DataAttrHeader(kodachi::DataAttribute const& attr)
            : m_attr_hash(getHash(attr))
            , m_total_size(0) // Calculated in ctor body
            , m_time_sample_count(attr.getNumberOfTimeSamples())
            , m_values_per_time_sample(attr.getNumberOfValues())
            , m_tuple_count(attr.getNumberOfTuples())
        {
            // Attribute type
            m_flags[0] = static_cast<char>(attr.getType());
            calculateTotalSize(attr);
        }

        int getAttrType() const { return static_cast<int>(m_flags[0]); }
        std::uint64_t getTupleSize() const
        {
            if (m_tuple_count != 0) {
                return m_values_per_time_sample / m_tuple_count;
            }
            else {
                return 0;
            }
        }

        void calculateTotalSize(kodachi::DataAttribute const& attr)
        {
            const kodachi::DataAttribute d_attr(attr);

            // Can only handle DataAttributes
            if (!d_attr.isValid()) {
                return;
            }

            const std::uint64_t totalValueCount =
                    m_time_sample_count * m_values_per_time_sample;

            std::uint64_t totalValueSize = 0;

            if (m_flags[0] == kFnKatAttributeTypeInt) {
                totalValueSize = totalValueCount * sizeof(kodachi::IntAttribute::value_type);
            }
            else if (m_flags[0] == kFnKatAttributeTypeFloat) {
                totalValueSize = totalValueCount * sizeof(kodachi::FloatAttribute::value_type);
            }
            else if (m_flags[0] == kFnKatAttributeTypeDouble) {
                totalValueSize = totalValueCount * sizeof(kodachi::DoubleAttribute::value_type);
            }
            // Special case: StringAttribute
            else if (m_flags[0] == kFnKatAttributeTypeString) {
                kodachi::StringAttribute s_attr = attr;

                std::size_t totalCharCount = 0;
                std::size_t idx = 0;
                kodachi::StringAttribute::accessor_type samplesAccess = s_attr.getSamples();
                for (const auto& sample : samplesAccess) {
                    for (const char* cstr : sample) {
                        totalCharCount += (std::strlen(cstr) + 1);
                    }

                    ++idx;
                }

                totalValueSize =
                          // Total size to fit all values (including null chars)
                        + (totalCharCount * sizeof(char))
                          // To store the size of each string as a uint64_t;
                          // decided to do this in order to avoid calls to strlen()
                        + totalValueCount * sizeof(std::uint64_t);
            }
            // Unknown type
            else {
                return;
            }

            m_total_size =
                      m_header_size_bytes
                      // To store time samples right after header
                    + m_time_sample_count * sizeof(float)
                      // Total size to fit all values
                    + totalValueSize;
        }

        // <----------- 56 bytes ----------->
        // ----------------------------------
        // | ID | Flags | H | N | S | V | T |
        // ----------------------------------
        char*
        writeToCharArray(char* begin) const
        {
            if (begin == nullptr) {
                return nullptr;
            }

            char* _begin = begin;

            // Write out m_id
            std::memcpy(reinterpret_cast<void*>(_begin), // Destination
                        reinterpret_cast<const void*>(kBinaryFileHeaderID.data()), // Source
                        kBinaryFileHeaderID.size()); // Size
            _begin = _begin + kBinaryFileHeaderID.size();

            // Write out m_flags
            std::memcpy(reinterpret_cast<void*>(_begin), // Destination
                        reinterpret_cast<const void*>(m_flags), // Source
                        3); // Size
            _begin = _begin + 3;

            // Write out the rest (5 x std::uint64_t)
            constexpr std::uint64_t writeSize = 5 * sizeof(std::uint64_t);
            std::memcpy(reinterpret_cast<void*>(_begin), // Destination
                        reinterpret_cast<const void*>(&m_attr_hash), // Source
                        writeSize);

            return _begin + writeSize; // return the end pointer
        }

        // <----------- 56 bytes ----------->
        // ----------------------------------
        // | ID | flags | H | N | S | V | T |
        // ----------------------------------
        bool
        fromCharArray(const char* begin,
                      // Out:
                      const char* & begin_samples,
                      const char* & begin_data)
        {
            if (begin == nullptr) {
                return false;
            }

            const char* _begin = begin;

            // Read m_id
            std::string id(13, '\0');
            std::memcpy(reinterpret_cast<void*>(&id[0]), // Destination
                        reinterpret_cast<const void*>(_begin), // Source
                        13); // Size
            _begin = _begin + 13;

            // Make sure this was written with kodachi_cache's custom toBinary()
            if (id != kBinaryFileHeaderID) {
                return false;
            }

            // Read m_flags
            //
            // Flags: char[3] , 3 bytes, (up to) 24 bits to use for flags
            //        Initially going to use a single char, flag[0], to hold the
            //        the attribute type (a value from 1 to 4):
            //           - kFnKatAttributeTypeInt    1
            //           - kFnKatAttributeTypeFloat  2
            //           - kFnKatAttributeTypeDouble 3
            //           - kFnKatAttributeTypeString 4
            //
            std::memcpy(reinterpret_cast<void*>(m_flags), // Destination
                        reinterpret_cast<const void*>(_begin), // Source
                        3); // Size
            _begin = _begin + 3;

            // Read the rest (5 x std::uint64_t)
            constexpr std::uint64_t readSize = 5 * sizeof(std::uint64_t);
            std::memcpy(reinterpret_cast<void*>(&m_attr_hash), // Destination
                        reinterpret_cast<const void*>(_begin), // Source
                        readSize);
            _begin = _begin + readSize;

            // Outputs
            begin_samples = _begin;
            _begin = _begin + (m_time_sample_count * sizeof( float ));

            begin_data = _begin;

            return true;
        }

        // <----------- 56 bytes ----------->
        // ----------------------------------
        // | ID | Flags | H | N | S | V | T |
        // ----------------------------------

        char m_flags[3] { static_cast<char>(kFnKatAttributeTypeNull), '\0', '\0' }; // 3 bytes

        std::uint64_t m_attr_hash              = 0; // H
        std::uint64_t m_total_size             = 0; // N
        std::uint64_t m_time_sample_count      = 0; // S
        std::uint64_t m_values_per_time_sample = 0; // V
        std::uint64_t m_tuple_count            = 0; // T

        // (13 + 3) * sizeof(char) + (5 * sizeof(std::uint64_t))
        static constexpr std::uint64_t m_header_size_bytes = 56;

        std::uint64_t getHeaderSize() const { return m_header_size_bytes; }
        std::uint64_t getDataSize() const { return m_total_size - m_header_size_bytes; }
        std::uint64_t getTotalSize() const { return m_total_size; }
    };

    std::string
    buildFullAttrName(const char* _str_begin,
                      const Node* nodeArray,
                      std::size_t size,
                      std::size_t index)
    {
        std::stack<std::string> fullPath;
        std::uint64_t cur_idx = index;
        while (cur_idx != kInvalidParentIdx) {
            std::string partialName( _str_begin + nodeArray[cur_idx].m_str_offset );
            cur_idx = nodeArray[cur_idx].m_parent_idx;

            if (partialName == kGroupAttrRootName) {
                continue; // skip "root", since it's implicit in GroupAttribute
            }

            fullPath.push(std::move(partialName));
        }

        std::string result;
        while (!fullPath.empty()) {
            result += fullPath.top();
            fullPath.pop();

            if (!fullPath.empty()) {
                result += ".";
            }
        }

        return result;
    }

    /*
     * Binary file layout (DataAttribute):
     *
     *  <------ N ------>
     *  -------------------------
     *  | Header |     Data     |
     *  -------------------------
     *
     * 1) Header (handled using class DataAttrHeader):
     *
     *  <----------- 56 bytes ----------->
     *  ----------------------------------
     *  | ID | Flags | H | N | S | V | T |
     *  ----------------------------------
     *
     *      1) ID   : char[13], "kodachi_cache"
     *      2) Flags: char[3] , 3 bytes, (up to) 24 bits to use for flags
     *          Initially going to use a single char, flag[0], to hold the
     *          the attribute type (a value from 1 to 4):
     *              - kFnKatAttributeTypeInt    1
     *              - kFnKatAttributeTypeFloat  2
     *              - kFnKatAttributeTypeDouble 3
     *              - kFnKatAttributeTypeString 4
     *      3) H    : uint64_t, this kodachi::Attribute's hash value
     *      4) N    : uint64_t, total size of this object in bytes; from &ID[0]
     *                to past-the-end memory location:
     *                     sizeof(header) + sizeof(sample times) + sizeof(values)
     *
     *      5) S    : uint64_t, number of time samples
     *      6) V    : uint64_t, number of values per time sample
     *      7) T    : uint64_t, number of tuples
     *
     * 2) Data section (handled using function template DataAttrToBinary<T>()):
     *
     *  NOTE: StringAttribute size is a special case!
     *
     *  <----- (S + S * V) * sizeof(data_type) ------>
     *  ----------------------------------------------
     *  | sample times | values (flattened 2D array) |
     *  ----------------------------------------------
     */
    template <typename AttributeType>
    char*
    DataAttrToBinary(AttributeType const& attr,
                     char* begin,
                     std::uint64_t dataSize)
    {
        static_assert(std::is_base_of<kodachi::DataAttribute, AttributeType>::value,
                      "kodachi::DataAttribute must be a base of AttributeType.");

        //----------------------------------
        // Prepare

        const std::uint64_t valueCount      = attr.getNumberOfValues();
        const std::uint64_t timeCount       = attr.getNumberOfTimeSamples();
        const std::uint64_t timeSamplesSize = timeCount * sizeof(float);
        std::vector<float> sampleTimes; sampleTimes.reserve(timeCount); // samples are always float values

        // There is one row of values per each time sample
        std::vector<const typename AttributeType::value_type*> rowPointers;
        rowPointers.reserve(timeCount); // Type depends on DataAttribute::value_type

        typename AttributeType::accessor_type samplesAccess = attr.getSamples();

        for (const auto& s : samplesAccess) {
            sampleTimes.push_back(s.getSampleTime());
            rowPointers.push_back(s.data());
        }

        //----------------------------------
        // Write out data
        //
        // <----- (S + S * V) * sizeof(data_type) ------>
        // ----------------------------------------------
        // | sample times | values (flattened 2D array) |
        // ----------------------------------------------
        //

        char* _begin = begin;

        // Write out sample times
        std::memcpy(reinterpret_cast<void*>(_begin),
                    reinterpret_cast<const void*>(sampleTimes.data()),
                    timeSamplesSize);
        _begin = _begin + timeSamplesSize;

        // Write out values
        // NOTE: size depends on DataAttribute::value_type
        const std::size_t rowSize = valueCount * sizeof( typename AttributeType::value_type );
        for (std::size_t idx = 0; idx < timeCount; ++idx) {
            std::memcpy(reinterpret_cast<void*>(_begin),
                        reinterpret_cast<const void*>(rowPointers[idx]),
                        rowSize);
            _begin = _begin + rowSize;
        }

        return _begin;
    }

    template <typename AttributeType>
    AttributeType
    BinaryToDataAttr(DataAttrHeader header,
                     const char* begin,
                     const char* begin_time_samples,
                     const char* begin_values)
    {
        // Steps:
        // 1) Read the header,
        // 2) Read the sample times,
        // 3) Read the values
        // 4) Create the attribute using zero-copy util

        const char* _begin_time_samples = begin_time_samples;
        const char* _begin_values       = begin_values;

        std::vector<const typename AttributeType::value_type *> values(header.m_time_sample_count);

        const std::size_t sizeOfValuesPerSample =
                header.m_values_per_time_sample * sizeof(typename AttributeType::value_type);

        for (std::size_t idx = 0; idx < header.m_time_sample_count; ++idx) {
            values[idx] =
                    reinterpret_cast<const typename AttributeType::value_type *>(
                            _begin_values + (idx * sizeOfValuesPerSample));
        }

        return { reinterpret_cast<const float*>(_begin_time_samples),
                 static_cast<std::int64_t>(header.m_time_sample_count),
                 values.data(),
                 static_cast<std::int64_t>(header.m_values_per_time_sample),
                 static_cast<std::int64_t>(header.getTupleSize()) };
    }

    /*
     * StringAttribute is different from the general case, we need to go
     * over individual strings and copy char by char (including the null char)
     * into vector [bin]. Note that, potentially, each sample is going to end
     * up having a different size, therefore we have to record the total size of
     * values for each sample as well.
     *
     * Here is the memory layout for [bin]:
     *
     * --------------------------------
     * | Header | Sample times | Data |
     * --------------------------------
     *
     *      Data:
     *      <------ sample 0 -----><----- sample 1 ----->
     *      -------------------------------------------------------
     *      | size(0) | values(0) | size(1) | values(1) | ....... |
     *      -------------------------------------------------------
     *       uint64_t   char*       uint64_t   char*   ....
     *
     * NOTE, since we are recording the size of values for each sample,
     * then if
     *      C = total number of characters (including null chars),
     * the size of the [Data] section is:
     *      C + ( S * sizeof(uint64_t) )
     */

    // Wrapper for 2D array of [const char*]s
    class ManagedStringValues {
    public:
        using cstring = const char*;

        ManagedStringValues() = delete;
        ManagedStringValues(const ManagedStringValues&) = delete;
        ManagedStringValues(ManagedStringValues&&) = delete;

        ManagedStringValues(std::size_t sample_count, std::size_t values_per_sample)
            : m_sample_count(sample_count)
            , m_values_per_sample(values_per_sample)
        {
            allocate();
        }

        ManagedStringValues(std::size_t sample_count,
                            std::size_t values_per_sample,
                            const char* data)
            : m_sample_count(sample_count)
            , m_values_per_sample(values_per_sample)
        {
            allocate();
            fill(data);
        }

        ManagedStringValues(std::size_t sample_count,
                            std::size_t values_per_sample,
                            std::vector<char> const& data)
            : m_sample_count(sample_count)
            , m_values_per_sample(values_per_sample)
        {
            allocate();
            fill(data.data());
        }

        ~ManagedStringValues()
        {
            clear();
        }

        void allocate()
        {
            m_data = new cstring*[m_sample_count];
            for (std::size_t idx = 0; idx < m_sample_count; ++idx) {
                m_data[idx] = new cstring[m_values_per_sample];
            }
        }

        void clear()
        {
            for (std::size_t idx = 0; idx < m_sample_count; ++idx) {
                delete [] m_data[idx];
                m_data[idx] = nullptr;
            }

            delete [] m_data;
            m_data = nullptr;
        }

        /*
         * Input layout:
         *
         *  <---------- (values per sample) * (sample count) --------->
         *  <--- values per sample ---><--- values per sample --->
         *  -----------------------------------------------------------
         *  |           S(0)          |           S(1)           | ...|
         *  -----------------------------------------------------------
         *
         *  S(i)
         *  -----------------------------------------------------
         *  | strlen(0) | str(0)   | strlen(1) | str(1) ...     |
         *  -----------------------------------------------------
         */
        void
        fill(const char* data)
        {
            const char* _begin = data;
            for (std::size_t sidx = 0; sidx < m_sample_count; ++sidx) {
                for (std::size_t vidx = 0; vidx < m_values_per_sample; ++vidx) {
                    std::uint64_t strLength = 0;
                    _begin = read_from_char_array<std::uint64_t>(_begin, strLength);

                    m_data[sidx][vidx] = _begin;
                    _begin = _begin + strLength; // move forward
                }
            }
        }

        const char*** get() const { return m_data; }

        std::size_t m_sample_count      = 0;
        std::size_t m_values_per_sample = 0;

        cstring** m_data = nullptr;
    };

    template <>
    char*
    DataAttrToBinary(kodachi::StringAttribute const& attr,
                     char* begin,
                     std::uint64_t totalSizeBytes)
    {
        //----------------------------------
        // Prepare

        const std::uint64_t timeCount  = attr.getNumberOfTimeSamples();
        std::vector<float> sampleTimes;

        sampleTimes.reserve(timeCount); // ALWAYS float values
        std::vector<std::vector<std::size_t>> strlens(timeCount);
        std::size_t idx = 0;
        kodachi::StringAttribute::accessor_type samplesAccess = attr.getSamples();
        for (const auto& sample : samplesAccess) {
            for (const char* cstr : sample) {
                strlens[idx].push_back((std::strlen(cstr) + 1));
            }
            sampleTimes.push_back(sample.getSampleTime());
            ++idx;
        }

        //----------------------------------
        // Write out data

        char* _begin = begin;

        // Write out sample times
        // NOTE: sample times are ALWAYS of type float
        const std::size_t sampleArraySize = sampleTimes.size() * sizeof( float );
        std::memcpy(reinterpret_cast<void*>(_begin),
                    reinterpret_cast<const void*>(sampleTimes.data()),
                    sampleArraySize);
        _begin = _begin + sampleArraySize;

        // Write out values
        for (std::size_t sidx = 0; sidx < samplesAccess.size(); ++sidx) {
            // Now write out c-strings one by one:
            //      [c-string length] followed by [c-string]
            //
            const auto& currSample = samplesAccess[sidx];

            std::size_t vidx = 0;
            for (const char* cstr : currSample) {
                const std::size_t len = strlens[sidx][vidx];
                _begin = write_to_char_array<std::uint64_t>(len, _begin);

                std::memcpy(reinterpret_cast<void*>(_begin),
                            reinterpret_cast<const void*>(cstr),
                            len);
                _begin = _begin + len;

                ++vidx;
            }
        }

        return _begin;
    }

    template <>
    kodachi::StringAttribute
    BinaryToDataAttr(DataAttrHeader header,
                     const char* begin,
                     const char* begin_time_samples,
                     const char* begin_values)
    {
        ManagedStringValues strvalues(
                header.m_time_sample_count,
                header.m_values_per_time_sample,
                begin_values);

        return { reinterpret_cast<const float*>(begin_time_samples),
                 static_cast<std::int64_t>(header.m_time_sample_count),
                 strvalues.get(),
                 static_cast<std::int64_t>(header.m_values_per_time_sample),
                 static_cast<std::int64_t>(header.getTupleSize()) };
    }

    /**
     * Intermediate representation of a DataAttribute.
     */
    class Data {
    public:
        Data() = default;

        Data(kodachi::Attribute const& attr)
            : m_header(attr)
            , m_attr(attr)
        {
        }

        bool isValid() const { return m_attr.isValid(); }

        char*
        writeToCharArray(char* begin) const
        {
            if (begin == nullptr || !isValid()) {
                return nullptr;
            }

            char* _begin = begin;

            //----------------------------
            // Write out header

            _begin = m_header.writeToCharArray(_begin);

            //----------------------------
            // Now write the data:

            const std::uint64_t dataSize = m_header.getDataSize();

            if (m_header.getAttrType() == kFnKatAttributeTypeInt) {
                _begin = DataAttrToBinary<kodachi::IntAttribute>(m_attr,
                                                                 _begin,
                                                                 dataSize);
            }
            else if (m_header.getAttrType() == kFnKatAttributeTypeFloat) {
                _begin = DataAttrToBinary<kodachi::FloatAttribute>(m_attr,
                                                                   _begin,
                                                                   dataSize);
            }
            else if (m_header.getAttrType() == kFnKatAttributeTypeDouble) {
                _begin = DataAttrToBinary<kodachi::DoubleAttribute>(m_attr,
                                                                    _begin,
                                                                    dataSize);
            }
            else if (m_header.getAttrType() == kFnKatAttributeTypeString) {
                _begin = DataAttrToBinary<kodachi::StringAttribute>(m_attr,
                                                                    _begin,
                                                                    dataSize);
            }

            return _begin; // end
        }

        const char*
        fromCharArray(const char* begin, kodachi::DataAttribute & attr)
        {
            if (begin == nullptr) {
                return nullptr;
            }

            const char* _begin = begin;
            const char* _begin_samples = nullptr;
            const char* _begin_values  = nullptr;

            //------------------------------------------
            // Read the header

            m_header.fromCharArray(_begin, _begin_samples, _begin_values);

            //------------------------------------------
            // Now read the binary data and convert to kodachi::Attribute:
            //
            // If it's a GroupAttribute, use the custom parseBinary() function,
            // but if it's a DataAttribute, use FnAttribute::Attribute::parseBinary()
            //

            const int attrType = m_header.getAttrType();

            if (attrType == kFnKatAttributeTypeInt) {
                attr = BinaryToDataAttr<kodachi::IntAttribute>(
                        m_header,
                        _begin,
                        _begin_samples,
                        _begin_values);
            }
            else if (attrType == kFnKatAttributeTypeFloat) {
                attr = BinaryToDataAttr<kodachi::FloatAttribute>(
                        m_header,
                        _begin,
                        _begin_samples,
                        _begin_values);
            }
            else if (attrType == kFnKatAttributeTypeDouble) {
                attr = BinaryToDataAttr<kodachi::DoubleAttribute>(
                        m_header,
                        _begin,
                        _begin_samples,
                        _begin_values);
            }
            else if (attrType == kFnKatAttributeTypeString) {
                attr = BinaryToDataAttr<kodachi::StringAttribute>(
                        m_header,
                        _begin,
                        _begin_samples,
                        _begin_values);
            }

            return _begin + m_header.getTotalSize();
        }

        DataAttrHeader m_header;
        kodachi::DataAttribute   m_attr;
    };

    /**
     * Intermediate representation of an Attribute held by the source
     * GroupAttribute.
     */
    struct DFSNode {
        DFSNode(std::uint64_t self_idx,
                std::uint64_t parent_idx,
                const std::string& attr_name,
                const std::string& path,
                kodachi::Attribute attr)
            : m_self_idx(self_idx)
            , m_parent_idx(parent_idx)
            , m_attr_name(attr_name)
            , m_path(path)
            , m_path_hash(std::hash<std::string>{}(m_path))
            , m_attr(attr)
            , m_is_leaf(kodachi::isLeaf(m_attr))
        {
        }

        DFSNode(std::uint64_t self_idx,
                std::uint64_t parent_idx,
                std::string&& attr_name,
                std::string&& path,
                kodachi::Attribute&& attr)
            : m_self_idx(self_idx)
            , m_parent_idx(parent_idx)
            , m_attr_name(std::move(attr_name))
            , m_path(std::move(path))
            , m_path_hash(std::hash<std::string>{}(m_path))
            , m_attr(std::move(attr))
            , m_is_leaf(kodachi::isLeaf(m_attr))
        {
        }

        DFSNode() = delete;
        DFSNode(const DFSNode&) = default;
        DFSNode(DFSNode&&) = default;
        DFSNode& operator=(const DFSNode&) = default;
        DFSNode& operator=(DFSNode&&) = default;

        bool isLeaf() const { return m_is_leaf; }
        bool getGroupInherit() const { return (m_is_leaf ? false : kodachi::GroupAttribute(m_attr).getGroupInherit()); }
        std::uint64_t getAttrHash() const { return getHash(m_attr); }

        std::uint64_t m_self_idx   = 0;
        std::uint64_t m_parent_idx = 0;

        std::string m_attr_name;
        std::string m_path;
        std::uint64_t m_path_hash = 0;

        kodachi::Attribute m_attr;

        bool m_is_leaf = false;
    };

    //--------------------------------------------------------------------------------

    std::vector<DFSNode>
    DFSFlattenGroupAttr(kodachi::GroupAttribute const& ga)
    {
        std::set<std::size_t /* std::string hash */> visited;

        const std::uint64_t nodeListSize = countNodes(ga);
        std::vector<DFSNode> nodeList; nodeList.reserve(nodeListSize);

        nodeList.emplace_back(nodeList.size(), // self idx
                              kInvalidParentIdx, // parent idx (no parent)
                              kGroupAttrRootName, // attr name
                              kGroupAttrRootName, // path to attr
                              ga);
        std::stack<DFSNode*> DFSStack;
        DFSStack.emplace(&nodeList.back());

        while (!DFSStack.empty()) {
            DFSNode* currNodePtr = DFSStack.top();

            const kodachi::GroupAttribute& currGA = currNodePtr->m_attr;
            const std::uint64_t childCount = currGA.getNumberOfChildren();

            // Find the first child that has not been visited so far

            std::int64_t childIdx = 0;
            std::string childName;
            std::string pathToChild;
            std::size_t pathToChildHash = 0;
            kodachi::Attribute childAttr;

            for (; childIdx < childCount; ++childIdx) {
                childAttr = currGA.getChildByIndex(childIdx);
                childName = currGA.getChildName(childIdx);

                if (currNodePtr->m_parent_idx == kInvalidParentIdx) {
                    pathToChild = childName;
                }
                else {
                    std::ostringstream oss;
                    oss << currNodePtr->m_path << "." << childName;
                    pathToChild = oss.str();
                }

                pathToChildHash = std::hash<std::string>{}(pathToChild);

                // If already visited, skip
                if (visited.count(pathToChildHash) == 1) {
                    continue;
                }
                // Found the first child that has not been visited before, break
                // and process it.
                else {
                    break;
                }
            }

            // If all children already visited:
            //      1) update the "visited" set with current node hash, AND
            //      2) there is nothing more to do here, pop the stack and skip to next item
            if (childIdx == childCount) {
                visited.emplace(currNodePtr->m_path_hash);
                DFSStack.pop();

                continue;
            }
            // First time encountering child at index [idx], deal with it:
            //      if it is a LEAF (a DataAttribute): print out AND update "visited",
            //      otherwise push onto the stack
            else {
                const bool childIsLeaf = kodachi::isLeaf(childAttr);

                // Add the new node to nodeList
                nodeList.emplace_back(nodeList.size(),         // self idx
                                      currNodePtr->m_self_idx, // parent idx
                                      std::move(childName),    // attr name
                                      std::move(pathToChild),  // path to attr
                                      std::move(childAttr));
                if (childIsLeaf) {
                    visited.emplace(pathToChildHash);
                    continue;
                }
                else {
                    DFSStack.emplace(&nodeList.back());
                }
            }
        }

        return nodeList;
    }

    /*
     * Binary file layout (GroupAttribute):
     *
     *  -----------------------------------------------------------
     *  | Header | Flattened graph | Attr names | Data Attributes |
     *  -----------------------------------------------------------
     *
     * 1) Header:
     *
     *  <--------------- 40 bytes --------------->
     *  ------------------------------------------
     *  |  ID  |  Flags  |   N   |   S   |   D   |
     *  ------------------------------------------
     *
     *      1) ID   : char[13], "kodachi_cache"
     *      2) Flags: char[3] , 3 bytes, 24 bits to use for flags
     *      3) N    : uint64_t, total number of graph nodes
     *      4) S    : uint64_t, sum of sizes of all node names (attr names as null-terminated c-strings).
     *      5) D    : uint64_t, total size of all data contained in leaf nodes
     *
     * 2) Flattened graph (graph section):
     *
     *  <--------- N * sizeof(Node) ---------->
     *  ---------------------------------------
     *  | Node(0) | Node(1) | ... | Node(N-1) |
     *  ---------------------------------------
     *
     *  This list is built by traversing the original graph (GroupAttribute) using BFS,
     *  and building instances of type Node:
     *
     *  Each Node object contains:
     *      1) (uint64_t) Hash value, to be used to find the data attribute
     *      2) (uint64_t) Parent's index (in the flattened graph)
     *      3) (uint64_t) Attribute name offset (in the strings section)
     *
     * 3) Attribute names (strings section):
     *
     *  -----------------------------------------------
     *  | Name(0) |x| Name(1) |x| ... |x| Name(N-1) |x|
     *  -----------------------------------------------
     *
     *  Each node has a name associated with it (attribute name in GroupAttribute);
     *  they are all packed together in the strings section.
     *
     *  4) Data section:
     *
     *  ---------------------------------------
     *  | Data(0) | Data(1) | ... | Data(M-1) |
     *  ---------------------------------------
     *
     *  This section holds M number of Data objects. Each
     *  data object contains:
     *      1) (uint64)     Size (to find the end of current data object in the char array)
     *      2) (uint64_t)   Hash
     *      3) (1 x char)   Flags
     *      4) (1 x char)   Type
     *      5) (char array) Data
     */
    std::vector<char>
    convertToBinary(kodachi::GroupAttribute const& attr)
    {
        if (!attr.isValid()) {
            return { };
        }

        std::vector<DFSNode> dfsNodeList = DFSFlattenGroupAttr(attr); // this is the most expensive part!

        std::vector<Node> nodeList; nodeList.reserve(dfsNodeList.size());
        std::uint64_t stringSectionSize = 0;
        for (const auto& dfs_node : dfsNodeList) {
            nodeList.emplace_back(
                    dfs_node.getAttrHash(),
                    dfs_node.m_parent_idx,
                    stringSectionSize);

            stringSectionSize += dfs_node.m_attr_name.size() + 1 /* null char */;
        }

        //---------------------------------------------------

        std::uint64_t dataSectionSize = 0;
        std::vector<Data> dataSection;
        {
            std::unordered_set<std::uint64_t> visited;

            for (const auto& dfs_node : dfsNodeList) {
                // Only interest in leaves (DataAttributes)
                if (!isLeaf(dfs_node.m_attr)) {
                    continue;
                }

                const std::uint64_t attrHash = dfs_node.getAttrHash();

                // Skip if already visited
                if (visited.find(attrHash) != visited.end()) {
                    continue;
                }

                visited.emplace(attrHash); // mark this as 'visited'

                dataSection.emplace_back(dfs_node.m_attr);
                dataSectionSize += dataSection.back().m_header.getTotalSize();
            }
        }

        // By this point we should know the total size:
        //   i) the header: 40 bytes
        //  ii) the flattened graph: N * sizeof(Node)
        // iii) the size of string section: stringSectionSize
        //
        std::vector<char> bin(
                  GroupAttrHeader::m_header_size
                + nodeList.size() * sizeof(Node)
                + stringSectionSize
                + dataSectionSize);

        char* _begin = bin.data();

        // Write header
        GroupAttrHeader header(nodeList.size(),
                               stringSectionSize,
                               dataSectionSize,
                               attr.getGroupInherit());
        _begin = header.writeToCharArray(_begin);

        // Write graph
        for (auto const& node : nodeList) {
            _begin = node.writeToCharArray(_begin);
        }

        // Write attribute names
        for (std::size_t idx = 0, offset = 0; idx < dfsNodeList.size(); ++idx) {
            const auto& node = dfsNodeList[idx];

            std::memcpy(reinterpret_cast<void*>(_begin),
                        reinterpret_cast<const void*>(node.m_attr_name.data()),
                        node.m_attr_name.size());
            _begin[node.m_attr_name.size()] = '\0'; // Make sure ends with a null char

            offset = (node.m_attr_name.size() + 1 /* null char */);
            _begin = _begin + offset;
        }

        // Write data
        for (auto const& data : dataSection) {
            _begin = data.writeToCharArray(_begin);
        }

        return bin;
    }

    kodachi::GroupAttribute
    readFromBinary(const char* bin, std::size_t size)
    {
        kodachi::GroupBuilder gb;

        const char* _begin = bin;
        const char* _node_begin = nullptr;
        const char* _str_begin  = nullptr;
        const char* _data_begin = nullptr;

        GroupAttrHeader header;
        if (!header.readFromCharArray(_begin, _node_begin, _str_begin, _data_begin)) {
            return { };
        }

        const bool groupInherit = header.getGroupInherit(); // GroupInherit

        //-----------------------------------
        // Read data, and build a hash-to-attr table

        std::unordered_map<std::uint64_t, kodachi::Attribute> hashToAttrTable;
        const char* _data_cur = _data_begin;
        const char* _data_end = _data_begin + header.m_total_data_size;
        while (_data_cur != nullptr && _data_cur != _data_end) {
            Data tmpData;
            kodachi::DataAttribute dataAttr;
            _data_cur = tmpData.fromCharArray(_data_cur, dataAttr);

            hashToAttrTable.emplace(
                    tmpData.m_header.m_attr_hash,
                    std::move(dataAttr));
        }

        //-----------------------------------
        // Now rebuild the GroupAttribute

        const char* _str_curr = _str_begin;
        const Node* nodes = reinterpret_cast<const Node*>(_node_begin);

        gb.setGroupInherit(groupInherit);

        for (std::uint64_t idx = 0; idx < header.m_node_count; ++idx) {
            const Node& node = nodes[idx];

            auto iter = hashToAttrTable.find(node.m_hash);
            if (iter == hashToAttrTable.end()) {
                continue;
            }

            _str_curr = _str_begin + node.m_str_offset;
            const std::string attrPath =
                    buildFullAttrName(_str_begin, nodes, header.m_node_count, idx);

            gb.set(attrPath, iter->second);
        }

        return gb.build();
    }

    //--------------------------------------------------------------------------------
    // Conversion while directly reading/writing from/to disk to help
    // lower memory consumption.
    //

    class FileHandle {
    public:
        FileHandle() { }

        FileHandle(const std::string& filename)
            : m_filename(filename)
        {
        }

        ~FileHandle() { close(); }

        bool is_valid() const { return m_handle != nullptr; }

        bool end_of_file() const { return std::feof(m_handle) != 0; }

        bool open_for_read(const std::string& filename)
        {
            return internal_open(filename, "r");
        }

        bool open_for_write(const std::string& filename)
        {
            return internal_open(filename, "w");
        }

        bool
        close()
        {
            if (m_handle == nullptr) {
                return true;
            }

            if (std::fclose(m_handle) != 0) {
                return false; // failed
            }

            m_handle = nullptr;
            m_filename.clear();

            return true;
        }

        bool
        read(char* begin,
             std::size_t size)
        {
            return std::fread(
                    reinterpret_cast<void*>(begin),
                    1,
                    size,
                    m_handle) == size;
        }

        bool
        write(const char* begin,
              std::size_t size)
        {
            return std::fwrite(
                    reinterpret_cast<const void*>(begin),
                    1,    /* size of each element is 1 byte */
                    size, /* number of elements */
                    m_handle) == size;
        }

    private:
        bool
        internal_open(const std::string& filename, const char* mode)
        {
            // Don't do anything if a file is already open,
            // user must call FileHandle::close() first.
            if (m_handle != nullptr) { return false; }

            m_handle = std::fopen(filename.c_str(), mode);
            if (m_handle == nullptr) {
                return false;
            }

            m_filename = filename;
            return true;
        }

        std::string m_filename;
        std::FILE*  m_handle = nullptr;
    };

    void
    convertToBinary_directDiskWrite(const kodachi::GroupAttribute& attr, const std::string& filename)
    {
        if (!attr.isValid()) {
            return;
        }

        const std::string tempFileName = filename + ".tmp";

        FileHandle file_out;
        file_out.open_for_write(tempFileName);
        if (!file_out.is_valid()) {
            return;
        }

        std::vector<DFSNode> dfsNodeList = DFSFlattenGroupAttr(attr);

        std::vector<Node> nodeList; nodeList.reserve(dfsNodeList.size());
        std::uint64_t stringSectionSize = 0;
        for (const auto& dfs_node : dfsNodeList) {
            nodeList.emplace_back(
                    dfs_node.getAttrHash(),
                    dfs_node.m_parent_idx,
                    stringSectionSize);

            stringSectionSize += dfs_node.m_attr_name.size() + 1 /* null char */;
        }

        //---------------------------------------------------

        std::uint64_t dataSectionSize = 0;
        std::vector<Data> dataSection;
        {
            std::unordered_set<std::uint64_t> visited;

            for (const auto& dfs_node : dfsNodeList) {
                // Only interest in leaves (DataAttributes)
                if (!isLeaf(dfs_node.m_attr)) {
                    continue;
                }

                const std::uint64_t attrHash = dfs_node.getAttrHash();

                // Skip if already visited
                if (visited.find(attrHash) != visited.end()) {
                    continue;
                }

                visited.emplace(attrHash); // mark this as 'visited'

                dataSection.emplace_back(dfs_node.m_attr);
                dataSectionSize += dataSection.back().m_header.getTotalSize();
            }
        }

        //---------------------------
        // 1) Write header and node list to disk

        const std::size_t node_list_size = nodeList.size() * sizeof(Node);

        std::vector<char> bin(GroupAttrHeader::m_header_size + node_list_size);
        char* _begin = bin.data();

        GroupAttrHeader header(nodeList.size(),
                               stringSectionSize,
                               dataSectionSize,
                               attr.getGroupInherit());
        _begin = header.writeToCharArray(_begin);

        // Write graph
        for (auto const& node : nodeList) {
            _begin = node.writeToCharArray(_begin);
        }

        bool success = file_out.write(bin.data(), bin.size());
        bin.clear(); // free memory

        //---------------------------
        // 2) Write attribute names

        bin.resize(stringSectionSize);
        _begin = bin.data();

        for (std::size_t idx = 0, offset = 0; idx < dfsNodeList.size(); ++idx) {
            const auto& node = dfsNodeList[idx];

            std::memcpy(reinterpret_cast<void*>(_begin),
                        reinterpret_cast<const void*>(node.m_attr_name.data()),
                        node.m_attr_name.size());
            _begin[node.m_attr_name.size()] = '\0'; // Make sure ends with a null char

            offset = (node.m_attr_name.size() + 1 /* null char */);
            _begin = _begin + offset;
        }

        success = file_out.write(bin.data(), bin.size());
        bin.clear(); // free memory
        _begin = nullptr;

        //---------------------------
        // 3) Write data

        for (auto const& data : dataSection) {
            bin.resize(data.m_header.getTotalSize());

            data.writeToCharArray(bin.data());
            success = file_out.write(bin.data(), bin.size());

            bin.clear();
        }

        //---------------------------
        // Finished, now close the file and then rename it
        // to remove the ".tmp" extension

        file_out.close();

        std::rename(tempFileName.c_str(), filename.c_str());
    }

    kodachi::GroupAttribute
    readFromBinary_directDiskRead(const std::string& filename)
    {
        FileHandle file_in;
        file_in.open_for_read(filename);
        if (!file_in.is_valid()) {
            return { };
        }

        kodachi::GroupBuilder gb;

        //---------------------------
        // 1) Read the header first:
        //      the first 40 bytes at the beginning of the file.

        std::vector<char> bin(GroupAttrHeader::m_header_size);
        bool success = file_in.read(bin.data(), bin.size());
        char* _begin = bin.data();

        GroupAttrHeader header;
        if (!header.readFromCharArray(_begin)) {
            return { };
        }

        bin.clear(); // free memory
        _begin = nullptr;

        const bool groupInherit = header.getGroupInherit(); // GroupInherit

        //---------------------------
        // 2) Read the node list

        const std::size_t nodeListSize = header.m_node_count * sizeof(Node);
        std::vector<char> nodesBin(nodeListSize);
        _begin = nodesBin.data();

        success = file_in.read(_begin, nodeListSize);
        _begin = nullptr;

        //---------------------------
        // 3) Read the attribute name list

        std::vector<char> stringList(header.m_string_section_size);
        _begin = stringList.data();

        success = file_in.read(_begin, stringList.size());

        _begin = nullptr;

        //---------------------------
        // 4) Read data, and build a hash-to-attr table

        std::unordered_map<std::uint64_t, kodachi::DataAttribute> hashToAttrTable;

        while (! file_in.end_of_file()) {
            std::vector<char> dataBin;

            // Data header:
            //
            // <----------- 56 bytes ----------->
            // ----------------------------------
            // | ID | Flags | H | N | S | V | T |
            // ----------------------------------

            dataBin.resize(DataAttrHeader::m_header_size_bytes);
            success = file_in.read(dataBin.data(), dataBin.size());

            DataAttrHeader dataHeader;
            const char* dummy = nullptr;
            if (dataHeader.fromCharArray(dataBin.data(), dummy, dummy) == false) {
                continue;
            }

            dataBin.clear();
            dataBin.resize(dataHeader.getDataSize());
            success = file_in.read(dataBin.data(), dataBin.size());

            const int attrType = dataHeader.getAttrType();
            const char* _begin_samples = dataBin.data();
            const char* _begin_values  = _begin_samples + dataHeader.m_time_sample_count * sizeof(float);

            kodachi::DataAttribute attr;
            if (attrType == kFnKatAttributeTypeInt) {
                attr = BinaryToDataAttr<kodachi::IntAttribute>(
                        dataHeader,
                        _begin,
                        _begin_samples,
                        _begin_values);
            }
            else if (attrType == kFnKatAttributeTypeFloat) {
                attr = BinaryToDataAttr<kodachi::FloatAttribute>(
                        dataHeader,
                        _begin,
                        _begin_samples,
                        _begin_values);
            }
            else if (attrType == kFnKatAttributeTypeDouble) {
                attr = BinaryToDataAttr<kodachi::DoubleAttribute>(
                        dataHeader,
                        _begin,
                        _begin_samples,
                        _begin_values);
            }
            else if (attrType == kFnKatAttributeTypeString) {
                attr = BinaryToDataAttr<kodachi::StringAttribute>(
                        dataHeader,
                        _begin,
                        _begin_samples,
                        _begin_values);
            }

            hashToAttrTable.emplace(
                    dataHeader.m_attr_hash,
                    std::move(attr));
        }

        success = file_in.close();

        //-----------------------------------
        // Now rebuild the GroupAttribute

        const char* _str_begin = stringList.data();
        const char* _str_curr  = _str_begin;
        const Node* nodes = reinterpret_cast<const Node*>(nodesBin.data());

        gb.setGroupInherit(groupInherit);

        for (std::uint64_t idx = 0; idx < header.m_node_count; ++idx) {
            const Node& node = nodes[idx];

            auto iter = hashToAttrTable.find(node.m_hash);
            if (iter == hashToAttrTable.end()) {
                continue;
            }

            _str_curr = _str_begin + node.m_str_offset;
            const std::string attrPath =
                    buildFullAttrName(_str_begin, nodes, header.m_node_count, idx);

            gb.set(attrPath, iter->second);
        }

        return gb.build();
    }
} // namespace kodachi


// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// Linux
#include <uuid/uuid.h>

// C++
#include <string>

//--------------------------------------------------

namespace kodachi
{
    class KodachiOpId
    {
    private:
        mutable std::string mStr;
        uuid_t mUuid;

    public:

        //--------------------------------------------
        // https://linux.die.net/man/3/uuid_generate

        // Creates a NULL UUID.
        KodachiOpId() : mUuid{} { }

        // Generate a UUID
        static KodachiOpId generate()
        {
            uuid_t newId;
            uuid_generate(newId);
            return KodachiOpId(newId);
        }

        //--------------------------------------------
        // https://linux.die.net/man/3/uuid_parse

        explicit
        KodachiOpId(const uuid_t uuid)
        {
            uuid_copy(mUuid, uuid);

            // convert to string
            char cstr[37];
            uuid_unparse(mUuid, cstr);
            mStr = std::string(cstr);
        }

        explicit
        KodachiOpId(const char* uuid)
            : mStr((uuid != nullptr ? uuid : ""))
        {
            uuid_parse(uuid, mUuid);
        }

        explicit
        KodachiOpId(const std::string& uuid)
            : mStr(uuid)
        {
            uuid_parse(uuid.c_str(), mUuid);
        }

        //--------------------------------------------
        // https://linux.die.net/man/3/uuid_copy

        KodachiOpId(const KodachiOpId& id)
            : mStr(id.mStr)
        {
            uuid_copy(mUuid, id.mUuid);
        }

        KodachiOpId(KodachiOpId&& id)
            : mStr(std::move(id.mStr))
        {
            uuid_copy(mUuid, id.mUuid);
            uuid_clear(id.mUuid); // https://linux.die.net/man/3/uuid_clear
        }

        KodachiOpId& operator=(const KodachiOpId& id)
        {
            mStr = id.mStr;
            uuid_copy(mUuid, id.mUuid);
            return *this;
        }

        KodachiOpId& operator=(KodachiOpId&& id)
        {
            mStr = std::move(id.mStr);
            uuid_copy(mUuid, id.mUuid);
            uuid_clear(id.mUuid); // https://linux.die.net/man/3/uuid_clear
            return *this;
        }

        //--------------------------------------------
        // https://linux.die.net/man/3/uuid_clear

        void clear() { uuid_clear(mUuid); mStr.clear(); }

        //--------------------------------------------
        // https://linux.die.net/man/3/uuid_is_null

        bool is_null() const { return (uuid_is_null(mUuid) == 1); }

        //--------------------------------------------

        bool is_valid() const { return (uuid_type(mUuid) > 0); }

        //--------------------------------------------
        // Misc.

        const unsigned char* data() const { return mUuid; }

        //--------------------------------------------
        // https://linux.die.net/man/3/uuid_unparse

        const std::string& str() const
        {
            // NOTE: uuid_unparse() converts the supplied UUID from the binary
            //       representation into a 36-byte string (plus tailing '\0').
            if (mStr.empty()) {
                char cstr[37];
                uuid_unparse(mUuid, cstr);
                mStr = std::string(cstr);
            }

            return mStr;
        }

        // Can't decide whether or not we should enable implicit conversion to
        // std::string; commenting out these conversions for now, may enable
        // one of them in the future (probably the second one).
        //
        //      operator std::string() const { return str(); }
        //      operator std::string const&() const { return str(); }

        //--------------------------------------------
        // https://linux.die.net/man/3/uuid_compare

        bool operator==(const KodachiOpId& rhs) const { return (uuid_compare(mUuid, rhs.mUuid) == 0); }
        bool operator!=(const KodachiOpId& rhs) const { return (uuid_compare(mUuid, rhs.mUuid) != 0); }
        bool operator<(const KodachiOpId& rhs) const { return (uuid_compare(mUuid, rhs.mUuid) < 0); }

        friend std::ostream& operator<<(std::ostream& os, const KodachiOpId& id)
        {
            os << id.str();
            return os;
        }
    };

} // namespace kodachi

//--------------------------------------------------
// hash<kodachi::KodachiOpId> specialization

namespace std
{
    template <>
    struct hash<kodachi::KodachiOpId>
    {
        std::size_t operator()(const kodachi::KodachiOpId& id) const
        {
            return std::hash<std::string>{}(id.str());
        }
    };
} // namespace std


// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// Include the cc instead of the header so we can access the anonymous functions
#include <scene_rdl2/scene/rdl2/Types.h>

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <pdevunit/pdevunit.h>

#include <set>
#include <sstream>
#include "../rdl_util/RDLObjectCache.cc"

namespace kodachi_moonray {
namespace unittest {

class TestRdlObjectCache : public CppUnit::TestFixture
{
    CPPUNIT_TEST_SUITE(TestRdlObjectCache);
    CPPUNIT_TEST( testGetValueType );
    CPPUNIT_TEST_SUITE_END();
public:
    void testGetValueType();
};

void
TestRdlObjectCache::testGetValueType()
{
    const std::set<arras::rdl2::AttributeType> unhandledTypes {
        arras::rdl2::AttributeType::TYPE_UNKNOWN,
        // TODO: Decide if we want to handle these types
        arras::rdl2::AttributeType::TYPE_SCENE_OBJECT_INDEXABLE
    };

    // TODO: This assumes TYPE_SCENE_OBJECT_INDEXABLE will always be the last
    // value in the enum
    const int end =
            static_cast<int>(arras::rdl2::AttributeType::TYPE_SCENE_OBJECT_INDEXABLE);

    for (int i = 0; i <= end; ++i) {
        const arras::rdl2::AttributeType type = static_cast<arras::rdl2::AttributeType>(i);
        if (unhandledTypes.count(type) > 0) {
            continue;
        }

        const std::string typeStr(arras::rdl2::attributeTypeName(type));
        if (getValueType(typeStr) == kFnRendererObjectValueTypeUnknown) {
            std::ostringstream ss;
            ss << "Unhandled arras::rdl2::AttributeType: " << typeStr;
            CPPUNIT_FAIL(ss.str());
        }
    }
}

} // namespace unittest
} // namespace kodachi_moonray

int
main(int argc, char *argv[])
{
    CPPUNIT_TEST_SUITE_REGISTRATION(kodachi_moonray::unittest::TestRdlObjectCache);

    return pdevunit::run(argc, argv);
}


// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <pdevunit/pdevunit.h>

#include "../../util/unittest/AttrUtilTests.h"

int
main(int argc, char *argv[])
{
    CPPUNIT_TEST_SUITE_REGISTRATION(kodachi_moonray::unittest::TestAttrUtil);

    return pdevunit::run(argc, argv);
}


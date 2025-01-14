// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <scene_rdl2/scene/rdl2/rdl2.h>

using namespace arras;

RDL2_DSO_ATTR_DECLARE

// support for arbitrary data. Vector of UserData
rdl2::AttributeKey<rdl2::SceneObjectVector> attrPrimitiveAttributes;

RDL2_DSO_ATTR_DEFINE(rdl2::Geometry);

attrPrimitiveAttributes =
    sceneClass.declareAttribute<rdl2::SceneObjectVector>("primitive_attributes", { "primitive attributes" });
sceneClass.setMetadata(attrPrimitiveAttributes, "label", "primitive attributes");
sceneClass.setMetadata(attrPrimitiveAttributes, "comment", "Vector of UserData. "
    "Each UserData should contain a CONSTANT rate value for each instance");


RDL2_DSO_ATTR_END


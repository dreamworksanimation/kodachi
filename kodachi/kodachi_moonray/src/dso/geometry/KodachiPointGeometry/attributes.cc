// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <scene_rdl2/scene/rdl2/rdl2.h>

using namespace arras;

RDL2_DSO_ATTR_DECLARE

rdl2::AttributeKey<rdl2::String> attrScenegraphLocation;
rdl2::AttributeKey<rdl2::SceneObject*> attrKodachiRuntime;

DECLARE_COMMON_MOTION_BLUR_ATTRIBUTES

RDL2_DSO_ATTR_DEFINE(rdl2::Geometry);

    attrScenegraphLocation = sceneClass.declareAttribute<rdl2::String>("scenegraph_location");
    sceneClass.setMetadata(attrScenegraphLocation, "label", "scenegraph location");

    attrKodachiRuntime = sceneClass.declareAttribute<rdl2::SceneObject*>("kodachi_runtime");
    sceneClass.setMetadata(attrKodachiRuntime, "label", "kodachi runtime");
    
DEFINE_COMMON_MOTION_BLUR_ATTRIBUTES    

RDL2_DSO_ATTR_END


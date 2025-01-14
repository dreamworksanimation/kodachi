// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <scene_rdl2/scene/rdl2/rdl2.h>

using namespace arras;

RDL2_DSO_ATTR_DECLARE

rdl2::AttributeKey<rdl2::String> attrOptree;
rdl2::AttributeKey<rdl2::Int> attrOptreeMode;
rdl2::AttributeKey<rdl2::Bool> attrFlushPluginCaches;
rdl2::AttributeKey<rdl2::String> attrRezResolve;
rdl2::AttributeKey<rdl2::String> attrWorkingDirectory;

RDL2_DSO_ATTR_DEFINE(rdl2::SceneObject);

    attrOptree = sceneClass.declareAttribute<rdl2::String>("optree");
    sceneClass.setMetadata(attrOptree, "label", "optree");

    attrOptreeMode = sceneClass.declareAttribute<rdl2::Int>("optree_mode", arras::rdl2::Int(0), rdl2::FLAGS_ENUMERABLE);
    sceneClass.setMetadata(attrOptreeMode, "label", "optree mode");
    sceneClass.setEnumValue(attrOptreeMode, 0, "base64");
    sceneClass.setEnumValue(attrOptreeMode, 1, "file");

    attrFlushPluginCaches = sceneClass.declareAttribute<rdl2::Bool>("flush_plugin_caches", arras::rdl2::Bool(true));
    sceneClass.setMetadata(attrFlushPluginCaches, "label", "flush plugin caches");

    attrRezResolve = sceneClass.declareAttribute<rdl2::String>("rez_resolve");
    sceneClass.setMetadata(attrRezResolve, "label", "rez resolve");
    sceneClass.setMetadata(attrRezResolve, "comment", "The value of '$REZ_RESOLVE'"
            "in the process that created this KodachiRuntime");

    attrWorkingDirectory = sceneClass.declareAttribute<rdl2::String>("working_directory");
    sceneClass.setMetadata(attrWorkingDirectory, "label", "working directory");
    sceneClass.setMetadata(attrRezResolve, "comment", "The working directory"
                "of the process that created this KodachiRuntime");

RDL2_DSO_ATTR_END


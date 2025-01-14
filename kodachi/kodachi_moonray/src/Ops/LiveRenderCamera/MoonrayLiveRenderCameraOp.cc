// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>

namespace {
const kodachi::string_view kRoot("/root");

// Moonray doesn't handle camera switching, so fake it by making a new
// camera location and copying the render camera to it. This won't allow for
// switching Camera SceneClass types.
class MoonrayLiveRenderCameraOp: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        if (interface.getInputLocationPath() == kRoot) {
            static const kodachi::StringAttribute kLiveRenderCameraNameAttr(
                    "/root/__scenebuild/camera");

            // get the current render camera name and replace it with the
            // live-render camera name
            const kodachi::StringAttribute cameraNameAttr =
                    interface.getAttr("renderSettings.cameraName");

            interface.setAttr("renderSettings.cameraName", kLiveRenderCameraNameAttr);

            interface.createChild("__scenebuild", "",
                    kodachi::GroupAttribute("cameraName", cameraNameAttr, false));
            return;
        }

        // This opArg should only exist at /root/__scenebuild
        const kodachi::StringAttribute cameraNameAttr =
                interface.getOpArg("cameraName");

        if (cameraNameAttr.isValid()) {
            const kodachi::string_view cameraName = cameraNameAttr.getValueCStr();

            interface.prefetch(cameraName);

            if (!interface.doesLocationExist(cameraName)) {
                kodachi::ReportError(interface,
                        kodachi::concat("camera does not exist: '", cameraName, "'"));
                return;
            }

            // prevent recursively creating a 'camera' child
            interface.replaceChildTraversalOp("", kodachi::GroupAttribute{});
            interface.copyLocationToChild("camera", cameraName);
        }

        interface.stopChildTraversal();
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        kodachi::OpDescriptionBuilder builder;

        builder.setSummary("Copies the render camera to '/root/__scenebuild/camera' and updates renderSettings");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(MoonrayLiveRenderCameraOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayLiveRenderCameraOp, "MoonrayLiveRenderCamera", 0, 1);
}


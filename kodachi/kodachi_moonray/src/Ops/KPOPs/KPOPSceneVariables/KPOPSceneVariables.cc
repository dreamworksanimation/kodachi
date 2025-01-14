// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/logging/KodachiLogging.h>

#include <array>
#include <set>

namespace {

KdLogSetup("KPOPSceneVariables");

class KPOPSceneVariables: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        if (interface.atRoot()) {
            static const kodachi::StringAttribute kDiskRender("diskRender");
            bool diskRender =
                kodachi::StringAttribute(interface.getOpArg("renderType")) == kDiskRender;

            // We need the SceneObject name of the render camera
            const kodachi::GroupAttribute renderSettingsAttr =
                    interface.getAttr("renderSettings");

            // MoonrayGlobalStatements is a mixture of rdl2::SceneVariables
            // and other variables. Isolate the SceneVariables
            const kodachi::GroupAttribute moonrayGlobalStatementsAttr =
                    interface.getAttr("moonrayGlobalStatements");

            kodachi::GroupBuilder sceneVariablesGb;
            sceneVariablesGb.setGroupInherit(false);

            // frame
            {
                const kodachi::FloatAttribute frameAttr =
                        interface.getOpArg("system.timeSlice.currentTime");
                if (frameAttr.isValid()) {
                    sceneVariablesGb.set("frame", frameAttr);
                }
            }

            // layer
            {
                static const kodachi::StringAttribute kDefaultLayerAttr(
                        "/root/__scenebuild/layer/default");

                sceneVariablesGb.set("layer", kDefaultLayerAttr);
            }

            // aperture_window, region_window, sub_viewport
            {
                // The DWA viewportWindow is at xy==screen_window_offset and wh==xyRes
                int offset[2] = {0, 0};
                if (diskRender ||
                    moonrayGlobalStatementsAttr.getChildByName("aperture_window").isValid()) {
                    // monitor requires aperture_window to be at 0,0
                    kodachi::IntAttribute screenWindowOffsetAttr(
                        moonrayGlobalStatementsAttr.getChildByName("screen_window_offset"));
                    if (screenWindowOffsetAttr.isValid()) {
                        auto&& xy(screenWindowOffsetAttr.getNearestSample(0.f));
                        offset[0] = xy[0];
                        offset[1] = xy[1];
                    }
                }
                kodachi::IntAttribute xyResAttr(renderSettingsAttr.getChildByName("xyRes"));
                auto&& xyRes(xyResAttr.getNearestSample(0.f));

                // This is NOT the aperture from the format! It is the outer edge before
                // any cropping or padding
                int apertureWindow[4] = {
                    offset[0],
                    offset[1],
                    offset[0] + xyRes[0],
                    offset[1] + xyRes[1]};
                sceneVariablesGb.set("aperture_window", kodachi::IntAttribute(apertureWindow, 4, 1));

                // region_window is the pixels to render
                int regionWindow[4] = {
                    offset[0],
                    offset[1],
                    offset[0] + xyRes[0],
                    offset[1] + xyRes[1]};
                // overscan makes the data window larger but does not change the offset
                {
                    // presumably for historical Katana reasons, overscan can be
                    // a float or int attribute of 1-4 values.
                    // overscan values are in pixels, in the order of
                    // left bottom right top
                    const kodachi::DataAttribute overscanAttr(
                        renderSettingsAttr.getChildByName("overscan"));

                    if (overscanAttr.isValid()) {
                        int overscan[4] = {0, 0, 0, 0};
                        std::size_t numValues = overscanAttr.getNumberOfValues();
                        if (numValues > 4) numValues = 4;

                        const kodachi::AttributeType overScanType =
                                overscanAttr.getType();

                        if (overScanType == kodachi::kAttrTypeFloat) {
                            const kodachi::FloatAttribute fOverscan(overscanAttr);
                            if (numValues == 1) {
                                const int oscan = lrintf(fOverscan.getValue());
                                overscan[0] = oscan;
                                overscan[1] = oscan;
                                overscan[2] = oscan;
                                overscan[3] = oscan;
                            } else {
                                auto&& overscanSample = fOverscan.getNearestSample(0.f);
                                for (size_t i = 0; i < numValues; ++i)
                                    overscan[i] = lrintf(overscanSample[i]);
                            }
                        } else if (overScanType == kodachi::kAttrTypeInt) {
                            const kodachi::IntAttribute iOverscan(overscanAttr);
                            if (numValues == 1) {
                                const int oscan = iOverscan.getValue();
                                overscan[0] = oscan;
                                overscan[1] = oscan;
                                overscan[2] = oscan;
                                overscan[3] = oscan;
                            } else {
                                auto&& overscanSample = iOverscan.getNearestSample(0.f);
                                for (size_t i = 0; i < numValues; ++i)
                                    overscan[i] = overscanSample[i];
                            }
                        }
                        // convert overscan into regionWindow
                        regionWindow[0] -= overscan[0];
                        regionWindow[1] -= overscan[1];
                        regionWindow[2] += overscan[2];
                        regionWindow[3] += overscan[3];
                    }
                }

                // sub_viewport is cropping relative to region_window
                int subViewport[4] = {
                    0,
                    0,
                    regionWindow[2]-regionWindow[0],
                    regionWindow[3]-regionWindow[1]};
                {
                    // Crop is in fractions of viewport+overscan and y is inverted
                    const kodachi::FloatAttribute cropWindowAttr =
                        renderSettingsAttr.getChildByName("cropWindow");
                    if (cropWindowAttr.isValid()) {
                        auto&& crop(cropWindowAttr.getNearestSample(0.f));
                        const float w = subViewport[2];
                        const float h = subViewport[3];
                        subViewport[0] = lrintf(crop[0] * w);
                        subViewport[2] = lrintf(crop[1] * w);
                        subViewport[3] = h - lrintf(crop[2] * h);
                        subViewport[1] = h - lrintf(crop[3] * h);
                    }
                }
                {
                    // ROI is left, bottom, width, height in viewport coordinates
                    const kodachi::IntAttribute roiAttr =
                        renderSettingsAttr.getChildByName("ROI");
                    if (roiAttr.isValid()) {
                        auto&& roi(roiAttr.getNearestSample(0.f));
                        int x = offset[0] - regionWindow[0];
                        int y = offset[1] - regionWindow[1];
                        subViewport[0] = std::max(subViewport[0], x + roi[0]);
                        subViewport[1] = std::max(subViewport[1], y + roi[1]);
                        subViewport[2] = std::min(subViewport[2], x + roi[0] + roi[2]);
                        subViewport[3] = std::min(subViewport[3], y + roi[1] + roi[3]);
                    }
                }
                // don't send negative width/height
                if (subViewport[2] < subViewport[0]) subViewport[2] = subViewport[0];
                if (subViewport[3] < subViewport[1]) subViewport[3] = subViewport[1];

                if (diskRender) {
                    // to make tiling work this must be put into the region
                    regionWindow[2] = regionWindow[0] + subViewport[2];
                    regionWindow[3] = regionWindow[1] + subViewport[3];
                    regionWindow[0] += subViewport[0];
                    regionWindow[1] += subViewport[1];
                } else {
                    sceneVariablesGb.set("sub_viewport", kodachi::IntAttribute(subViewport, 4, 1));
                }

                sceneVariablesGb.set("region_window", kodachi::IntAttribute(regionWindow, 4, 1));
            }

            // motion_steps, enable_motion_blur
            {
                const int numSamples = kodachi::GetNumSamples(interface);
                const float shutterOpen = kodachi::GetShutterOpen(interface);
                const float shutterClose = kodachi::GetShutterClose(interface);
                const bool mbEnabled = (numSamples >= 2
                                    && (std::fabs(shutterOpen - shutterClose) >
                                        std::numeric_limits<float>::epsilon()));

                // Moonray does no like to have motion_steps values if they are the same
                if (mbEnabled) {
                    const std::array<float, 2> shutterTimes
                    { shutterOpen, shutterClose };
                    sceneVariablesGb.set("motion_steps", kodachi::FloatAttribute(
                            shutterTimes.data(), shutterTimes.size(), 1));
                } else {
                    sceneVariablesGb.set("motion_steps", kodachi::FloatAttribute(shutterOpen));
                    sceneVariablesGb.set("enable_motion_blur", kodachi::IntAttribute(false));
                }
            }

            // enable_dof
            {
                // DOF is enabled by default in SceneVariables, but we decided
                // to disable it unless it is explicitly set
                const kodachi::IntAttribute enableDofAttr =
                        moonrayGlobalStatementsAttr.getChildByName("enable DOF");

                if (enableDofAttr.isValid()) {
                    sceneVariablesGb.set("enable_dof", enableDofAttr);
                }
            }

            // multi-machine render attributes
            {
                const kodachi::GroupAttribute backendSettingsAttr =
                        interface.getAttr("kodachi.backendSettings");

                const kodachi::IntAttribute machineIdAttr =
                        backendSettingsAttr.getChildByName("machineId");
                const kodachi::IntAttribute numMachinesAttr =
                        backendSettingsAttr.getChildByName("numMachines");

                if (numMachinesAttr.isValid() && machineIdAttr.isValid()) {
                    sceneVariablesGb.set("machine_id", machineIdAttr);
                    sceneVariablesGb.set("num_machines", numMachinesAttr);
                }
            }

            // deep id / cryptomatte attributes
            {
                std::vector<std::string> deepIds;

                const kodachi::StringAttribute deepIdAttr =
                        moonrayGlobalStatementsAttr.getChildByName("deep_id_attribute_names");

                if (deepIdAttr.isValid()) {
                    const auto deepIdSample = deepIdAttr.getNearestSample(0.0f);

                    deepIds.insert(deepIds.end(), deepIdSample.begin(), deepIdSample.end());
                }

                const kodachi::GroupAttribute outputChannels =
                        moonrayGlobalStatementsAttr.getChildByName("outputChannels");

                if (outputChannels.isValid()) {
                    std::set<std::string> cryptomatteIds;

                    kodachi::GroupBuilder cmgb;
                    cmgb.setGroupInherit(false);

                    for (const auto& child : outputChannels) {
                        const kodachi::GroupAttribute outputChannel = child.attribute;
                        const kodachi::StringAttribute result =
                                outputChannel.getChildByName("result");

                        if (result.isValid() && result == "cryptomatte") {

                            const kodachi::StringAttribute layer =
                                    outputChannel.getChildByName("cryptomatte_layer");

                            if (layer.isValid()) {
                                cryptomatteIds.insert(layer.getValue());
                                cmgb.set(layer.getValue(), kodachi::IntAttribute(1));
                            }
                        }
                    }

                    deepIds.insert(deepIds.end(), cryptomatteIds.begin(), cryptomatteIds.end());

                    const kodachi::GroupAttribute cryptoAttr = cmgb.build();
                    if (cryptoAttr.getNumberOfChildren() > 0) {
                        interface.setAttr("cryptomatte", cryptoAttr);
                    }
                }

                if (!deepIds.empty()) {
                    sceneVariablesGb.set("deep_id_attribute_names", kodachi::StringAttribute(deepIds));
                }
            }

            // Any moonrayGlobalStatements attributes set will take priority
            // This set is to remove ones that must not take priority, and to remove
            // ones that moonray does not need.
            const static std::set<kodachi::string_view> ignoreSet{
                "format_aperture_window",
                "screen_window_offset",
                "multi threaded",
                "scene file output",
                "skip render",
                "log limit",
                "reuse cached materials",
                "lightsetCaching",
                "primitiveAttributeCaching",
                "autoInstancing",
                "max curve clump size",
                "disable object splitting",
                "id pass snapshot delay",
                "scene file input",
                "camera",
                "live_render_fps",
                "preview_render_fps",
                "vectorized",
                "outputChannels",
                "enable DOF",
                "deep_id_attribute_names"
            };
            for (auto x : moonrayGlobalStatementsAttr) {
                if (not ignoreSet.count(x.name))
                    sceneVariablesGb.set(x.name, x.attribute);
            }

            // camera
            {
                const kodachi::StringAttribute cameraPathAttr =
                        renderSettingsAttr.getChildByName("cameraName");

                if (cameraPathAttr.isValid()) {
                    sceneVariablesGb.set("camera", cameraPathAttr);
                } else {
                    KdLogInfo("'renderSettings.cameraName' not set");
                }

            }

            // The SceneVariables class is a singleton, but we will pretend
            // to create one so that we can set the variables in the same way
            // as any other scene object
            static const kodachi::StringAttribute kSceneVariablesAttr("SceneVariables");
            static const std::string kSceneVariablesPath("/root/__scenebuild/sceneVariables");
            static const std::string kRdl2("rdl2");

            kodachi::op_args_builder::StaticSceneCreateOpArgsBuilder sscb(true);
                        sscb.createEmptyLocation(kSceneVariablesPath, kRdl2);

            sscb.setAttrAtLocation(kSceneVariablesPath,
                                   "rdl2.sceneObject.sceneClass",
                                   kSceneVariablesAttr);
            sscb.setAttrAtLocation(kSceneVariablesPath,
                                   "rdl2.sceneObject.name",
                                   kodachi::StringAttribute(kSceneVariablesPath));
            sscb.setAttrAtLocation(kSceneVariablesPath,
                                   "rdl2.sceneObject.attrs",
                                   sceneVariablesGb.build());
            sscb.setAttrAtLocation(kSceneVariablesPath,
                                   "rdl2.sceneObject.disableAliasing",
                                   kodachi::IntAttribute(true));

            interface.execOp("StaticSceneCreate", sscb.build());
        }

        interface.stopChildTraversal();
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        OpDescriptionBuilder builder;

        builder.setSummary("Takes attributes from root and the active camera to populate an rdl2::SceneVariables");
        builder.setHelp("SceneVariables is a singleton, so multiple locations are not supported");

        return builder.build();
    }
};

///////////////////////////////////////////////////////////////////////////////////////////

DEFINE_KODACHIOP_PLUGIN(KPOPSceneVariables)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPSceneVariables, "KPOPSceneVariables", 0, 1);
}


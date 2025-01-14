// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "Manipulators/BarnDoorsLightFilterManipulator.h"
#include "Manipulators/DecayLightFilterManipulator.h"
#include "LightDelegateComponent.h"
#include "LightLayer.h"

#include "CameraViewerModifier.h"
#include "LightViewerModifier.h"
#include "Manipulators/LightManipulators.h"

// the DEFINE and REGISTER macros don't seem to like namespaces,
// but using namespace seems to avoid the issue
using namespace MoonrayKatana;

namespace {
    DEFINE_VMP_PLUGIN(LightViewerModifier);
    DEFINE_VMP_PLUGIN(LightFilterViewerModifier);
    DEFINE_VMP_PLUGIN(CameraViewerModifier)

    DEFINE_VIEWER_DELEGATE_COMPONENT_PLUGIN(LightDelegateComponent);
    DEFINE_VIEWPORT_LAYER_PLUGIN(LightLayer);
    DEFINE_MANIPULATOR_PLUGIN(ConeAngleManipulator);
    DEFINE_MANIPULATOR_PLUGIN(RadiusManipulator);
    DEFINE_MANIPULATOR_PLUGIN(AspectRatioManipulator);
    //DEFINE_MANIPULATOR_PLUGIN(ExposureManipulator);
    DEFINE_MANIPULATOR_PLUGIN(SizeManipulator);
    DEFINE_MANIPULATOR_PLUGIN(DecayLightFilterManipulator);
    DEFINE_MANIPULATOR_PLUGIN(BarnDoorsLightFilterManipulator);
    DEFINE_MANIPULATOR_HANDLE_PLUGIN(MoonrayLabelManipulatorHandle);
    DEFINE_MANIPULATOR_HANDLE_PLUGIN(DecayLightFilterManipulatorHandle);
    DEFINE_MANIPULATOR_HANDLE_PLUGIN(BarnDoorsLightFilterManipulatorHandle);
    DEFINE_MANIPULATOR_HANDLE_PLUGIN(ConeAngleManipulatorHandle);
    DEFINE_MANIPULATOR_HANDLE_PLUGIN(RadiusManipulatorHandle);
    DEFINE_MANIPULATOR_HANDLE_PLUGIN(AspectRatioManipulatorHandle);
    DEFINE_MANIPULATOR_HANDLE_PLUGIN(SizeManipulatorHandle);
    DEFINE_MANIPULATOR_HANDLE_PLUGIN(SizeEdgeManipulatorHandle);
}

void registerPlugins()
{
    REGISTER_PLUGIN(LightViewerModifier, "MoonrayLightViewerModifier", 0, 1);
    REGISTER_PLUGIN(LightFilterViewerModifier, "MoonrayLightFilterViewerModifier", 0, 1);
    REGISTER_PLUGIN(CameraViewerModifier, "DWACameraViewerModifier", 0, 1);

    REGISTER_PLUGIN(LightDelegateComponent, LightDelegateComponent::NAME, 0, 1);
    REGISTER_PLUGIN(LightLayer, "MoonrayLightLayer", 0, 1);
    REGISTER_PLUGIN(ConeAngleManipulator, "MoonrayConeAngleManipulator", 0, 1);
    REGISTER_PLUGIN(RadiusManipulator, "MoonrayRadiusManipulator", 0, 1);
    REGISTER_PLUGIN(AspectRatioManipulator, "MoonrayAspectRatioManipulator", 0, 1);
    //REGISTER_PLUGIN(ExposureManipulator, "MoonrayExposureManipulator", 0, 1);
    REGISTER_PLUGIN(SizeManipulator, "MoonraySizeManipulator", 0, 1);
    REGISTER_PLUGIN(DecayLightFilterManipulator, "MoonrayDecayLightFilterManipulator", 0, 1);
    REGISTER_PLUGIN(BarnDoorsLightFilterManipulator, "MoonrayBarnDoorsLightFilterManipulator", 0, 1);
    REGISTER_PLUGIN(MoonrayLabelManipulatorHandle, "MoonrayLabelManipulatorHandle", 0, 1);
    REGISTER_PLUGIN(DecayLightFilterManipulatorHandle, "MoonrayDecayLightFilterManipulatorHandle", 0, 1);
    REGISTER_PLUGIN(BarnDoorsLightFilterManipulatorHandle, "MoonrayBarnDoorsLightFilterManipulatorHandle", 0, 1);
    REGISTER_PLUGIN(ConeAngleManipulatorHandle, "MoonrayConeAngleManipulatorHandle", 0, 1);
    REGISTER_PLUGIN(RadiusManipulatorHandle, "MoonrayRadiusManipulatorHandle", 0, 1);
    REGISTER_PLUGIN(AspectRatioManipulatorHandle, "MoonrayAspectRatioManipulatorHandle", 0, 1);
    REGISTER_PLUGIN(SizeManipulatorHandle, "MoonraySizeManipulatorHandle", 0, 1);
    REGISTER_PLUGIN(SizeEdgeManipulatorHandle, "MoonraySizeEdgeManipulatorHandle", 0, 1);
}


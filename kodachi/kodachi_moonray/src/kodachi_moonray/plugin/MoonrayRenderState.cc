// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// self
#include "MoonrayRenderState.h"

// kodachi
#include <kodachi/cache/CacheUtils.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi_moonray/kodachi_geometry/KodachiGeometry.h>
#include <kodachi_moonray/kodachi_runtime_wrapper/KodachiRuntimeWrapper.h>

// scene_rdl2
#include <scene_rdl2/scene/rdl2/Attribute.h>
#include <scene_rdl2/scene/rdl2/Displacement.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>
#include <scene_rdl2/scene/rdl2/GeometrySet.h>
#include <scene_rdl2/scene/rdl2/Layer.h>
#include <scene_rdl2/scene/rdl2/Light.h>
#include <scene_rdl2/scene/rdl2/LightFilter.h>
#include <scene_rdl2/scene/rdl2/LightFilterSet.h>
#include <scene_rdl2/scene/rdl2/LightSet.h>
#include <scene_rdl2/scene/rdl2/Material.h>
#include <scene_rdl2/scene/rdl2/SceneContext.h>
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/ShadowSet.h>
#include <scene_rdl2/scene/rdl2/TraceSet.h>
#include <scene_rdl2/scene/rdl2/UserData.h>
#include <scene/rdl2/Utils.h>

// stl
#include <algorithm>
#include <exception>
#include <fstream>

#include <sys/stat.h>

using namespace arras;

namespace {
KdLogSetup("MoonrayRenderState");

union HashUnion
{
    uint32_t ui32;
    float f32;
};

void NullDeleter(rdl2::SceneObject*) {};

void
setUpLogging(const kodachi::GroupAttribute& globalSettings)
{
    // get the current filter and adjust its logging level
    const kodachi::IntAttribute logLevelAttr =
            globalSettings.getChildByName("log limit");

    const int logLevel = logLevelAttr.getValue(kKdLoggingSeverityError, false);

    kodachi::KodachiLogging::setSeverity(static_cast<KdLoggingSeverity>(logLevel));
}

const kodachi::Hash kNullHash{};

const kodachi::Hash& getFalseHash()
{
    static kodachi::Hash sFalseHash;

    if (sFalseHash == kNullHash) {
        const kodachi::IntAttribute falseAttr(false);
        sFalseHash = falseAttr.getHash();
    }

    return sFalseHash;
}

} // anonymous namespace

namespace kodachi_moonray {

MoonrayRenderState::MoonrayRenderState(const kodachi::GroupAttribute& rootAttrs)
{
    const kodachi::IntAttribute isLiveRenderAttr =
            rootAttrs.getChildByName("kodachi.backendSettings.isLiveRender");

    mIsLiveRender = isLiveRenderAttr.getValue(false, false);

    const kodachi::IntAttribute skipRenderAttr =
            rootAttrs.getChildByName("moonrayGlobalStatements.skip render");

    mSkipRender = skipRenderAttr.getValue(false, false);

    KdLogDebug("IsLiveRender: " << std::boolalpha << mIsLiveRender << std::noboolalpha);
    KdLogDebug("Skip Render: " << std::boolalpha << mSkipRender << std::noboolalpha);

    const kodachi::IntAttribute machineIdAttr =
            rootAttrs.getChildByName("kodachi.backendSettings.machineId");

    mMachineId = machineIdAttr.getValue(-1, false);

    const kodachi::IntAttribute numMachinesAttr =
            rootAttrs.getChildByName("kodachi.backendSettings.numMachines");

    mNumMachines = numMachinesAttr.getValue(-1, false);
}

MoonrayRenderState::~MoonrayRenderState()
{
}

void
MoonrayRenderState::useNewSceneContext()
{
    if (mKodachiRuntime) {
        mKodachiRuntime = nullptr;
    }

    mSceneContext = SceneContextPtr(new rdl2::SceneContext,
                                    std::default_delete<rdl2::SceneContext>());
}

void
MoonrayRenderState::useExternalSceneContext(rdl2::SceneContext* scp)
{
    if (mKodachiRuntime) {
        mKodachiRuntime = nullptr;
    }

    // We're not responsible for deleting the scene context
    mSceneContext = SceneContextPtr(scp, [](rdl2::SceneContext*){});
}

void
MoonrayRenderState::initializeKodachiRuntimeObject(const kodachi::GroupAttribute& opTreeAttr)
{
    mKodachiRuntime = mSceneContext->createSceneObject("KodachiRuntime", "KodachiRuntime");

    static_cast<KodachiRuntimeWrapper*>(mKodachiRuntime)->setOpTree(opTreeAttr);
}

void
MoonrayRenderState::processLocation(
        const kodachi::StringAttribute& locationPathAttr,
        const kodachi::GroupAttribute& locationAttributes)
{
    const std::string locationPath = locationPathAttr.getValue();

    // Threadlogpool setup
    kodachi::KodachiLogging::ThreadLogPool tPool(true, locationPath);

    // error and type checking
    // if a location's type is error, then cancel processing by throwing an exception
    // if a location contains an errorMessage, then log the error and don't process the location
    // if a location contains a warningMessage, then log the error but still process the location
    {
        static const kodachi::StringAttribute kErrorAttr("error");

        const kodachi::StringAttribute typeAttr =
                        locationAttributes.getChildByName("type");

        const kodachi::StringAttribute errorMessageAttr =
                            locationAttributes.getChildByName("errorMessage");

        if (typeAttr == kErrorAttr) {
            std::stringstream ss;
            ss << "Critical error at location '" << locationPath << "'";
            if (errorMessageAttr.isValid()) {
                ss << " - " << errorMessageAttr.getValueCStr();
            }

            throw std::runtime_error(ss.str());
        }

        if (errorMessageAttr.isValid()) {
            KdLogError(errorMessageAttr.getValueCStr());
            return;
        }

        const kodachi::StringAttribute warningMessageAttr =
                locationAttributes.getChildByName("warningMessage");
        if (warningMessageAttr.isValid()) {
            KdLogWarn(warningMessageAttr.getValueCStr());
        }

    }

    // We only care about locations that have a top-level rdl2 attribute
    const kodachi::GroupAttribute rdl2Attr =
            locationAttributes.getChildByName("rdl2");

    if (!rdl2Attr.isValid()) {
        return;
    }

    KdLogDebug("Processing rdl2 location");
    // rdl2 attribute can have 5 children:
    // - meta (used by KPOPs mainly, used here to get shutterOpen and shutterClose)
    // - rdlFile - StringAttribute of path to rdla|rdlb file to be loaded
    // - sceneObject
    // - layerAssign
    // - geoSetAssign

    const kodachi::StringAttribute rdlFileAttr =
            rdl2Attr.getChildByName("rdlFile");

    if (rdlFileAttr.isValid()) {
        addDeferredRdlArchiveUpdate(rdlFileAttr.getValue());
    }

    const kodachi::GroupAttribute sceneObjectAttr =
            rdl2Attr.getChildByName("sceneObject");

    kodachi::GroupAttribute layerAssignAttr =
            rdl2Attr.getChildByName("layerAssign");

    kodachi::GroupAttribute geometrySetAssignAttr =
            rdl2Attr.getChildByName("geoSetAssign");

    if (sceneObjectAttr.isValid()) {
        const kodachi::FloatAttribute shutterOpenAttr =
                rdl2Attr.getChildByName("meta.shutterOpen");

        const kodachi::FloatAttribute shutterCloseAttr =
                rdl2Attr.getChildByName("meta.shutterClose");

        const float shutterOpen = shutterOpenAttr.getValue(0.f, false);
        const float shutterClose = shutterCloseAttr.getValue(0.f, false);

        /**
         * instance.ID
         *
         * We only want to create the geometry for the first appearance of
         * an instanceID. In all other cases we want to make a GroupGeometry
         * as a reference to the object
         */
        const kodachi::StringAttribute instanceIDAttr =
                locationAttributes.getChildByName("instance.ID");

        bool makeReference = false;
        // TODO: handle live render auto-instancing changes
        if (!isLiveRender() && instanceIDAttr.isValid()) {
            InstanceIdMap::const_accessor constAccessor;
            if (mInstanceIdMap.find(constAccessor, instanceIDAttr)) {
                // We have already seen this ID
                makeReference = true;
                if (!constAccessor->second.second) {
                    // this is the second time we have seen this ID
                    // flag this ID's source object to be converted into
                    // GroupGeometry
                    constAccessor.release();
                    InstanceIdMap::accessor accessor;
                    mInstanceIdMap.find(accessor, instanceIDAttr);
                    accessor->second.second = true;
                }
            } else {
                // This is the first time we have seen this ID
                constAccessor.release();
                InstanceIdMap::accessor accessor;
                if (mInstanceIdMap.insert(accessor, instanceIDAttr)) {
                    // use this SceneObject as the potential instance source
                    accessor->second.first = locationPathAttr;
                    // we only want to convert it to an instance source if we
                    // see the ID again
                    accessor->second.second = false;

                    // Store all of the data necessary to set the attrs
                    // on the GroupGeometry if this object needs to be
                    // turned into an instance source
                    kodachi::GroupBuilder instanceSourceGb;
                    instanceSourceGb
                        .set("instanceAttrs", sceneObjectAttr.getChildByName("instance.attrs"))
                        .set("instanceSourceAttrs", sceneObjectAttr.getChildByName("instanceSource.attrs"))
                        .set("layerAssign", layerAssignAttr)
                        .set("geometrySetAssign", geometrySetAssignAttr)
                        .set("shutterOpen", shutterOpenAttr)
                        .set("shutterClose", shutterCloseAttr);

                    mPotentialInstanceSourceData[instanceIDAttr] = instanceSourceGb.build();

                } else {
                    // A different thread already registered an instance
                    // source for this ID while we were waiting for write
                    // access
                    makeReference = true;
                    accessor->second.second = true;
                }
            }
        }

        if (makeReference) {
            // Make a GroupGeometry with this location's xform that references
            // the instance source for the instance id
            static const kodachi::StringAttribute kGroupGeometry("GroupGeometry");

            const SceneObjectPtr groupGeometry =
                    getOrCreateSceneObject(locationPath, kGroupGeometry,
                                      kodachi::concat(locationPath, "_GroupGeometry"), false);

            const rdl2::Attribute* referenceGeometriesAttribute =
                    groupGeometry->getSceneClass().getAttribute(
                            rdl2::Geometry::sReferenceGeometries);

            // The instance source will be aliased during post processing
            addDeferredConnection(groupGeometry,
                    *referenceGeometriesAttribute, instanceIDAttr);

            // GroupGeometry has its own xform, Geometry values, and
            // CONSTANT rate primtive attributes
            const kodachi::GroupAttribute instanceAttrsAttr =
                    sceneObjectAttr.getChildByName("instance.attrs");

            const kodachi::GroupAttribute instanceArbAttrs =
                    sceneObjectAttr.getChildByName("instance.arbitrary");

            if (instanceAttrsAttr.isValid()) {
                setSceneObjectAttributes(groupGeometry, instanceAttrsAttr,
                                         shutterOpen, shutterClose);
            }

            if (instanceArbAttrs.isValid()) {
                rdl2::SceneObject::UpdateGuard guard(groupGeometry.get());
                groupGeometry->set("primitive_attributes",
                        createInstanceUserData(locationPath, instanceArbAttrs));
            }
        } else {
            const kodachi::StringAttribute sceneClassAttr =
                    sceneObjectAttr.getChildByName("sceneClass");

            const kodachi::StringAttribute nameAttr =
                    sceneObjectAttr.getChildByName("name");

            const kodachi::IntAttribute disableAliasingAttr =
                    sceneObjectAttr.getChildByName("disableAliasing");

            const bool disableAliasing = disableAliasingAttr.getValue(false, false);

            if (sceneClassAttr.isValid() && nameAttr.isValid()) {

                // If the sceneclass is RenderOutput and the name is not
                // /root/__scenebuild/renderoutput/primary, then add this
                // output to a deferred list.  This guarantees that the
                // beauty output is always the first to be processed and added
                // to the scenecontext.  This ensures that beauty is always
                // part0 in a multi-part exr
                if (sceneClassAttr == "RenderOutput") {
                    const std::string name = nameAttr.getValue();

                    if (name != "/root/__scenebuild/renderoutput/primary") {
                        // Not primary.  Add this to the deferred list
                        addDeferredRenderOutputCreation(locationPath, rdl2Attr);
                        return;
                    }
                }


                // createSceneObject will log errors if unable to create object
                const SceneObjectPtr sceneObject =
                        getOrCreateSceneObject(locationPath, sceneClassAttr,
                                          nameAttr.getValue(), disableAliasing);
                if (not sceneObject) return;

                // We only want to set the attributes for this object if we
                // are the first thread to process this location during this
                // processing iteration
                if (mProcessedSceneObjects.insert(sceneObject.get()).second) {
                    // set all of the attributes
                    const kodachi::GroupAttribute attrsAttr =
                            sceneObjectAttr.getChildByName("attrs");

                    setSceneObjectAttributes(sceneObject, attrsAttr,
                                             shutterOpen, shutterClose);

                    const kodachi::GroupAttribute kodachiGeometryAttr =
                            sceneObjectAttr.getChildByName("kodachiGeometry");

                    if (kodachiGeometryAttr.isValid()) {
                        KodachiGeometry* kodachiGeometry =
                                static_cast<KodachiGeometry*>(sceneObject.get());

                        // We don't need to hold onto the kodachi geometry attr
                        // if we aren't going to need it during render prep
                        if (!skipRender()) {
                            if (isLiveRender()) {
                                if (kodachiGeometry->mKodachiAttr.isValid()) {
                                    const auto currentHash = kodachiGeometry->mKodachiAttr.getHash();
                                    if (currentHash != kodachiGeometryAttr.getHash()) {
                                        KdLogDebug("Updating KodachiGeometry Attribute");
                                        // Since this isn't an rdl2::Attribute we need
                                        // to inform Moonray that the object need to
                                        // be reprocessed
                                        kodachiGeometry->requestUpdate();
                                        kodachiGeometry->mDeformed = true;
                                    }
                                }

                                kodachiGeometry->mReleaseAttr = false;
                            }

                            kodachiGeometry->mKodachiAttr = kodachiGeometryAttr;
                        }

                        if (kodachiGeometryUseRuntime()) {
                            arras::rdl2::SceneObject::UpdateGuard guard(kodachiGeometry);
                            kodachiGeometry->set("kodachi_runtime", mKodachiRuntime);
                            kodachiGeometry->set("scenegraph_location", locationPath);
                        }
                    }
                } else {
                    KdLogDebug("Already set attributes for this SceneObject");
                }

                // remember TraceSet entries for later
                const kodachi::StringAttribute baked(sceneObjectAttr.getChildByName("baked"));
                if (baked.isValid())
                    addTraceSetEntries(sceneObject, locationPath, baked);

            } else {
                KdLogWarn("rdl2.sceneObject attribute requires 'sceneClass' and 'name' children");
            }
        }
    }

    if (layerAssignAttr.isValid()) {
        if (getIdPassManager().isEnabled()) {
            // we can't register the actual Geometry* at this time because
            // there is no guarantee that it has been created yet, we
            // may be processing one of the parts before processing the
            // parent geometry.
            const kodachi::IntAttribute idAttr =
                    locationAttributes.getChildByName(getIdPassManager().getIdAttrName());

            if (idAttr.isValid()) {
                const kodachi::StringAttribute geometryAttr =
                        layerAssignAttr.getChildByName("geometry");
                const kodachi::StringAttribute partAttr =
                        layerAssignAttr.getChildByName("part");

                kodachi::GroupAttribute idRegistrationAttr;
                if (partAttr.isValid()) {
                    idRegistrationAttr =
                            kodachi::GroupAttribute("id", idAttr,
                                                    "geometry", geometryAttr,
                                                    "part", partAttr,
                                                    "location", locationPathAttr,
                                                    false);
                } else {
                    idRegistrationAttr =
                            kodachi::GroupAttribute("id", idAttr,
                                                    "geometry", geometryAttr,
                                                    "location", locationPathAttr,
                                                    false);
                }
                addDeferredIdRegistration(std::move(idRegistrationAttr));
            }
        }

        // Cryptomatte object IDs
        const kodachi::FloatAttribute objectIdAttr =
                locationAttributes.getChildByName("kodachi.cryptomatte.cryptomatte_object_id");
        if (objectIdAttr.isValid()) {
            float hashFloatId = objectIdAttr.getValue();
            mCryptomatteObjectIds.push_back(std::make_pair(locationPath, hashFloatId));
        }

        addDeferredLayerAssignment(std::move(layerAssignAttr));
    }

    if (geometrySetAssignAttr.isValid()) {
        addDeferredGeoSetAssignment(std::move(geometrySetAssignAttr));
    }
}

void
MoonrayRenderState::deleteLocation(const kodachi::StringAttribute& locationPathAttr)
{
    const kodachi::string_view location = locationPathAttr.getValueCStr();

    SceneObjectHashMap::const_accessor a;
    if (mActiveSceneObjects.find(a, location)) {
        rdl2::SceneObject* sceneObject = a->second.second.get();
        KdLogDebug("deleteLocation: " << location);
        hideSceneObject(sceneObject);
        mActiveSceneObjects.erase(a);
        // TODO: Delete connections to this SceneObject?
    }

    if (mActiveInstanceSourceSceneObjects.find(a, location)) {
        rdl2::SceneObject* instanceSceneObject = a->second.second.get();
        KdLogDebug("deleteLocation instance source: " << location);
        hideSceneObject(instanceSceneObject);
        mActiveInstanceSourceSceneObjects.erase(a);
        // TODO: Delete connections to this SceneObject?
    }
}

void
MoonrayRenderState::processingComplete()
{
    mProcessedSceneObjects.clear();

    processPotentialInstanceSources();

    processTraceSetEntries();

    processDeferredRenderOutputCreations();

    processDeferredConnectionTargetReplacements();
    processDeferredConnections();

    processDeferredLayerAssignments();
    processDeferredGeoSetAssignments();

    processDeferredRdlArchiveUpdates();
    processDeferredIdRegistrations();

    auto& sceneVariables = mSceneContext->getSceneVariables();
    if (mNumMachines > 1) {
        arras::rdl2::SceneVariables::UpdateGuard guard(&sceneVariables);
        sceneVariables.set(arras::rdl2::SceneVariables::sMachineId, mMachineId);
        sceneVariables.set(arras::rdl2::SceneVariables::sNumMachines, mNumMachines);
    }

    mApertureWindow = sceneVariables.getRezedApertureWindow();
    mRegionWindow = sceneVariables.getRezedRegionWindow();
    mSubViewport = sceneVariables.getRezedSubViewport();

    // subViewport is relative to region window. If it starts at 0,0 and has the
    // same dimensions, then it covers the entire region window.
    mIsROIEnabled = (mSubViewport.mMinX != 0 ||
                     mSubViewport.mMinY != 0 ||
                     mSubViewport.width() != mRegionWindow.width() ||
                     mSubViewport.height() != mRegionWindow.height());
}

void
MoonrayRenderState::loadRdlSceneFile(const std::string& sceneFile)
{
    try {
        rdl2::readSceneFromFile(sceneFile, *mSceneContext);
    } catch (const std::exception& e) {
        KdLogError("Error loading rdl scene file '"
                   << sceneFile << "'(" << e.what() << ")");
    }
}

void
mkdirForFilepath(const std::string& filePath)
{
    const std::size_t lastSlash = filePath.find_last_of("/");

    if (lastSlash != std::string::npos) {
        const std::string directoryPath = filePath.substr(0, lastSlash);
        kodachi::cache_utils::recursiveMkdir(directoryPath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    }
}

void
MoonrayRenderState::writeSceneToFile(const std::string& filePath)
{
    KdLogInfo("Begin scene file output: '" << filePath << "'");

    // Make sure the directory exists
    mkdirForFilepath(filePath);

    try {
        rdl2::writeSceneToFile(*mSceneContext, filePath);
    } catch (const std::exception& e) {
        KdLogError("Error writing scene file: " << e.what());
        return;
    }
    
    // Commit the changes from the initial scene build to clean
    // the SceneContext for delta writing.
    mSceneContext->commitAllChanges();
    
    KdLogDebug("Scene file output complete.");
}

void
MoonrayRenderState::writeCryptomatteManifest(const std::string& filePath)
{
    // TODO change to debug
    KdLogDebug("Begin cryptomatte file output: '" << filePath << "'");

    // Make sure the directory exists
    mkdirForFilepath(filePath);

    std::ofstream outputFile(filePath);
    for (const auto& it : mCryptomatteObjectIds) {
        HashUnion hash;
        hash.f32 = it.second;
        const uint32_t hashId = hash.ui32;

        KdLogDebug(it.first << ": " << hashId << " 0x" << std::hex << hashId);
        outputFile << it.first << " 0x" << std::hex << hashId << "\n";
    }
    outputFile.close();

    KdLogDebug("Cryptomatte file output complete");
}

MoonrayRenderState::SceneObjectPtr
MoonrayRenderState::getOrCreateSceneObject(const kodachi::string_view& locationPath,
                                           const kodachi::StringAttribute& classNameAttr,
                                           const std::string& objectName,
                                           bool disableAliasing)
{
    // Check if there is already an active object of this type
    rdl2::SceneObject* oldSceneObject = nullptr;

    {
        const kodachi::string_view activeName =
                disableAliasing ? objectName : locationPath;

        SceneObjectHashMap::const_accessor constAccessor;
        if (mActiveSceneObjects.find(constAccessor, activeName)) {
            if (constAccessor->second.first == classNameAttr) {
                // SceneObject of the specified SceneClass is already active
                return constAccessor->second.second;
            } else {
                // The SceneClass type of this location has changed so
                // "delete" the current scene object and "create" the new one
                oldSceneObject = constAccessor->second.second.get();
                mActiveSceneObjects.erase(constAccessor);
            }
        }
    }

    rdl2::SceneObject* sceneObject = nullptr;

    // See if we have already cached the SceneClass
    const auto sceneClassIter = mSceneClassMap.find(classNameAttr);

    if (sceneClassIter == mSceneClassMap.end()) {
        // We haven't so try to create a sceneObject of that type
        const std::string className = classNameAttr.getValue();
        try {
            sceneObject = mSceneContext->createSceneObject(className, objectName);

            // cache the SceneClass
            mSceneClassMap.insert(
                    std::make_pair(classNameAttr, &sceneObject->getSceneClass()));
        } catch (const std::exception&) {
            KdLogError("Could not create SceneObject of type '" << className << "' from SceneContext");
            return nullptr;
        }
    } else {
        // TODO: Ideally we would be able to pass the SceneClass itself,
        // But for now at least this prevents us from having to allocate a string
        // each time we want to construct a SceneObject
        sceneObject = mSceneContext->createSceneObject(
                sceneClassIter->second->getName(), objectName);
    }

    kodachi::string_view activeName = sceneObject->getName();
    if (!disableAliasing) {
        // remove the '_ClassName' suffix
        activeName.remove_suffix(sceneObject->getSceneClass().getName().size() + 1);
        if (activeName != locationPath) {
            KdLogError("activeName != locationPath");
        }
    }

    // In the event that a different thread created this SceneObject concurrently
    // use the one already in the map
    SceneObjectHashMap::accessor a;
    if (mActiveSceneObjects.insert(a, activeName)) {
        a->second = std::make_pair(classNameAttr, SceneObjectPtr(sceneObject, NullDeleter));

        if (oldSceneObject) {
            hideSceneObject(oldSceneObject);
            addDeferredConnectionTargetReplacement(oldSceneObject, a->second.second);
        }
    }

    return a->second.second;
}

rdl2::SceneObject*
MoonrayRenderState::getSceneObject(const kodachi::string_view& objectName,
                                   const char* type)
{
    SceneObjectHashMap::const_accessor a;
    if (mActiveSceneObjects.find(a, objectName)) {
        return a->second.second.get();
    } else {
        if (type)
            KdLogWarn(objectName << ": could not find " << type);
        return nullptr;
    }
}

void
MoonrayRenderState::hideSceneObject(arras::rdl2::SceneObject* sceneObject)
{
    // So far we only need to worry about visibility of Node-type SceneObjects
    if (!sceneObject->isA<rdl2::Node>()) {
        return;
    }

    const auto& sceneClass = sceneObject->getSceneClass();
    const auto& sceneClassData = getSceneClassData(sceneClass);
    const auto& attributeIdMap = std::get<1>(sceneClassData);
    const auto& falseHash = getFalseHash();

    SetValueHashVec hashVec;
    {
        SetValueHashMap::accessor accessor;
        if (mSetValueHashMap.insert(accessor, sceneObject)) {
            hashVec = std::move(accessor->second);
        }
    }

    // Keep track that we're setting the visibility for these object to false.
    // If the object appears again, then we will automatically reset any flags
    // to their default value.
    const auto setAttrFalseFunc = [&](const rdl2::AttributeKey<bool>& attrKey)
            {
                const auto attribute = sceneClass.getAttribute(attrKey);
                const std::size_t attributeId = attributeIdMap.at(attribute);
                hashVec.emplace_back(attributeId, falseHash);
                sceneObject->set(attrKey, false);
            };

    rdl2::SceneObject::UpdateGuard g(sceneObject);

    if (sceneObject->isA<rdl2::Geometry>()) {
        setAttrFalseFunc(rdl2::Geometry::sVisibleCamera);
        setAttrFalseFunc(rdl2::Geometry::sVisibleShadow);
        setAttrFalseFunc(rdl2::Geometry::sVisibleDiffuseReflection);
        setAttrFalseFunc(rdl2::Geometry::sVisibleDiffuseTransmission);
        setAttrFalseFunc(rdl2::Geometry::sVisibleGlossyReflection);
        setAttrFalseFunc(rdl2::Geometry::sVisibleGlossyTransmission);
        setAttrFalseFunc(rdl2::Geometry::sVisibleMirrorReflection);
        setAttrFalseFunc(rdl2::Geometry::sVisibleMirrorTransmission);
        setAttrFalseFunc(rdl2::Geometry::sVisiblePhase);
    } else if (sceneObject->isA<rdl2::Light>()) {
        setAttrFalseFunc(rdl2::Light::sOnKey);
    }

    SetValueHashMap::accessor accessor;
    mSetValueHashMap.insert(accessor, sceneObject);
    accessor->second = std::move(hashVec);
}

void
MoonrayRenderState::resetAttributeToDefault(const SceneObjectPtr& obj,
                                            const rdl2::Attribute& attr)
{
    switch(attr.getType()) {
    case rdl2::AttributeType::TYPE_BOOL:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Bool>(attr));
        break;
    case rdl2::AttributeType::TYPE_INT:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Int>(attr));
        break;
    case rdl2::AttributeType::TYPE_LONG:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Long>(attr));
        break;
    case rdl2::AttributeType::TYPE_FLOAT:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Float>(attr));
        break;
    case rdl2::AttributeType::TYPE_DOUBLE:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Double>(attr));
        break;
    case rdl2::AttributeType::TYPE_STRING:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::String>(attr));
        break;
    case rdl2::AttributeType::TYPE_RGB:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Rgb>(attr));
        break;
    case rdl2::AttributeType::TYPE_RGBA:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Rgba>(attr));
        break;
    case rdl2::AttributeType::TYPE_VEC2F:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Vec2f>(attr));
        break;
    case rdl2::AttributeType::TYPE_VEC2D:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Vec2d>(attr));
        break;
    case rdl2::AttributeType::TYPE_VEC3F:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Vec3f>(attr));
        break;
    case rdl2::AttributeType::TYPE_VEC4F:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Vec4f>(attr));
        break;
    case rdl2::AttributeType::TYPE_VEC4D:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Vec4d>(attr));
        break;
    case rdl2::AttributeType::TYPE_VEC3D:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Vec3d>(attr));
        break;
    case rdl2::AttributeType::TYPE_MAT4F:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Mat4f>(attr));
        break;
    case rdl2::AttributeType::TYPE_MAT4D:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Mat4d>(attr));
        break;
    case rdl2::AttributeType::TYPE_SCENE_OBJECT:
        {
            const rdl2::AttributeKey<rdl2::SceneObject*> key(attr);
            const rdl2::SceneObject* target = obj->get(key);
            removeConnection(obj, &attr, target);

            obj->resetToDefault(key);
        }
        break;
    case rdl2::AttributeType::TYPE_BOOL_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::BoolVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_INT_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::IntVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_LONG_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::LongVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_FLOAT_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::FloatVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_DOUBLE_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::DoubleVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_STRING_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::StringVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_RGB_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::RgbVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_RGBA_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::RgbaVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_VEC2F_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Vec2fVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_VEC2D_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Vec2dVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_VEC3F_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Vec3fVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_VEC4F_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Vec4fVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_VEC4D_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Vec4dVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_VEC3D_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Vec3dVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_MAT4F_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Mat4fVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_MAT4D_VECTOR:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::Mat4dVector>(attr));
        break;
    case rdl2::AttributeType::TYPE_SCENE_OBJECT_VECTOR:
        {
            const rdl2::AttributeKey<rdl2::SceneObjectVector> key(attr);
            const rdl2::SceneObjectVector& targets = obj->get(key);

            for (const rdl2::SceneObject* target : targets) {
                removeConnection(obj, &attr, target);
            }

            obj->resetToDefault(key);
        }
        break;
    case rdl2::AttributeType::TYPE_SCENE_OBJECT_INDEXABLE:
        obj->resetToDefault(rdl2::AttributeKey<rdl2::SceneObjectIndexable>(attr));
        break;
    default:
        KdLogWarn("ResetAttributeToDefault - Unhandled case: "
                  << rdl2::attributeTypeName(attr.getType()));
    }

    if (attr.isBindable()) {
        resetBinding(obj, attr);
    }
}

void
MoonrayRenderState::addDeferredConnection(const SceneObjectPtr& sourceObject,
                                          const rdl2::Attribute& attr,
                                          kodachi::StringAttribute targetLocation)
{
    if (!targetLocation.isValid()) {
        KdLogWarn("addDeferredAssignment - targetLocation is invalid");
        return;
    }

    // There are cases where the targetLocation attr being valid doesn't mean it
    // was set with a valid SceneGraphLocation. For example, all RenderOutput
    // attributes are localized, so the 'exr header attributes' attribute can be
    // valid and not set by the user at the same time.
    if (targetLocation == "") {
        return;
    }

    mDeferredConnections.emplace_back(sourceObject, &attr, std::move(targetLocation));
}

void
MoonrayRenderState::processPotentialInstanceSources()
{
    static const kodachi::StringAttribute kGroupGeometry("GroupGeometry");

    for (const auto iter : mPotentialInstanceSourceData) {
        // Find the Location registered for the ID. If it has references,
        // Create a GroupGeometry
        InstanceIdMap::const_accessor constAccessor;
        if (mInstanceIdMap.find(constAccessor, iter.first) && constAccessor->second.second) {
            const auto instanceIDAttr = constAccessor->first;
            const kodachi::string_view instanceSourceLocation = constAccessor->second.first.getValueCStr();

            kodachi::StringAttribute instanceSourceSceneClass;
            SceneObjectPtr instanceSourceObject;
            {
                SceneObjectHashMap::const_accessor sceneObjectAccessor;
                mActiveSceneObjects.find(sceneObjectAccessor, instanceSourceLocation);
                instanceSourceSceneClass = sceneObjectAccessor->second.first;
                instanceSourceObject = sceneObjectAccessor->second.second;

                // ActiveSceneObject keys are backed by their values, so erase the entry
                mActiveSceneObjects.erase(sceneObjectAccessor);

                // This location was turned into an instance source.  Remember
                // the location and the original sceneobject for any "part" layer
                // assignments later
                SceneObjectHashMap::accessor a;
                if (mActiveInstanceSourceSceneObjects.insert(a, instanceSourceLocation)) {
                    a->second = std::make_pair(instanceSourceSceneClass, instanceSourceObject);
                }
            }

            // reset the xform on the instance source object, since it
            // will be set on the GroupGeometry instead. Set any additional overrides
            {
                rdl2::SceneObject::UpdateGuard g(instanceSourceObject.get());
                instanceSourceObject->resetToDefault(rdl2::Node::sNodeXformKey);
            }

            const kodachi::GroupAttribute instanceAttrsAttr = iter.second.getChildByName("instanceAttrs");
            const kodachi::GroupAttribute instanceSourceAttrsAttr = iter.second.getChildByName("instanceSourceAttrs");
            const kodachi::GroupAttribute layerAssignAttr = iter.second.getChildByName("layerAssign");
            const kodachi::GroupAttribute geometrySetAssignAttr = iter.second.getChildByName("geometrySetAssign");
            const kodachi::FloatAttribute shutterOpenAttr = iter.second.getChildByName("shutterOpen");
            const kodachi::FloatAttribute shutterCloseAttr = iter.second.getChildByName("shutterClose");
            const float shutterOpen = shutterOpenAttr.getValue(0.f, false);
            const float shutterClose = shutterCloseAttr.getValue(0.f, false);

            // Set any overrides on the instance source (like clamping mesh_resolution)
            setSceneObjectAttributes(instanceSourceObject, instanceSourceAttrsAttr, shutterOpen, shutterClose);

            const SceneObjectPtr groupGeometry =
                    getOrCreateSceneObject(instanceSourceLocation, kGroupGeometry,
                                      kodachi::concat(instanceSourceLocation, "_GroupGeometry"), false);

            setSceneObjectAttributes(groupGeometry, instanceAttrsAttr,
                                     shutterOpen, shutterClose);

            // Make the GroupGeometry reference the source geometry
            {
                rdl2::SceneObject::UpdateGuard g(groupGeometry.get());
                groupGeometry->set(rdl2::Geometry::sReferenceGeometries,
                        { instanceSourceObject.get() });
            }

            if (layerAssignAttr.isValid()) {
                kodachi::GroupBuilder layerAssignGb;
                layerAssignGb
                    .update(layerAssignAttr)
                    .set("geometry", instanceIDAttr);
                addDeferredLayerAssignment(layerAssignGb.build());
            }
            if (geometrySetAssignAttr.isValid()) {
                kodachi::GroupBuilder geoSetAssignGb;
                geoSetAssignGb
                    .update(geometrySetAssignAttr)
                    .set("geometry", instanceIDAttr);
                addDeferredGeoSetAssignment(geoSetAssignGb.build());
            }

            SceneObjectHashMap::accessor accessor;
            // Use the instanceIDAttr as the backing for the key with the
            // assumption that no entries will be erased from the InstanceIdMap
            mActiveSceneObjects.insert(accessor, instanceIDAttr.getValueCStr());
            accessor->second = std::make_pair(instanceSourceSceneClass, instanceSourceObject);
        }
    }

    mPotentialInstanceSourceData.clear();
}

void
MoonrayRenderState::processDeferredConnections()
{
    for (auto& binding : mDeferredConnections) {
        const SceneObjectPtr& sourceObject = std::get<0>(binding);
        const rdl2::Attribute* attr = std::get<1>(binding);
        auto& targetAttr = std::get<2>(binding);

        rdl2::SceneObject::UpdateGuard g(sourceObject.get());

        if (attr->isBindable()) {
            rdl2::SceneObject* targetObject =
                getSceneObject(targetAttr.getValueCStr(), "SceneObject");
            if (!targetObject)
                continue;
            if (isLiveRender()) {
                rdl2::SceneObject* currentObject = getBinding(sourceObject, *attr);
                if (currentObject != targetObject) {
                    setBinding(sourceObject, *attr, targetObject);
                    updateConnection(sourceObject, attr, currentObject, targetObject);
                }

            } else {
                setBinding(sourceObject, *attr, targetObject);
            }
            continue;
        }

        switch (attr->getType()) {

        case rdl2::AttributeType::TYPE_SCENE_OBJECT: {
            const char* targetLocation = targetAttr.getValueCStr();
            rdl2::SceneObject* targetObject =
                getSceneObject(targetLocation, "SceneObject");
            if (!targetObject)
                continue;

            const rdl2::AttributeKey<rdl2::SceneObject*> key(*attr);

            if (isLiveRender()) {
                const rdl2::SceneObject* currentObject = sourceObject->get(key);
                if (currentObject != targetObject) {
                    sourceObject->set(key, targetObject);
                    updateConnection(sourceObject, attr, currentObject, targetObject);
                    KdLogDebug("Set SceneObject* '" << targetLocation << "' to "
                               << sourceObject->getName() << "/" << attr->getName());
                }
            } else {
                sourceObject->set(key, targetObject);
                KdLogDebug("Set SceneObject* '" << targetLocation << "' to "
                           << sourceObject->getName() << "/" << attr->getName());
            }

        } break;

        case rdl2::AttributeType::TYPE_SCENE_OBJECT_VECTOR: {

            if (sourceObject->isA<rdl2::LightSet>()) {
                // Lightsets store the light list is a particular order, you must
                // set it using the API, and not setting the attribute directly
                rdl2::LightSet* lightSet = sourceObject->asA<rdl2::LightSet>();
                if (isLiveRender()) {
                    // put the current lights into an actual set
                    std::set<rdl2::SceneObject*> ls(lightSet->getLights().begin(),
                                                    lightSet->getLights().end());

                    for (const kodachi::string_view targetLocation :
                                             targetAttr.getNearestSample(0.f)) {
                        rdl2::SceneObject* targetObject =
                            getSceneObject(targetLocation, "light");
                        if (!targetObject) continue;
                        if (targetObject->isA<rdl2::Light>()) {
                            rdl2::Light* light = targetObject->asA<rdl2::Light>();
                            if (!lightSet->contains(light)) {
                                KdLogDebug("Adding light '" << light->getName() << "' to LightSet '" << lightSet->getName() << "'");
                                lightSet->add(light);
                                registerConnection(sourceObject, attr, light);
                            } else {
                                // this light stays the same
                                ls.erase(light);
                            }
                        } else {
                            KdLogWarn(targetLocation << ": Not a light");
                        }
                    }

                    // remove any lights from the set that weren't in the targetAttr
                    for (auto sceneObject : ls) {
                        KdLogDebug("Removing light '" << sceneObject->getName() << "' from LightSet '" << lightSet->getName() << "'");
                        lightSet->remove(sceneObject->asA<rdl2::Light>());
                        removeConnection(sourceObject, attr, sceneObject);
                    }
                } else {
                    for (const kodachi::string_view targetLocation :
                                             targetAttr.getNearestSample(0.f)) {
                        rdl2::SceneObject* targetObject =
                            getSceneObject(targetLocation, "light");
                        if (!targetObject) continue;
                        if (targetObject->isA<rdl2::Light>()) {
                            lightSet->add(targetObject->asA<rdl2::Light>());
                        } else {
                            KdLogWarn(targetLocation << ": Not a light");
                        }
                    }
                }

            } else if (sourceObject->isA<rdl2::LightFilterSet>()) {
                // Lightsets store the light list is a particular order, you must
                // set it using the API, and not setting the attribute directly
                rdl2::LightFilterSet* lightFilterSet = sourceObject->asA<rdl2::LightFilterSet>();
                if (isLiveRender()) {
                    // put the current lights into an actual set
                    std::set<rdl2::SceneObject*> lfs(lightFilterSet->getLightFilters().begin(),
                                                     lightFilterSet->getLightFilters().end());

                    for (const kodachi::string_view targetLocation :
                                             targetAttr.getNearestSample(0.f)) {
                        rdl2::SceneObject* targetObject =
                            getSceneObject(targetLocation, "light filter");
                        if (!targetObject) continue;
                        if (targetObject->isA<rdl2::LightFilter>()) {
                            rdl2::LightFilter* lightFilter = targetObject->asA<rdl2::LightFilter>();
                            if (!lightFilterSet->contains(lightFilter)) {
                                KdLogDebug("Adding light filter '" << lightFilter->getName()
                                           << "' to LightFilterSet '" << lightFilterSet->getName() << "'");
                                lightFilterSet->add(lightFilter);
                                registerConnection(sourceObject, attr, lightFilter);
                            } else {
                                // this light stays the same
                                lfs.erase(lightFilter);
                            }
                        } else {
                            KdLogWarn(targetLocation << ": Not a light filter");
                        }
                    }

                    // remove any lights from the set that weren't in the targetAttr
                    for (auto sceneObject : lfs) {
                        KdLogDebug("Removing light filter '" << sceneObject->getName() << "' from LightFilterSet '" << lightFilterSet->getName() << "'");
                        lightFilterSet->remove(sceneObject->asA<rdl2::LightFilter>());
                        removeConnection(sourceObject, attr, sceneObject);
                    }
                } else {
                    for (const kodachi::string_view targetLocation :
                                             targetAttr.getNearestSample(0.f)) {
                        rdl2::SceneObject* targetObject =
                            getSceneObject(targetLocation, "light filter");
                        if (!targetObject) continue;
                        if (targetObject->isA<rdl2::LightFilter>()) {
                            lightFilterSet->add(targetObject->asA<rdl2::LightFilter>());
                        } else {
                            KdLogWarn(targetLocation << ": Not a light filter");
                        }
                    }
                }

            } else {
                const rdl2::AttributeKey<rdl2::SceneObjectVector> key(*attr);
                rdl2::SceneObjectVector targetObjects;
                targetObjects.reserve(targetAttr.getNumberOfValues());

                if (isLiveRender()) {
                    std::set<rdl2::SceneObject*> objectSet;

                    for (rdl2::SceneObject* object : sourceObject->get(key))
                        objectSet.insert(object);

                    for (auto targetLocation : targetAttr.getNearestSample(0.f)) {
                        rdl2::SceneObject* targetObject =
                            getSceneObject(targetLocation, "SceneObject");
                        if (!targetObject) continue;
                        targetObjects.push_back(targetObject);
                        registerConnection(sourceObject, attr, targetObject);
                        objectSet.erase(targetObject);
                    }

                    for (rdl2::SceneObject* object : objectSet)
                        removeConnection(sourceObject, attr, object);

                } else {
                    for (auto targetLocation : targetAttr.getNearestSample(0.f)) {
                        rdl2::SceneObject* targetObject =
                            getSceneObject(targetLocation, "SceneObject");
                        if (targetObject)
                            targetObjects.push_back(targetObject);
                    }
                }

                sourceObject->set(key, targetObjects);
            }
        } break;

        default:
            KdLogError(sourceObject->getName() << '.' << attr->getName() << ": Unhandled attrType");
        }
    }

    mDeferredConnections.clear();
}

void
MoonrayRenderState::addDeferredLayerAssignment(kodachi::GroupAttribute assignmentAttr)
{
    const kodachi::StringAttribute layerAttr = assignmentAttr.getChildByName("layer");
    const kodachi::StringAttribute geometryAttr = assignmentAttr.getChildByName("geometry");
    const kodachi::StringAttribute lightSetAttr = assignmentAttr.getChildByName("lightSet");

    // We always expect at least a layer, geometry and lightset
    if (!layerAttr.isValid()) {
        KdLogWarn("Layer assignment is missing 'layer' attr");
        return;
    }

    if (!geometryAttr.isValid()) {
        KdLogWarn("Layer assignment is missing 'geometry' attr");
        return;
    }

    if (!lightSetAttr.isValid()) {
        KdLogWarn("Layer assignment is missing 'lightSet' attr");
        return;
    }

    mDeferredLayerAssignments.push_back(std::move(assignmentAttr));
}

void
MoonrayRenderState::processDeferredLayerAssignments()
{
    for (const auto& assignmentAttr : mDeferredLayerAssignments) {

        const kodachi::StringAttribute layerAttr = assignmentAttr.getChildByName("layer");
        const kodachi::StringAttribute geometryAttr = assignmentAttr.getChildByName("geometry");
        const kodachi::StringAttribute partAttr = assignmentAttr.getChildByName("part");
        const kodachi::StringAttribute materialAttr = assignmentAttr.getChildByName("material");
        const kodachi::StringAttribute lightSetAttr = assignmentAttr.getChildByName("lightSet");
        const kodachi::StringAttribute displacementAttr = assignmentAttr.getChildByName("displacement");
        const kodachi::StringAttribute volumeShaderAttr = assignmentAttr.getChildByName("volumeShader");
        const kodachi::StringAttribute lightFilterSetAttr = assignmentAttr.getChildByName("lightFilterSet");
        const kodachi::StringAttribute shadowSetAttr = assignmentAttr.getChildByName("shadowSet");

        rdl2::Layer* layer = nullptr;
        {
            const char* targetLocation = layerAttr.getValueCStr();
            rdl2::SceneObject* layerSceneObject =
                getSceneObject(targetLocation, "Layer");
            if (!layerSceneObject) continue;
            layer = layerSceneObject->asA<rdl2::Layer>();
            if (!layer) {
                KdLogWarn(targetLocation << ": Not a Layer");
                continue;
            }
        }

        std::string partName = partAttr.getValue(std::string{}, false);
        rdl2::Geometry* geometry = nullptr;
        {
            const char* targetLocation = geometryAttr.getValueCStr();
            rdl2::SceneObject* geometrySceneObject =
                getSceneObject(targetLocation, "Geometry");
            if (!geometrySceneObject) continue;

            geometry = geometrySceneObject->asA<rdl2::Geometry>();
            if (!geometry) {
                KdLogWarn(targetLocation << ": Not a geometry");
                continue;
            }

            // Special casing for per-part material assignments and instances.
            // The instance source geometry itself will be added to the layer
            // pretty easily because it's actually being retrieved by the
            // instanceId and not the location.  However, the parts of that
            // instance source dont have an instanceID, so when we try to get
            // the source geometry for this part location, we're going to end
            // up getting the Group/InstanceGeometry instead.  BUT, we can
            // get to the instance source geometry from mActiveInstanceSourceSceneObjects.
            // Also, this avoids doing per-part material assignments on instances,
            // which isn't a thing.
            const std::string& geometrySceneClassName = geometry->getSceneClass().getName();
            if (!partName.empty() &&
                (geometrySceneClassName == "GroupGeometry" ||
                 geometrySceneClassName == "InstanceGeometry")) {
                SceneObjectHashMap::const_accessor a;
                if (mActiveInstanceSourceSceneObjects.find(a, targetLocation)) {
                    geometrySceneObject = a->second.second.get();
                } else {
                    KdLogDebug("Skipping part layer assignment for instance: " << targetLocation);
                    continue;
                }
                geometry = geometrySceneObject->asA<rdl2::Geometry>();
            }
        }

        rdl2::LayerAssignment layerAssignment;

        if (materialAttr.isValid()) {
            rdl2::SceneObject* materialSceneObject =
                getSceneObject(materialAttr.getValueCStr(), "Material");
            if (materialSceneObject)
                layerAssignment.mMaterial = materialSceneObject->asA<rdl2::Material>();
        }

        if (lightSetAttr.isValid()) {
            rdl2::SceneObject* lightSetSceneObject =
                getSceneObject(lightSetAttr.getValueCStr(), "LightSet");
            if (lightSetSceneObject)
                layerAssignment.mLightSet = lightSetSceneObject->asA<rdl2::LightSet>();
        }

        if (displacementAttr.isValid()) {
            rdl2::SceneObject* displacementSceneObject =
                getSceneObject(displacementAttr.getValueCStr(), "Displacement");
            if (displacementSceneObject)
                layerAssignment.mDisplacement = displacementSceneObject->asA<rdl2::Displacement>();
        }

        if (volumeShaderAttr.isValid()) {
            rdl2::SceneObject* volumeShaderSceneObject =
                getSceneObject(volumeShaderAttr.getValueCStr(), "VolumeShader");
            if (volumeShaderSceneObject)
                layerAssignment.mVolumeShader = volumeShaderSceneObject->asA<rdl2::VolumeShader>();
        }

        if (lightFilterSetAttr.isValid()) {
            rdl2::SceneObject* lightFilterSetSceneObject =
                getSceneObject(lightFilterSetAttr.getValueCStr(), "LightFilterSet");
            if (lightFilterSetSceneObject)
                layerAssignment.mLightFilterSet = lightFilterSetSceneObject->asA<rdl2::LightFilterSet>();
        }

        if (shadowSetAttr.isValid()) {
            rdl2::SceneObject* shadowSetSceneObject =
                getSceneObject(shadowSetAttr.getValueCStr(), "ShadowSet");
            if (shadowSetSceneObject)
                layerAssignment.mShadowSet = shadowSetSceneObject->asA<rdl2::ShadowSet>();
        }

        if (geometry->getSceneClass().getName() == "GroupGeometry" ||
                geometry->getSceneClass().getName() == "InstanceGeometry") {
            // The only things that should be assigned to an instance are
            // the LightSet, LightFilterSet, and ShadowSet
            partName.clear();
            layerAssignment.mMaterial = nullptr;
            layerAssignment.mDisplacement = nullptr;
            layerAssignment.mVolumeShader = nullptr;
        } else if (!layerAssignment.mMaterial && !layerAssignment.mVolumeShader) {
            // We expect non-instance related layer assignments to have
            // either a material or volume shader. Moonray may crash if otherwise.
            continue;
        }

        // NOTE: Layer assignment is not thread safe
        rdl2::Layer::UpdateGuard updateGuard(layer);
        layer->assign(geometry, partName, layerAssignment);
    }

    mDeferredLayerAssignments.clear();
}

void
MoonrayRenderState::addDeferredGeoSetAssignment(kodachi::GroupAttribute assignmentAttr)
{
    mDeferredGeoSetAssignments.push_back(std::move(assignmentAttr));
}

void
MoonrayRenderState::processDeferredGeoSetAssignments()
{
    for (const auto& assignmentAttr : mDeferredGeoSetAssignments) {
        const kodachi::StringAttribute geosetAttr = assignmentAttr.getChildByName("geometrySet");
        const kodachi::StringAttribute geometryAttr = assignmentAttr.getChildByName("geometry");

        if (!geosetAttr.isValid()) {
            KdLogWarn("'geometrySet' attribute is not valid");
            continue;
        }

        if (!geometryAttr.isValid()) {
            KdLogWarn("'geometry' attribute is not valid");
            continue;
        }

        rdl2::SceneObject* geosetSceneObject =
            getSceneObject(geosetAttr.getValueCStr(), "GeometrySet");
        if (!geosetSceneObject)
            continue;

        rdl2::SceneObject* geometrySceneObject =
            getSceneObject(geometryAttr.getValueCStr(), "Geometry");
        if (!geometrySceneObject)
            continue;

        rdl2::GeometrySet* geometrySet = geosetSceneObject->asA<rdl2::GeometrySet>();
        rdl2::Geometry* geometry = geometrySceneObject->asA<rdl2::Geometry>();

        rdl2::GeometrySet::UpdateGuard updateGuard(geometrySet);
        geometrySet->add(geometry);
    }

    mDeferredGeoSetAssignments.clear();
}

void
MoonrayRenderState::addDeferredRdlArchiveUpdate(const std::string& rdlFileName)
{
    mDeferredRdlArchiveUpdates.push_back(rdlFileName);
}

void
MoonrayRenderState::processDeferredRdlArchiveUpdates()
{
    for (const std::string& rdlFileName : mDeferredRdlArchiveUpdates) {
        loadRdlSceneFile(rdlFileName);
    }

    mDeferredRdlArchiveUpdates.clear();
}

void
MoonrayRenderState::addTraceSetEntries(
    const SceneObjectPtr& traceSet, const std::string& location,
    const kodachi::StringAttribute& baked)
{
    mTraceSetEntries.emplace_back(traceSet.get(), location, baked);
}

void
MoonrayRenderState::processTraceSetEntries()
{
    for (auto&& i : mTraceSetEntries) {
        rdl2::SceneObject* object(std::get<0>(i));
        rdl2::TraceSet* traceSet(object->asA<rdl2::TraceSet>());
        const std::string& location(std::get<1>(i));
        const kodachi::StringAttribute& baked(std::get<2>(i));
        if (not traceSet) {
            KdLogError(location << ": " << object->getName() << " is not a TraceSet");
            continue;
        }
        KdLogDebug("Appending to TraceSet " << traceSet->getName() << " from " << location);
        rdl2::TraceSet::UpdateGuard updateGuard(traceSet);
        // the entries in baked are relative to parent of location
        const std::string parent(location.substr(0, location.rfind('/')));
        rdl2::Geometry* prevGeometry = 0;
        std::string prevGeometryName;
        for (const std::string& relPath : baked.getNearestSample(0)) {
            // see if it is a part first:
            auto&& n = relPath.rfind('/');
            if (n != std::string::npos) {
                std::string path(parent + relPath.substr(0,n));
                const char* part = relPath.c_str()+n+1;
                if (path == prevGeometryName) {
                    traceSet->assign(prevGeometry, part);
                    continue;
                }
                rdl2::SceneObject* targetObject = getSceneObject(path, nullptr);
                if (targetObject) {
                    rdl2::Geometry* geometry = targetObject->asA<rdl2::Geometry>();
                    if (geometry) {
                        traceSet->assign(geometry, part);
                        prevGeometry = geometry;
                        prevGeometryName = path;
                        continue;
                    }
                }
            }
            // see if it is a geometry:
            std::string path(parent + relPath);
            rdl2::SceneObject* targetObject = getSceneObject(path, nullptr);
            if (targetObject) {
                rdl2::Geometry* geometry = targetObject->asA<rdl2::Geometry>();
                if (geometry) {
                    traceSet->assign(geometry, "");
                    continue;
                }
            }
            // failure
            KdLogWarn(parent << ": could not find Part or Geometry " << path);
        }
    }
}

void
MoonrayRenderState::registerConnection(const SceneObjectPtr& source,
                                       const arras::rdl2::Attribute* sourceAttr,
                                       const arras::rdl2::SceneObject* target)
{
    if (isLiveRender()) {
        ReverseConnectionsHashMap::accessor accessor;
        mReverseConnections.insert(accessor, target);

        AttributeConnectionVec& connections = accessor->second;

        for (const auto& connection : connections) {
            if (connection.second == sourceAttr && connection.first.lock() == source) {
                // already exists
                return;
            }
        }

        connections.emplace_back(std::make_pair(source, sourceAttr));
    }
}

void
MoonrayRenderState::removeConnection(const SceneObjectPtr& source,
                                     const arras::rdl2::Attribute* sourceAttr,
                                     const arras::rdl2::SceneObject* target)
{
    ReverseConnectionsHashMap::accessor accessor;
    if (mReverseConnections.find(accessor, target)) {
        AttributeConnectionVec& connectionVec = accessor->second;
        auto iter = connectionVec.begin();
        for (; iter != connectionVec.end(); ++iter) {
            if (iter->second == sourceAttr && iter->first.lock() == source) {
                connectionVec.erase(iter);
                break;
            }
        }
    }
}

void
MoonrayRenderState::updateConnection(const SceneObjectPtr& source,
                                     const arras::rdl2::Attribute* sourceAttr,
                                     const arras::rdl2::SceneObject* oldTarget,
                                     const arras::rdl2::SceneObject* newTarget)
{
    if (oldTarget) {
        removeConnection(source, sourceAttr, oldTarget);
    }

    if (newTarget) {
        registerConnection(source, sourceAttr, newTarget);
    }
}

void
MoonrayRenderState::addDeferredConnectionTargetReplacement(
        arras::rdl2::SceneObject* src, SceneObjectPtr dst)
{
    if (isLiveRender() && src != dst.get()) {
        mDeferredConnectionReplacements.push_back({src, dst});
    }
}


void
MoonrayRenderState::processDeferredConnectionTargetReplacements()
{
    for (auto& replacementPair : mDeferredConnectionReplacements) {
        rdl2::SceneObject* oldTarget = replacementPair.first;
        SceneObjectPtr newTarget = replacementPair.second;

        // get the connections for the old object
        AttributeConnectionVec oldConnections;
        {
            ReverseConnectionsHashMap::accessor accessor;
            if (mReverseConnections.find(accessor, oldTarget)) {
                oldConnections = std::move(accessor->second);
            } else {
                KdLogDebug("Target has no connections: " << oldTarget->getName());
            }

            mReverseConnections.erase(accessor);
        }

        AttributeConnectionVec newConnections;

        for (const auto& attributeConnection : oldConnections) {
            const SceneObjectPtr sourceObject = attributeConnection.first.lock();
            if (sourceObject) {
                newConnections.emplace_back(attributeConnection);

                const rdl2::Attribute* attribute = attributeConnection.second;

                rdl2::SceneObject::UpdateGuard guard(sourceObject.get());

                if (attribute->isBindable()) {
                    setBinding(newTarget, *attribute, sourceObject.get());
                    continue;
                }

                const auto attrType = attribute->getType();

                if (attrType == rdl2::AttributeType::TYPE_SCENE_OBJECT) {
                    const rdl2::AttributeKey<rdl2::SceneObject*> attrKey(*attribute);
                    sourceObject->set(attrKey, newTarget.get());
                } else if (attrType == rdl2::AttributeType::TYPE_SCENE_OBJECT_VECTOR) {
                    if (sourceObject->isA<rdl2::LightSet>()) {
                        rdl2::LightSet* lightSet = sourceObject->asA<rdl2::LightSet>();

                        rdl2::Light* oldLight = oldTarget->asA<rdl2::Light>();
                        rdl2::Light* newLight = newTarget->asA<rdl2::Light>();

                        if (oldLight) {
                            lightSet->remove(oldLight);
                        }

                        if (newLight) {
                            lightSet->add(newLight);
                        }
                    } else if (sourceObject->isA<rdl2::LightFilterSet>()) {
                        rdl2::LightFilterSet* lightSet = sourceObject->asA<rdl2::LightFilterSet>();

                        rdl2::LightFilter* oldLightFilter = oldTarget->asA<rdl2::LightFilter>();
                        rdl2::LightFilter* newLightFilter = newTarget->asA<rdl2::LightFilter>();

                        if (oldLightFilter) {
                            lightSet->remove(oldLightFilter);
                        }

                        if (newLightFilter) {
                            lightSet->add(newLightFilter);
                        }
                    } else {
                        rdl2::AttributeKey<rdl2::SceneObjectVector> attrKey(*attribute);
                        rdl2::SceneObjectVector sceneObjectVec = sourceObject->get(attrKey);
                        for (std::size_t i = 0; i < sceneObjectVec.size(); ++i) {
                            if (sceneObjectVec[i] == oldTarget) {
                                sceneObjectVec[i] = newTarget.get();
                                break;
                            }
                        }

                        sourceObject->set(attrKey, std::move(sceneObjectVec));
                    }
                }
            }
        }

        if (!newConnections.empty()) {
            ReverseConnectionsHashMap::accessor accessor;
            mReverseConnections.insert(accessor, newTarget.get());
            accessor->second = std::move(newConnections);
        }
    }

    mDeferredConnectionReplacements.clear();
}

void
MoonrayRenderState::addDeferredRenderOutputCreation(const std::string& locationPath, kodachi::GroupAttribute sceneObjectAttr)
{
    mDeferredRenderOutputCreations.push_back(std::make_pair(locationPath, sceneObjectAttr));
}

void
MoonrayRenderState::processDeferredRenderOutputCreations()
{
    for (auto& locationObjectPair : mDeferredRenderOutputCreations) {
        const kodachi::GroupAttribute rdl2Attr = locationObjectPair.second;

        const kodachi::GroupAttribute sceneObjectAttr =
                rdl2Attr.getChildByName("sceneObject");

        const kodachi::FloatAttribute shutterOpenAttr =
                rdl2Attr.getChildByName("meta.shutterOpen");

        const kodachi::FloatAttribute shutterCloseAttr =
                rdl2Attr.getChildByName("meta.shutterClose");

        const float shutterOpen = shutterOpenAttr.getValue(0.f, false);
        const float shutterClose = shutterCloseAttr.getValue(0.f, false);

        const std::string& locationPath = locationObjectPair.first;

        const kodachi::StringAttribute sceneClassAttr =
            sceneObjectAttr.getChildByName("sceneClass");

        const kodachi::StringAttribute nameAttr =
            sceneObjectAttr.getChildByName("name");

        const kodachi::IntAttribute disableAliasingAttr =
            sceneObjectAttr.getChildByName("disableAliasing");

        const bool disableAliasing = disableAliasingAttr.getValue(false, false);

        if (sceneClassAttr.isValid() && nameAttr.isValid()) {
            // createSceneObject will log errors if unable to create object
            const SceneObjectPtr sceneObject =
                    getOrCreateSceneObject(locationPath, sceneClassAttr,
                                      nameAttr.getValue(), disableAliasing);

            // We only want to set the attributes for this object if we
            // are the first thread to process this location during this
            // processing iteration
            if (sceneObject && mProcessedSceneObjects.insert(sceneObject.get()).second) {
                // set all of the attributes
                const kodachi::GroupAttribute attrsAttr =
                        sceneObjectAttr.getChildByName("attrs");

                setSceneObjectAttributes(sceneObject, attrsAttr,
                                         shutterOpen, shutterClose);

            }
        }
    }

    mDeferredRenderOutputCreations.clear();
}

void
MoonrayRenderState::addDeferredIdRegistration(kodachi::GroupAttribute registrationAttr)
{
    mDeferredIdRegistrations.push_back(std::move(registrationAttr));
}

void
MoonrayRenderState::processDeferredIdRegistrations()
{
    if (!mIdPassManager.isEnabled()) {
        return;
    }

    for (const auto& idRegistrationAttr : mDeferredIdRegistrations) {
        const kodachi::IntAttribute idAttr =
                idRegistrationAttr.getChildByName("id");

        const kodachi::StringAttribute geometryAttr =
                idRegistrationAttr.getChildByName("geometry");

        const kodachi::StringAttribute partAttr =
                idRegistrationAttr.getChildByName("part");

        const kodachi::StringAttribute locationAttr =
                idRegistrationAttr.getChildByName("location");

        rdl2::SceneObject* geometry =
            getSceneObject(geometryAttr.getValueCStr(), "Geometry");
        if (!geometry)
            continue;

        std::string part;
        if (partAttr.isValid()) {
            part = partAttr.getValue();
        }

        mIdPassManager.registerGeometry(locationAttr, idAttr,
                                        geometry->asA<rdl2::Geometry>(), part);
    }

    mDeferredIdRegistrations.clear();
}

void
MoonrayRenderState::setAttrValue(const SceneObjectPtr& obj,
                                 const rdl2::Attribute* rdl2attr,
                                 kodachi::Attribute value,
                                 float shutterOpen, float shutterClose)
{

    if (sKdLoggingClient.isSeverityEnabled(kKdLoggingSeverityDebug)) {
        std::ostringstream oss;
        oss << "setAttrValue - " << rdl2attr->getName() << ": ";
        kodachi::GetAttrValueAsPrettyText(oss, value, 3);
        sKdLoggingClient.log(oss.str(), kKdLoggingSeverityDebug);
    }

    const auto valueAttrType = value.getType();

    // Check for bindings first
    if (rdl2attr->isBindable()) {
        // Attributes can have a value and a binding. In this case we want to
        // defer the binding and set the value immediately.
        if (valueAttrType == kodachi::kAttrTypeGroup) {
            const kodachi::GroupAttribute bindingAttr(std::move(value));
            kodachi::StringAttribute stringAttr(bindingAttr.getChildByName("bind"));
            if (stringAttr.isValid()) {
                addDeferredConnection(obj, *rdl2attr, std::move(stringAttr));
            } else if (isLiveRender()) {
                resetBinding(obj, *rdl2attr);
            }

            value = bindingAttr.getChildByName("value");
            if (!value.isValid()) {
                return;
            }
        } else if (valueAttrType == kodachi::kAttrTypeString) {
            // This assumes that bindable attributes won't ever expect type string
            addDeferredConnection(obj, *rdl2attr, std::move(value));
            return;
        } else if (isLiveRender()) {
            resetBinding(obj, *rdl2attr);
        }
    }

    try {
        switch(rdl2attr->getType()) {
        case rdl2::AttributeType::TYPE_BOOL:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Bool>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_INT:
            if (rdl2attr->isEnumerable() && valueAttrType ==
                          kodachi::kAttrTypeString) {
                // This is us attempting to set an enum by string. Try and convert it to the
                // corresponding int
                const kodachi::StringAttribute stringAttr(value);
                const kodachi::string_view stringValue = stringAttr.getValueCStr();
                auto finder = [&] (const rdl2::Attribute::EnumValueItem& item)
                                         { return item.second == stringValue; };
                auto it = std::find_if(rdl2attr->beginEnumValues(),
                                       rdl2attr->endEnumValues(), finder);
                if (it != rdl2attr->endEnumValues()) {
                    setAttrValue(obj, rdl2::AttributeKey<rdl2::Int>(*rdl2attr),
                                 kodachi::IntAttribute((*it).first), shutterOpen, shutterClose);
                    break;
                }
            }
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Int>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_LONG:
            // upcast object::Value ints to longs
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Long>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_FLOAT:
            // downcast object::Value doubles to floats
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Float>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_DOUBLE:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Double>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_STRING:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::String>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_RGB:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Rgb>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_RGBA:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Rgba>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_VEC2F:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Vec2f>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_VEC2D:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Vec2d>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_VEC3F:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Vec3f>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_VEC4F:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Vec4f>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_VEC3D:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Vec3d>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_MAT4F:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Mat4f>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_MAT4D:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Mat4d>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_SCENE_OBJECT_INDEXABLE:
        case rdl2::AttributeType::TYPE_SCENE_OBJECT_VECTOR:
        case rdl2::AttributeType::TYPE_SCENE_OBJECT:
            // Set this at the end when we know the object should have been created
            addDeferredConnection(obj, *rdl2attr, std::move(value));
            break;
        case rdl2::AttributeType::TYPE_BOOL_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::BoolVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_INT_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::IntVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_LONG_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::LongVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_FLOAT_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::FloatVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_DOUBLE_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::DoubleVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_STRING_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::StringVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_RGB_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::RgbVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_RGBA_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::RgbaVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_VEC2F_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Vec2fVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_VEC2D_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Vec2dVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_VEC3F_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Vec3fVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_VEC3D_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Vec3dVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_VEC4F_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Vec4fVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_VEC4D_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Vec4dVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_MAT4F_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Mat4fVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        case rdl2::AttributeType::TYPE_MAT4D_VECTOR:
            setAttrValue(obj, rdl2::AttributeKey<rdl2::Mat4dVector>(*rdl2attr),
                    std::move(value), shutterOpen, shutterClose);
            break;
        default:
            KdLogDebug("Attribute '" << rdl2attr->getName() << "' is of unhandled "
                     << "attribute type "
                     << rdl2::attributeTypeName(rdl2attr->getType()));
            break;
        }
    } catch (const std::exception& e) {
        KdLogError("Error setting attribute: '" << rdl2attr->getName()
                   << "' for object: '" << obj->getName() << "' - " << e.what());
    }
}

template <class T>
void
MoonrayRenderState::setAttrValue(const SceneObjectPtr& obj,
                                 const rdl2::AttributeKey<T>& attributeKey,
                                 kodachi::DataAttribute attr,
                                 float shutterOpen,
                                 float shutterClose)
{
    try {
        // we expect data to already be interpolated to the correct sample times
        // Some attributes are blurrable, so set their shutterOpen/Close values
        // if available
        if (attributeKey.isBlurrable() && attr.getNumberOfTimeSamples() > 1) {
            obj->set(attributeKey, util::rdl2Convert<T>(attr, shutterOpen),
                    rdl2::AttributeTimestep::TIMESTEP_BEGIN);

            obj->set(attributeKey, util::rdl2Convert<T>(attr, shutterClose),
                    rdl2::AttributeTimestep::TIMESTEP_END);
        } else {
            obj->set(attributeKey, util::rdl2Convert<T>(attr, shutterOpen));
        }
    } catch (const std::exception & e) {
        KdLogWarn("Exception setting attribute - " << e.what());
    }
}

rdl2::SceneObject*
MoonrayRenderState::getBinding(const SceneObjectPtr& sourceObject,
                               const arras::rdl2::Attribute& attr)
{
    try {
        switch(attr.getType()) {
        case rdl2::AttributeType::TYPE_BOOL:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Bool>(attr));
        case rdl2::AttributeType::TYPE_INT:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Int>( attr));
        case rdl2::AttributeType::TYPE_LONG:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Long>(attr));
        case rdl2::AttributeType::TYPE_FLOAT:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Float>(attr));
        case rdl2::AttributeType::TYPE_DOUBLE:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Double>(attr));
        case rdl2::AttributeType::TYPE_STRING:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::String>(attr));
        case rdl2::AttributeType::TYPE_RGB:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Rgb>(attr));
        case rdl2::AttributeType::TYPE_RGBA:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Rgba>(attr));
        case rdl2::AttributeType::TYPE_VEC2F:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec2f>(attr));
        case rdl2::AttributeType::TYPE_VEC2D:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec2d>(attr));
        case rdl2::AttributeType::TYPE_VEC3F:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec3f>(attr));
        case rdl2::AttributeType::TYPE_VEC3D:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec3d>(attr));
        case rdl2::AttributeType::TYPE_VEC4F:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec4f>(attr));
        case rdl2::AttributeType::TYPE_VEC4D:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec4d>(attr));
        case rdl2::AttributeType::TYPE_MAT4F:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Mat4f>(attr));
        case rdl2::AttributeType::TYPE_MAT4D:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Mat4d>(attr));
        case rdl2::AttributeType::TYPE_BOOL_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::BoolVector>(attr));
        case rdl2::AttributeType::TYPE_INT_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::IntVector>(attr));
        case rdl2::AttributeType::TYPE_LONG_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::LongVector>(attr));
        case rdl2::AttributeType::TYPE_FLOAT_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::FloatVector>(attr));
        case rdl2::AttributeType::TYPE_DOUBLE_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::DoubleVector>(attr));
        case rdl2::AttributeType::TYPE_STRING_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::StringVector>(attr));
        case rdl2::AttributeType::TYPE_RGB_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::RgbVector>(attr));
        case rdl2::AttributeType::TYPE_RGBA_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::RgbaVector>(attr));
        case rdl2::AttributeType::TYPE_VEC2F_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec2fVector>(attr));
        case rdl2::AttributeType::TYPE_VEC2D_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec2dVector>(attr));
        case rdl2::AttributeType::TYPE_VEC3F_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec3fVector>(attr));
        case rdl2::AttributeType::TYPE_VEC3D_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec3dVector>(attr));
        case rdl2::AttributeType::TYPE_VEC4F_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec4fVector>(attr));
        case rdl2::AttributeType::TYPE_VEC4D_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec4dVector>(attr));
        case rdl2::AttributeType::TYPE_MAT4F_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Mat4fVector>(attr));
        case rdl2::AttributeType::TYPE_MAT4D_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Mat4dVector>(attr));
        case rdl2::AttributeType::TYPE_SCENE_OBJECT:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::SceneObject*>(attr));
        case rdl2::AttributeType::TYPE_SCENE_OBJECT_VECTOR:

            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::SceneObjectVector>(attr));
        default:
            KdLogError("Cannot get binding for attribute '" << attr.getName()
                       << "' of Source Object '" << sourceObject->getName() << "'");
            break;
        }
    } catch (std::exception& e) {
        KdLogError(" - Error getting binding for attribute: " << e.what());
    }

    return nullptr;
}

void
MoonrayRenderState::setBinding(const SceneObjectPtr& sourceObject,
                               const arras::rdl2::Attribute& attr,
                               arras::rdl2::SceneObject* targetObject)
{
    try {
        switch(attr.getType()) {
        case rdl2::AttributeType::TYPE_BOOL:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Bool>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_INT:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Int>( attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_LONG:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Long>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_FLOAT:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Float>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_DOUBLE:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Double>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_STRING:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::String>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_RGB:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Rgb>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_RGBA:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Rgba>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_VEC2F:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Vec2f>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_VEC2D:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Vec2d>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_VEC3F:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Vec3f>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_VEC3D:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Vec3d>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_VEC4F:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Vec4f>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_VEC4D:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Vec4d>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_MAT4F:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Mat4f>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_MAT4D:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Mat4d>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_BOOL_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::BoolVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_INT_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::IntVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_LONG_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::LongVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_FLOAT_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::FloatVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_DOUBLE_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::DoubleVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_STRING_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::StringVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_RGB_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::RgbVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_RGBA_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::RgbaVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_VEC2F_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Vec2fVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_VEC2D_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Vec2dVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_VEC3F_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Vec3fVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_VEC3D_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Vec3dVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_VEC4F_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Vec4fVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_VEC4D_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Vec4dVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_MAT4F_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Mat4fVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_MAT4D_VECTOR:
            sourceObject->setBinding(rdl2::AttributeKey<rdl2::Mat4dVector>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_SCENE_OBJECT:
            sourceObject->set(rdl2::AttributeKey<rdl2::SceneObject*>(attr), targetObject);
            break;
        case rdl2::AttributeType::TYPE_SCENE_OBJECT_VECTOR:
            // Since this function only takes a single SceneObject pointer,
            // just assume they wanted a vector of length 1
            sourceObject->set(rdl2::AttributeKey<rdl2::SceneObjectVector>(attr),
                              rdl2::SceneObjectVector{targetObject});
            break;
        default:
            KdLogError("Cannot bind attribute '" << attr.getName()
                       << "' of Source Object '" << sourceObject->getName()
                       << "' to Target Object '" << targetObject->getName() << "'");
            break;
        }
    } catch (std::exception& e) {
        KdLogError(" - Error binding attribute: " << e.what());
    }
}

void
MoonrayRenderState::resetBinding(const SceneObjectPtr& obj, const
                                 arras::rdl2::Attribute& attr)
{
    const rdl2::SceneObject* target = getBinding(obj, attr);
    if (target) {
        setBinding(obj, attr, nullptr);
        removeConnection(obj, &attr, target);
    }
}

void
MoonrayRenderState::setSceneObjectAttributes(const SceneObjectPtr& obj,
                                             const kodachi::GroupAttribute attrsAttr,
                                             float shutterOpen, float shutterClose)
{
    KdLogDebug("setting attributes on SceneObject: " << obj->getName());

    const rdl2::SceneClass& sceneClass = obj->getSceneClass();

    const SceneClassData& sceneClassData = getSceneClassData(sceneClass);
    const AttributeLookupMap& attributeMap = std::get<0>(sceneClassData);

    rdl2::SceneObject::UpdateGuard updateGuard(obj.get());

    if (!isLiveRender()) {
        for (const auto attrPair : attrsAttr) {
            const auto iter = attributeMap.find(attrPair.name);
            if (iter != attributeMap.end()) {
                const rdl2::Attribute* rdl2Attr = iter->second.first;
                setAttrValue(obj, rdl2Attr, attrPair.attribute, shutterOpen, shutterClose);
            } else {
                KdLogWarn("SceneClass '" << sceneClass.getName() <<
                          "' does not have attribute '" << attrPair.name << "'");
            }
        }

        return;
    }

    // unpack the hashes for the previously set values
    const Rdl2AttrVec& attrVec = std::get<2>(sceneClassData);
    const std::size_t numAttrs = attrVec.size();

    std::vector<bool> resetToDefault(numAttrs, false);
    std::vector<kodachi::Hash> attributeHashes(numAttrs);

    {
        // if we have processed this object previously, then get the hashes
        // for the existing values
        SetValueHashMap::const_accessor constAccessor;
        if (mSetValueHashMap.find(constAccessor, obj.get())) {
            for (const auto& setValueHash : constAccessor->second) {
                const std::size_t idx = setValueHash.first;
                // Mark all previously set Attributes as needing to be reset
                // We will check if they still have a non-default value in the
                // next step
                resetToDefault[idx] = true;

                // set the previous hashes
                attributeHashes[idx] = setValueHash.second;
            }
        }
    }

    // set new values if the hashes are different from previous values
    for (const auto attrPair : attrsAttr) {
        const auto iter = attributeMap.find(attrPair.name);
        if (iter != attributeMap.end()) {
            const Attribute& attribute = iter->second;
            const std::size_t idx = attribute.second;
            const kodachi::Hash hash = attrPair.attribute.getHash();

            if (attributeHashes[idx] != hash) {
                setAttrValue(obj, attribute.first, attrPair.attribute,
                             shutterOpen, shutterClose);
                attributeHashes[idx] = hash;
            }

            resetToDefault[idx] = false;

        } else {
            KdLogWarn("SceneClass '" << sceneClass.getName() <<
                      "' does not have attribute '" << attrPair.name << "'");
        }
    }

    SetValueHashVec hashVec;

    for (std::size_t i = 0; i < numAttrs; ++i) {
        if (resetToDefault[i]) {
            const rdl2::Attribute& attr = *attrVec[i];
            KdLogDebug("resetAttributeToDefault: " << attr.getName());
            resetAttributeToDefault(obj, attr);

            // Also reset the stored hash for this attribute
            attributeHashes[i] = kNullHash;
        }

        if (attributeHashes[i] != kNullHash) {
            hashVec.emplace_back(i, attributeHashes[i]);
        }
    }

    SetValueHashMap::accessor accessor;
    mSetValueHashMap.insert(accessor, obj.get());
    accessor->second = std::move(hashVec);
}

rdl2::SceneObjectVector
MoonrayRenderState::createInstanceUserData(const std::string& locationPath,
                                           const kodachi::GroupAttribute& instanceArbAttrs)
{
    static const kodachi::StringAttribute kBoolAttr("bool");
    static const kodachi::StringAttribute kIntAttr("int");
    static const kodachi::StringAttribute kStringAttr("string");
    static const kodachi::StringAttribute kFloatAttr("float");
    static const kodachi::StringAttribute kColorAttr("color");
    static const kodachi::StringAttribute kVec2fAttr("vec2f");
    static const kodachi::StringAttribute kVec3fAttr("vec3f");
    static const kodachi::StringAttribute kMat4fAttr("mat4f");

    static const std::string kUserData("UserData");

    rdl2::SceneObjectVector userDataVector;

    for (const auto arbAttrPair : instanceArbAttrs) {
        const kodachi::GroupAttribute arbAttr(arbAttrPair.attribute);

        const kodachi::StringAttribute typeAttr = arbAttr.getChildByName("type");
        const kodachi::Attribute valueAttr = arbAttr.getChildByName("value");

        if (typeAttr.isValid() && valueAttr.isValid()) {
            const std::string keyName(arbAttrPair.name);

            const std::string userDataName =
                    kodachi::concat(locationPath, "/", keyName, "_UserData");
            // TODO: Call getOrCreateSceneObject for live-render book-keeping
            rdl2::UserData* userData = mSceneContext->createSceneObject(
                    kUserData, userDataName)->asA<rdl2::UserData>();

            rdl2::UserData::UpdateGuard guard(userData);

            if (typeAttr == kBoolAttr) {
                const kodachi::IntAttribute boolValueAttr(valueAttr);
                if (boolValueAttr.isValid()) {
                    rdl2::BoolVector boolVector;
                    boolVector.push_back(boolValueAttr.getValue());
                    userData->setBoolData(keyName, boolVector);
                }
            } else if (typeAttr == kIntAttr) {
                const kodachi::IntAttribute intValueAttr(valueAttr);
                if (intValueAttr.isValid()) {
                    userData->setIntData(keyName, {intValueAttr.getValue()});
                }
            } else if (typeAttr == kStringAttr) {
                const kodachi::StringAttribute stringValueAttr(valueAttr);
                if (stringValueAttr.isValid()) {
                    userData->setStringData(keyName, {stringValueAttr.getValue()});
                }
            } else if (typeAttr == kFloatAttr) {
                const kodachi::FloatAttribute floatAttr(valueAttr);
                if (floatAttr.isValid()) {
                    userData->setFloatData(keyName, {floatAttr.getValue()});
                }
            } else if (typeAttr == kColorAttr) {
                const kodachi::FloatAttribute colorAttr(valueAttr);
                if (colorAttr.getNumberOfValues() == 3) {
                    const auto colorSample = colorAttr.getNearestSample(0.f);
                    rdl2::Rgb color(colorSample[0], colorSample[1], colorSample[2]);
                    userData->setColorData(keyName, {color});
                }
            } else if (typeAttr == kVec2fAttr) {
                const kodachi::FloatAttribute vec2fAttr(valueAttr);
                if (vec2fAttr.getNumberOfValues() == 2) {
                    const auto vec2fSample = vec2fAttr.getNearestSample(0.f);
                    userData->setVec2fData(keyName, {rdl2::Vec2f(vec2fSample.data())});
                }
            } else if (typeAttr == kVec3fAttr) {
                const kodachi::FloatAttribute vec3fAttr(valueAttr);
                if (vec3fAttr.getNumberOfValues() == 3) {
                    const auto vec3fSample = vec3fAttr.getNearestSample(0.f);
                    userData->setVec3fData(keyName, {rdl2::Vec3f(vec3fSample.data())});
                }
            } else if (typeAttr == kMat4fAttr) {
                const kodachi::FloatAttribute mat4fAttr(valueAttr);
                if (mat4fAttr.getNumberOfValues() == 16) {
                    const auto mat4fSample = mat4fAttr.getNearestSample(0.f);
                    userData->setMat4fData(keyName,
                            {rdl2::Mat4f(mat4fSample[0],  mat4fSample[1],  mat4fSample[2],  mat4fSample[3],
                                         mat4fSample[4],  mat4fSample[5],  mat4fSample[6],  mat4fSample[7],
                                         mat4fSample[8],  mat4fSample[9],  mat4fSample[10], mat4fSample[11],
                                         mat4fSample[12], mat4fSample[13], mat4fSample[14], mat4fSample[15])});
                }
            }

            userDataVector.push_back(userData);
        }
    }

    return userDataVector;
}

const MoonrayRenderState::SceneClassData&
MoonrayRenderState::getSceneClassData(const rdl2::SceneClass& sceneClass)
{
    // get the AttributeMap for the sceneClass
    auto sceneClassDataIter = mSceneClassDataMap.find(&sceneClass);

    if (sceneClassDataIter == mSceneClassDataMap.end()) {
        // First time we've seen this SceneClass, so build the SceneClassData.
        AttributeLookupMap attrLookupMap;
        AttributeIdMap attrIdMap;
        Rdl2AttrVec attrVec(sceneClass.beginAttributes(), sceneClass.endAttributes());

        for (std::size_t i = 0; i < attrVec.size(); ++ i) {
            const rdl2::Attribute* rdl2Attr = attrVec[i];

            const Attribute attribute(rdl2Attr, i);

            attrIdMap.emplace(attribute);

            // add the name
            attrLookupMap.emplace(rdl2Attr->getName(), attribute);

            // add the aliases
            for (const std::string& alias : rdl2Attr->getAliases()) {
                attrLookupMap.emplace(alias, attribute);
            }
        }

        // Try to insert into the SceneClass map.
        // If another thread was also building the attribute map for the
        // same class and inserted theirs first, this one will be discarded.
        // TODO: This can be improved when we move to TBB2018
        sceneClassDataIter = mSceneClassDataMap.insert(
                std::make_pair(&sceneClass, std::make_tuple(std::move(attrLookupMap), std::move(attrIdMap), std::move(attrVec)))).first;
    }

    return sceneClassDataIter->second;
}

} // namespace kodachi_moonray


// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnGeolib/op/FnGeolibOp.h>

#include <FnAttribute/FnAttribute.h>
#include <FnAttribute/FnGroupBuilder.h>
#include <FnPluginSystem/FnPlugin.h>

#include <sstream>

namespace {

class MoonrayCookMaterialInterfaceOp : public Foundry::Katana::GeolibOp
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        const FnAttribute::GroupAttribute parametersAttr = interface.getAttr("material.parameters");
        const FnAttribute::GroupAttribute interfaceAttr = interface.getAttr("material.interface");

        if (parametersAttr.isValid() && interfaceAttr.isValid()) {
            for (int64_t i = 0; i < parametersAttr.getNumberOfChildren(); ++i) {
                const FnAttribute::Attribute paramAttr = parametersAttr.getChildByIndex(i);

                if (!paramAttr.isValid() && paramAttr.getType() == kFnKatAttributeTypeNull) {
                    continue;
                }

                const FnAttribute::StringAttribute srcAttr =
                        interfaceAttr.getChildByName(parametersAttr.getChildName(i) + ".src");
                if (srcAttr.isValid()) {
                    const std::string src = srcAttr.getValue();
                    const size_t dotPos = src.rfind('.');

                    std::ostringstream buffer;
                    buffer << "material.nodes." << src.substr(0, dotPos)
                           << ".parameters." << src.substr(dotPos + 1);

                    interface.setAttr(buffer.str(), paramAttr);
                }
            }

            interface.deleteAttr("material.parameters");
            interface.deleteAttr("material.interface");
        }
    }

    static FnAttribute::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        return FnAttribute::GroupAttribute();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(MoonrayCookMaterialInterfaceOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayCookMaterialInterfaceOp, "MoonrayCookMaterialInterface", 0, 1);
}


// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "SubscriberSocket.h"

// katana
#include <FnRender/plugin/RenderBase.h>
#include <FnRender/plugin/RenderSettings.h>

// system
#include <cstdlib>
#include <fontconfig/fontconfig.h>
#include <memory>

namespace Foundry {
namespace Katana {
class KatanaPipe;
}
}

namespace mfk {
class KodachiRenderMethod;
class MoonrayArrasSettings;

enum class KatanaRenderMethod;

} // namespace mfk

class MoonrayRender : public FnKat::Render::RenderBase
{
public:
    static void flush() {};

    MoonrayRender(FnKat::FnScenegraphIterator rootIterator,
                  FnAttribute::GroupAttribute arguments);
    ~MoonrayRender();

    int start() override;
    int pause() override;
    int resume() override;
    int stop() override;
    int processControlCommand(const std::string& command) override;

    void configureDiskRenderOutputProcess(FnKat::Render::DiskRenderOutputProcess& diskRenderOutputProcess,
                                          const std::string& outputName,
                                          const std::string& outputPath,
                                          const std::string& renderMethodName,
                                          const float& frameTime) const override;

    static Foundry::Katana::Render::RenderBase*
    create(FnKat::FnScenegraphIterator rootIterator,
           FnAttribute::GroupAttribute args)
    {
        return new MoonrayRender(rootIterator, args);
    }

    Foundry::Katana::KatanaPipe* getImagePipe();

    void resetProgress() { mNextProgress = 0.0; }
    void logProgress(const float progress);

    static FnPluginStatus setHost(FnPluginHost* host);

    // ideally we wouldn't ever use the rootIterator, but there are some Katana
    // helpers that require it
    using FnKat::Render::RenderBase::getRootIterator;
    using FnKat::Render::RenderBase::getRenderMethodName;
    using FnKat::Render::RenderBase::applyRenderThreadsOverride;
    using FnKat::Render::RenderBase::getKatanaTempDirectory;
    using FnKat::Render::RenderBase::getKatanaHost;
    using FnKat::Render::RenderBase::getRenderTime;
    using FnKat::Render::RenderBase::useRenderPassID;
    using FnKat::Render::RenderBase::findArgument;

protected:
    bool openPipe();

    Foundry::Katana::KatanaPipe* mImagePipe = nullptr;
    std::unique_ptr<mfk::KodachiRenderMethod> mRenderHandler;

    std::unique_ptr<mfk::SubscriberSocket> mSubscriberSocket;

    // Progress helper
    float mNextProgress = 0.0;
};


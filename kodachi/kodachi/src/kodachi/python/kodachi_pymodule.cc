// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


// Local
#include "Helpers.h"
#include "kodachi_pymodule.h"

// pybind11
#include <pybind11/pybind11.h>

// Kodachi
#include <kodachi/backend/BackendClient.h>
#include <kodachi/Kodachi.h>
#include <kodachi/KodachiOpId.h>
#include <kodachi/KodachiRuntime.h>
#include <kodachi/OpTreeBuilder.h>

PYBIND11_MAKE_OPAQUE(std::vector<std::string>);
PYBIND11_MAKE_OPAQUE(std::vector<kodachi::KodachiOpId>);
PYBIND11_MAKE_OPAQUE(std::vector<kodachi::OpTreeBuilder::Op::Ptr>);

extern "C"
{
    int pykodachi_bootstrap(const char* katana_root)
    {
        const std::string katanaRootStr =
                (katana_root == nullptr ? "" : katana_root);

        return kodachi::bootstrap(katanaRootStr);
    }

    void pykodachi_initialize()
    {
        kodachi::KdPluginHost* pluginHost = kodachi::getHost();

        kodachi::Attribute::setHost(pluginHost);
        kodachi::BackendClient::setHost(pluginHost);
        kodachi::GroupBuilder::setHost(pluginHost);
        kodachi::KodachiRuntime::setHost(pluginHost);
        kodachi::OpTreeBuilder::setHost(pluginHost);
    }

} // extern "C"

PYBIND11_MODULE(pykodachi, module)
{
    module.doc() = "Kodachi Python bindings (pybind11).";

    pykodachi::pykodachi_bind_vector<std::string>(module, "StringVector");

    pykodachi::registerKodachiCacheRegistry(module);

    pykodachi::registerKodachiOpId(module);
    pykodachi::registerOpTreeBuilder(module);
    pykodachi::registerOpTreeUtil(module);
    pykodachi::registerUtils(module);

    pykodachi::registerKodachiRuntime(module);
    pykodachi::registerLocationData(module);
    pykodachi::registerTransaction(module);
    pykodachi::registerClient(module);
    pykodachi::registerOp(module);

    pykodachi::registerDataMessage(module);
    pykodachi::registerBackendClient(module);

    pykodachi::registerTraversal(module);

    module.def("setNumberOfThreads",
               &kodachi::setNumberOfThreads,
               pybind11::arg("numThreads"),
               "Set to '0' for automatic");
}


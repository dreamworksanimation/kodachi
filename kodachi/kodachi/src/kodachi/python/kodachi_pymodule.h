// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// pybind11
#include <pybind11/pybind11.h>
PYBIND11_DECLARE_HOLDER_TYPE(T, std::shared_ptr<T>);

namespace pykodachi
{
    void registerKodachiRuntime(pybind11::module &);
    void registerLocationData(pybind11::module &);
    void registerTransaction(pybind11::module &);
    void registerClient(pybind11::module &);
    void registerOp(pybind11::module &);

    void registerKodachiOpId(pybind11::module &);
    void registerOpTreeBuilder(pybind11::module &);
    void registerOpTreeUtil(pybind11::module &);
    void registerUtils(pybind11::module &);

    void registerDataMessage(pybind11::module &);
    void registerBackendClient(pybind11::module &);

    void registerTraversal(pybind11::module &);

    void registerKodachiCacheRegistry(pybind11::module &);
} // namespace pykodachi


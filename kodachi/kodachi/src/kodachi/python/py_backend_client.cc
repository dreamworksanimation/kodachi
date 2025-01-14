// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


// Local
#include "Helpers.h"
#include "kodachi_pymodule.h"

// Kodachi
#include <kodachi/backend/BackendClient.h>

// pybind11
#include <pybind11/pybind11.h>
#include <pybind11/stl_bind.h>
#include <pybind11/operators.h>

namespace pykodachi
{
    using BackendClient    = kodachi::BackendClient;
    using BackendClientPtr = std::shared_ptr<kodachi::BackendClient>;

    using DataMessage    = kodachi::BackendClient::DataMessage;
    using DataMessagePtr = std::shared_ptr<kodachi::BackendClient::DataMessage>;

    namespace internal
    {
        pybind11::object
        DataMessage_getAttr(DataMessagePtr self)
        {
            return convertFnAttributeToPyObject(self->getAttr());
        }

        pybind11::object
        DataMessage_getPayload(DataMessagePtr self, uint64_t idx)
        {
            std::shared_ptr<void> payload = self->getPayload(idx);
            if (payload == nullptr) {
                return pybind11::object { };
            }

            return pybind11::reinterpret_borrow<pybind11::object>(PyLong_FromVoidPtr(payload.get()));
        }

        bool
        BackendClient_initialize(BackendClientPtr self, pybind11::object opTreePyObj)
        {
            return self->initialize(convertPyObjectToFnAttribute(opTreePyObj));
        }

        pybind11::object
        BackendClient_getStaticData(const std::string& pluginName,
                                    pybind11::object configPyObject)
        {
            return convertFnAttributeToPyObject(
                    BackendClient::getStaticData(
                            pluginName,
                            convertPyObjectToFnAttribute(configPyObject)));
        }

        void
        BackendClient_setData(BackendClientPtr self, pybind11::object dataPyObj)
        {
            const kodachi::GroupAttribute data =
                    convertPyObjectToFnAttribute(dataPyObj);

            pybind11::gil_scoped_release gil_release;
            self->setData(data);
        }

        DataMessagePtr
        BackendClient_getData(BackendClientPtr self, pybind11::object queryPyObj)
        {
            const kodachi::GroupAttribute queryAttr =
                    convertPyObjectToFnAttribute(queryPyObj);

            pybind11::gil_scoped_release gil_release;
            auto dataMessage = self->getData(queryAttr);
            if (!dataMessage.isValid()) {
                return { };
            }

            return std::make_shared<DataMessage>(std::move(dataMessage));
        }
    } // namespace internal

    //--------------------------------------------------------------------------------------

    void
    registerDataMessage(pybind11::module& module)
    {
        pybind11::class_<DataMessage, DataMessagePtr>(module, "DataMessage")
               .def("getAttr", &internal::DataMessage_getAttr)

               .def("getPayload", &internal::DataMessage_getPayload, pybind11::arg("idx"))

               .def("isValid", &DataMessage::isValid);
    }

    void
    registerBackendClient(pybind11::module& module)
    {
        pybind11::class_<BackendClient, BackendClientPtr>(module, "BackendClient")

               .def(pybind11::init([]() { return std::make_shared<BackendClient>(); }))

               .def("initialize", &internal::BackendClient_initialize, pybind11::arg("rootOp"))

               .def("start", &BackendClient::start, pybind11::call_guard<pybind11::gil_scoped_release>())

               .def("stop", &BackendClient::stop, pybind11::call_guard<pybind11::gil_scoped_release>())

               .def_static("getStaticData",
                           &internal::BackendClient_getStaticData,
                           pybind11::arg("pluginName"), pybind11::arg("configAttr"))

               .def("isValid", &BackendClient::isValid)

               .def("setData", &internal::BackendClient_setData, pybind11::arg("data"))

               .def("getData", &internal::BackendClient_getData, pybind11::arg("query"));
    }

} // namespace pykodachi

//----------------------------------------------------------------------------


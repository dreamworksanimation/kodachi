// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


// Kodachi
#include <kodachi/KodachiRuntime.h>
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/GroupBuilder.h>

// Local
#include "Helpers.h"

namespace pykodachi
{
    //--------------------------------------------------------------------------------------

    using KodachiRuntime    = kodachi::KodachiRuntime;
    using KodachiRuntimePtr = std::shared_ptr<KodachiRuntime>;

    using Client    = kodachi::KodachiRuntime::Client;
    using ClientPtr = std::shared_ptr<Client>;

    using LocationData    = kodachi::KodachiRuntime::LocationData;
    using LocationDataPtr = std::shared_ptr<LocationData>;

    using Op    = kodachi::KodachiRuntime::Op;
    using OpPtr = std::shared_ptr<Op>;

    using Transaction    = kodachi::KodachiRuntime::Transaction;
    using TransactionPtr = std::shared_ptr<Transaction>;

    //--------------------------------------------------------------------------------------

    namespace internal
    {
        //------------------------------------
        // KodachiRuntime::LocationData

        pybind11::object
        LocationData_getPotentialChildren(LocationDataPtr self)
        {
            return convertFnAttributeToPyObject(
                        self->getPotentialChildren());
        }

        pybind11::object
        LocationData_getAttrs(LocationDataPtr self)
        {
            return convertFnAttributeToPyObject(self->getAttrs());
        }

        //------------------------------------
        // KodachiRuntime::Transaction

        void
        Transaction_setOpArgs(TransactionPtr self,
                              Op::Ptr op,
                              const std::string& opType,
                              pybind11::object argsPyObjPtr)
        {
            const kodachi::GroupAttribute args =
                    internal::convertPyObjectToFnAttribute(argsPyObjPtr);
            self->setOpArgs(op, opType, args);
        }

        void
        Transaction_setOpInputs(TransactionPtr self,
                                OpPtr op,
                                pybind11::list inputList)
        {
            const std::vector<OpPtr> inputs =
                    PyContainerToStdVector<OpPtr>(inputList);

            self->setOpInputs(op, inputs);
        }

        ClientPtr
        Transaction_createClient(TransactionPtr self)
        {
            return self->createClient();
        }

        void
        Transaction_setClientOp(TransactionPtr self, ClientPtr client, OpPtr op)
        {
            self->setClientOp(client, op);
        }

        pybind11::list
        Transaction_parseGraph(TransactionPtr self, pybind11::object graphAttrPyObject)
        {
            const kodachi::GroupAttribute graphAttr =
                    internal::convertPyObjectToFnAttribute(graphAttrPyObject);

            const std::vector<OpPtr> ops = self->parseGraph(graphAttr);

            return StdVectorToPyList(ops.cbegin(), ops.cend());
        }

        OpPtr
        Transaction_appendOpChain(TransactionPtr self, OpPtr op, pybind11::object opCollectionPyObj)
        {
            const kodachi::GroupAttribute opsAttr =
                    convertPyObjectToFnAttribute(opCollectionPyObj);

            return self->appendOpChain(op, opsAttr);
        }

        Op::Ptr
        Transaction_appendOps(TransactionPtr self, OpPtr op, pybind11::list opList)
        {
            const std::vector<OpPtr> appendedOps =
                                PyContainerToStdVector<OpPtr>(opList);

            return self->appendOps(op, appendedOps);
        }

        //------------------------------------
        // KodachiRuntime::Op

        pybind11::list
        Op_getInputs(OpPtr self)
        {
            std::vector<Op::Ptr> inputs = self->getInputs();
            return StdVectorToPyList(inputs.cbegin(), inputs.cend());
        }

        pybind11::tuple
        Op_getOpArgs(OpPtr self)
        {
            const std::pair<std::string, kodachi::Attribute> args = self->getOpArgs();
            return pybind11::make_tuple(
                    args.first,
                    convertFnAttributeToPyObject(args.second));
        }

        //------------------------------------
        // KodachiRuntime::Client

        LocationData
        Client_cookLocation(ClientPtr self, const std::string& locationPath, bool evict)
        {
            return self->cookLocation(locationPath, evict);
        }

        //------------------------------------
        // KodachiRuntime

        pybind11::object
        KodachiRuntime_describeOp(KodachiRuntimePtr self,
                                  std::string opType)
        {
            return internal::convertFnAttributeToPyObject(self->describeOp(opType));
        }

        pybind11::object
        KodachiRuntime_getRegisteredOpTypes(KodachiRuntimePtr self)
        {
            return internal::convertFnAttributeToPyObject(self->getRegisteredOpTypes());
        }

//        TransactionPtr
//        KodachiRuntime_createTransaction(KodachiRuntimePtr self)
//        {
//            pybind11::gil_scoped_release gil_release;
//            return self->createTransaction();
//        }

//        FnGeolibCommitId /* std::int32_t */
//        KodachiRuntime_commit(KodachiRuntimePtr self, TransactionPtr txn)
//        {
//            pybind11::gil_scoped_release gil_release;
//            return self->commit(txn);
//        }

//        std::string
//        KodachiRuntime_getRootLocationPath(KodachiRuntimePtr self)
//        {
//            pybind11::gil_scoped_release gil_release;
//            return self->getRootLocationPath();
//        }

        pybind11::object
        KodachiRuntime_getOptions(KodachiRuntimePtr self)
        {
            return internal::convertFnAttributeToPyObject(self->getOptions());
        }

        void
        KodachiRuntime_setOptions(KodachiRuntimePtr self,
                                  pybind11::object optionsPyObjPtr)
        {
            const kodachi::GroupAttribute options =
                    internal::convertPyObjectToFnAttribute(optionsPyObjPtr);

            pybind11::gil_scoped_release gil_release;
            self->setOptions(options);
        }

    } // namespace internal

    //--------------------------------------------------------------------------------------

    void
    registerLocationData(pybind11::module& module)
    {
        pybind11::class_<LocationData, LocationDataPtr>(module, "LocationData")
               .def(pybind11::init<>())

               .def("getLocationPath", &LocationData::getLocationPath)

               .def("doesLocationExist", &LocationData::doesLocationExist)

               .def("getAttrs", &internal::LocationData_getAttrs)

               .def("getPotentialChildren", &internal::LocationData_getPotentialChildren);

        pykodachi_bind_vector<LocationData>(module, "LocationDataVector");
    }

    void
    registerClient(pybind11::module& module)
    {
        pybind11::class_<Client, ClientPtr>(module, "Client")

               .def("getOp", &Client::getRuntime, pybind11::call_guard<pybind11::gil_scoped_release>())

               .def("getRuntime", &Client::getRuntime)

               .def("cookLocation", &internal::Client_cookLocation,
                    pybind11::call_guard<pybind11::gil_scoped_release>(),
                    pybind11::arg("locationPath"), pybind11::arg("evict") = true);
    }

    void
    registerTransaction(pybind11::module& module)
    {
        pybind11::class_<Transaction, TransactionPtr>(module, "Transaction")

               .def("createOp", &Transaction::createOp)

               .def("setOpArgs", &internal::Transaction_setOpArgs,
                    pybind11::arg("op"), pybind11::arg("opType"), pybind11::arg("args"))

               .def("setOpInputs", &internal::Transaction_setOpInputs,
                    pybind11::arg("op"), pybind11::arg("inputList"))

               .def("createClient", &internal::Transaction_createClient)

               .def("setClientOp", &internal::Transaction_setClientOp,
                    pybind11::arg("client"), pybind11::arg("op"))

               .def("appendOpChain", &internal::Transaction_appendOpChain,
                    pybind11::arg("op"), pybind11::arg("opChain"))

               .def("appendOps", &internal::Transaction_appendOps,
                    pybind11::arg("op"), pybind11::arg("opList"))

               .def("parseGraph", &internal::Transaction_parseGraph, pybind11::arg("graphAttr"));
    }

    void
    registerOp(pybind11::module& module)
    {
        pybind11::class_<Op, OpPtr>(module, "Op")

               .def("getInputs", &internal::Op_getInputs)

               .def("getOpArgs", &internal::Op_getOpArgs)

               .def("getOpId", &Op::getOpId, pybind11::return_value_policy::reference_internal);
    }

    void
    registerKodachiRuntime(pybind11::module& module)
    {
        pybind11::class_<KodachiRuntime, KodachiRuntimePtr>(module, "KodachiRuntime")

               .def_static("createRuntime", &KodachiRuntime::createRuntime)

               .def("describeOp", &internal::KodachiRuntime_describeOp, pybind11::arg("opType"))

               .def("getRegisteredOpTypes", &internal::KodachiRuntime_getRegisteredOpTypes)

               .def("isValidOp", &KodachiRuntime::isValidOp, pybind11::arg("opId"))

               .def("getOpFromOpId", &KodachiRuntime::getOpFromOpId, pybind11::arg("opId"))

               .def("createTransaction", &KodachiRuntime::createTransaction, pybind11::call_guard<pybind11::gil_scoped_release>())

               .def("commit", &KodachiRuntime::commit, pybind11::arg("txn"), pybind11::call_guard<pybind11::gil_scoped_release>())

               .def("getLatestCommitId", &KodachiRuntime::getLatestCommitId)

               .def("getRootLocationPath", &KodachiRuntime::getRootLocationPath, pybind11::call_guard<pybind11::gil_scoped_release>())

               .def("getOptions", &internal::KodachiRuntime_getOptions)

               .def("setOptions", &internal::KodachiRuntime_setOptions, pybind11::arg("options"))

               .def("flushCaches", &KodachiRuntime::flushCaches);
    }

} // namespace pykodachi

//----------------------------------------------------------------------------


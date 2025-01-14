// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


// Local
#include "Helpers.h"
#include "kodachi_pymodule.h"

// Kodachi
#include <kodachi/KodachiOpId.h>
#include <kodachi/OpTreeBuilder.h>

// pybind11
#include <pybind11/pybind11.h>
#include <pybind11/stl_bind.h>
#include <pybind11/operators.h>

namespace pykodachi
{
    namespace internal
    {

        pybind11::list
        OpTreeBuilder_findTerminalOps(pybind11::object optreePyObj)
        {
            const kodachi::GroupAttribute optree =
                    internal::convertPyObjectToFnAttribute(optreePyObj);

            const std::vector<kodachi::KodachiOpId> result =
                    kodachi::OpTreeBuilder::findTerminalOps(optree);

            return StdVectorToPyList(result.begin(), result.end());
        }

        kodachi::OpTreeBuilder::Ptr
        OpTreeBuilder_setOpArgs(kodachi::OpTreeBuilder::Ptr self,
                                kodachi::OpTreeBuilder::Op::Ptr op,
                                const std::string& opType,
                                pybind11::object opArgsPyObj)
        {
            const kodachi::GroupAttribute opArgs =
                    internal::convertPyObjectToFnAttribute(opArgsPyObj);

            pybind11::gil_scoped_release gil_release;
            self->setOpArgs(op, opType, opArgs);

            return self;
        }

        kodachi::OpTreeBuilder::Ptr
        OpTreeBuilder_setOpInputs(kodachi::OpTreeBuilder::Ptr self,
                                  kodachi::OpTreeBuilder::Op::Ptr op,
                                  pybind11::list opInputsPyList)
        {
            std::vector<kodachi::OpTreeBuilder::Op::Ptr> opInputs =
                    PyContainerToStdVector<kodachi::OpTreeBuilder::Op::Ptr>(opInputsPyList);

            pybind11::gil_scoped_release gil_release;
            self->setOpInputs(op, opInputs);

            return self;
        }

        pybind11::list
        OpTreeBuilder_merge(kodachi::OpTreeBuilder::Ptr self, pybind11::object optreePyObj)
        {
            const kodachi::GroupAttribute optree =
                    internal::convertPyObjectToFnAttribute(optreePyObj);

            std::vector<kodachi::OpTreeBuilder::Op::Ptr> result;
            {
                pybind11::gil_scoped_release gil_release;
                result = self->merge(optree);
            }

            return StdVectorToPyList(result.cbegin(), result.cend());
        }

        pybind11::list
        OpTreeBuilder_appendOpChain(kodachi::OpTreeBuilder::Ptr self,
                                    kodachi::OpTreeBuilder::Op::Ptr op,
                                    pybind11::list opChainPyObj)
        {
            const kodachi::GroupAttribute opChain =
                    internal::convertPyObjectToFnAttribute(opChainPyObj);

            std::vector<kodachi::OpTreeBuilder::Op::Ptr> result;
            {
                pybind11::gil_scoped_release gil_release;
                result = self->appendOpChain(op, opChain);
            }

            return StdVectorToPyList(result.cbegin(), result.cend());
        }

        pybind11::object
        OpTreeBuilder_buildDelta(kodachi::OpTreeBuilder::Ptr self,
                                 kodachi::OpTreeBuilder::BuildMode mode)
        {
            pybind11::gil_scoped_release gil_release;
            return internal::convertFnAttributeToPyObject(self->buildDelta(mode));
        }

        pybind11::object
        OpTreeBuilder_build(kodachi::OpTreeBuilder::Ptr self,
                            kodachi::OpTreeBuilder::Op::Ptr terminalOp,
                            kodachi::OpTreeBuilder::BuildMode mode)
        {
            pybind11::gil_scoped_release gil_release;
            return internal::convertFnAttributeToPyObject(self->build(terminalOp, mode));
        }
    } // namespace internal

    //--------------------------------------------------------------------------------------

    void
    registerKodachiOpId(pybind11::module& module)
    {
        pybind11::class_<kodachi::KodachiOpId, std::shared_ptr<kodachi::KodachiOpId>>(module, "KodachiOpId")
               .def(pybind11::init<>())
               .def(pybind11::init<const std::string&>(), pybind11::arg("id"))

               .def_static("generate", &kodachi::KodachiOpId::generate)

               .def("clear", &kodachi::KodachiOpId::clear)

               .def("is_null", &kodachi::KodachiOpId::is_null)

               .def("is_valid", &kodachi::KodachiOpId::is_valid)

               .def("str",
                    &kodachi::KodachiOpId::str,
                    pybind11::return_value_policy::reference)

               .def("__str__",
                    &kodachi::KodachiOpId::str,
                    pybind11::return_value_policy::reference)

               .def(pybind11::self == pybind11::self)
               .def(pybind11::self != pybind11::self)
               .def(pybind11::self < pybind11::self);

        // NOTE: must be paired with a PYBIND11_MAKE_OPAQUE(...) in kodachi_pymodule.cc
        pykodachi_bind_vector<kodachi::KodachiOpId>(module, "KodachiOpIdVector");
    }

    void
    registerOpTreeBuilder(pybind11::module& module)
    {
        pybind11::class_<kodachi::OpTreeBuilder,
                         std::shared_ptr<kodachi::OpTreeBuilder>> opTreeBuilderPyClass(module, "OpTreeBuilder");

        //----------------------------

        pybind11::class_<kodachi::OpTreeBuilder::Op,
                         std::shared_ptr<kodachi::OpTreeBuilder::Op>>(
                                 /* class binding instead of module */ opTreeBuilderPyClass,
                                 "Op")
                .def_readonly("mId", &kodachi::OpTreeBuilder::Op::mId)

                .def(pybind11::self == pybind11::self)
                .def(pybind11::self != pybind11::self)
                .def(pybind11::self < pybind11::self);

        // NOTE: must be paired with a PYBIND11_MAKE_OPAQUE(...) in kodachi_pymodule.cc
        pykodachi_bind_vector<kodachi::OpTreeBuilder::Op::Ptr>(module, "KodachiOpVector");

        //----------------------------

        pybind11::enum_<kodachi::OpTreeBuilder::BuildMode>(/* class binding instead of module */ opTreeBuilderPyClass, "BuildMode")
                .value("FLUSH", kodachi::OpTreeBuilder::BuildMode::FLUSH)
                .value("RETAIN", kodachi::OpTreeBuilder::BuildMode::RETAIN);

        //----------------------------

        opTreeBuilderPyClass
                .def(pybind11::init<>())

                .def_static("findTerminalOps", &internal::OpTreeBuilder_findTerminalOps, pybind11::arg("optree"))

                .def("createOp",
                     &kodachi::OpTreeBuilder::createOp,
                     pybind11::call_guard<pybind11::gil_scoped_release>())

                .def("contains",
                     &kodachi::OpTreeBuilder::contains,
                     pybind11::call_guard<pybind11::gil_scoped_release>(),
                     pybind11::arg("op"))

                .def("getOpFromOpId",
                     &kodachi::OpTreeBuilder::getOpFromOpId,
                     pybind11::call_guard<pybind11::gil_scoped_release>(),
                     pybind11::arg("opId"))

                .def("setOpArgs",
                     &internal::OpTreeBuilder_setOpArgs,
                     pybind11::arg("op"), pybind11::arg("opType"), pybind11::arg("opArgs"))

                .def("setOpInputs",
                     &internal::OpTreeBuilder_setOpInputs, pybind11::arg("op"), pybind11::arg("opInputs"))

                .def("merge", &internal::OpTreeBuilder_merge, pybind11::arg("optree"))

                .def("appendOp",
                     &kodachi::OpTreeBuilder::appendOp,
                     pybind11::call_guard<pybind11::gil_scoped_release>(),
                     pybind11::arg("op1"), pybind11::arg("op2"))

                .def("appendOpChain", &internal::OpTreeBuilder_appendOpChain, pybind11::arg("op"), pybind11::arg("opChain"))

                .def("buildDelta", &internal::OpTreeBuilder_buildDelta, pybind11::arg("mode") = kodachi::OpTreeBuilder::BuildMode::FLUSH)

                .def("build", &internal::OpTreeBuilder_build, pybind11::arg("terminalOp"), pybind11::arg("mode") = kodachi::OpTreeBuilder::BuildMode::FLUSH);
    }

} // namespace pykodachi

//----------------------------------------------------------------------------


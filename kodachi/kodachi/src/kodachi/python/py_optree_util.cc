// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "Helpers.h"

#include <kodachi/OpTreeUtil.h>

namespace pykodachi
{
    using Runtime = kodachi::KodachiRuntime;
    using Client  = kodachi::KodachiRuntime::Client;

    namespace internal
    {
        class OpTreeUtilDummy { };

        pybind11::object
        OpTreeUtil_convertToKodachiOpTree(pybind11::object optreePyObj)
        {
            const kodachi::GroupAttribute optree =
                    internal::convertPyObjectToFnAttribute(optreePyObj);

            return internal::convertFnAttributeToPyObject(
                    kodachi::optree_util::convertToKodachiOpTree(optree));
        }

        Client::Ptr
        OpTreeUtil_loadOpTree(Runtime::Ptr runtime, pybind11::object optreePyObj)
        {
            const kodachi::GroupAttribute optree =
                    internal::convertPyObjectToFnAttribute(optreePyObj);

            return kodachi::optree_util::loadOpTree(runtime, optree);
        }

        pybind11::object
        kodachi_loadImplicitResolversOpCollection()
        {
            return internal::convertFnAttributeToPyObject(
                                kodachi::optree_util::loadImplicitResolversOpCollection());
        }

        pybind11::object
        kodachi_addSystemOpArgsToOpCollection(
                pybind11::object opCollectionPyObj,
                pybind11::object sysOpArgsAttrPyObj)
        {
            const kodachi::GroupAttribute opCollectionAttr =
                    convertPyObjectToFnAttribute(opCollectionPyObj);

            const kodachi::GroupAttribute systemArgsAttr =
                    convertPyObjectToFnAttribute(sysOpArgsAttrPyObj);

            kodachi::GroupAttribute newOpDesc;
            {
                pybind11::gil_scoped_release gil_release;
                newOpDesc =
                        kodachi::optree_util::addSystemOpArgsToOpCollection(opCollectionAttr,
                                                                            systemArgsAttr);
            }

            return convertFnAttributeToPyObject(newOpDesc);
        }
    } // namespace internal

    void
    registerOpTreeUtil(pybind11::module& module)
    {
        pybind11::class_<internal::OpTreeUtilDummy>(module, "optree_util")

            .def_static("convertToKodachiOpTree", &internal::OpTreeUtil_convertToKodachiOpTree, pybind11::arg("optree"))

            .def_static("loadOpTree", &internal::OpTreeUtil_loadOpTree, pybind11::arg("runtime"), pybind11::arg("optree"))

            .def_static("loadImplicitResolversOpCollection",
                 &internal::kodachi_loadImplicitResolversOpCollection,
                 "Parses one or more XMLs on disk to build a group attribute containing a collection of "
                 "op descriptions; each entry is itself a GroupAttribute, and contains at least "
                 "two attributes:"
                 "\n\t 1) an opType (StringAttribute), and"
                 "\n\t 2) an opArgs (GroupAttribute)"
                 "\n"
                 "Other attributes may be present, e.g. addSystemOpArgs (IntAttribute), etc."
                 "\n"
                 "Input: no direct input. Reads the full path to the XML file(s) by reading "
                 "KODACHI_RESOLVERS_COLLECTION_XML environment variable."
                 "\n"
                 "Output: a kodachi::GroupAttribute object.")

            .def_static("addSystemOpArgsToOpCollection",
                 &internal::kodachi_addSystemOpArgsToOpCollection,
                 pybind11::arg("opCollection"), pybind11::arg("systemOpArgs"),
                 "Takes in a collection of op descriptions (a GroupAttribute "
                 "containing opType, opArgs, etc) and a GroupAttribute containing "
                 "system op args."
                 "\n"
                 "Goes over all the op descriptions and checks if any of them has "
                 "an \"addSystemOpArgs\", and whether or not it is set to 1 (true), "
                 " if yes, then the opArgs is updated by adding a copy of systemOpArgs."
                 "\n"
                 "Returns a new GroupAttribute containing the modified opDescrCollection."
                 "\n"
                 "All \"addSystemOpArgs\" are removed to avoid overwriting the \"system\" "
                 "attributes in subsequent calls of this function on a previously processed "
                 "collection.");
    }
} // namespace pykodachi


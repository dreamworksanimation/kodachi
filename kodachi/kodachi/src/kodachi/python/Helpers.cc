// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "Helpers.h"

#include <dlfcn.h>

#include <kodachi/logging/KodachiLogging.h>

KdLogSetup("KodachiRuntime");

namespace pykodachi
{
    namespace internal
    {
        FnAttribute::Attribute
        convertPyObjectToFnAttribute(pybind11::object pyobj)
        {
            static const std::string PATH_TO_SO =
                    [] () -> std::string
                    {
                        const char* katanaRootEnv = ::getenv("REZ_KODACHI_ROOT");
                        return (katanaRootEnv == nullptr ?
                                ""
                                :
                                std::string(katanaRootEnv) + "/katana_python/PyFnAttribute.so");
                    } ();

            if (PATH_TO_SO.empty()) {
                KdLogError("[Kodachi Python Bindings] pykodachi::internal::convertPyObjectToFnAttribute(PyObject*) "
                           "environment variable REZ_KODACHI_ROOT not found.");
                return { };
            }

            void* dso =
                    ::dlopen(PATH_TO_SO.c_str(), RTLD_LOCAL | RTLD_LAZY);

            if (dso == nullptr) {
                KdLogError("[Kodachi Python Bindings] pykodachi::internal::convertPyObjectToFnAttribute(PyObject*) "
                           "failed to load shared library PyFnAttribute.so.");
                return { };
            }

            // Find this symbol:
            //      Geolib3::PyFnAttribute::v1::AttributeFromPyObject(_object*)
            void* attrFromPyObjVoidPtr =
                    ::dlsym(dso, "_ZN7Geolib313PyFnAttribute2v121AttributeFromPyObjectEP7_object");

            if (attrFromPyObjVoidPtr == nullptr) {
                KdLogError("[Kodachi Python Bindings] pykodachi::internal::convertPyObjectToFnAttribute(PyObject*) "
                           "failed to load symbol Geolib3::PyFnAttribute::v1::AttributeFromPyObject(_object*)");
                return { };
            }

            using FuncPtr = FnAttribute::Attribute (*) (PyObject*);

            FuncPtr attrFromPyObjFunc =
                     reinterpret_cast<FuncPtr>(attrFromPyObjVoidPtr);

            return attrFromPyObjFunc(pyobj.ptr());
        }

        pybind11::object
        convertFnAttributeToPyObject(const FnAttribute::Attribute& attr)
        {
            static const std::string PATH_TO_SO =
                    [] () -> std::string
                    {
                        const char* katanaRootEnv = ::getenv("REZ_KODACHI_ROOT");
                        return (katanaRootEnv == nullptr ?
                                ""
                                :
                                std::string(katanaRootEnv) + "/katana_python/PyFnAttribute.so");
                    } ();

            if (PATH_TO_SO.empty()) {
                KdLogError("[Kodachi Python Bindings] pykodachi::internal::convertFnAttributeToPyObject(const FnAttribute::Attribute&) "
                           "environment variable REZ_KODACHI_ROOT not found.");
                return { };
            }

            void* dso =
                    ::dlopen(PATH_TO_SO.c_str(), RTLD_LOCAL | RTLD_LAZY);

            if (dso == nullptr) {
                KdLogError("[Kodachi Python Bindings] pykodachi::internal::convertFnAttributeToPyObject(const FnAttribute::Attribute&) "
                           "failed to load shared library PyFnAttribute.so.");
                return { };
            }

            // Find this symbol:
            //      Geolib3::PyFnAttribute::v1::PyAttributeFromAttribute(Geolib3::internal::FnAttribute::Attribute const&)
            void* pyObjFromAttrVoidPtr =
                    ::dlsym(dso, "_ZN7Geolib313PyFnAttribute2v124PyAttributeFromAttributeERKNS_8internal11FnAttribute9AttributeE");

            if (pyObjFromAttrVoidPtr == nullptr) {
                KdLogError("[Kodachi Python Bindings] pykodachi::internal::convertFnAttributeToPyObject(const FnAttribute::Attribute&) "
                           "failed to load symbol Geolib3::PyFnAttribute::v1::PyAttributeFromAttribute(Geolib3::internal::FnAttribute::Attribute const&)");
                return { };
            }

            using FuncPtr = PyObject* (*) (FnAttribute::Attribute);

            FuncPtr pyObjFromAttrFunc =
                     reinterpret_cast<FuncPtr>(pyObjFromAttrVoidPtr);

            return pybind11::reinterpret_borrow<pybind11::object>( pyObjFromAttrFunc(attr) );
        }

    } // namespace internal

} // namespace pykodachi


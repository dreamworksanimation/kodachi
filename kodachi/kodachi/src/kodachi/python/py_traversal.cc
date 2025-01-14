// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include <kodachi/Traversal.h>
#include <kodachi/KodachiRuntime.h>

#include "Helpers.h"

namespace pykodachi
{
    using Traversal    = kodachi::Traversal;
    using TraversalPtr = std::shared_ptr<Traversal>;
    using ClientPtr    = std::shared_ptr<kodachi::KodachiRuntime::Client>;

    namespace internal
    {
        // Traversal constructor used to take a second arg.
        // ignore it to prevent existing python code from breaking.
        TraversalPtr
        Traversal_Constructor(const ClientPtr& client, pybind11::args args, pybind11::kwargs kwargs)
        {
            if (args.size() > 0 || (kwargs && kwargs.contains("setCookedLocationsActive"))) {
                pybind11::print("DEPRECATED: kodachi Traversal no longer uses the 'setCookedLocationsActive' arg");
            }

            return std::make_shared<Traversal>(client);
        }

        pybind11::list
        Traversal_getLocations(TraversalPtr self)
        {
            using LocData = kodachi::KodachiRuntime::LocationData;

            std::vector<LocData> locs = self->getLocations();

            pybind11::list pyList { };
            for (const auto& loc : locs) {
                pyList.append( std::make_shared<LocData>( loc ) );
            }

            return pyList;
        }
    }

    void
    registerTraversal(pybind11::module& module)
    {
        pybind11::class_<Traversal, TraversalPtr>(module, "Traversal")
               .def(pybind11::init(&internal::Traversal_Constructor), pybind11::arg("kodachiClient"))

               .def("getLocation", &Traversal::getLocation, pybind11::call_guard<pybind11::gil_scoped_release>())

               .def("getLocations", &internal::Traversal_getLocations, pybind11::call_guard<pybind11::gil_scoped_release>())

               .def("setRootLocationPath", &Traversal::setRootLocationPath, pybind11::arg("rootPath"))

               .def("getRootLocationPath",
                    &Traversal::getRootLocationPath,
                    pybind11::return_value_policy::reference_internal)

               .def("isValid", &Traversal::isValid, pybind11::call_guard<pybind11::gil_scoped_release>())

               .def_readonly_static("kParallelTraversal", &Traversal::kParallelTraversal);
    }

} // namespace pykodachi


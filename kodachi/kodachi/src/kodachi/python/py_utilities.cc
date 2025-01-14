// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


// Kodachi
#include <kodachi/KodachiOpId.h>
#include <kodachi/OpTreeBuilder.h>
#include <kodachi/OpTreeUtil.h>
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/GroupBuilder.h>

// C++
#include <fstream>
#include <string>

// Local
#include "Helpers.h"

namespace pykodachi
{
    namespace internal
    {

        //-------------------------------------------------

        class ImplicitResolverRegistry
        {
        private:
            static kodachi::GroupBuilder mGroupBuilder;

        public:
            using Ptr = std::shared_ptr<internal::ImplicitResolverRegistry>;

            static void
            add(const std::string& opType,
                pybind11::object opArgsPyObj,
                int priority,
                const std::string& ignore,
                bool addSystemOpArgs)
            {
                kodachi::GroupAttribute opArgs =
                        internal::convertPyObjectToFnAttribute(opArgsPyObj);

                kodachi::GroupBuilder attrsGB;
                attrsGB.set("priority", kodachi::IntAttribute( priority ));
                attrsGB.set("opType", kodachi::StringAttribute(opType));
                attrsGB.set("opArgs", opArgs);
                attrsGB.set("ignore", kodachi::StringAttribute(ignore));
                attrsGB.set("addSystemOpArgs",
                            kodachi::IntAttribute(
                                    static_cast<int>(addSystemOpArgs)));

                mGroupBuilder.setWithUniqueName("op", attrsGB.build());
            }

            static bool
            writeXML()
            {
                std::ofstream fout("kodachi_implicit_resolvers.xml");
                if (fout.fail()) {
                    return false;
                }

                kodachi::GroupAttribute resolvers = mGroupBuilder.build();
                if (!resolvers.isValid()) {
                    return false;
                }

                fout << resolvers.getXML();
                fout.close();

                return true;
            }
        };

        kodachi::GroupBuilder ImplicitResolverRegistry::mGroupBuilder;

        std::uint64_t
        getCppStringHash_u64(const std::string& str)
        {
            return std::hash<std::string>{}(str);
        }

        std::int64_t
        getCppStringHash_s64(const std::string& str)
        {
            return static_cast<std::int64_t>(std::hash<std::string>{}(str));
        }

    } // namespace internal

    //--------------------------------------------------------------------------------------

    void
    registerUtils(pybind11::module& module)
    {
        module.def_submodule("ImplicitResolverRegistry")

               .def("writeXML", &internal::ImplicitResolverRegistry::writeXML)

               .def("add",
                    &internal::ImplicitResolverRegistry::add,
                    pybind11::arg("opType"),
                    pybind11::arg("opArgs"),
                    pybind11::arg("priority"),
                    pybind11::arg("ignore"),
                    pybind11::arg("addSystemOpArgs"));

        //-----------------------------
        // Miscellaneous utility functions

        module.def_submodule("utils")

               .def("cppStringHash_u64", &internal::getCppStringHash_u64, pybind11::arg("string"))

               .def("cppStringHash_s64", &internal::getCppStringHash_s64, pybind11::arg("string"));
    }

} // namespace pykodachi

//----------------------------------------------------------------------------


// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <internal/FnAttribute/FnAttribute.h>

// C++
#include <deque>
#include <map>
#include <memory>
#include <vector>

// pybind11
#include <pybind11/pybind11.h>
#include <pybind11/stl_bind.h>

namespace pykodachi
{
    namespace internal
    {
        FnAttribute::Attribute convertPyObjectToFnAttribute(pybind11::object pyobj);
        pybind11::object convertFnAttributeToPyObject(const FnAttribute::Attribute& attr);

    } // namespace internal

    //-----------------------------------------
    // Type aliases, alias templates, etc.

    template <typename T>
    using IterCat_t = typename std::iterator_traits<T>::iterator_category;

    template <typename ConstIterator>
    using ConstIterToVec_t = typename std::vector<typename ConstIterator::value_type>::const_iterator;

    template <typename ConstIterator>
    using ConstIterToDeque_t = typename std::deque<typename ConstIterator::value_type>::const_iterator;

    template <typename ConstIterator>
    using ConstIterToMap_t =
            typename std::map<typename ConstIterator::value_type::first_type,
                              typename ConstIterator::value_type::second_type>::const_iterator;

    //-----------------------------------------

    template <typename T, typename PythonContainer>
    inline std::vector<T>
    PyContainerToStdVector(PythonContainer pycontainer)
    {
        static_assert((std::is_same<PythonContainer, pybind11::list>::value ||
                       std::is_same<PythonContainer, pybind11::tuple>::value),
                      "kodachi::internal::PyContainerToStdVector<T, PythonContainer> can only accept "
                      "[ PythonContainer = boost::python::list ] or "
                      "[ PythonContainer = boost::python::tuple ].");

        const std::size_t containerSize = pycontainer.size();
        if (containerSize == 0) {
            return { };
        }

        std::vector<T> resultVect;
        resultVect.reserve(containerSize);
        for (std::size_t idx = 0; idx < containerSize; ++idx) {
            resultVect.emplace_back( pybind11::cast<T>( pycontainer[idx] ) );
        }

        return resultVect;
    }

    template<typename ConstIterator>
    inline pybind11::list
    StdVectorToPyList(ConstIterator beginIter, ConstIterator endIter)
    {
        //-----------------------------------------

        // 1) Expect ConstIterator to be in RandomAccessIterator category, meaning
        //    it's an iterator to an element in one of these STL containers:
        //      std::array, std::vector, or std::deque
        static_assert(std::is_same<std::random_access_iterator_tag, IterCat_t<ConstIterator>>::value,
                      "kodachi::internal::StdVectorToPyList<ConstIterator>, type ConstIterator "
                      "must be a Constant RandomAccessIterator.");

        // 2) Only a std::vector<T, U>::const_iterator is acceptable.
        static_assert(std::is_same<ConstIterToVec_t<ConstIterator>, ConstIterator>::value,
                      "kodachi::internal::StdVectorToPyList<ConstIterator>, type ConstIterator "
                      "must be of type std::vector<T>::const_iterator.");

        //-----------------------------------------

        pybind11::list pyList { };

        for (auto iter = beginIter; iter != endIter; ++iter) {
            pyList.append(*iter);
        }

        return pyList;
    }

    template <typename T>
    inline void
    pykodachi_bind_vector(pybind11::module& module, const std::string& name)
    {
        pybind11::bind_vector<std::vector<T>, std::shared_ptr<std::vector<T>>>(module, name);
    }

} // namespace pykodachi


// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/op/XFormUtil.h>

#include <OpenEXR/ImathVec.h>
#include <OpenEXR/ImathMatrix.h>

#include <string>
#include <array>
#include <vector>
#include <cmath>

namespace kodachi
{
    namespace internal
    {
        enum class IntersectionTestResult : unsigned int
        {
            INTERSECTS = 0,
            FULLY_OUTSIDE,
            FULLY_INSIDE
        };

        /// @brief Class representing a plane in its implicit form:    n.P + d = 0
        /// Note the memory layout: 7 doubles
        class Plane
        {
        public:
            Imath::V3d mPoint;  // P
            Imath::V3d mNormal; // n
            double mD = 0.0;    // d

            Plane() = default;

            // 3 points on the plane (CCW) that form 2 perpendicular vectors
            Plane(const Imath::V3d& p_1,
                  const Imath::V3d& p_mid, // shared vertex
                  const Imath::V3d& p_2)
                : mPoint(p_mid)
                , mNormal( (p_2 - p_mid).cross(p_1 - p_mid).normalized() )
                , mD( -1.0 * mNormal.dot(mPoint) )
            {
            }

            double
            distance(const Imath::V3d& point) const
            {
                return (point.dot(mNormal) + mD);
            }

            IntersectionTestResult
            AABBTest(const Imath::V3d& aabbCenter,
                     const Imath::V3d& halfVector) const
            {
                const double extent = halfVector.x * std::abs(mNormal.x)
                                    + halfVector.y * std::abs(mNormal.y)
                                    + halfVector.z * std::abs(mNormal.z);

                const double distanceToPlane = distance(aabbCenter);

                if (distanceToPlane - extent > 0.0) {
                    return IntersectionTestResult::FULLY_OUTSIDE;
                }
                else if (distanceToPlane + extent < 0.0) {
                    return IntersectionTestResult::FULLY_INSIDE;
                }

                return IntersectionTestResult::INTERSECTS;
            }

            bool
            atFront(const Imath::V3d& point) const
            {
                return ( (point - mPoint).dot(mNormal) > 0.0 );
            }
        };

        inline double
        degreesToRadians(double degrees)
        {
            constexpr double piOver180 = (M_PI / 180.0);
            return degrees * piOver180;
        }

        inline Imath::M44d
        XFormAttrToIMath(const kodachi::DoubleAttribute& attr, float t = 0.0f)
        {
            if (!attr.isValid()) {
                return Imath::M44d();
            }

            kodachi::DoubleAttribute::array_type v = attr.getNearestSample(t);
            if (v.size() < 16) {
                return Imath::M44d();
            }

            return Imath::M44d((double(*)[4]) v.data());
        }

        class Frustum
        {
        private:
            static constexpr std::size_t sVertexCount = 7; // only 7 out of 8 is needed
            static constexpr std::size_t sPlaneCount  = 6;

            /* To hold the coordinates of 7 vertices needed to form the frustum planes:
             *      [0] near bottom-left
             *      [1] near bottom-right
             *      [2] near top-right
             *      [3] near top-left
             *
             *      [4] far bottom-left
             *      [5] far top-left
             *      [6] far top-right
             */
            kodachi::DoubleAttribute mRawFrustumData;

            /*
             * NOTE: Apply whatever transformations needed BEFORE passing frustum
             *       vertices to this method.
             *
             * Uses the above information to calculate 6 planes (points in CCW order):
             *      Near plane:     near top-left -> near bottom-left -> near bottom-right
             *                      [ 3 ] -> [ 0 ] -> [ 1 ]
             *
             *      Far plane:      far bottom-left -> far top-left -> far top-right
             *                      [ 4 ] -> [ 5 ] -> [ 6 ]
             *
             *      Top plane:      far top-left -> near top-left -> near top-right
             *                      [ 5 ] -> [ 3 ] -> [ 2 ]
             *
             *      Bottom plane:   near bottom-right -> near bottom-left -> far bottom-left
             *                      [ 1 ] -> [ 0 ] -> [ 4 ]
             *
             *      Left plane:     near bottom-left -> near top-left -> far top-left
             *                      [ 0 ] -> [ 3 ] -> [ 5 ]
             *
             *      Right plane:    far top-right -> near top-right -> near bottom-right
             *                      [ 6 ] -> [ 2 ] -> [ 1 ]
             *
             * Fills member variable mPlanes with 6 Plane objects.
             *
             */
            void
            formPlaneEquationsFromVertices(const std::vector<Imath::V3d>& vertices)
            {
                static constexpr std::size_t sFaceIndices[sPlaneCount][3]
                                                      {
                                                        { 3, 0, 1 }, // near
                                                        { 4, 5, 6 }, // far

                                                        { 5, 3, 2 }, // top
                                                        { 1, 0, 4 }, // bottom

                                                        { 0, 3, 5 }, // left
                                                        { 6, 2, 1 }  // right
                                                      };

                for (std::size_t faceIdx = 0; faceIdx < sPlaneCount; ++faceIdx) {
                    mPlanes[faceIdx] = Plane(vertices[sFaceIndices[faceIdx][0]],
                                             vertices[sFaceIndices[faceIdx][1]],
                                             vertices[sFaceIndices[faceIdx][2]]);
                }
            }

        public:
            std::array<Plane, sPlaneCount> mPlanes;

            Frustum() = default;

            /* Ctor, takes in a Katana DoubleAttribute containing 21 double values
             * corresponding to the following points on the frustum:
             *
             *      [0 - 2] near bottom-left
             *      [3 - 5] near bottom-right
             *      [6 - 8] near top-right
             *      [9 - 11] near top-left
             *
             *      [12 - 14] far bottom-left
             *      [15 - 17] far top-left
             *      [18 - 20] far top-right
             *
             * Applies a transformation matrix to these points, then forms the
             * plane equations.
             *
             */
            Frustum(const kodachi::DoubleAttribute& rawFrustumData,
                    const Imath::M44d& xform)
            {
                const kodachi::DoubleAttribute::array_type rawData =
                        rawFrustumData.getNearestSample(0.0f);

                std::vector<Imath::V3d> vertices(sVertexCount);
                std::memcpy(vertices.data(), rawData.data(), sVertexCount * sizeof(Imath::V3d));

                // Apply xform
                for (auto& vertex : vertices) {
                    vertex *= xform;
                }

                formPlaneEquationsFromVertices(vertices);
            }

            // Returns a copy of data held by mRawFrustumData as an array of
            // 21 doubles:
            //      7 vertices x sizeof(Imath::V3d) / sizeof(double) = 7 x 24 / 8 = 21
            kodachi::DoubleAttribute
            getAsFnDoubleAttribute() const
            {
                return mRawFrustumData;
            }

            static Imath::M44d
            calculatePerspectiveProjectionMatrix(double near, double far,
                                                 double left, double right,
                                                 double bottom, double top)
            {
                const double near_x_2 = 2.0 * near;
                const double r_min_l_inv = 1.0 / (right - left);
                const double t_min_b_inv = 1.0 / (top - bottom);
                const double f_min_n_inv = 1.0 / (far - near);

                return { near_x_2 * r_min_l_inv,
                         0.0,
                         0.0,
                         0.0,

                         0.0,
                         near_x_2 * t_min_b_inv,
                         0.0,
                         0.0,

                         -1.0 * (right + left) * r_min_l_inv,
                         -1.0 * (top + bottom) * t_min_b_inv,
                         (far + near) * f_min_n_inv,
                         1.0,

                         0.0,
                         0.0,
                         -2.0 * far * near * f_min_n_inv,
                         0.0 };
            }

            // calculate frustum vertices from given camera attrs
            // padding can be optionally specified to offset the frustum
            static kodachi::DoubleAttribute
            calculateFrustumVertices(const kodachi::GroupAttribute& cameraAttrs,
                                     float padding = 1.0f)
            {
                const kodachi::DoubleAttribute fovAttr    = cameraAttrs.getChildByName("fov");
                const kodachi::DoubleAttribute nearAttr   = cameraAttrs.getChildByName("near");
                const kodachi::DoubleAttribute farAttr    = cameraAttrs.getChildByName("far");
                const kodachi::DoubleAttribute leftAttr   = cameraAttrs.getChildByName("left");
                const kodachi::DoubleAttribute rightAttr  = cameraAttrs.getChildByName("right");
                const kodachi::DoubleAttribute bottomAttr = cameraAttrs.getChildByName("bottom");
                const kodachi::DoubleAttribute topAttr    = cameraAttrs.getChildByName("top");

                if (!fovAttr.isValid()
                        || !nearAttr.isValid() || !farAttr.isValid() || !leftAttr.isValid()
                        || !rightAttr.isValid() || !bottomAttr.isValid() || !topAttr.isValid()) {
                    return { }; // need all of the above to form a frustum
                }

                const double near   = nearAttr.getValue();
                const double far    = farAttr.getValue();
                const double left   = leftAttr.getValue() - padding;
                const double right  = rightAttr.getValue() + padding;
                const double bottom = bottomAttr.getValue() - padding;
                const double top    = topAttr.getValue() + padding;

                const kodachi::StringAttribute projectionAttr =
                        cameraAttrs.getChildByName("projection");
                float slope, scale;
                const bool ortho = (projectionAttr == "orthographic");
                if (ortho) {
                    const kodachi::DoubleAttribute orthoWidthAttr =
                            cameraAttrs.getChildByName("orthographicWidth");
                    slope = 0;
                    scale = orthoWidthAttr.getValue() / fabs(right - left);
                } else {
                    const kodachi::DoubleAttribute fovAttr = cameraAttrs.getChildByName("fov");
                    const double fov = fovAttr.getValue() * M_PI / 180;
                    slope = tan(fov / 2);
                    scale = 1;
                }

                std::vector<Imath::V3d> vertices;
                vertices.reserve(sVertexCount);

                if (!ortho) scale = near*slope;
                vertices.emplace_back(scale*left, scale*bottom, -near);
                vertices.emplace_back(scale*right, scale*bottom, -near);
                vertices.emplace_back(scale*right, scale*top, -near);
                vertices.emplace_back(scale*left, scale*top, -near);

                if (!ortho) scale = far*slope;
                vertices.emplace_back(scale*left, scale*bottom, -far);
                vertices.emplace_back(scale*left, scale*top, -far);
                vertices.emplace_back(scale*right, scale*top, -far);
                vertices.emplace_back(scale*right, scale*bottom, -far);

                return kodachi::DoubleAttribute(
                        reinterpret_cast<const double*>(vertices.data()),
                        // NOTE: ignore the last vertex (far bottom-right), not needed here.
                        sVertexCount * 3,
                        1);
            }

            IntersectionTestResult
            AABBIntersection(const Imath::V3d& aabbMin, const Imath::V3d& aabbMax) const
            {
                const Imath::V3d aabbCenter = (aabbMax + aabbMin) * 0.5;
                const Imath::V3d halfVector = (aabbMax - aabbMin) * 0.5;

                bool intersecting = false;
                for (std::size_t faceIdx = 0; faceIdx < sPlaneCount; ++faceIdx) {
                    const IntersectionTestResult result =
                            mPlanes[faceIdx].AABBTest(aabbCenter, halfVector);

                    if (result == IntersectionTestResult::FULLY_OUTSIDE) {
                        return IntersectionTestResult::FULLY_OUTSIDE;
                    }
                    else if (result == IntersectionTestResult::INTERSECTS) {
                        intersecting = true;
                    }
                }

                if (intersecting) {
                    return IntersectionTestResult::INTERSECTS;
                }

                return IntersectionTestResult::FULLY_INSIDE;
            }

            bool
            containsPoint(const Imath::V3d& point) const
            {
                // A point must be behind all 6 frustum faces to be completely inside
                // the frustum.
                for (std::size_t faceIdx = 0; faceIdx < sPlaneCount; ++faceIdx) {
                    if (mPlanes[faceIdx].atFront(point)) {
                        return false;
                    }
                }

                return true;
            }
        };

        class Mesh
        {
        private:
            enum Sides { INSIDE = -1, BOTH = 0, OUTSIDE = 1 };

            // Projects the given points onto a vector, D
            template<class T>
            static int
            WhichSide(const std::vector<Imath::Vec3<T>>& S, const Imath::V3d& D, const Imath::V3d& P)
            {
                unsigned int pos = 0;
                unsigned int neg = 0;
                for (unsigned int i = 0; i < S.size(); ++i) {
                    const double t = D.dot(S[i] - P);

                    if (t > 0) {
                        pos++;
                    }
                    else if (t < 0) {
                        neg++;
                    }

                    if (pos && neg) {
                        return BOTH;
                    }
                }
                return (pos ? OUTSIDE : INSIDE);
            }

            template<class T>
            static int
            WhichSide(const kodachi::array_view<Imath::Vec3<T>>& S, const Imath::V3d& D, const Imath::V3d& P)
            {
                unsigned int pos = 0;
                unsigned int neg = 0;
                for (unsigned int i = 0; i < S.size(); ++i) {
                    const double t = D.dot(S[i] - P);

                    if (t > 0) {
                        pos++;
                    }
                    else if (t < 0) {
                        neg++;
                    }

                    if (pos && neg) {
                        return BOTH;
                    }
                }
                return (pos ? OUTSIDE : INSIDE);
            }

        public:
            std::vector<Imath::V3d> points;
            kodachi::IntAttribute::array_type verts;
            kodachi::IntAttribute::array_type faceIndices;

            std::size_t
            faceCount() const
            {
                return faceIndices.size() - 1;
            }

            Imath::V3d
            getFaceNormal(unsigned faceId) const
            {
                const Imath::V3d& p1 = points[verts[faceIndices[faceId]]];
                const Imath::V3d& p2 = points[verts[faceIndices[faceId]+1]];
                const Imath::V3d& p3 = points[verts[faceIndices[faceId]+2]];

                const Imath::V3d u = p1 - p2;
                const Imath::V3d v = p3 - p2;

                return Imath::V3d(u.y * v.z - u.z * v.y,
                                  u.z*v.x - u.x*v.z,
                                  u.x*v.y - u.y*v.x).normalized();
            }

            const Imath::V3d&
            getFaceVertex(const unsigned faceId) const
            {
                return points[verts[faceIndices[faceId]]];
            }

            bool
            doesIntersect(const Mesh& target) const
            {
                const Mesh* smallMesh = nullptr;
                const Mesh* bigMesh = nullptr;

                // Test faces from the smaller mesh first
                if (this->faceCount() < target.faceCount()) {
                    smallMesh = this;
                    bigMesh = &target;
                }
                else {
                    smallMesh = &target;
                    bigMesh = this;
                }

                // Loop over each face in box A
                for (unsigned i = 0; i < smallMesh->faceCount(); ++i) {
                    Imath::V3d D = smallMesh->getFaceNormal(i);
                    if (WhichSide(bigMesh->points, D, smallMesh->getFaceVertex(i)) > 0) {
                        return false;
                    }
                }

                // Loop over each face in box B
                for (unsigned i = 0; i < bigMesh->faceCount(); ++i) {
                    Imath::V3d D = bigMesh->getFaceNormal(i);
                    if (WhichSide(smallMesh->points, D, bigMesh->getFaceVertex(i)) > 0) {
                        return false;
                    }
                }
                return true;
            }

            // intersection with array of points
            bool
            doesIntersect(const kodachi::array_view<Imath::V3f> points) const
            {
                 for (unsigned i = 0; i < faceCount(); ++i) {
                     Imath::V3d D = getFaceNormal(i);
                     if (WhichSide(points, D, getFaceVertex(i)) > 0) {
                         return false;
                     }
                 }
                 return true;
            }

            void
            transformMesh(const Imath::M44d& xform)
            {
                for (auto& pt : points) {
                    pt *= xform;
                }
            }
        };

        bool
        GetTransformedBoundAsMesh(kodachi::internal::Mesh& outMesh,
                                  const kodachi::DoubleAttribute& boundsAttr,
                                  const kodachi::GroupAttribute& xformAttr)
        {
            if (boundsAttr.isValid()) {
                const kodachi::DoubleAttribute::array_type bound = boundsAttr.getNearestSample(0.0f);

                if (bound.size() != 6) {
                    return false;
                }

                for (std::size_t i = 0; i < 6; ++i) {
                    if (isinf(bound[i]) || isnan(bound[i])) {
                        return false;
                    }
                }

                const kodachi::DoubleAttribute matrix = kodachi::RemoveTimeSamplesIfAllSame(
                    kodachi::RemoveTimeSamplesUnneededForShutter(
                        kodachi::XFormUtil::CalcTransformMatrixAtExistingTimes(
                            xformAttr).first, 0, 0));

                const Imath::M44d xform = kodachi::internal::XFormAttrToIMath(matrix, 0.0f);

                const Imath::V3d minVector = Imath::V3d(bound[0], bound[2], bound[4]);
                const Imath::V3d maxVector = Imath::V3d(bound[1], bound[3], bound[5]);

                outMesh.points.reserve(8);
                outMesh.points.push_back(Imath::V3d(minVector.x, minVector.y, maxVector.z) * xform);
                outMesh.points.push_back(Imath::V3d(maxVector.x, minVector.y, maxVector.z) * xform);
                outMesh.points.push_back(Imath::V3d(minVector.x, maxVector.y, maxVector.z) * xform);
                outMesh.points.push_back(Imath::V3d(maxVector.x, maxVector.y, maxVector.z) * xform);
                outMesh.points.push_back(Imath::V3d(minVector.x, maxVector.y, minVector.z) * xform);
                outMesh.points.push_back(Imath::V3d(maxVector.x, maxVector.y, minVector.z) * xform);
                outMesh.points.push_back(Imath::V3d(minVector.x, minVector.y, minVector.z) * xform);
                outMesh.points.push_back(Imath::V3d(maxVector.x, minVector.y, minVector.z) * xform);

                static const kodachi::IntAttribute::value_type sBBoxVertexList[24] = { 2, 3, 1, 0,
                                                                                     4, 5, 3, 2,
                                                                                     6, 7, 5, 4,
                                                                                     0, 1, 7, 6,
                                                                                     3, 5, 7, 1,
                                                                                     4, 2, 0, 6 };

                static const kodachi::IntAttribute::value_type sBBoxFaceIndices[7] = {0, 4, 8, 12, 16, 20, 24};

                static const kodachi::IntAttribute sBBoxVertexListAttr(sBBoxVertexList, 24, 1);
                static const kodachi::IntAttribute sBBoxFaceIndicesAttr(sBBoxFaceIndices, 7, 1);

                outMesh.verts = kodachi::IntAttribute::array_type(
                        kodachi::IntAttribute::array_type(sBBoxVertexListAttr, 0.0f));
                outMesh.faceIndices = kodachi::IntAttribute::array_type(
                        kodachi::IntAttribute::array_type(sBBoxFaceIndicesAttr, 0.0f));

                return true;
            }
            return false;
        }

        // Converts the specified location's worldspace bound into a Mesh instance.
        bool
        GetTransformedBoundAsMesh(kodachi::GeolibCookInterface& interface,
                                  kodachi::internal::Mesh& outMesh,
                                  const std::string location = "")
        {
            return GetTransformedBoundAsMesh(outMesh,
                                             interface.getAttr("bound", location),
                                             kodachi::GetGlobalXFormGroup(interface, location));
        }

        bool
        GetTransformedMesh(kodachi::internal::Mesh& outMesh,
                           const kodachi::GroupAttribute& geometryAttr,
                           const kodachi::GroupAttribute& xformAttr)
        {
            const kodachi::FloatAttribute pAttr = geometryAttr.getChildByName("point.P");
            const kodachi::IntAttribute vertexListAttr = geometryAttr.getChildByName("poly.vertexList");
            const kodachi::IntAttribute startIndexAttr = geometryAttr.getChildByName("poly.startIndex");

            if (pAttr.isValid() && vertexListAttr.isValid() && startIndexAttr.isValid()) {
                kodachi::DoubleAttribute matrix = kodachi::RemoveTimeSamplesIfAllSame(
                        kodachi::RemoveTimeSamplesUnneededForShutter(
                                kodachi::XFormUtil::CalcTransformMatrixAtExistingTimes(
                                        xformAttr).first, 0, 0));

                const Imath::M44d xform = kodachi::internal::XFormAttrToIMath(matrix, 0.0f);

                const kodachi::FloatAttribute::array_type points = pAttr.getNearestSample(0);

                outMesh.points.reserve(pAttr.getNumberOfValues());
                for (std::size_t i = 0; i < pAttr.getNumberOfTuples(); ++i) {
                    const std::size_t idx = i * 3;
                    outMesh.points.push_back(Imath::V3d(points[idx], points[idx + 1], points[idx + 2]) * xform);
                }

                outMesh.verts = vertexListAttr.getNearestSample(0.0f);
                outMesh.faceIndices = startIndexAttr.getNearestSample(0.0f);

                return true;
            }
            return false;
        }

        // Gets the Mesh instance for the specified location in worldspace.
        bool
        GetTransformedMesh(kodachi::GeolibCookInterface &interface,
                           kodachi::internal::Mesh& outMesh,
                           const std::string location = "")
        {
            return GetTransformedMesh(outMesh, interface.getAttr("geometry", location),
                    kodachi::GetGlobalXFormGroup(interface, location));
        }

    } // namespace internal
} // namespace kodachi


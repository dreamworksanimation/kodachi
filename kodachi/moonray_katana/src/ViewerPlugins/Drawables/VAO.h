// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>
#include <map>
#include <FnViewer/plugin/FnMathTypes.h>
#include <GL/gl.h>

typedef struct __GLXcontextRec* GLXContext;
using Foundry::Katana::ViewerAPI::Vec3f;

namespace MoonrayKatana
{

/**
 * Maintain an OpenGL VertexArrayObject (VAO), and the VertexBufferObjects (VBOs) that
 * it uses. A new VAO is created per OpenGL context as they cannot be shared. This
 * assumes VBOs are shared amoung all contexts.
 *
 * This can be used to draw Manipulator handle elements, such as lines, points
 * or meshes.
 *
 * Taken from Foundry example code. Modified to work with mulitple GL contexts and
 * to remove code unrelated to VAOs.
 */
class VAO
{
    /// Buffer types
    enum BUFFER {
        VERTEX_BUFFER = 0,
        NORMAL_BUFFER,
        INDEX_BUFFER,
        NUM_BUFFERS
    };

public:
    /// Constructor
    VAO() {}

    /// Destructor
    ~VAO() { cleanup(); }
    void cleanup();

    /// True if setup() has been called (and cleanup() not called)
    bool isReady() const { return m_vbo[VERTEX_BUFFER]; }

    /// Sets up (or replace) the vertex buffers
    void setup(const float* v, const float* n, size_t numV, const unsigned* i, size_t numI);
    void setup(const float* v, size_t numV, const unsigned* i, size_t numI) { setup(v,0,numV,i,numI); }
    void setup(
        const std::vector<Vec3f>& v,
        const std::vector<Vec3f>& n,
        const std::vector<unsigned int>& i)
    {
        setup(&v[0].x, n.empty() ? nullptr : &n[0].x, v.size(), &i[0], i.size());
    }
    void setup(const std::vector<Vec3f>& v, const std::vector<unsigned int>& i) {
        setup(&v[0].x, nullptr, v.size(), &i[0], i.size());
    }
    /// Overwrite vertex buffers (these cannot resize them!)
    void updateVertices(const float* v, size_t numV);
    void updateVertices(const std::vector<Vec3f>& v) { updateVertices(&v[0].x, v.size()); }
    void updateNormals(const float* n, size_t numV);
    void updateNormals(const std::vector<Vec3f>& n) { updateNormals(&n[0].x, n.size()); }

    /// Draw using any of GL_POINTS, GL_LINE_STRIP, GL_LINE_LOOP, GL_LINES,
    /// GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_TRIANGES, GL_QUAD_STRIP, GL_QUADS,
    /// or GL_POLYGON
    void draw(GLenum mode = GL_TRIANGLES) const;
    /// Only draw the first N points
    void draw(GLenum mode, unsigned n) const;

    void drawLines() const { draw(GL_LINES); }

    // copy is not allowed! It would double-delete the vbos
    VAO(const VAO&) = delete;
    VAO& operator=(const VAO&) = delete;
    // But move construction and assignment work:
    VAO(VAO&& a) {*this = std::move(a);}
    VAO& operator=(VAO&&);

private:
    mutable std::map<GLXContext, unsigned> vaomap;
    unsigned m_vbo[NUM_BUFFERS] = {0};
    unsigned m_numIndices = 0;
    bool normals = false;
};

}


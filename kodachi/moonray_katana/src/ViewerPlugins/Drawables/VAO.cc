// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "VAO.h"

extern "C" {
    extern GLXContext glXGetCurrentContext( void );
};

// Taken from Foundry example code. Helper class
// to render a mesh from VAOBuilder using
// vertex arrays. Modified to work with mulitple GL contexts.

namespace MoonrayKatana
{

VAO& VAO::operator=(VAO&& a) {
    std::swap(vaomap, a.vaomap);
    std::swap(m_vbo[VERTEX_BUFFER], a.m_vbo[VERTEX_BUFFER]);
    std::swap(m_vbo[NORMAL_BUFFER], a.m_vbo[NORMAL_BUFFER]);
    std::swap(m_vbo[INDEX_BUFFER], a.m_vbo[INDEX_BUFFER]);
    std::swap(m_numIndices, a.m_numIndices);
    std::swap(normals, a.normals);
    return *this;
}

// Restore to the pre-setup() state where it will not draw
void VAO::cleanup()
{
    if (glXGetCurrentContext()) { // don't crash on exit
        glDeleteBuffers(NUM_BUFFERS, &m_vbo[0]);
        unsigned& vao(vaomap[glXGetCurrentContext()]);
        if (glIsVertexArray(vao))
            glDeleteVertexArrays(1, &vao);
        // VAOs in other contexts are leaked, not much can be done about that...
    }

    m_vbo[VERTEX_BUFFER] = 0;
    m_vbo[NORMAL_BUFFER] = 0;
    m_vbo[INDEX_BUFFER] = 0;
    normals = false;
    vaomap.clear();
}

// Create or replace all the VBO's with new ones provided here
void
VAO::setup(const float* vertices, const float* normals, size_t numV,
           const unsigned* indices, size_t numI)
{
    if (not m_vbo[0]) glGenBuffers(NUM_BUFFERS, &m_vbo[0]);

    // Fill the vertex buffer
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo[VERTEX_BUFFER]);
    glBufferData(GL_ARRAY_BUFFER, numV * 3 * sizeof(float), vertices, GL_STATIC_DRAW);

    // Fill the normal buffer
    if (normals) {
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo[NORMAL_BUFFER]);
        glBufferData(GL_ARRAY_BUFFER, numV * 3 * sizeof(float), normals, GL_STATIC_DRAW);
        this->normals = true;
    } else {
        this->normals = false;
    }

    // Fill the index buffer
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_vbo[INDEX_BUFFER]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, numI * sizeof(unsigned), indices, GL_STATIC_DRAW);
    m_numIndices = numI;

    // Unbind buffers
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void
VAO::updateVertices(const float* vertices, size_t numV)
{
    if (!isReady()) return;
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo[VERTEX_BUFFER]);
    glBufferData(GL_ARRAY_BUFFER, numV * 3 * sizeof(float), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void
VAO::updateNormals(const float* normals, size_t numV)
{
    if (!isReady() || !this->normals) return;
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo[NORMAL_BUFFER]);
    glBufferData(GL_ARRAY_BUFFER, numV * 3 * sizeof(float), normals, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void
VAO::draw(GLenum mode) const
{
    draw(mode, m_numIndices);
}

void
VAO::draw(GLenum mode, unsigned n) const
{
    if (not isReady() || not n) return;

    // This function actually creats the VAO (per context) from the VBO's created
    // by setup().
    unsigned& vao(vaomap[glXGetCurrentContext()]);
    if (vao) {
        glBindVertexArray(vao);
    } else {
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo[VERTEX_BUFFER]);
        glEnableVertexAttribArray(VERTEX_BUFFER);
        glVertexAttribPointer(VERTEX_BUFFER, 3, GL_FLOAT, GL_FALSE, 0, 0);
        if (normals) {
            glBindBuffer(GL_ARRAY_BUFFER, m_vbo[NORMAL_BUFFER]);
            glEnableVertexAttribArray(NORMAL_BUFFER);
            glVertexAttribPointer(NORMAL_BUFFER, 3, GL_FLOAT, GL_FALSE, 0, 0);
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_vbo[INDEX_BUFFER]);
    }

    glDrawElements(mode, n, GL_UNSIGNED_INT, 0);

    glBindVertexArray(0);
}

}


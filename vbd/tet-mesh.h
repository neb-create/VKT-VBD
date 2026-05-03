#pragma once
#include "defines.h"
#include "half-edge.h"
#include <vector>
#include <array>
#include "tetgen.h"

using namespace glm;

struct Tet {
    HVertex* v[4];
    mat3 DmInv; // rest pose inverse
};

class TetMesh {
    vector<uPtr<HVertex>> vertices;
    vector<Tet> tets;
    vector<array<uint32_t, 3>> surfaceTris;

    // Build from TetGen output
    void FromTetgenOutput(const tetgenio& out);

    // Precompute DmInv for each tet from current positions (call once at rest pose)
    void ComputeRestPose();

    // Extract surface as flat arrays for rendering
    void ToRenderArrays(vector<vec3>* outPositions, vector<vec3>* outNormals, vector<uint32_t>* outIndices);

	// Convert half-edge mesh
    void FromHalfEdge(const HalfEdgeMesh& heMesh);
    uPtr<HalfEdgeMesh> ToHalfEdge() const;

};

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


public:

    vector<uPtr<HVertex>> vertices;
    vector<Tet> tets;
    vector<array<uint32_t, 3>> surfaceTris;

    unordered_map<uint64_t, float> restLengths;
    unordered_map<uint32_t, vector<HVertex*>> vertexNeighbors;
    unordered_map<uint32_t, vector<Tet*>> vertexTets;

    void PreCompute();

    TetMesh() = default;
    TetMesh(const TetMesh&);

	friend class VBDSolver;

    // Build from TetGen output
    void FromTetgenOutput(const tetgenio& out);

    // Extract surface as flat arrays for rendering
    void ToRenderArrays(vector<vec3>* outPositions, vector<vec3>* outNormals, vector<uint32_t>* outIndices);

	// Convert half-edge mesh
    void FromHalfEdge(const HalfEdgeMesh& heMesh);
    uPtr<HalfEdgeMesh> ToHalfEdge() const;

    uPtr<Mesh> convertToMesh(const VulkanReferences& ref);

};

int VertexPairID(HVertex* a, HVertex* b);
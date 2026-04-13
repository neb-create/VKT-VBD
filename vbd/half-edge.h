#pragma once

#include "defines.h"
#include "scene/mesh.h"

using namespace std;
using namespace glm;

struct HVertex;
struct Face;
struct HalfEdge;

struct HVertex {
    uint id;
    
    vec3 pos;
    HalfEdge* incomingEdge;

    vec3 vel;
};

struct HalfEdge {
    uint id;

    HalfEdge* next;
    HalfEdge* sym;
    HVertex* nextVertex;
    Face* face;
};

struct Face {
    uint id;

    vec3 color;

    HalfEdge* edge;
};

class VBDSolver;

class HalfEdgeMesh {
public:
    HalfEdgeMesh(const HalfEdgeMesh&);
    HalfEdgeMesh();
    
    void LoadFromOBJ(const string&);
    uPtr<Mesh> convertToMesh(const VulkanReferences&);
private:
    friend class VBDSolver;

    uint pointCountBound;
    vector<uPtr<HVertex>> vertices;
    vector<uPtr<HalfEdge>> halfEdges;
    vector<uPtr<Face>> faces;

    inline Face* addFace() {
        faces.push_back(mkU<Face>());
        uint id = faces.size() - 1;
        faces[id]->id = id;
        faces[id]->color = vec3(1.0, 0.3, 0.5);
        return faces[id].get();
    }
    inline HalfEdge* addEdge() {
        halfEdges.push_back(mkU<HalfEdge>());
        uint id = halfEdges.size() - 1;
        halfEdges[id]->id = id;
        return halfEdges[id].get();
    }
    inline HVertex* addVertex(vec3 pos) {
        vertices.push_back(mkU<HVertex>());

        HVertex* v = vertices[vertices.size() - 1].get();
        v->pos = pos;
        v->id = vertices.size() - 1;
        v->vel = vec3(0.0f); // TODO: initial vel could be attrib or parameter

        return v;
    }

    inline uint vertexPairToID(HVertex* v1, HVertex* v2) {
        return glm::min(v1->id, v2->id) * pointCountBound + glm::max(v1->id, v2->id);
    }

    //
    void LoadVertex(const string&);
    void LoadFace(const string&, unordered_map<int, HalfEdge*>* vertexEndPointToEdge);
};

void TriangulateConvexFace(Face* f, vector<vec3>* positions, vector<vec3>* colors, vector<vec3>* normals, vector<uint32_t>* indices);

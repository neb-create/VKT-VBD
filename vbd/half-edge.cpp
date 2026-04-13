#include "half-edge.h"
#include <iostream>
#include <fstream>

HalfEdgeMesh::HalfEdgeMesh(const HalfEdgeMesh& other) {
    this->pointCountBound = other.pointCountBound;

    unordered_map<int, HalfEdge*> halfEdgeIDToPtr;
    unordered_map<int, Face*> faceIDToPtr;
    unordered_map<int, HVertex*> vertexIDToPtr;

    for (const uPtr<HVertex>& oVert : other.vertices) {
        vertices.push_back(mkU<HVertex>(*oVert));
        vertexIDToPtr[vertices[vertices.size() - 1]->id] = vertices[vertices.size() - 1].get();
    }
    for (const uPtr<HalfEdge>& oEdge : other.halfEdges) {
        halfEdges.push_back(mkU<HalfEdge>(*oEdge));
        halfEdgeIDToPtr[halfEdges[halfEdges.size() - 1]->id] = halfEdges[halfEdges.size() - 1].get();
    }
    for (const uPtr<Face>& oFace : other.faces) {
        faces.push_back(mkU<Face>(*oFace));
        faceIDToPtr[faces[faces.size() - 1]->id] = faces[faces.size() - 1].get();
    }

    for (uPtr<HVertex>& v : vertices) {
        v->incomingEdge = halfEdgeIDToPtr[v->incomingEdge->id];
    }
    for (uPtr<HalfEdge>& h : halfEdges) {
        h->next = halfEdgeIDToPtr[h->next->id];
        h->sym = halfEdgeIDToPtr[h->sym->id];
        h->nextVertex = vertexIDToPtr[h->nextVertex->id];
        h->face = faceIDToPtr[h->face->id];
    }
    for (uPtr<Face>& f : faces) {
        f->edge = halfEdgeIDToPtr[f->edge->id];
    }
}

HalfEdgeMesh::HalfEdgeMesh() {}

uPtr<Mesh> HalfEdgeMesh::convertToMesh(const VulkanReferences& ref) {
    uPtr<Mesh> mesh = mkU<Mesh>();
    
    vector<vec3> positions;
    vector<vec3> colors;
    vector<vec3> normals;
    vector<uint32_t> indices;

    // Triangulate
    for (const auto& face : faces) {
        TriangulateConvexFace(face.get(), &positions, &colors, &normals, &indices);
    }

    // Create
    mesh->CreateFromArrays(ref, positions, colors, normals, indices);

    return std::move(mesh);
}

void TriangulateConvexFace(Face* f, vector<vec3>* positions, vector<vec3>* colors, vector<vec3>* normals, vector<uint32_t>* indices) {

    uint sideCount = 0;

    vec3 faceNormal = normalize(cross(
        f->edge->next->nextVertex->pos - f->edge->nextVertex->pos,
        f->edge->next->next->nextVertex->pos - f->edge->nextVertex->pos));

    HalfEdge* startEdge = f->edge;
    HalfEdge* currEdge = startEdge;
    do {
        positions->push_back(currEdge->nextVertex->pos);
        colors->push_back(f->color);
        normals->push_back(faceNormal);
        currEdge = currEdge->next;
        sideCount++;
    } while (currEdge != startEdge);

    for (int i = 0; i < sideCount - 2; i++) {
        indices->push_back(positions->size() - sideCount);
        indices->push_back(positions->size() - sideCount + i + 1);
        indices->push_back(positions->size() - sideCount + i + 2);
    }

}

//
vector<string> split(string s, char delimeter) {
    vector<string> stringVec = vector<string>();

    size_t prevIndex = 0;
    size_t currIndex = s.find(delimeter, prevIndex);
    while (currIndex != string::npos) {
        stringVec.push_back(s.substr(prevIndex, currIndex - prevIndex));

        prevIndex = currIndex + 1;
        currIndex = s.find(delimeter, prevIndex);
    }

    // Add the last segment of the string if there's anything left to add
    if (prevIndex < s.length()) {
        stringVec.push_back(s.substr(prevIndex, s.length() - prevIndex));
    }

    return stringVec;
}


void HalfEdgeMesh::LoadVertex(const string& vertexLine) {
    vector<string> coords = split(vertexLine, ' ');
    vec3 v = vec3(stof(coords[1]), stof(coords[2]), stof(coords[3]));
    addVertex(v);
}

void HalfEdgeMesh::LoadFace(const string& faceLine, unordered_map<int, HalfEdge*>* vertexEndPointToEdge) {
    int prevVertIndex = -1;

    Face* face = addFace();
    HalfEdge* prevHalfEdge = nullptr;
    HalfEdge* firstHalfEdge = nullptr;

    vector<string> triplets = split(faceLine.substr(2), ' ');
    triplets.push_back(triplets.at(0)); // Want to iterate in a cycle
    for (string triplet : triplets) {

        string vertIndexString{ triplet.substr(0, triplet.find('/')) };
        int vertIndex = std::stoi(vertIndexString) - 1;

        if (prevVertIndex != -1) {
            // Create half edge
            HalfEdge* edge = addEdge();
            edge->nextVertex = vertices[vertIndex].get();
            vertices[vertIndex]->incomingEdge = edge;
            edge->face = face; face->edge = edge;
            if (prevHalfEdge != nullptr) {
                prevHalfEdge->next = edge;
            }
            else {
                firstHalfEdge = edge;
            }

            // Set sym or prepare for other edge to sym to me
            int vertexPairID = std::min(vertIndex, prevVertIndex) * vertices.size() + std::max(vertIndex, prevVertIndex);
            if (vertexEndPointToEdge->contains(vertexPairID)) {
                edge->sym = vertexEndPointToEdge->at(vertexPairID);
                vertexEndPointToEdge->at(vertexPairID)->sym = edge;
            }
            else {
                (*vertexEndPointToEdge)[vertexPairID] = edge;
            }

            // Update prev half edge
            prevHalfEdge = edge;
        }

        prevVertIndex = vertIndex;

    }

    // Link into cycle
    prevHalfEdge->next = firstHalfEdge;
    face->edge = firstHalfEdge;
}

void HalfEdgeMesh::LoadFromOBJ(const string& fileName) {
    ifstream f(fileName);

    if (!f.is_open()) {
        cerr << "Failed to open file: " << fileName << endl;
        return;
    }

    // The int represents a pair of vertices: I use the indices of the vertices and the total num of vertices to get a unique pair ID
    unordered_map<int, HalfEdge*> vertexEndPointToEdge;

    string line;
    while (getline(f, line)) {
        if (line.length() == 0) {
            continue;
        }
        else if (line.starts_with("v ")) {
            LoadVertex(line);
        }
        else if (line.starts_with("f ")) {
            LoadFace(line, &vertexEndPointToEdge);
        }
    }

    f.close();
}
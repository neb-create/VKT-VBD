#include "tet-mesh.h"

void TetMesh::FromTetgenOutput(const tetgenio& out) {
    vertices.clear();
    tets.clear();
    surfaceTris.clear();

    // Vertices
    for (int i = 0; i < out.numberofpoints; i++) {
        auto v = mkU<HVertex>();
        v->pos = vec3(
            (float)out.pointlist[i * 3 + 0],
            (float)out.pointlist[i * 3 + 1],
            (float)out.pointlist[i * 3 + 2]
        );
        v->id = i;
        vertices.push_back(std::move(v));
    }

    // Tets
    for (int i = 0; i < out.numberoftetrahedra; i++) {
        Tet t;
        for (int j = 0; j < 4; j++) {
            t.v[j] = vertices[out.tetrahedronlist[i * 4 + j]].get();
        }
        tets.push_back(t);
    }

    // Surface triangles
    for (int i = 0; i < out.numberoftrifaces; i++) {
        surfaceTris.push_back({
            (uint32_t)out.trifacelist[i * 3 + 0],
            (uint32_t)out.trifacelist[i * 3 + 1],
            (uint32_t)out.trifacelist[i * 3 + 2]
            });
    }

    ComputeRestPose();
}

void TetMesh::ComputeRestPose() {
    for (Tet& t : tets) {
        // Dm = [v1-v0, v2-v0, v3-v0]
        vec3 col0 = t.v[1]->pos - t.v[0]->pos;
        vec3 col1 = t.v[2]->pos - t.v[0]->pos;
        vec3 col2 = t.v[3]->pos - t.v[0]->pos;
        mat3 Dm = mat3(col0, col1, col2);
        t.DmInv = glm::inverse(Dm);
    }
}

void TetMesh::ToRenderArrays(vector<vec3>* outPositions, vector<vec3>* outNormals, vector<uint32_t>* outIndices) {
    outPositions->clear();
    outNormals->clear();
    outIndices->clear();

    for (const auto& tri : surfaceTris) {
        vec3 a = vertices[tri[0]]->pos;
        vec3 b = vertices[tri[1]]->pos;
        vec3 c = vertices[tri[2]]->pos;

        vec3 normal = normalize(cross(b - a, c - a));

        uint32_t base = outPositions->size();
        outPositions->push_back(a);
        outPositions->push_back(b);
        outPositions->push_back(c);
        outNormals->push_back(normal);
        outNormals->push_back(normal);
        outNormals->push_back(normal);
        outIndices->push_back(base);
        outIndices->push_back(base + 1);
        outIndices->push_back(base + 2);
    }
}

void TetMesh::FromHalfEdge(const HalfEdgeMesh& heMesh) {
    // Extract vertices and indices from half-edge mesh
    vector<vec3> positions;
    vector<vec3> normals;
    vector<uint32_t> indices;
    const_cast<HalfEdgeMesh&>(heMesh).TriangleMeshToVertexIndices(&positions, &normals, &indices);

    // Fill TetGen input
    tetgenio in, out;
    in.numberofpoints = positions.size();
    in.pointlist = new REAL[in.numberofpoints * 3];
    for (int i = 0; i < positions.size(); i++) {
        in.pointlist[i * 3 + 0] = positions[i].x;
        in.pointlist[i * 3 + 1] = positions[i].y;
        in.pointlist[i * 3 + 2] = positions[i].z;
    }

    in.numberoffacets = indices.size() / 3;
    in.facetlist = new tetgenio::facet[in.numberoffacets];
    in.facetmarkerlist = new int[in.numberoffacets];
    for (int i = 0; i < in.numberoffacets; i++) {
        tetgenio::facet& f = in.facetlist[i];
        f.numberofpolygons = 1;
        f.polygonlist = new tetgenio::polygon[1];
        f.numberofholes = 0;
        f.holelist = nullptr;
        tetgenio::polygon& p = f.polygonlist[0];
        p.numberofvertices = 3;
        p.vertexlist = new int[3];
        p.vertexlist[0] = indices[i * 3 + 0];
        p.vertexlist[1] = indices[i * 3 + 1];
        p.vertexlist[2] = indices[i * 3 + 2];
        in.facetmarkerlist[i] = 0;
    }

    tetgenbehavior b;
    b.parse_commandline((char*)"pq1.4Y");
    tetrahedralize(&b, &in, &out);

    // Build TetMesh from output
    FromTetgenOutput(out);
}

uPtr<HalfEdgeMesh> TetMesh::ToHalfEdge() const {
    auto heMesh = mkU<HalfEdgeMesh>();
    unordered_map<int, HalfEdge*> vertexPairToEdge;

    // Add vertices
    for (const auto& v : vertices) {
        auto newVert = mkU<HVertex>();
        newVert->pos = v->pos;
        newVert->id = v->id;
        heMesh->vertices.push_back(std::move(newVert));
    }

    // Add faces from surface tris
    for (const auto& tri : surfaceTris) {
        Face* face = heMesh->addFace();
        HalfEdge* prevEdge = nullptr;
        HalfEdge* firstEdge = nullptr;

        int triVerts[3] = { (int)tri[0], (int)tri[1], (int)tri[2] };

        for (int i = 0; i < 3; i++) {
            int currVert = triVerts[i];
            int nextVert = triVerts[(i + 1) % 3];

            HalfEdge* edge = heMesh->addEdge();
            edge->nextVertex = heMesh->vertices[nextVert].get();
            heMesh->vertices[nextVert]->incomingEdge = edge;
            edge->face = face;
            face->edge = edge;

            // Sym linking
            int pairID = std::min(currVert, nextVert) * vertices.size() + std::max(currVert, nextVert);
            if (vertexPairToEdge.count(pairID)) {
                edge->sym = vertexPairToEdge[pairID];
                vertexPairToEdge[pairID]->sym = edge;
            }
            else {
                vertexPairToEdge[pairID] = edge;
            }

            if (prevEdge) prevEdge->next = edge;
            else firstEdge = edge;
            prevEdge = edge;
        }
        prevEdge->next = firstEdge;
        face->edge = firstEdge;
    }

    return heMesh;
}
//#include "Eigen/Eigen"
//#include "igl/AABB.h"
//#include "igl/signed_distance.h"
//
//void test() {
//	Eigen::MatrixXf vertices; // Vx3
//	Eigen::MatrixXi indices; // Fx3
//
//	// TODO: mesh function helper that fills up matrices (or returns new ones)
//
//	// have a function return the vertices/indices, or use a pointer to fill it up with the <<, not sure how Xf/Xi works
//
//	igl::AABB<Eigen::MatrixXf, 3> meshTree;
//	meshTree.init(vertices, indices);
//
//	Eigen::RowVector3f query;
//	query << 1.0f, 2.0f, 3.0f;
//
//	int faceID; Eigen::RowVector3f closestPnt; float sqrDistance;
//	// igl::signed_distance(query, vertices, indices, SIGNED_DISTANCE_TYPE_PSEUDONORMAL, )
//	sqrDistance = meshTree.squared_distance(vertices, indices, query, faceID, closestPnt);
//}


#include "vbd-solver.h"
#include "helper/math.h"

VBDSolver::VBDSolver() : startPoseMesh(nullptr), lastSimulatedMesh(nullptr), lastSimulatedFrame(0), constrainedVerts(), currMaterial(SIMPLE_SPRING) {}

bool IsConstrained(HVertex* vert) {
	return false;// abs(vert->pos.x) >= 0.97 && vert->pos.y > -0.01;//vert->pos.y > 0.75f;
}

void VBDSolver::ResetSimulation(uPtr<HalfEdgeMesh> newStartPoseMesh, uPtr<HalfEdgeMesh> collisionMeshSource) {
	if (newStartPoseMesh == nullptr && startPoseMesh == nullptr) {
		std::cerr << "ERROR: ResetSimulation called without new start pose mesh when we don't have one!" << std::endl;
		return;
	}

	if (currMaterial >= 2) {
		useTetMesh = true;
	}
	else {
		useTetMesh = false;
	}

	if (newStartPoseMesh != nullptr) {
		facesInfo.clear();
		constrainedVerts.clear();
		startPoseMesh = mkU<HalfEdgeMesh>(*newStartPoseMesh);
		startPoseMesh->TriangulateAllFaces();

		if (currMaterial == SIMPLE_SPRING) {
			startPoseMesh->ComputeRestLengths();
		}

		if (currMaterial == STVK_CLOTH) {
			ComputeClothFaceInfo();
		}

		for (const uPtr<HVertex>& v : startPoseMesh->vertices) {
			if (IsConstrained(v.get()))
				constrainedVerts.insert(v->id);
		}
		if (useTetMesh) {
			startPoseTetMesh = mkU<TetMesh>();
			startPoseTetMesh->FromHalfEdge(*startPoseMesh);
		}
	}

	if (enableCollisionMesh && collisionMeshSource != nullptr) {
		collisionMesh = mkU<HalfEdgeMesh>(*collisionMeshSource);
		collisionMesh->TriangulateAllFaces();
		collisionMesh->Translate(collisionOffset);
	}

	lastSimulatedFrame = 0;
	lastSimulatedMesh = mkU<HalfEdgeMesh>(*startPoseMesh); // Copy

	if (useTetMesh) {

		lastSimulatedTetMesh = mkU<TetMesh>(*startPoseTetMesh); // Copy
		lastSimulatedTetMesh->PreCompute();

	}
}

void VBDSolver::SimulateUpToFrame(uint frameIndex) {
	// Past data is obsolete
	if (lastSimulatedFrame > frameIndex) {
		ResetSimulation();
	}

	for (int i = lastSimulatedFrame; i < frameIndex; i++) {
		SimulateOneFrame();
	}
}

void VBDSolver::ComputePlaneCollision(vec3 planeNormal, vec3 planePoint, HVertex* vert, vec3& collisionForce, mat3& collisionHessian) {
	float d = fmax(0.0f, dot(planePoint - vert->pos, planeNormal));
	if (d > 0.0f) {

		// calculate force and hessian
		vec3 floorCollisionForce = kc * d * planeNormal;
		mat3 floorCollisionHessian = kc * glm::outerProduct(planeNormal, planeNormal);

		// output
		collisionForce += floorCollisionForce;
		collisionHessian += floorCollisionHessian;

	}
}

void VBDSolver::ComputeTriangleCollision(HVertex* vert, HVertex* a, HVertex* b, HVertex* c, vec3& collisionForce, mat3& collisionHessian) {
	if (vert == a || vert == b || vert == c) return;

	vec3 normal = normalize(cross(b->pos - a->pos, c->pos - a->pos));
	float d = dot(vert->pos - a->pos, normal);

	if (d > -collisionThreshold && d < 0.0f) {
		vec3 proj = vert->pos - d * normal;

		vec3 ab = b->pos - a->pos, ac = c->pos - a->pos, ap = proj - a->pos;
		float d00 = dot(ab, ab), d01 = dot(ab, ac), d11 = dot(ac, ac);
		float d20 = dot(ap, ab), d21 = dot(ap, ac);
		float denom = d00 * d11 - d01 * d01;
		float v = (d11 * d20 - d01 * d21) / denom;
		float w = (d00 * d21 - d01 * d20) / denom;
		float u = 1.0f - v - w;

		if (u < 0.f || v < 0.f || w < 0.f) return;

		collisionForce += kc * (-d) * normal;
		collisionHessian += kc * outerProduct(normal, normal);
	}
}

void VBDSolver::ComputeCollisionForceAndHessian(HVertex* vert, vec3& collisionForce, mat3& collisionHessian) {

	// plane collision
	if (enableCollisionPlane) {
		vec3 planeNormal = normalize(vec3(sin(glm::radians(planeTilt)), cos(glm::radians(planeTilt)), 0.0f));
		vec3 planePoint = vec3(0, planeHeight, 0);
		ComputePlaneCollision(planeNormal, planePoint, vert, collisionForce, collisionHessian);
	}

	// collision against mesh triangles
	if (enableCollisionMesh && collisionMesh != nullptr) {
		for (const uPtr<Face>& f : collisionMesh->faces) {
			// Rightnow: assuming mesh is triangle only
			HVertex* a = f->edge->nextVertex;
			HVertex* b = f->edge->next->nextVertex;
			HVertex* c = f->edge->next->next->nextVertex;
			ComputeTriangleCollision(vert, a, b, c, collisionForce, collisionHessian);
		}

		// self intersection (DISABLED ATM)
		//for (const uPtr<Face>& f : lastSimulatedMesh->faces) {
		//	HVertex* a = f->edge->nextVertex;
		//	HVertex* b = f->edge->next->nextVertex;
		//	HVertex* c = f->edge->next->next->nextVertex;
		//	ComputeTriangleCollision(vert, a, b, c, collisionForce, collisionHessian);
		//}

	}

}

void VBDSolver::ComputeInertiaForceAndHessian(vec3 pos, vec3 externalPos, vec3& inertiaForce, mat3& inertiaHessian) {
	inertiaForce = -m / (dt * dt) * (pos - externalPos);
	inertiaHessian = m / (dt * dt) * glm::identity<mat3>();
}


void VBDSolver::ComputeClothFaceInfo() {
	// Foreach face, compute restArea and Dm^-1 using basis, record which vertices are which
	for (const uPtr<Face>& f : startPoseMesh->faces) {
		facesInfo[f->id] = FaceInfo();
		FaceInfo* fi = &facesInfo[f->id];

		int i = 0;
		HalfEdge* currEdge = f->edge;
		do {
			assert(i <= 2); // Triangles ONLY

			fi->vertIDs[i] = currEdge->nextVertex->id;

			currEdge = currEdge->next;
			++i;
		} while (currEdge != f->edge);

		array<vec3, 3> vp = array<vec3, 3>();
		for (int i = 0; i < 3; i++) {
			vp[i] = startPoseMesh->vertices[fi->vertIDs[i]]->pos;
		}

		// Forming 2D orthonormal basis out of triangle
		vec3 basisX = normalize(vp[1] - vp[0]);
		vec3 basisY = normalize(cross(basisX, cross(basisX, vp[2] - vp[0])));

		// Use above basis to make MATERIAL COORDINATES, like uvs but specifically chosen to maintain angles btwn edges & avoid non-uniform scale that might happen with normal uvs (its ok if our material coordinates are arbitrarily rotated or translated or uniformly scaled, but nothing else is ok)
		array<vec2, 3> materialVP = array<vec2, 3>();
		for (int i = 0; i < 3; i++) {
			materialVP[i] = vec2(dot(vp[i], basisX), dot(vp[i], basisY));
		}

		// Inverse rest shape
		mat2 restShape = mat2x2(materialVP[1] - materialVP[0], materialVP[2] - materialVP[0]);
		fi->invRestShape = inverse(restShape);

		// Rest area
		fi->restArea = 0.5f * length(cross(vp[1] - vp[0], vp[2] - vp[0]));
	}
}

mat3 VBDSolver::ComputeClothNeighborHessian(const HalfEdgeMesh& mesh, Face* face, HVertex* v) {
	//
	const FaceInfo& fInfo = facesInfo[face->id];
	array<vec3, 3> vp;
	for (int i = 0; i < 3; i++) {
		vp[i] = mesh.vertices[fInfo.vertIDs[i]]->pos;
	}

	//
	int localVertexID = -1;
	for (int i = 0; i < 3; i++) {
		if (fInfo.vertIDs[i] == v->id) {
			localVertexID = i;
			break;
		}
	}
	assert(localVertexID != -1);

	mat3 dxMat = glm::identity<mat3>();
	mat3 hessian = mat3();
	for (int i = 0; i < 3; i++) {
		vec3 dx = dxMat[i];

		glm::mat2x3 dDs;
		vec2 g;
		switch (localVertexID) {
		case 0:
			g = -(fInfo.invRestShape[0] + fInfo.invRestShape[1]);
			dDs = mat2x3(-dx, -dx); break;
		case 1:
			g = fInfo.invRestShape[0];
			dDs = mat2x3(dx, vec3(0)); break;
		case 2:
			g = fInfo.invRestShape[1];
			dDs = mat2x3(vec3(0), dx); break;
		}

		// dx/dX, change in world space pos per change in rest space pos
		mat2x3 deformationMat = mat2x3(vp[1] - vp[0], vp[2] - vp[0]) * fInfo.invRestShape;
		mat2x3 dDeformationMat = dDs * fInfo.invRestShape;

		mat2x2 strain = 0.5f * (transpose(deformationMat) * deformationMat - glm::identity<mat2x2>());
		mat2x2 dStrain = 0.5f * (transpose(dDeformationMat) * deformationMat + transpose(deformationMat) * dDeformationMat);

		mat2x3 dStress = dDeformationMat * (2 * u * strain + lambda * trace(strain) * glm::identity<mat2x2>()) + deformationMat * (2 * u * dStrain + lambda * trace(dStrain) * glm::identity<mat2x2>());
		vec3 df = -fInfo.restArea * dStress * g;

		hessian[i] = df;
	}

	return hessian;
}

vec3 VBDSolver::ComputeClothNeighborForce(const HalfEdgeMesh& mesh, Face* face, HVertex* v) {
	//
	const FaceInfo& fInfo = facesInfo[face->id];
	array<vec3, 3> vp;
	for (int i = 0; i < 3; i++) {
		vp[i] = mesh.vertices[fInfo.vertIDs[i]]->pos;
	}

	//
	int localVertexID = -1;
	for (int i = 0; i < 3; i++) {
		if (fInfo.vertIDs[i] == v->id) {
			localVertexID = i;
			break;
		}
	}
	assert(localVertexID != -1);

	vec2 g;
	switch (localVertexID) {
	case 0:
		g = -(fInfo.invRestShape[0] + fInfo.invRestShape[1]); break;
	case 1:
		g = fInfo.invRestShape[0]; break;
	case 2:
		g = fInfo.invRestShape[1]; break;
	}

	// dx/dX, change in world space pos per change in rest space pos
	mat2x3 deformationMat = mat2x3(vp[1] - vp[0], vp[2] - vp[0]) * fInfo.invRestShape;

	mat2x2 strain = 0.5f * (transpose(deformationMat) * deformationMat - glm::identity<mat2x2>());

	mat2x3 stress = deformationMat * (2 * u * strain + lambda * trace(strain) * glm::identity<mat2x2>());
	vec3 f = -fInfo.restArea * stress * g;

	return f;
}


vec3 VBDSolver::PredictPosition(HVertex* vert, vec3 externalPos) {
	if (constrainedVerts.count(vert->id) != 0) return vert->pos;

	vec3 inertiaForce = vec3(0); mat3 inertiaHessian = mat3(0);
	ComputeInertiaForceAndHessian(vert->pos, externalPos, inertiaForce, inertiaHessian);

	vec3 neighborForce = vec3(0);
	mat3 neighborHessian = mat3(0);
	HalfEdge* currEdge = vert->incomingEdge;
	do {
		HVertex* neighborVert = currEdge->sym->nextVertex;

		vec3 d = vert->pos - neighborVert->pos;
		float l = length(d);
		vec3 dNormalized = normalize(d);
		mat3 dNormalizedOuterProd = glm::outerProduct(dNormalized, dNormalized);

		float edgeRestLength = startPoseMesh->GetRestLength(vert, neighborVert) * restLen;

		neighborForce += -k * (l - edgeRestLength) * dNormalized;
		neighborHessian += k * (dNormalizedOuterProd + (1.0f / l) * (l - edgeRestLength) * (glm::identity<mat3>() - dNormalizedOuterProd));

		currEdge = currEdge->next->sym;
	} while (currEdge != vert->incomingEdge);

	vec3 collisionForce = vec3(0);  mat3 collisionHessian = mat3(0);
	ComputeCollisionForceAndHessian(vert, collisionForce, collisionHessian);

	vec3 force = inertiaForce + neighborForce + collisionForce;
	mat3 hessian = inertiaHessian + neighborHessian + collisionHessian;

	vec3 deltaX = glm::inverse(hessian) * force;
	return vert->pos + deltaX;
}

vec3 VBDSolver::PredictPositionCloth(const HalfEdgeMesh& mesh, HVertex* vert, vec3 externalPos) {
	if (constrainedVerts.count(vert->id) != 0) return vert->pos;

	vec3 inertiaForce = vec3(0); mat3 inertiaHessian = mat3(0);
	ComputeInertiaForceAndHessian(vert->pos, externalPos, inertiaForce, inertiaHessian);

	vec3 neighborForce = vec3(0);
	mat3 neighborHessian = mat3(0);

	vec3 collisionForce = vec3(0);  mat3 collisionHessian = mat3(0);
	ComputeCollisionForceAndHessian(vert, collisionForce, collisionHessian);

	HalfEdge* currEdge = vert->incomingEdge;
	do {
		Face* currFace = currEdge->face;

		neighborForce += ComputeClothNeighborForce(mesh, currFace, vert);
		neighborHessian += ComputeClothNeighborHessian(mesh, currFace, vert);

		currEdge = currEdge->next->sym;
	} while (currEdge != vert->incomingEdge);

	vec3 force = inertiaForce + neighborForce + collisionForce;
	mat3 hessian = inertiaHessian + neighborHessian + collisionHessian;
	// if (dot(force, force) <= 0.1f) return vert->pos;

	vec3 deltaX = glm::inverse(hessian) * force;
	// if (dot(deltaX, deltaX) >= 2.0f) return vert->pos;
	return vert->pos + deltaX;
}

vec3 VBDSolver::PredictPositionTetSpring(HVertex* vert, vec3 externalPos) {
	//if (constrainedVerts.count(vert->id) != 0) return vert->pos;

	vec3 inertiaForce = vec3(0); mat3 inertiaHessian = mat3(0);
	ComputeInertiaForceAndHessian(vert->pos, externalPos, inertiaForce, inertiaHessian);

	vec3 neighborForce = vec3(0);
	mat3 neighborHessian = mat3(0);

	// Collision Force and Hessian
	vec3 collisionForce = vec3(0); mat3 collisionHessian = mat3(0);
	if (vert->isSurface) {
		ComputeCollisionForceAndHessian(vert, collisionForce, collisionHessian);
	}

	// Neighbor spring forces: Find every neighbor in Tetmesh;
	for (HVertex* neighborVert : lastSimulatedTetMesh->vertexNeighbors[vert->id]) {

		vec3 d = vert->pos - neighborVert->pos;
		float l = length(d);
		vec3 dNormalized = normalize(d);
		mat3 dNormalizedOuterProd = glm::outerProduct(dNormalized, dNormalized);

		float edgeRestLength = lastSimulatedTetMesh->restLengths[VertexPairID(vert, neighborVert)] * restLen;

		neighborForce += -k * (l - edgeRestLength) * dNormalized;
		neighborHessian += k * (dNormalizedOuterProd + (1.0f / l) * (l - edgeRestLength) * (glm::identity<mat3>() - dNormalizedOuterProd));

	}

	vec3 force = inertiaForce + neighborForce + collisionForce;
	mat3 hessian = inertiaHessian + neighborHessian + collisionHessian;

	vec3 deltaX = glm::inverse(hessian) * force;
	return vert->pos + deltaX;
}

void VBDSolver::ComputeNeoHookForceAndHessian(HVertex* vert, Tet* tet, vec3& force, mat3& hessian) {

	// Todo, below is temp instruction from llm

	// 1. Compute deformation gradient F = Ds * DmInv
	//    where Ds = [v1-v0, v2-v0, v3-v0] from current positions

	// 2. Compute J = det(F)
	//    J < 0 means tet is inverted, may need handling

	// 3. Compute dE/dF (the first Piola-Kirchhoff stress P)
	//    P = mu * (F - F^-T) + lambda * ln(J) * F^-T

	// 4. Compute per-vertex force contribution via chain rule
	//    H = P * DmInv^T  (3x3 matrix)
	//    force on v1 += -H * [1,0,0]^T
	//    force on v2 += -H * [0,1,0]^T
	//    force on v3 += -H * [0,0,1]^T
	//    force on v0 += -(force_v1 + force_v2 + force_v3)
	//    only accumulate the contribution for `vert`

	// 5. Compute per-vertex hessian contribution
	//    This is the hard part — see Smith et al. 2018 section 4
	//    Result is a 3x3 matrix accumulated into hessian

}

vec3 VBDSolver::PredictPositionTetNeoHook(HVertex* vert, vec3 externalPos) {

	// Inertia Force and Hessian
	vec3 inertiaForce = vec3(0); mat3 inertiaHessian = mat3(0);
	ComputeInertiaForceAndHessian(vert->pos, externalPos, inertiaForce, inertiaHessian);

	// NeoHookean Force and Hessian
	vec3 neoHookForce = vec3(0); mat3 neoHookHessian = mat3(0);
	for (Tet* tet : lastSimulatedTetMesh->vertexTets[vert->id]) {
		ComputeNeoHookForceAndHessian(vert, tet, neoHookForce, neoHookHessian);
	}

	// Collision Force and Hessian
	vec3 collisionForce = vec3(0); mat3 collisionHessian = mat3(0);
	if (vert->isSurface) {
		ComputeCollisionForceAndHessian(vert, collisionForce, collisionHessian);
	}

	vec3 force = inertiaForce + neoHookForce + collisionForce;
	mat3 hessian = inertiaHessian + neoHookHessian + collisionHessian;

	vec3 deltaX = glm::inverse(hessian) * force;
	return vert->pos + deltaX;
}

void VBDSolver::SimulateOneFrameTet() {
	if (lastSimulatedTetMesh == nullptr) {
		std::cerr << "ERROR: SimulateOneFrameTet() called with no lastSimulatedMesh" << std::endl;
	}

	// Predict external positions & save positions
	vector<vec3> oldPositions(lastSimulatedTetMesh->vertices.size());
	vector<vec3> externalPredictedPositions(lastSimulatedTetMesh->vertices.size());
	for (int i = 0; i < lastSimulatedTetMesh->vertices.size(); i++) {
		vec3 externalAcc = g;
		oldPositions[i] = lastSimulatedTetMesh->vertices[i]->pos;
		externalPredictedPositions[i] = lastSimulatedTetMesh->vertices[i]->pos + lastSimulatedTetMesh->vertices[i]->vel * dt + externalAcc * dt * dt;
	}

	for (int j = 0; j < iterCount; j++) {
		for (int i = 0; i < lastSimulatedTetMesh->vertices.size(); i++) {
			HVertex* v = lastSimulatedTetMesh->vertices[i].get();

			switch (currMaterial) {
			case TET_SPRING:
				v->pos = PredictPositionTetSpring(v, externalPredictedPositions[i]);
				break;

			case TET_NEOHOOK:
				v->pos = PredictPositionTetNeoHook(v, externalPredictedPositions[i]);
				break;
			}
		}
	}

	for (int i = 0; i < lastSimulatedTetMesh->vertices.size(); i++) {
		HVertex* v = lastSimulatedTetMesh->vertices[i].get();
		v->vel = (1.0f / dt) * (v->pos - oldPositions[i]);
		v->vel *= 0.98f; // DAMPING
	}

}

void VBDSolver::SimulateOneFrameTri() {
	if (lastSimulatedMesh == nullptr) {
		std::cerr << "ERROR: SimulateOneFrame() called with no lastSimulatedMesh" << std::endl;
	}

	// Predict external positions & save positions
	vector<vec3> oldPositions(lastSimulatedMesh->vertices.size());
	vector<vec3> externalPredictedPositions(lastSimulatedMesh->vertices.size());
	for (int i = 0; i < lastSimulatedMesh->vertices.size(); i++) {
		vec3 externalAcc = g;
		oldPositions[i] = lastSimulatedMesh->vertices[i]->pos;
		externalPredictedPositions[i] = lastSimulatedMesh->vertices[i]->pos + lastSimulatedMesh->vertices[i]->vel * dt + externalAcc * dt * dt;
	}

	for (int j = 0; j < iterCount; j++) {
		for (int i = 0; i < lastSimulatedMesh->vertices.size(); i++) {
			HVertex* v = lastSimulatedMesh->vertices[i].get();

			switch (currMaterial) {
			case SIMPLE_SPRING:
				v->pos = PredictPosition(v, externalPredictedPositions[i]);
				break;

			case STVK_CLOTH:
				v->pos = PredictPositionCloth(*lastSimulatedMesh, v, externalPredictedPositions[i]);
				break;
			}
		}
	}

	for (int i = 0; i < lastSimulatedMesh->vertices.size(); i++) {
		HVertex* v = lastSimulatedMesh->vertices[i].get();
		v->vel = (1.0f / dt) * (v->pos - oldPositions[i]);
		// v->pos = oldPositions[i] + 0.98f * v->vel * dt; // I dont think this is the right way to do DAMPING
		v->vel *= 0.98f; // DAMPING
	}
}

void VBDSolver::SimulateOneFrame() {

	lastSimulatedFrame++;

	if (useTetMesh) {
		SimulateOneFrameTet();
	}
	else {
		SimulateOneFrameTri();
	}

}
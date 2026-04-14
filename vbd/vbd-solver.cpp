#include "vbd-solver.h"

VBDSolver::VBDSolver() : startPoseMesh(nullptr), lastSimulatedMesh(nullptr), lastSimulatedFrame(0) {}

void VBDSolver::ResetSimulation(uPtr<HalfEdgeMesh> newStartPoseMesh) {
	if (newStartPoseMesh != nullptr) {
		startPoseMesh = std::move(newStartPoseMesh);
	}
	if (startPoseMesh == nullptr) {
		std::cerr << "ERROR: ResetSimulation called without new start pose mesh when we don't have one!" << std::endl;
	}

	lastSimulatedFrame = 0;
	lastSimulatedMesh = mkU<HalfEdgeMesh>(*startPoseMesh); // Copy
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

vec3 VBDSolver::PredictPosition(HVertex* vert, vec3 externalPos) {
	if (vert->pos.y > 0.75f) return vert->pos;

	vec3 inertiaForce = -m / (dt * dt) * (vert->pos - externalPos);
	mat3 inertiaHessian = m / (dt*dt) * glm::identity<mat3>();

	vec3 neighborForce = vec3(0);
	mat3 neighborHessian = mat3(0);

	HalfEdge* currEdge = vert->incomingEdge;
	do {
		HVertex* neighborVert = currEdge->sym->nextVertex;

		vec3 d = vert->pos - neighborVert->pos;
		float l = length(d);
		vec3 dNormalized = normalize(d);
		mat3 dNormalizedOuterProd = glm::outerProduct(dNormalized, dNormalized);

		neighborForce += -k * (l - restLen) * dNormalized;
		neighborHessian += k * (dNormalizedOuterProd + (1.0f / l) * (l - restLen) * (glm::identity<mat3>() - dNormalizedOuterProd));

		currEdge = currEdge->next->sym;
	} while (currEdge != vert->incomingEdge);

	vec3 force = inertiaForce + neighborForce;
	mat3 hessian = inertiaHessian + neighborHessian;

	vec3 deltaX = glm::inverse(hessian) * force;
	return vert->pos + deltaX;
}

void VBDSolver::SimulateOneFrame() {
	if (lastSimulatedMesh == nullptr) {
		std::cerr << "ERROR: SimulateOneFrame() called with no lastSimulatedMesh" << std::endl;
	}
	lastSimulatedFrame++;

	// Predict external positions & save positions
	vector<vec3> oldPositions(lastSimulatedMesh->vertices.size());
	vector<vec3> externalPredictedPositions(lastSimulatedMesh->vertices.size());
	for (int i = 0; i < lastSimulatedMesh->vertices.size(); i++) {
		vec3 externalAcc = g;
		oldPositions[i] = lastSimulatedMesh->vertices[i]->pos;
		externalPredictedPositions[i] = lastSimulatedMesh->vertices[i]->pos + lastSimulatedMesh->vertices[i]->vel * dt + externalAcc * dt * dt;
	}

#ifdef SCRATCH
	// Scratch
	uPtr<HalfEdgeMesh> scratchMesh = mkU<HalfEdgeMesh>(*lastSimulatedMesh);

	// Gaussian Seidel Iterations
	HalfEdgeMesh *currOldMesh, *currNewMesh;
	for (int i = 0; i < iterCount; i++) {
		currOldMesh = i % 2 == 0 ? lastSimulatedMesh.get() : scratchMesh.get();
		currNewMesh = i % 2 == 0 ? scratchMesh.get() : lastSimulatedMesh.get();

		for (int i = 0; i < scratchMesh->vertices.size(); i++) {
			currNewMesh->vertices[i]->pos = PredictPosition(currOldMesh->vertices[i].get(), externalPredictedPositions[i]);
		}
	}

	// Velocity update
	for (int i = 0; i < currNewMesh->vertices.size(); i++) {
		currNewMesh->vertices[i]->vel = (1.0f / dt) * (currNewMesh->vertices[i]->pos - oldPositions[i]);
	}

	// Copy back
	lastSimulatedMesh = mkU<HalfEdgeMesh>(*currNewMesh); // no need to copy again
#else
	for (int i = 0; i < iterCount; i++) {
		for (int i = 0; i < lastSimulatedMesh->vertices.size(); i++) {
			HVertex* v = lastSimulatedMesh->vertices[i].get();
			v->pos = PredictPosition(v, externalPredictedPositions[i]);
		}
	}

	for (int i = 0; i < lastSimulatedMesh->vertices.size(); i++) {
		HVertex* v = lastSimulatedMesh->vertices[i].get();
		v->vel = (1.0f / dt) * (v->pos - oldPositions[i]);
	}
#endif
}
#pragma once

#include "defines.h"
#include "half-edge.h"
#include "tet-mesh.h"
#include <iostream>
#include <unordered_set>

typedef int PhysicsMaterialID;
#define SIMPLE_SPRING 0
#define STVK_CLOTH 1

struct FaceInfo {
	float restArea;
	mat2 invRestShape;
	array<int, 3> vertIDs; // vertIDs[i] = id => id's local triangle index is i
};

class VBDSolver {
public:
	VBDSolver();

	void ResetSimulation(uPtr<HalfEdgeMesh> newStartPoseMesh = nullptr, uPtr<HalfEdgeMesh> newCollisionMesh = nullptr);
	void SimulateUpToFrame(uint frameIndex);
	uPtr<HalfEdgeMesh> lastSimulatedMesh;

private:
	friend class VBDManager;

	bool useTetMesh = false;

	uPtr<HalfEdgeMesh> startPoseMesh;
	std::unordered_set<int> constrainedVerts;
	int lastSimulatedFrame;

	uPtr<TetMesh> startPoseTetMesh;
	uPtr<TetMesh> lastSimulatedTetMesh;

	void SimulateOneFrame();
	vec3 PredictPosition(HVertex* vert, vec3 externalPos);
	vec3 PredictPositionCloth(const HalfEdgeMesh& mesh, HVertex* vert, vec3 externalPos);

	int iterCount = 5;
	float dt = 1.0f / 24.0f;
	vec3 g = vec3(0.0f, -0.98f, 0.0f);
	float m = 1.0f;

	// For Simple Spring Only
	float k = 150.0f;
	float restLen = 1.0;

	// For StVK Cloth Only
	float u = 1.0f;
	float lambda = 1.0f;

	// For Collision Computation
	void ComputePlaneCollision(vec3 planeNormal, vec3 planePoint, HVertex* vert, vec3& collisionForce, mat3& collisionHessian);
	void ComputeTriangleCollision(HVertex* vert, HVertex* a, HVertex* b, HVertex* c, vec3& collisionForce, mat3& collisionHessian);
	float kc = 1e6;
	float collisionThreshold = 0.1;
	uPtr<HalfEdgeMesh> collisionMesh = nullptr;
	bool enableCollisionMesh = false;
	vec3 collisionOffset = vec3(-1, -2.5, 0.0);

	// For StVK Cloth, different ComputeHessian/ComputeForce functions can be written for different materials, but the 'element' changes too often to generalize (simple spring uses vert, cloth stvk uses triangle, later materials will use tetrahedrons)
	void ComputeFaceInfo();
	mat3 ComputeHessian(const HalfEdgeMesh& mesh, Face* face, HVertex*);
	vec3 ComputeForce(const HalfEdgeMesh& mesh, Face* face, HVertex* v);

	std::unordered_map<int, FaceInfo> facesInfo;

	//
	PhysicsMaterialID currMaterial;
};
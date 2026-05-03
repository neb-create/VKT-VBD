#pragma once

#include "defines.h"
#include "half-edge.h"
#include "vbd-solver.h"
#include "imgui/imgui.h"

class VBDManager {
public:
	void Initialize(const VulkanReferences&);

	void DrawUI();

	vector<uPtr<Mesh>> meshes;
	uPtr<Mesh> collisionRenderMesh;

	bool IsSolverUsingCollisionMesh() { return solver.enableCollisionMesh; };
private:
	void Bake();

	uPtr<HalfEdgeMesh> initialMesh;
	uPtr<HalfEdgeMesh> collisionMesh;
	char collisionMeshPath[256] = "models/diagCube.obj";
	const VulkanReferences* ref;
	VBDSolver solver;

	int frameCount = 10;
	int currMaterialIndex = 0;
};
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
private:
	void Bake();

	uPtr<HalfEdgeMesh> initialMesh;
	const VulkanReferences* ref;
	VBDSolver solver;

	int frameCount = 250;
	int currMaterialIndex = 0;
};
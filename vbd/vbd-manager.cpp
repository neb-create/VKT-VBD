#include "vbd-manager.h"

#include "vbd-solver.h"

void VBDManager::Initialize(const VulkanReferences& ref) {
	uPtr<HalfEdgeMesh> initialMesh = mkU<HalfEdgeMesh>();
	initialMesh->LoadFromOBJ("models/sphere.obj");

	VBDSolver solver;
	solver.ResetSimulation(std::move(initialMesh));

	for (int i = 0; i < 100; i++) {
		solver.SimulateUpToFrame(i);
		meshes.push_back(std::move(solver.lastSimulatedMesh->convertToMesh(ref)));
	}
}
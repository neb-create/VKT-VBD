#include "vbd-manager.h"

#include "vbd-solver.h"

void VBDManager::Initialize(const VulkanReferences& ref) {
	this->ref = &ref;
	this->initialMesh = mkU<HalfEdgeMesh>();
	initialMesh->LoadFromOBJ("models/tiltedPlane.obj"); // TODO: triangulate all faces for better connectivity?
	initialMesh->TriangulateAllFaces();

	Bake();
}

void VBDManager::Bake() {
	ref->graphicsQueue.waitIdle();
	meshes.clear();

	solver.ResetSimulation(std::move(initialMesh));
	for (int i = 0; i < frameCount; i++) {
		solver.SimulateUpToFrame(i);
		meshes.push_back(std::move(solver.lastSimulatedMesh->convertToMesh(*ref)));
	}
}

void VBDManager::DrawUI() {
	ImGui::Begin("VBD Solver");

	if (ImGui::Button("Bake")) {
		Bake();
	}

	ImGui::SeparatorText("Sim");

	float g = solver.g.y;
	if (ImGui::SliderFloat("Gravity", &g, -10, 10, "%.2f")) {
		solver.g = vec3(0, g, 0);
	}
	ImGui::SliderInt("Frame Count", &frameCount, 5, 1000);
	ImGui::SliderInt("Iteration Count", &solver.iterCount, 1, 40);
	int fps = static_cast<int>(glm::ceil(1.0f / solver.dt));
	if (ImGui::SliderInt("Sim FPS", &fps, 1, 48)) {
		solver.dt = 1.0f / (1.0f * fps);
	}
	ImGui::SliderFloat("Mass", &solver.m, 0.05f, 10.0f, "%.2f");

	ImGui::SeparatorText("Physics Material");

	const array<string, 2> materialNames = {
		"Simple Spring",
		"StVK Cloth"
	};
	if (ImGui::Button(("Physics Material: " + materialNames[currMaterialIndex]).c_str())) {
		ImGui::OpenPopup("select_physics_material_popup");
	}
	if (ImGui::BeginPopup("select_physics_material_popup")) {
		ImGui::SeparatorText("Choose Material");
		for (int i = 0; i < materialNames.size(); i++) {
			if (ImGui::Selectable(materialNames[i].c_str())) {
				solver.currMaterial = i;
				currMaterialIndex = i;
			}
		}
		ImGui::EndPopup();
	}

	switch (currMaterialIndex) {
	case 0:

		ImGui::SliderFloat("Spring Constant", &solver.k, 1.0f, 1000.0f, "%.1f");
		ImGui::SliderFloat("Rest Length", &solver.restLen, 0.01f, 2.0f, "%.2f");

		break;

	case 1:

		ImGui::SliderFloat("Area Change Resistance", &solver.lambda, 0.1f, 100.0f, "%.1f");
		ImGui::SliderFloat("Shear Resistance", &solver.u, 0.1f, 100.0f, "%.1f");

		break;
	}

	ImGui::End();
}
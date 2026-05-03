#include "vbd-manager.h"

#include "vbd-solver.h"

void VBDManager::Initialize(const VulkanReferences& ref) {
	this->ref = &ref;
	this->initialMesh = mkU<HalfEdgeMesh>();
	this->collisionMesh = mkU<HalfEdgeMesh>();
	initialMesh->LoadFromOBJ("models/sphere.obj"); // TODO: triangulate all faces for better connectivity?

	collisionMesh->LoadFromOBJ(collisionMeshPath);
	solver.enableCollisionMesh;

	//Bake();
}

void VBDManager::Bake() {

	ref->graphicsQueue.waitIdle();
	meshes.clear();

	solver.currMaterial = currMaterialIndex;

	solver.ResetSimulation(mkU<HalfEdgeMesh>(*initialMesh), mkU<HalfEdgeMesh>(*collisionMesh));
	for (int i = 0; i < frameCount; i++) {
		solver.SimulateUpToFrame(i);

		if (currMaterialIndex <= 1) {
			// case tri mesh
			meshes.push_back(std::move(solver.lastSimulatedMesh->convertToMesh(*ref)));
		}
		if (currMaterialIndex >= 2) {
			// case tet mesh
			meshes.push_back(std::move(solver.lastSimulatedTetMesh->convertToMesh(*ref)));
		}
	}

	collisionRenderMesh = solver.collisionMesh ? solver.collisionMesh->convertToMesh(*ref) : nullptr;

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
	if (ImGui::SliderInt("Sim FPS", &fps, 1, 120)) {
		solver.dt = 1.0f / (1.0f * fps);
	}
	ImGui::SliderFloat("Mass", &solver.m, 0.05f, 10.0f, "%.2f");

	ImGui::SeparatorText("Physics Material");

	const array<string, 4> materialNames = {
		"Simple Spring",
		"StVK Cloth",
		"Tet Spring",
		"Tet NeoHook"
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

	case 2:

		ImGui::SliderFloat("Spring Constant", &solver.k, 1.0f, 1000.0f, "%.1f");
		ImGui::SliderFloat("Rest Length", &solver.restLen, 0.01f, 2.0f, "%.2f");

		break;

	}

	ImGui::SeparatorText("Collision");
	ImGui::SliderFloat("Collision Stiffness", &solver.kc, 1.0f, 1e7f, "%.1f");
	ImGui::SliderFloat("Collision Threshold", &solver.collisionThreshold, 0.001f, 1.0f, "%.3f");
	// Collision mesh
	ImGui::SeparatorText("Collision Mesh");
	ImGui::SameLine(); ImGui::SeparatorText("Collision");

	// Ground Plane
	ImGui::Checkbox("Ground Plane", &solver.enableCollisionPlane);
	if (solver.enableCollisionPlane) {
		ImGui::SetNextItemWidth(150);
		ImGui::SliderFloat("Height##plane", &solver.planeHeight, -10.0f, 10.0f, "%.2f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(150);
		ImGui::SliderFloat("Tilt##plane", &solver.planeTilt, -45.0f, 45.0f, "%.1f");
	}

	// Collision Mesh
	ImGui::Checkbox("Collision Mesh", &solver.enableCollisionMesh);
	if (solver.enableCollisionMesh) {
		ImGui::InputText("OBJ Path", collisionMeshPath, sizeof(collisionMeshPath));
		ImGui::SameLine();
		if (ImGui::Button("Load")) {
			solver.collisionMesh = mkU<HalfEdgeMesh>();
			solver.collisionMesh->LoadFromOBJ(collisionMeshPath);
			collisionRenderMesh = solver.collisionMesh->convertToMesh(*ref);
		}
		ImGui::SetNextItemWidth(150);
		ImGui::SliderFloat("X##offset", &solver.collisionOffset.x, -5.0f, 5.0f, "%.2f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(150);
		ImGui::SliderFloat("Y##offset", &solver.collisionOffset.y, -5.0f, 5.0f, "%.2f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(150);
		ImGui::SliderFloat("Z##offset", &solver.collisionOffset.z, -5.0f, 5.0f, "%.2f");
	}

	ImGui::End();
}
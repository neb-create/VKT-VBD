#pragma once

#include "defines.h"
#include "half-edge.h"

class VBDManager {
public:
	void Initialize(const VulkanReferences&);
	vector<uPtr<Mesh>> meshes;
private:
};
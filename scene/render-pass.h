#pragma once

#include "defines.h"

#include "scene/material.h"
#include "scene/render-target.h"
#include "scene/mesh.h"
#include "scene/texture.h"
#include "scene/pipeline.h"

class WRenderPass {
public:
	void Create(const VulkanReferences&);

	void Start(RenderTarget&, vk::raii::CommandBuffer* cmd);
	void EnqueueSetMaterial(const Material&);
	void EnqueueDraw(const Mesh&);
	void FinishExecute(bool waitForFinish = true);

private:
	void WaitForFinish();

	const VulkanReferences* ref = nullptr;
	const RenderTarget* target = nullptr;

	vk::raii::CommandBuffer* currCmd = nullptr;
	vk::raii::Fence drawFence = nullptr;
};
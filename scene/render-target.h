#pragma once

#include "defines.h"

#include "scene/texture.h"

class RenderTarget {
public:
	// no, delete: void CreateFromImages(vk::raii::Image* colorImg, vk::raii::ImageView* colorView, vk::raii::Image* depthImg, vk::raii::ImageView* depthView);
	void CreateFromTexture(WTexture* colorTex, vk::raii::ImageView* colorView, WTexture* depthTex, vk::raii::ImageView* depthView);
	// void Create(uvec2 dim); will create textures first, for deferred rendering, but then we're actually OWNING the image so we'll need to create a texture (uPtr Texture = nullptr)
private:
	friend class WRenderPass;
	vk::raii::Image* colorImg;
	vk::raii::Image* depthImg;

	uvec2 dim;

	vk::raii::ImageView* colorView;
	vk::raii::ImageView* depthView;
};
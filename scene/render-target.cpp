#include "render-target.h"

void RenderTarget::CreateFromTexture(WTexture* colorTex, vk::raii::ImageView* colorView, WTexture* depthTex, vk::raii::ImageView* depthView) {
	this->colorImg = &colorTex->image;
	this->colorView = colorView;
	this->depthImg = depthTex ? &depthTex->image : nullptr;
	this->depthView = depthView;
	this->dim = uvec2(colorTex->width, colorTex->height);
}

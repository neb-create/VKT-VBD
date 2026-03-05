#include "render-target.h"

void RenderTarget::CreateFromTexture(WTexture* colorTex, vk::raii::ImageView* colorView, WTexture* depthTex, vk::raii::ImageView* depthView) {
	this->colorTex = colorTex;
	this->colorView = colorView;
	this->depthTex = depthTex;
	this->depthView = depthView;
}

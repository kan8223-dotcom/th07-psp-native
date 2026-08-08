#pragma once

#include "graphics/ZunGraphics.hpp"

ZunGraphics *Th07CreatePspGuBackend();
void Th07PspForgetSurface(const void *pixels);
void Th07PspMarkTextTexture(GfxTextureHandle texture);
unsigned int Th07PspTrimTextureCache();

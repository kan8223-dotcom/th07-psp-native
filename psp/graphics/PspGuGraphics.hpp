#pragma once

#include "graphics/ZunGraphics.hpp"

struct Th07PspSpriteVertex
{
    float u;
    float v;
    unsigned int color;
    float x;
    float y;
    float z;
};

ZunGraphics *Th07CreatePspGuBackend();
void Th07PspDrawSpriteQuads(const Th07PspSpriteVertex *vertices, unsigned int spriteCount);
void Th07PspDrawSpritePairs(const Th07PspSpriteVertex *vertices, unsigned int spriteCount);
void Th07PspForgetSurface(const void *pixels);
void Th07PspAllowNextWideStaticTexture();
bool Th07PspGetTextureContentSize(GfxTextureHandle texture, unsigned int *width,
                                  unsigned int *height);
void Th07PspMarkTextTexture(GfxTextureHandle texture);
void Th07PspBeginTextUploadBatch();
void Th07PspEndTextUploadBatch();
void Th07PspCompactTextTexture(GfxTextureHandle texture);
unsigned int Th07PspTrimTextureCache();
bool Th07PspCaptureFramebufferToTexture(GfxTextureHandle texture, int srcLeft, int srcTop,
                                        int srcWidth, int srcHeight, int dstLeft, int dstTop,
                                        int dstWidth, int dstHeight);
#if defined(TH07_PSP_PERF_DIAG)
void Th07PspPerfAddCalcTime(unsigned long long elapsedUs);
void Th07PspPerfAddDrawTime(unsigned long long elapsedUs);
void Th07PspPerfAddStageTime(unsigned long long elapsedUs);
void Th07PspPerfAddCalcJobTime(int priority, unsigned long long elapsedUs);
void Th07PspPerfAddDrawJobTime(int priority, unsigned long long elapsedUs);
#endif

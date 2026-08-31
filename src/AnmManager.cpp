#include "AnmManager.hpp"

#include <SDL2/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "AnmVm.hpp"
#include "AnmIdx.hpp"
#if defined(TH07_PSP_ASCII_POPUP_BATCH)
#include "AsciiManager.hpp"
#endif
#include "FileSystem.hpp"
#include "GameErrorContext.hpp"
#include "PspBulletRender.hpp"
#include "Rng.hpp"
#include "Stage.hpp"
#include "Supervisor.hpp"
#include "TextHelper.hpp"
#include "ZunMath.hpp"
#include "graphics/ZunGraphics.hpp"
#if defined(TH07_PSP)
#include <pspmath.h>
#if defined(TH07_PSP_PERF_M3)
#include <pspkernel.h>
#endif

#include "fileio.hpp"
#include "graphics/PspGuGraphics.hpp"
#endif
#if defined(TH07_PSP_1000)
#include "psp1000_arena.hpp"
#include "psp1000_title_cache.hpp"
#include "pbg4/Pbg4Archive.hpp"
#endif
#include "utils.hpp"

AnmManager *g_AnmManager;

VertexTex1DiffuseXyzrhw g_QuadVertices[4];

VertexTex1Xyzrhw g_QuadTemplate[4];

VertexTex1DiffuseXyz g_Quad3DFallback[4];

#if defined(TH07_PSP)
namespace
{
#if defined(TH07_PSP_PERF_M3)
constexpr unsigned int kPspM3EmitterSampleStride = 32u;
Th07PspM3EmitterWindow gPspM3EmitterWindow{};
unsigned int gPspM3EmitterOrdinal = 0;
class PspM3EmitterSample;
PspM3EmitterSample *gPspM3ActiveEmitterSample = nullptr;
int gPspM3FrontBatchOrigin = TH07_PSP_PERF_M3_BATCH_NONE;
bool gPspM3BulletBatchActive = false;
bool gPspM3BulletBatchCarryPending = false;
Th07PspSpriteVertex *gPspM3BulletBatchEnd = nullptr;
unsigned int gPspM3BulletBatchSprites = 0;

void PspM3ResetFrontBatchTracking()
{
    gPspM3FrontBatchOrigin = TH07_PSP_PERF_M3_BATCH_NONE;
    gPspM3BulletBatchCarryPending = false;
    gPspM3BulletBatchEnd = nullptr;
    gPspM3BulletBatchSprites = 0;
}

void PspM3NoteBulletAppend(AnmManager *manager)
{
    if (!manager || !gPspM3BulletBatchActive)
    {
        return;
    }
    if (manager->spritesToDraw == 0)
    {
        gPspM3FrontBatchOrigin = TH07_PSP_PERF_M3_BATCH_BULLET;
    }
    else if (gPspM3FrontBatchOrigin != TH07_PSP_PERF_M3_BATCH_BULLET)
    {
        // A bullet was joined to a pre-existing laser/item/other-owner front
        // batch.  Its later backend cost cannot be separated exactly.
        gPspM3FrontBatchOrigin = TH07_PSP_PERF_M3_BATCH_MIXED;
    }
}

class PspM3EmitterSample
{
  public:
    PspM3EmitterSample()
        : mActive((gPspM3EmitterOrdinal++ % kPspM3EmitterSampleStride) == 0u)
    {
        ++gPspM3EmitterWindow.emitterCalls;
        if (mActive)
        {
            ++gPspM3EmitterWindow.samples;
            if (gPspM3ActiveEmitterSample)
            {
                ++gPspM3EmitterWindow.phaseMismatches;
                mActive = false;
                return;
            }
            gPspM3ActiveEmitterSample = this;
            mLastUs = sceKernelGetSystemTimeWide();
        }
    }

    ~PspM3EmitterSample()
    {
        if (mBackendDepth != 0u)
        {
            ++gPspM3EmitterWindow.phaseMismatches;
            const unsigned long long nowUs = sceKernelGetSystemTimeWide();
            gPspM3EmitterWindow.excludedBackendUs += nowUs - mBackendStartUs;
            mBackendDepth = 0;
            mLastUs = nowUs;
        }
        RecordCurrentPhase();
        if (mActive)
        {
            if (gPspM3ActiveEmitterSample != this)
            {
                ++gPspM3EmitterWindow.phaseMismatches;
            }
            else
            {
                gPspM3ActiveEmitterSample = nullptr;
            }
        }
    }

    void Advance()
    {
        RecordCurrentPhase();
        if (mPhase < 3u)
        {
            ++mPhase;
        }
    }

    void NoteCull()
    {
        if (mActive)
        {
            ++gPspM3EmitterWindow.sampledCulls;
        }
    }

    void BackendBegin()
    {
        if (!mActive)
        {
            return;
        }
        if (mBackendDepth++ == 0u)
        {
            RecordCurrentPhase();
            mBackendStartUs = mLastUs;
        }
    }

    void BackendEnd()
    {
        if (!mActive)
        {
            return;
        }
        if (mBackendDepth == 0u)
        {
            ++gPspM3EmitterWindow.phaseMismatches;
            return;
        }
        if (--mBackendDepth == 0u)
        {
            const unsigned long long nowUs = sceKernelGetSystemTimeWide();
            gPspM3EmitterWindow.excludedBackendUs += nowUs - mBackendStartUs;
            mLastUs = nowUs;
        }
    }

  private:
    void RecordCurrentPhase()
    {
        if (!mActive)
        {
            return;
        }
        if (mBackendDepth != 0u)
        {
            return;
        }
        const unsigned long long nowUs = sceKernelGetSystemTimeWide();
        gPspM3EmitterWindow.phaseUs[mPhase] += nowUs - mLastUs;
        ++gPspM3EmitterWindow.phaseRecords[mPhase];
        mLastUs = nowUs;
    }

    bool mActive;
    unsigned int mPhase = 0;
    unsigned int mBackendDepth = 0;
    unsigned long long mLastUs = 0;
    unsigned long long mBackendStartUs = 0;
};
#endif

#if defined(TH07_PSP_PERF_M2)
unsigned int gPspBulletAxisEligible = 0;
unsigned int gPspBulletFallbackEligible = 0;
unsigned int gPspBulletCullRejects = 0;
#if defined(TH07_PSP_ASCII_POPUP_BATCH)
unsigned int gPspAsciiPopupBatchCalls = 0;
unsigned int gPspAsciiPopupBatchDigits = 0;
unsigned int gPspAsciiPopupBatchFallbacks = 0;
#endif
#endif

#if defined(TH07_PSP_BULLET_ROTATED_DIRECT)
float gPspRotatedViewportLeft = 0.0f;
float gPspRotatedViewportTop = 0.0f;
float gPspRotatedViewportRight = 0.0f;
float gPspRotatedViewportBottom = 0.0f;
#endif
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
void PreparePspPortraitTexture(u32 textureIdx)
{
    switch (textureIdx)
    {
    case ANM_FILE_FACE:
        // face_rm/mr/sk contains the selected protagonist's dialogue and bomb
        // portrait as two distinct full-size child atlases.
        Th07PspPrepareUpperPortraitTexture(TH07_PSP_PORTRAIT_SELF, textureIdx);
        break;
    case ANM_FILE_FACE + 1u:
        Th07PspPrepareUpperPortraitTexture(TH07_PSP_PORTRAIT_BOMB, textureIdx);
        break;
    case ANM_FILE_FACE_STAGE:
        // Current-stage protagonist/boss faces are the only atlases minified
        // to 256x256. Their ANM logical width/height remains unchanged.
        Th07PspPrepareUpperPortraitTexture(TH07_PSP_PORTRAIT_STAGE_0, textureIdx);
        break;
    case ANM_FILE_FACE_STAGE + 1u:
        Th07PspPrepareUpperPortraitTexture(TH07_PSP_PORTRAIT_STAGE_1, textureIdx);
        break;
    case ANM_FILE_FACE_STAGE + 2u:
        Th07PspPrepareUpperPortraitTexture(TH07_PSP_PORTRAIT_STAGE_2, textureIdx);
        break;
    case ANM_FILE_FACE_STAGE + 3u:
        Th07PspPrepareUpperPortraitTexture(TH07_PSP_PORTRAIT_STAGE_3, textureIdx);
        break;
    default:
        break;
    }
}

void CompletePspPortraitPrewarm(i32 anmIdx, unsigned int childCount)
{
    if (anmIdx == ANM_FILE_FACE_STAGE)
    {
        Th07PspCompleteUpperPortraitPrewarm(childCount);
    }
}
#endif

SDL_Surface *LoadPspMusicRawSurface()
{
    struct RawHeader
    {
        char magic[8];
        u32 width;
        u32 height;
    };
    static const char expectedMagic[8] = {'T', 'H', '0', '7', 'M', '5', '6', '5'};

    FILE *file = std::fopen("music_bg.rgb565", "rb");
    if (!file)
    {
        // Keep compatibility with early diagnostic installs which placed the
        // user-derived cache next to th07.dat.
        file = std::fopen("th7/music_bg.rgb565", "rb");
    }
    if (!file)
    {
        return nullptr;
    }
    RawHeader header{};
    if (std::fread(&header, 1, sizeof(header), file) != sizeof(header) ||
        std::memcmp(header.magic, expectedMagic, sizeof(expectedMagic)) != 0 ||
        header.width != 640 || header.height != 480)
    {
        std::fclose(file);
        return nullptr;
    }

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
        0, static_cast<i32>(header.width), static_cast<i32>(header.height), 16,
        SDL_PIXELFORMAT_RGB565);
    bool loaded = surface != nullptr;
    for (u32 y = 0; loaded && y < header.height; ++y)
    {
        void *row = static_cast<u8 *>(surface->pixels) + y * surface->pitch;
        loaded = std::fread(row, 1, header.width * 2u, file) == header.width * 2u;
    }
    std::fclose(file);
    if (!loaded)
    {
        SDL_FreeSurface(surface);
        return nullptr;
    }
    th07_psp_boot_note("music raw cache loaded");
    return surface;
}

inline float PspRenderFloor(float value)
{
    if (!std::isfinite(value) || value < -2147483520.0f || value > 2147483520.0f)
    {
        return floorf(value);
    }
    float result;
    asm volatile("floor.w.s %0, %1\n\t"
                 "cvt.s.w %0, %0"
                 : "=&f"(result)
                 : "f"(value));
    return result;
}

inline float PspBulletFloor(float value)
{
    // Bullet coordinates come from finite gameplay positions and finite ANM
    // sprite dimensions, and DrawPspBullet rejects non-intersecting geometry
    // before reaching this conversion.  Avoid repeating PspRenderFloor's
    // generic NaN/2^31 fallback guard four times for every axis-aligned bullet.
    float result;
    asm volatile("floor.w.s %0, %1\n\t"
                 "cvt.s.w %0, %0"
                 : "=&f"(result)
                 : "f"(value));
    return result;
}

inline void PspRenderSinCos(float angle, float *outSin, float *outCos)
{
    if (std::isfinite(angle) && angle >= -16.0f * ZUN_PI && angle <= 16.0f * ZUN_PI)
    {
        vfpu_sincos(angle, outSin, outCos);
        return;
    }
    sincosf(outSin, outCos, angle);
}

inline unsigned int PspGuColor(ZunColor color)
{
    return (color.color & 0xff00ff00u) | ((color.color & 0x00ff0000u) >> 16) |
           ((color.color & 0x000000ffu) << 16);
}

inline void WritePspSpriteVertex(Th07PspSpriteVertex &out, float x, float y, float z,
                                 float u, float v, ZunColor color)
{
    out.u = u;
    out.v = v;
    out.color = PspGuColor(color);
    out.x = x;
    out.y = y;
    out.z = z;
}
} // namespace

#if defined(TH07_PSP_PERF_M2)
void Th07PspTakeBulletDrawPerf(unsigned int *axisEligible, unsigned int *fallbackEligible,
                              unsigned int *cullRejects)
{
    if (axisEligible)
    {
        *axisEligible = gPspBulletAxisEligible;
    }
    if (fallbackEligible)
    {
        *fallbackEligible = gPspBulletFallbackEligible;
    }
    if (cullRejects)
    {
        *cullRejects = gPspBulletCullRejects;
    }
    gPspBulletAxisEligible = 0;
    gPspBulletFallbackEligible = 0;
    gPspBulletCullRejects = 0;
}

#if defined(TH07_PSP_ASCII_POPUP_BATCH)
void Th07PspTakeAsciiPopupBatchPerf(unsigned int *batchCalls, unsigned int *digits,
                                    unsigned int *fallbacks)
{
    if (batchCalls)
    {
        *batchCalls = gPspAsciiPopupBatchCalls;
    }
    if (digits)
    {
        *digits = gPspAsciiPopupBatchDigits;
    }
    if (fallbacks)
    {
        *fallbacks = gPspAsciiPopupBatchFallbacks;
    }
    gPspAsciiPopupBatchCalls = 0;
    gPspAsciiPopupBatchDigits = 0;
    gPspAsciiPopupBatchFallbacks = 0;
}
#endif
#endif

#if defined(TH07_PSP_PERF_M3)
void Th07PspTakeM3EmitterPerf(Th07PspM3EmitterWindow *window)
{
    if (!window)
    {
        return;
    }
    *window = gPspM3EmitterWindow;
    gPspM3EmitterWindow = Th07PspM3EmitterWindow{};
}

bool Th07PspM3EmitterPopulationValid(const Th07PspM3EmitterWindow *window,
                                     unsigned int sampledBulletDraws,
                                     unsigned int bulletVisits)
{
    if (!window)
    {
        return false;
    }
    const unsigned int minimumSamples =
        window->emitterCalls / kPspM3EmitterSampleStride;
    const unsigned int maximumSamples =
        (window->emitterCalls + kPspM3EmitterSampleStride - 1u) /
        kPspM3EmitterSampleStride;
    return window->samples >= minimumSamples &&
           window->samples <= maximumSamples &&
           window->emitterCalls == bulletVisits &&
           sampledBulletDraws == window->samples &&
           window->sampledCulls <= window->samples;
}

void Th07PspM3EmitterBackendBegin()
{
    if (gPspM3ActiveEmitterSample)
    {
        gPspM3ActiveEmitterSample->BackendBegin();
    }
}

void Th07PspM3EmitterBackendEnd()
{
    if (gPspM3ActiveEmitterSample)
    {
        gPspM3ActiveEmitterSample->BackendEnd();
    }
}

void Th07PspM3BulletBatchBegin()
{
    gPspM3BulletBatchActive = true;
    gPspM3BulletBatchCarryPending = false;
    gPspM3BulletBatchEnd = nullptr;
    gPspM3BulletBatchSprites = 0;
    if (g_AnmManager && g_AnmManager->spritesToDraw != 0)
    {
        // Anything queued before the first bullet belongs to the pre-bullet
        // (laser/item/earlier-owner) side of the ownership boundary.
        if (gPspM3FrontBatchOrigin == TH07_PSP_PERF_M3_BATCH_NONE)
        {
            gPspM3FrontBatchOrigin = TH07_PSP_PERF_M3_BATCH_PRE;
        }
    }
    else
    {
        gPspM3FrontBatchOrigin = TH07_PSP_PERF_M3_BATCH_NONE;
    }
}

void Th07PspM3BulletBatchEnd()
{
    gPspM3BulletBatchActive = false;
    if (g_AnmManager && g_AnmManager->spritesToDraw != 0 &&
        gPspM3FrontBatchOrigin == TH07_PSP_PERF_M3_BATCH_BULLET)
    {
        gPspM3BulletBatchCarryPending = true;
        gPspM3BulletBatchEnd = g_AnmManager->vertexBufferCurPtr;
        gPspM3BulletBatchSprites = g_AnmManager->spritesToDraw;
    }
}

unsigned int Th07PspM3FrontBatchUnresolved()
{
    return gPspM3BulletBatchCarryPending ||
                   gPspM3FrontBatchOrigin == TH07_PSP_PERF_M3_BATCH_MIXED
               ? 1u
               : 0u;
}
#endif
#endif

AnmManager::AnmManager()
{
    memset((void *)this, 0, sizeof(AnmManager));

    for (i32 i = 0; i < 2560; i++)
    {
        this->sprites[i].sourceFileIndex = -1;
    }
    g_QuadTemplate[0].w = g_QuadTemplate[1].w = g_QuadTemplate[2].w = g_QuadTemplate[3].w = 1.0f;
    g_QuadTemplate[0].textureUV.x = 0.0f;
    g_QuadTemplate[0].textureUV.y = 0.0f;
    g_QuadTemplate[1].textureUV.x = 1.0f;
    g_QuadTemplate[1].textureUV.y = 0.0f;
    g_QuadTemplate[2].textureUV.x = 0.0f;
    g_QuadTemplate[2].textureUV.y = 1.0f;
    g_QuadTemplate[3].textureUV.x = 1.0f;
    g_QuadTemplate[3].textureUV.y = 1.0f;
    g_QuadVertices[0].w = g_QuadVertices[1].w = g_QuadVertices[2].w = g_QuadVertices[3].w = 1.0f;
    g_QuadVertices[0].textureUV.x = 0.0f;
    g_QuadVertices[0].textureUV.y = 0.0f;
    g_QuadVertices[1].textureUV.x = 1.0f;
    g_QuadVertices[1].textureUV.y = 0.0f;
    g_QuadVertices[2].textureUV.x = 0.0f;
    g_QuadVertices[2].textureUV.y = 1.0f;
    g_QuadVertices[3].textureUV.x = 1.0f;
    g_QuadVertices[3].textureUV.y = 1.0f;

    this->currentTexture = 0;
    this->currentBlendMode = 0;
    this->currentColorOp = 0;
    this->currentTextureFactor.color = 1;
    this->currentVertexShader = 0;
    this->currentCameraMode = 255;
    this->currentZWriteDisable = 0;
    this->screenshotTextureId = -1;
}

AnmManager::~AnmManager()
{
}

void AnmManager::SetupVertexBuffer()
{
    this->vertexBufferContents[2].position.x = -128.0f;
    this->vertexBufferContents[0].position.x = -128.0f;
    this->vertexBufferContents[3].position.x = 128.0f;
    this->vertexBufferContents[1].position.x = 128.0f;
    this->vertexBufferContents[1].position.y = -128.0f;
    this->vertexBufferContents[0].position.y = -128.0f;
    this->vertexBufferContents[3].position.y = 128.0f;
    this->vertexBufferContents[2].position.y = 128.0f;
    this->vertexBufferContents[3].position.z = 0.0f;
    this->vertexBufferContents[2].position.z = 0.0f;
    this->vertexBufferContents[1].position.z = 0.0f;
    this->vertexBufferContents[0].position.z = 0.0f;
    this->vertexBufferContents[2].textureUV.x = 0.0f;
    this->vertexBufferContents[0].textureUV.x = 0.0f;
    this->vertexBufferContents[3].textureUV.x = 1.0f;
    this->vertexBufferContents[1].textureUV.x = 1.0f;
    this->vertexBufferContents[1].textureUV.y = 0.0f;
    this->vertexBufferContents[0].textureUV.y = 0.0f;
    this->vertexBufferContents[3].textureUV.y = 1.0f;
    this->vertexBufferContents[2].textureUV.y = 1.0f;

    g_Quad3DFallback[0].position = this->vertexBufferContents[0].position;
    g_Quad3DFallback[1].position = this->vertexBufferContents[1].position;
    g_Quad3DFallback[2].position = this->vertexBufferContents[2].position;
    g_Quad3DFallback[3].position = this->vertexBufferContents[3].position;
    g_Quad3DFallback[0].textureUV.x = this->vertexBufferContents[0].textureUV.x;
    g_Quad3DFallback[0].textureUV.y = this->vertexBufferContents[0].textureUV.y;
    g_Quad3DFallback[1].textureUV.x = this->vertexBufferContents[1].textureUV.x;
    g_Quad3DFallback[1].textureUV.y = this->vertexBufferContents[1].textureUV.y;
    g_Quad3DFallback[2].textureUV.x = this->vertexBufferContents[2].textureUV.x;
    g_Quad3DFallback[2].textureUV.y = this->vertexBufferContents[2].textureUV.y;
    g_Quad3DFallback[3].textureUV.x = this->vertexBufferContents[3].textureUV.x;
    g_Quad3DFallback[3].textureUV.y = this->vertexBufferContents[3].textureUV.y;
}

ZunResult AnmManager::LoadTexture(i32 textureIdx, const char *texturePath, u32 colorKey)
{
    u8 *srcData;

    ReleaseTexture(textureIdx);

    srcData = FileSystem::OpenFile(texturePath, 1);
    if (!srcData)
    {
        return ZUN_ERROR;
    }

    SDL_RWops *rw = SDL_RWFromMem(srcData, g_LastFileSize);
    SDL_Surface *surface = IMG_Load_RW(rw, 1);
    free(srcData);

    if (!surface)
    {
        return ZUN_ERROR;
    }

    if (colorKey != 0)
    {
        SDL_SetColorKey(surface, SDL_TRUE,
                        SDL_MapRGB(surface->format, (colorKey >> 16) & 0xFF, (colorKey >> 8) & 0xFF,
                                   colorKey & 0xFF));
    }

    SDL_Surface *converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(surface);

    this->textures[textureIdx] = g_Supervisor.gfxDevice->CreateTexture();
    g_Supervisor.gfxDevice->BindTexture(this->textures[textureIdx]);
    g_Supervisor.gfxDevice->SetTextureImage(converted->w, converted->h, PIXEL_RGBA,
                                            PIXEL_UNSIGNED_BYTE, converted->pixels);

#if defined(TH07_PSP)
    // Embedded ANM textures are immutable after upload.  Keeping an additional
    // RGBA8888 copy of every one exhausted the PSP heap on stage 4, whose five
    // background atlases alone duplicated about 2.5 MiB before stg4enm.anm was
    // opened.  The CPU copies are only consumed by CopyTexture/alpha merging;
    // both operate on the separately loaded/empty GUI atlases, never embedded
    // stage or enemy images.
    this->imageDataArray[textureIdx] = nullptr;
#else
    this->imageDataArray[textureIdx] = malloc(converted->pitch * converted->h);
    if (!this->imageDataArray[textureIdx])
    {
        SDL_FreeSurface(converted);
        return ZUN_ERROR;
    }
    memcpy(this->imageDataArray[textureIdx], converted->pixels, converted->pitch * converted->h);
#endif

    textureWidths[textureIdx] = converted->w;
    textureHeights[textureIdx] = converted->h;
    texturePitches[textureIdx] = converted->pitch;

    SDL_FreeSurface(converted);
    return ZUN_SUCCESS;
}

ZunResult AnmManager::LoadTextureEmbedded(u32 textureIdx, ZunImageInfoEmbedded *imageInfo)
{
    SDL_Surface *surface;

    ReleaseTexture(textureIdx);

#if defined(TH07_PSP)
    // front.anm textures 21 and 22 are the only embedded images read back by
    // Gui::CopyTemplateSpriteToSprite().  Every other embedded bitmap becomes
    // immutable after its GE upload, so retaining a second 32-bit CPU copy
    // wastes up to 1 MiB per 512x512 atlas and fragments title -> demo loads.
    const bool keepCpuCopy = textureIdx == ANM_FILE_FRONT ||
                             textureIdx == ANM_FILE_FRONT + 1;
    if (!keepCpuCopy &&
        (imageInfo->format == 1 || imageInfo->format == 3 || imageInfo->format == 5))
    {
        this->textures[textureIdx] = g_Supervisor.gfxDevice->CreateTexture();
        g_Supervisor.gfxDevice->BindTexture(this->textures[textureIdx]);
        const PixelFormat pixelFormat = imageInfo->format == 1   ? PIXEL_BGRA
                                        : imageInfo->format == 3 ? PIXEL_RGB
                                                                 : PIXEL_RGBA;
        const PixelDataType pixelType =
            imageInfo->format == 1   ? PIXEL_UNSIGNED_BYTE
            : imageInfo->format == 3 ? PIXEL_UNSIGNED_SHORT_5_6_5
                                     : PIXEL_UNSIGNED_SHORT_4_4_4_4;
#if defined(TH07_PSP_1000)
        if (imageInfo->unused_c == TH07_PSP_1000_TITLE_HIRES_IMAGE_MARKER)
        {
            Th07PspAllowNextWideStaticTexture();
        }
#endif
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
        PreparePspPortraitTexture(textureIdx);
#endif
        g_Supervisor.gfxDevice->SetTextureImage(imageInfo->width, imageInfo->height, pixelFormat,
                                                pixelType, imageInfo->data);
        this->imageDataArray[textureIdx] = nullptr;
        textureWidths[textureIdx] = imageInfo->width;
        textureHeights[textureIdx] = imageInfo->height;
        texturePitches[textureIdx] = imageInfo->width * 4;
        return ZUN_SUCCESS;
    }
#endif

    u32 bpp = g_TextureBytesPerPixel[imageInfo->format];
    u32 depth = bpp * 8;
    u32 pitch = imageInfo->width * bpp;

    u32 sdlFormat = SDL_PIXELFORMAT_UNKNOWN;
    switch (imageInfo->format)
    {
    case 1:
        sdlFormat = SDL_PIXELFORMAT_ARGB8888;
        break;
    case 2:
        sdlFormat = SDL_PIXELFORMAT_ARGB1555;
        break;
    case 3:
        sdlFormat = SDL_PIXELFORMAT_RGB565;
        break;
    case 4:
        sdlFormat = SDL_PIXELFORMAT_BGR24;
        break;
    case 5:
        sdlFormat = SDL_PIXELFORMAT_ARGB4444;
        break;
    default:
        return ZUN_ERROR;
    }

    surface = SDL_CreateRGBSurfaceWithFormatFrom(imageInfo->data, imageInfo->width,
                                                 imageInfo->height, depth, pitch, sdlFormat);
    if (!surface)
    {
        return ZUN_ERROR;
    }

    SDL_Surface *converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(surface);

    if (!converted)
    {
        return ZUN_ERROR;
    }

    this->textures[textureIdx] = g_Supervisor.gfxDevice->CreateTexture();
    g_Supervisor.gfxDevice->BindTexture(this->textures[textureIdx]);
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
    PreparePspPortraitTexture(textureIdx);
#endif
#if defined(TH07_PSP)
    if (imageInfo->format == 3)
    {
        g_Supervisor.gfxDevice->SetTextureImage(converted->w, converted->h, PIXEL_RGB,
                                                PIXEL_UNSIGNED_SHORT_5_6_5, imageInfo->data);
    }
    else if (imageInfo->format == 5)
    {
        g_Supervisor.gfxDevice->SetTextureImage(converted->w, converted->h, PIXEL_RGBA,
                                                PIXEL_UNSIGNED_SHORT_4_4_4_4, imageInfo->data);
    }
    else
#endif
    {
        g_Supervisor.gfxDevice->SetTextureImage(converted->w, converted->h, PIXEL_RGBA,
                                                PIXEL_UNSIGNED_BYTE, converted->pixels);
    }

#if defined(TH07_PSP)
    if (keepCpuCopy)
    {
        this->imageDataArray[textureIdx] = malloc(converted->pitch * converted->h);
        if (!this->imageDataArray[textureIdx])
        {
            SDL_FreeSurface(converted);
            ReleaseTexture(textureIdx);
            return ZUN_ERROR;
        }
        memcpy(this->imageDataArray[textureIdx], converted->pixels,
               converted->pitch * converted->h);
    }
    else
    {
        this->imageDataArray[textureIdx] = nullptr;
    }
#else
    this->imageDataArray[textureIdx] = malloc(converted->pitch * converted->h);
    if (!this->imageDataArray[textureIdx])
    {
        SDL_FreeSurface(converted);
        ReleaseTexture(textureIdx);
        return ZUN_ERROR;
    }
    memcpy(this->imageDataArray[textureIdx], converted->pixels, converted->pitch * converted->h);
#endif

    textureWidths[textureIdx] = converted->w;
    textureHeights[textureIdx] = converted->h;
    texturePitches[textureIdx] = converted->pitch;

    SDL_FreeSurface(converted);
    return ZUN_SUCCESS;
}

ZunResult AnmManager::LoadTextureAlphaChannel(i32 textureIdx, const char *texturePath)
{
    u8 *data;

    u8 *basePixels = (u8 *)this->imageDataArray[textureIdx];
    if (!basePixels)
    {
        return ZUN_ERROR;
    }

    data = FileSystem::OpenFile(texturePath, 0);
    if (!data)
    {
        return ZUN_ERROR;
    }
    SDL_RWops *rw = SDL_RWFromMem(data, g_LastFileSize);
    SDL_Surface *alphaSurface = IMG_Load_RW(rw, 1);
    free(data);

    if (!alphaSurface)
    {
        return ZUN_ERROR;
    }

    if (alphaSurface->w != (i32)this->textureWidths[textureIdx] ||
        alphaSurface->h != (i32)this->textureHeights[textureIdx])
    {
        SDL_FreeSurface(alphaSurface);
        return ZUN_ERROR;
    }

    SDL_Surface *converted = SDL_ConvertSurfaceFormat(alphaSurface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(alphaSurface);

    if (!converted)
    {
        return ZUN_ERROR;
    }

    u8 *alphaPixels = (u8 *)converted->pixels;

    for (u32 y = 0; y < this->textureHeights[textureIdx]; y++)
    {
        for (u32 x = 0; x < this->textureWidths[textureIdx]; x++)
        {
            u32 baseOffset = (y * this->texturePitches[textureIdx]) + (x * 4);
            u32 alphaOffset = (y * converted->pitch) + (x * 4);

            basePixels[baseOffset + 3] = alphaPixels[alphaOffset + 2];
        }
    }

    g_Supervisor.gfxDevice->BindTexture(this->textures[textureIdx]);
    g_Supervisor.gfxDevice->SetTextureImage(this->textureWidths[textureIdx],
                                            this->textureHeights[textureIdx], PIXEL_RGBA,
                                            PIXEL_UNSIGNED_BYTE, basePixels);

    SDL_FreeSurface(converted);
    return ZUN_SUCCESS;
}

ZunResult AnmManager::CreateEmptyTexture(i32 textureIdx, u32 width, u32 height,
                                         i32 textureFormat)
{
    ReleaseTexture(textureIdx);
    this->textures[textureIdx] = g_Supervisor.gfxDevice->CreateTexture();

    void *emptyData = nullptr;
#if defined(TH07_PSP)
    // capture.anm is written and sampled only by the PSP renderer. Its old
    // unused 512x512 RGBA CPU mirror cost 1 MiB for the entire game, including
    // the 64 MiB profile now that both PSP paths use direct GE capture.
    if (textureIdx != ANM_FILE_CAPTURE)
#endif
    {
        emptyData = calloc(width * height, 4);
    }
    g_Supervisor.gfxDevice->BindTexture(this->textures[textureIdx]);
#if defined(TH07_PSP)
    // Keep an empty ANM atlas in its declared format during registration.
    // TextHelper prewarms FreeType first; the PSP backend then promotes only
    // atlases which actually receive dynamic glyphs to RGBA8888 immediately
    // before their first sub-image upload.
    PixelDataType uploadType = PIXEL_UNSIGNED_BYTE;
    if (textureFormat == 3)
    {
        uploadType = PIXEL_UNSIGNED_SHORT_5_6_5;
    }
    else if (textureFormat == 5)
    {
        uploadType = PIXEL_UNSIGNED_SHORT_4_4_4_4;
    }
    g_Supervisor.gfxDevice->SetTextureImage(width, height, PIXEL_RGBA, uploadType, nullptr);
#else
    g_Supervisor.gfxDevice->SetTextureImage(width, height, PIXEL_RGBA, PIXEL_UNSIGNED_BYTE,
                                            emptyData);
#endif

    this->imageDataArray[textureIdx] = emptyData;

    textureWidths[textureIdx] = width;
    textureHeights[textureIdx] = height;
    texturePitches[textureIdx] = width * 4;

    return ZUN_SUCCESS;
}

i32 AnmManager::LoadAnms(i32 anmIdx, const char *path, i32 spriteIdxOffset)
{
    i32 res;
    u32 ownsMemory;
    AnmRawEntry *entry;

    entry = nullptr;
#if defined(TH07_PSP_1000)
    const bool isTitleArchive = path && std::strcmp(path, "data/title01.anm") == 0;
    bool loadedTitleCache = false;
    if (isTitleArchive)
    {
        std::size_t cacheBytes = 0;
        entry = reinterpret_cast<AnmRawEntry *>(th07_psp_1000_load_title_cache(
            g_Pbg4Archive.GetEntrySize("title01.anm"), &cacheBytes));
        if (entry)
        {
            g_LastFileSize = static_cast<u32>(cacheBytes);
            loadedTitleCache = true;
        }
    }
#endif
    if (!entry)
        entry = (AnmRawEntry *)FileSystem::OpenFile(path, 0);
    ownsMemory = 1;
    i32 startIdx = anmIdx;
    if (!entry)
    {
#if defined(TH07_PSP)
        th07_psp_boot_notef("ANM OPEN NG %s", path ? path : "?");
#endif
        g_GameErrorContext.Fatal("アニメが読み込めません。データが失われてるか壊れています\n");
        return ZUN_ERROR;
    }
#if defined(TH07_PSP_PERF_DIAG)
    th07_psp_boot_notef("ANM BEGIN %s %uK", path ? path : "?", g_LastFileSize / 1024u);
#endif
#if defined(TH07_PSP)
    // Embedded ANMs keep their scripts before textureOffset and the complete
    // bitmap after it.  Once a bitmap has been uploaded, retaining that tail
    // duplicates hundreds of KiB (several MiB for stage portraits) in the
    // PSP heap.  Build a compact script/header copy, point textureOffset at
    // the original bitmap only for LoadAnm(), then release the source file.
    // Every stored script/name pointer consequently targets the compact copy.
    {
        AnmRawEntry *sourceBase = entry;
        const u32 sourceSize = g_LastFileSize;
#if defined(TH07_PSP_1000)
        if (isTitleArchive && !loadedTitleCache)
            th07_psp_1000_build_title_cache(sourceBase, sourceSize);
#endif
        u32 sourceOffset = 0;
        u32 compactSize = 0;
        u32 entryCount = 0;
        bool canCompact = true;

        while (canCompact)
        {
            if (sourceOffset > sourceSize || sourceSize - sourceOffset < sizeof(AnmRawEntry))
            {
                canCompact = false;
                break;
            }
            AnmRawEntry *sourceEntry =
                reinterpret_cast<AnmRawEntry *>(reinterpret_cast<u8 *>(sourceBase) + sourceOffset);
            const u32 entrySpan =
                sourceEntry->nextOffset ? static_cast<u32>(sourceEntry->nextOffset)
                                        : sourceSize - sourceOffset;
            if (!sourceEntry->hasData || sourceEntry->textureOffset < (i32)sizeof(AnmRawEntry) ||
                static_cast<u32>(sourceEntry->textureOffset) > entrySpan ||
                entrySpan > sourceSize - sourceOffset)
            {
                canCompact = false;
                break;
            }
            compactSize += (static_cast<u32>(sourceEntry->textureOffset) + 3u) & ~3u;
            ++entryCount;
            if (!sourceEntry->nextOffset)
            {
                break;
            }
            sourceOffset += static_cast<u32>(sourceEntry->nextOffset);
        }

        u8 *compactBase = canCompact ? static_cast<u8 *>(malloc(compactSize)) : nullptr;
        if (compactBase)
        {
            sourceOffset = 0;
            u32 compactOffset = 0;
            i32 currentAnmIdx = anmIdx;
            i32 currentSpriteOffset = spriteIdxOffset;
            i32 loadedCount = 0;
            for (u32 compactEntryIdx = 0; compactEntryIdx < entryCount; ++compactEntryIdx)
            {
                AnmRawEntry *sourceEntry = reinterpret_cast<AnmRawEntry *>(
                    reinterpret_cast<u8 *>(sourceBase) + sourceOffset);
                const u32 metadataSize = static_cast<u32>(sourceEntry->textureOffset);
                const u32 compactSpan = (metadataSize + 3u) & ~3u;
                AnmRawEntry *compactEntry =
                    reinterpret_cast<AnmRawEntry *>(compactBase + compactOffset);
                memcpy(compactEntry, sourceEntry, metadataSize);
                if (compactSpan > metadataSize)
                {
                    memset(compactBase + compactOffset + metadataSize, 0,
                           compactSpan - metadataSize);
                }
                compactEntry->nextOffset =
                    compactEntryIdx + 1 < entryCount ? static_cast<i32>(compactSpan) : 0;

                const i32 storedTextureOffset = compactEntry->textureOffset;
                compactEntry->textureOffset = static_cast<i32>(
                    reinterpret_cast<u8 *>(sourceEntry) + storedTextureOffset -
                    reinterpret_cast<u8 *>(compactEntry));
                res = LoadAnm(currentAnmIdx, compactEntry, currentSpriteOffset,
                              compactEntryIdx == 0 ? 1 : 0);
                compactEntry->textureOffset = storedTextureOffset;
                if (res < 0)
                {
                    this->anmFiles[anmIdx].childCount = loadedCount;
                    if (loadedCount > 0)
                    {
                        ReleaseAnm(anmIdx);
                    }
                    else
                    {
                        free(compactBase);
                    }
                    FileSystem::ReleaseFile(sourceBase);
                    return res;
                }
                ++loadedCount;
                ++currentAnmIdx;
                currentSpriteOffset += res;
                compactOffset += compactSpan;
                if (sourceEntry->nextOffset)
                {
                    sourceOffset += static_cast<u32>(sourceEntry->nextOffset);
                }
            }
            this->anmFiles[anmIdx].childCount = loadedCount;
            FileSystem::ReleaseFile(sourceBase);
#if defined(TH07_PSP_1000)
            if (isTitleArchive)
                th07_psp_1000_trim_to_stage();
#endif
#if defined(TH07_PSP_PERF_DIAG)
            th07_psp_boot_notef("ANM OK %s SRC%uK META%uK N%u", path ? path : "?",
                                sourceSize / 1024u, compactSize / 1024u, entryCount);
#endif
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
            CompletePspPortraitPrewarm(anmIdx, entryCount);
#endif
            return ZUN_SUCCESS;
        }
#if defined(TH07_PSP_PERF_DIAG)
        th07_psp_boot_notef("ANM COMPACT SKIP %s SRC%uK META%uK VALID%u", path ? path : "?",
                            sourceSize / 1024u, compactSize / 1024u, canCompact ? 1u : 0u);
#endif
    }
#endif
    while (true)
    {
        res = LoadAnm(anmIdx, entry, spriteIdxOffset, ownsMemory);
        if (res < 0)
        {
            this->anmFiles[startIdx].childCount = anmIdx - startIdx;
            return res;
        }
        anmIdx++;
        if (entry->nextOffset == 0)
        {
            const unsigned int childCount = static_cast<unsigned int>(anmIdx - startIdx);
            this->anmFiles[startIdx].childCount = childCount;
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
            CompletePspPortraitPrewarm(startIdx, childCount);
#endif
            return ZUN_SUCCESS;
        }
        entry = (AnmRawEntry *)((u8 *)entry + entry->nextOffset);
        ownsMemory = 0;
        spriteIdxOffset = spriteIdxOffset + res;
    }
}

i32 AnmManager::LoadAnm(i32 textureIdx, AnmRawEntry *rawEntry, i32 spriteIdxOffset, u32 ownsMemory)
{
    char *name;
    AnmRawSprite *rawSprite;
    AnmLoadedSprite loadedSprite;
    i32 *curSprite;
    i32 i;
    AnmRawEntry *data;
    i32 id;

    id = 0;
    if (!rawEntry)
    {
        g_GameErrorContext.Fatal("アニメが読み込めません。データが失われてるか壊れています\n");
        return ZUN_ERROR;
    }
    if (textureIdx >= 50)
    {
        g_GameErrorContext.Fatal("テクスチャ格納先が足りません\n");
        return ZUN_ERROR;
    }
    ReleaseAnm(textureIdx);
    data = rawEntry;
    if (data->version != 2)
    {
        g_GameErrorContext.Fatal("アニメのバージョンが違います\n");
        return ZUN_ERROR;
    }
    data->textureIdx = textureIdx;
    data->ownsMemory = ownsMemory;
    if (!data->hasData)
    {
        name = (char *)((u8 *)data + data->nameOffset);
        if (*name == '@')
        {
            CreateEmptyTexture(data->textureIdx, data->width, data->height, data->format);
        }
        else
        {
            if (LoadTexture(data->textureIdx, name, data->format) != ZUN_SUCCESS)
            {
                g_GameErrorContext.Fatal(
                    "テクスチャ %s が読み込めません。データが失われてるか壊れています\n", name);
                return ZUN_ERROR;
            }
        }
        if (data->mipmapNameOffset != 0)
        {
            name = (char *)((u8 *)data + data->mipmapNameOffset);
            if (LoadTextureAlphaChannel(data->textureIdx, name) != ZUN_SUCCESS)
            {
                g_GameErrorContext.Fatal(
                    "テクスチャ %s が読み込めません。データが失われてるか壊れています\n", name);
                return ZUN_ERROR;
            }
        }
    }
    else
    {
        auto *embeddedImage =
            reinterpret_cast<ZunImageInfoEmbedded *>((u8 *)data + data->textureOffset);
#if defined(TH07_PSP_1000)
        // The first run still uploads the original ANM while it writes the
        // compact cache for later boots. Keep the horizontal detail of the
        // title, main-menu and difficulty lettering on that run as well.
        const char *embeddedName = reinterpret_cast<const char *>((u8 *)data + data->nameOffset);
        if (embeddedImage->unused_c != TH07_PSP_1000_TITLE_HIRES_IMAGE_MARKER &&
            (std::strcmp(embeddedName, "data/title/title02.png") == 0 ||
             std::strcmp(embeddedName, "data/title/title01.png") == 0 ||
             std::strcmp(embeddedName, "data/title/select01.png") == 0))
        {
            Th07PspAllowNextWideStaticTexture();
        }
#endif
        if (LoadTextureEmbedded(data->textureIdx, embeddedImage) != ZUN_SUCCESS)
        {
            g_GameErrorContext.Fatal(
                "テクスチャが読み込めません。データが失われてるか壊れています\n");
            return ZUN_ERROR;
        }
#if defined(TH07_PSP_1000)
        // Runtime title cache pixels are already reduced to the exact 256px
        // representation the GU backend would create.  Keep the original ANM
        // logical dimensions for sprite geometry and UV normalization.
        if (embeddedImage->unused_c == TH07_PSP_1000_TITLE_IMAGE_MARKER ||
            embeddedImage->unused_c == TH07_PSP_1000_TITLE_HIRES_IMAGE_MARKER)
        {
            this->textureWidths[data->textureIdx] = data->width;
            this->textureHeights[data->textureIdx] = data->height;
        }
#endif
    }
    this->textureNames[textureIdx] = (char *)((u8 *)data + data->nameOffset);

    u32 texWidth = this->textureWidths[textureIdx] ? this->textureWidths[textureIdx] : data->width;
    u32 texHeight =
        this->textureHeights[textureIdx] ? this->textureHeights[textureIdx] : data->height;
#if defined(TH07_PSP)
    // The PSP-1000 backend may store a 512px ANM atlas at 256px.  Sprite
    // geometry must stay in the original logical size, but its half-texel UV
    // inset must use the actual stored dimensions.  The existing cols/rows
    // conversion below supplies exactly that split when given content size.
    unsigned int contentWidth = 0;
    unsigned int contentHeight = 0;
    if (Th07PspGetTextureContentSize(this->textures[data->textureIdx], &contentWidth,
                                     &contentHeight))
    {
        texWidth = contentWidth;
        texHeight = contentHeight;
    }
#endif

    data->spriteIdxOffset = spriteIdxOffset;
    curSprite = data->dataOffsets;
    for (i = 0; i < data->numSprites; i++, curSprite++)
    {
        rawSprite = (AnmRawSprite *)((u8 *)data + *curSprite);
        loadedSprite.sourceFileIndex = data->textureIdx;
        loadedSprite.cols = (f32)texWidth / (f32)data->width;
        loadedSprite.rows = (f32)texHeight / (f32)data->height;
        loadedSprite.startPixelInclusive.x = loadedSprite.cols * rawSprite->offset.x;
        loadedSprite.startPixelInclusive.y = loadedSprite.rows * rawSprite->offset.y;
        loadedSprite.endPixelInclusive.x =
            (rawSprite->offset.x + rawSprite->size.x) * loadedSprite.cols;
        loadedSprite.endPixelInclusive.y =
            (rawSprite->offset.y + rawSprite->size.y) * loadedSprite.rows;
        loadedSprite.textureWidth = (f32)texWidth;
        loadedSprite.textureHeight = (f32)texHeight;
        if (id < rawSprite->id)
        {
            id = rawSprite->id;
        }
        if (rawSprite->id + spriteIdxOffset >= 2560)
        {
            g_GameErrorContext.Fatal("スプライトが格納できません。テーブルが不足しています\n");
            return ZUN_ERROR;
        }
        LoadSprite(rawSprite->id + spriteIdxOffset, &loadedSprite);
    }
    for (i = 0; i < data->numScripts; i++, curSprite += 2)
    {
        if (*curSprite + spriteIdxOffset >= 2560)
        {
            g_GameErrorContext.Fatal("アニメが格納できません。テーブルが不足しています\n");
            return ZUN_ERROR;
        }
        if (id < *curSprite)
        {
            id = *curSprite;
        }
        this->scripts[*curSprite + spriteIdxOffset] = (AnmRawInstr *)((u8 *)data + curSprite[1]);
        this->spriteIndices[*curSprite + spriteIdxOffset] = spriteIdxOffset;
    }
    this->anmFiles[textureIdx].raw = data;
    this->anmFiles[textureIdx].spriteIndexOffset = spriteIdxOffset;
    return id + 1;
}

void AnmManager::ReleaseAnm(i32 anmIdx)
{
    AnmRawEntry *rawEntry;
    i32 *afterHdr;
    i32 uvX;
    i32 i;
    i32 spriteIdxOffset;
    i32 *spriteIdx;

    if (anmIdx < 0 || (u32)anmIdx >= 50)
    {
        return;
    }

    if (this->anmFiles[anmIdx].raw)
    {
        afterHdr = this->anmFiles[anmIdx].raw->dataOffsets;
        spriteIdxOffset = this->anmFiles[anmIdx].spriteIndexOffset;
        rawEntry = this->anmFiles[anmIdx].raw;
        uvX = anmIdx + 1;
        for (i = 1; i < this->anmFiles[anmIdx].childCount; i++, uvX++)
        {
            ReleaseAnm(uvX);
        }
        for (i = 0; i < rawEntry->numSprites; i++, afterHdr++)
        {
            spriteIdx = (i32 *)((u8 *)rawEntry + *afterHdr);
            memset(&this->sprites[*spriteIdx + spriteIdxOffset], 0, sizeof(AnmLoadedSprite));
            this->sprites[*spriteIdx + spriteIdxOffset].sourceFileIndex = -1;
        }
        for (i = 0; i < rawEntry->numScripts; i++, afterHdr += 2)
        {
            this->scripts[*afterHdr + spriteIdxOffset] = NULL;
            this->spriteIndices[*afterHdr + spriteIdxOffset] = 0;
        }
        this->anmFiles[anmIdx].spriteIndexOffset = 0;
        ReleaseTexture(rawEntry->textureIdx);
        if (rawEntry->ownsMemory)
        {
            FileSystem::ReleaseFile(rawEntry);
        }
        this->anmFiles[anmIdx].raw = NULL;
        this->currentBlendMode = 255;
        this->currentColorOp = 255;
        this->currentVertexShader = 0;
        this->currentTexture = 0;
        this->anmFiles[anmIdx].childCount = 0;
    }
}

void AnmManager::ReleaseTexture(i32 textureIdx)
{
    if (textureIdx < 0 || (u32)textureIdx >= 264)
    {
        return;
    }

    g_Supervisor.gfxDevice->DeleteTexture(this->textures[textureIdx]);
    this->textures[textureIdx].id = 0;
    this->textureWidths[textureIdx] = 0;
    this->textureHeights[textureIdx] = 0;
    this->texturePitches[textureIdx] = 0;

    free(this->imageDataArray[textureIdx]);
    this->imageDataArray[textureIdx] = NULL;
}

void AnmManager::LoadSprite(u32 spriteIdx, AnmLoadedSprite *sprite)
{
    this->sprites[spriteIdx] = *sprite;
    this->sprites[spriteIdx].spriteId = this->loadedSpriteCount++;

    // Same texel-centre correction as the final TH06 port.  With linear
    // filtering, sampling an integer atlas edge blends the requested cell
    // with its neighbour; this caused the PSP HUD grid and softened the edge
    // of dynamic text glyphs.
    this->sprites[spriteIdx].uvStart.x =
        (this->sprites[spriteIdx].startPixelInclusive.x + 0.5f) /
        this->sprites[spriteIdx].textureWidth;
    this->sprites[spriteIdx].uvEnd.x =
        (this->sprites[spriteIdx].endPixelInclusive.x - 0.5f) /
        this->sprites[spriteIdx].textureWidth;
    this->sprites[spriteIdx].uvStart.y =
        (this->sprites[spriteIdx].startPixelInclusive.y + 0.5f) /
        this->sprites[spriteIdx].textureHeight;
    this->sprites[spriteIdx].uvEnd.y =
        (this->sprites[spriteIdx].endPixelInclusive.y - 0.5f) /
        this->sprites[spriteIdx].textureHeight;
    this->sprites[spriteIdx].widthPx = (this->sprites[spriteIdx].endPixelInclusive.x -
                                        this->sprites[spriteIdx].startPixelInclusive.x) /
                                       sprite->cols;
    this->sprites[spriteIdx].heightPx = (this->sprites[spriteIdx].endPixelInclusive.y -
                                         this->sprites[spriteIdx].startPixelInclusive.y) /
                                        sprite->rows;
}

ZunResult AnmManager::SetActiveSprite(AnmVm *vm, i32 spriteIdx)
{
    if (this->sprites[spriteIdx].sourceFileIndex < 0)
    {
        return ZUN_ERROR;
    }

    vm->activeSpriteIdx = (i16)spriteIdx;
    vm->sprite = &this->sprites[spriteIdx];
    vm->matrix.Identity();
    vm->uvMatrix.Identity();
    vm->matrix.m[0][0] = vm->sprite->widthPx / 256.0f;
    vm->matrix.m[1][1] = vm->sprite->heightPx / 256.0f;
    vm->uvMatrix.m[0][0] = vm->sprite->widthPx / vm->sprite->textureWidth * vm->sprite->cols;
    vm->uvMatrix.m[1][1] = vm->sprite->heightPx / vm->sprite->textureHeight * vm->sprite->rows;
    vm->worldTransformMatrix = vm->matrix;
    return ZUN_SUCCESS;
}

void AnmManager::SetAndExecuteScript(AnmVm *vm, AnmRawInstr *beginningOfScript)
{
    if (!beginningOfScript)
    {
        memset(vm, 0, sizeof(AnmVm));
    }
    else
    {
        vm->flip = 0;
        vm->Initialize();
        vm->beginningOfScript = beginningOfScript;
        vm->currentInstruction = vm->beginningOfScript;
        vm->currentTimeInScript = 0;
        vm->visible = 0;
        ExecuteScript(vm);
        this->scriptsExecutedThisFrame++;
    }
}

void AnmManager::SetRenderStateForVm(AnmVm *vm)
{
    ZunColor color;

    if ((u32)this->currentBlendMode != vm->blendMode)
    {
        // Sprites are batched until Flush().  Changing the GU blend function
        // first makes every queued sprite use the new VM's mode.  Player-shot
        // impact sprites share the player atlas, so their additive mode was
        // consequently applied to the already queued player sprite and made
        // it flash white while shots were hitting an enemy.
        this->Flush();
        this->currentBlendMode = vm->blendMode;
        if (!this->currentBlendMode)
        {
            g_Supervisor.gfxDevice->SetBlendMode(BLEND_ALPHA, BLEND_ALPHA);
        }
        else
        {
            g_Supervisor.gfxDevice->SetBlendMode(BLEND_ALPHA, BLEND_ONE);
        }
    }
    color.color = vm->useColor2 ? vm->color2.color : vm->color.color;
    if (!g_Supervisor.cfg.noVertexBuffers)
    {
        if (this->colorMulEnabled)
        {
            color.bytes.r = ZunColor::Multiply(color.bytes.r, this->color.bytes.r);
            color.bytes.g = ZunColor::Multiply(color.bytes.g, this->color.bytes.g);
            color.bytes.b = ZunColor::Multiply(color.bytes.b, this->color.bytes.b);
            color.bytes.a = ZunColor::Multiply(color.bytes.a, this->color.bytes.a);
        }
        if (this->currentTextureFactor.color != color.color)
        {
            this->currentTextureFactor.color = color.color;
            g_Supervisor.gfxDevice->SetTextureFactor(this->currentTextureFactor);
        }
    }
    else
    {
        if (this->colorMulEnabled)
        {
            color.bytes.r = ZunColor::Multiply(color.bytes.r, this->color.bytes.r);
            color.bytes.g = ZunColor::Multiply(color.bytes.g, this->color.bytes.g);
            color.bytes.b = ZunColor::Multiply(color.bytes.b, this->color.bytes.b);
            color.bytes.a = ZunColor::Multiply(color.bytes.a, this->color.bytes.a);
        }
        g_QuadVertices[0].color = color;
        g_QuadVertices[1].color = color;
        g_QuadVertices[2].color = color;
        g_QuadVertices[3].color = color;
        g_Quad3DFallback[0].diffuse = color;
        g_Quad3DFallback[1].diffuse = color;
        g_Quad3DFallback[2].diffuse = color;
        g_Quad3DFallback[3].diffuse = color;
    }
    if (!g_Supervisor.cfg.disableZBuffer && (u32)this->currentZWriteDisable != vm->zWriteDisable)
    {
        this->Flush();
        this->currentZWriteDisable = vm->zWriteDisable;
        g_Supervisor.gfxDevice->SetDepthMask(this->currentZWriteDisable == 0);
    }
    if ((u32)this->currentCameraMode != vm->cameraMode)
    {
        g_AnmManager->Flush();
        this->currentCameraMode = vm->cameraMode;
        if (!this->currentCameraMode)
        {
            g_Stage.SetupCameraStageBackground();
            g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);
        }
        else
        {
            g_Stage.UpdateCamera();
            g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);
        }
    }
    this->renderStateChangesThisFrame++;
}

void AnmManager::SyncRenderState(AnmVm *vm)
{
#if defined(TH07_PSP_PERF_M2)
    Th07PspPerfInternalBegin(TH07_PSP_PERF_INTERNAL_STATE);
#endif
    if ((u32)this->currentBlendMode != vm->blendMode)
    {
        this->Flush();
        this->currentBlendMode = vm->blendMode;
        if (!this->currentBlendMode)
        {
            g_Supervisor.gfxDevice->SetBlendMode(BLEND_ALPHA, BLEND_ALPHA);
        }
        else
        {
            g_Supervisor.gfxDevice->SetBlendMode(BLEND_ALPHA, BLEND_ONE);
        }
    }
    if (!g_Supervisor.cfg.disableZBuffer && (u32)this->currentZWriteDisable != vm->zWriteDisable)
    {
        this->Flush();
        this->currentZWriteDisable = vm->zWriteDisable;
        g_Supervisor.gfxDevice->SetDepthMask(this->currentZWriteDisable == 0);
    }
    this->renderStateChangesThisFrame++;
#if defined(TH07_PSP_PERF_M2)
    Th07PspPerfInternalEnd(TH07_PSP_PERF_INTERNAL_STATE);
#endif
}

ZunResult AnmManager::DrawInner(AnmVm *vm, u32 drawFlags, f32 pspClipBottom)
{
    ZunColor color;
    f32 triangleX1, triangleX2, triangleY1, triangleY2;

    g_QuadVertices[0].pos.x += this->offset.x;
    g_QuadVertices[0].pos.y += this->offset.y;
    g_QuadVertices[1].pos.x += this->offset.x;
    g_QuadVertices[1].pos.y += this->offset.y;
    g_QuadVertices[2].pos.x += this->offset.x;
    g_QuadVertices[2].pos.y += this->offset.y;
    g_QuadVertices[3].pos.x += this->offset.x;
    g_QuadVertices[3].pos.y += this->offset.y;

    if ((drawFlags & 1) != 0)
    {
#if defined(TH07_PSP)
        g_QuadVertices[0].pos.x = PspRenderFloor(g_QuadVertices[0].pos.x + 0.5f);
        g_QuadVertices[1].pos.x = PspRenderFloor(g_QuadVertices[1].pos.x + 0.5f);
        g_QuadVertices[0].pos.y = PspRenderFloor(g_QuadVertices[0].pos.y + 0.5f);
        g_QuadVertices[2].pos.y = PspRenderFloor(g_QuadVertices[2].pos.y + 0.5f);
#else
        g_QuadVertices[0].pos.x = floorf(g_QuadVertices[0].pos.x + 0.5f);
        g_QuadVertices[1].pos.x = floorf(g_QuadVertices[1].pos.x + 0.5f);
        g_QuadVertices[0].pos.y = floorf(g_QuadVertices[0].pos.y + 0.5f);
        g_QuadVertices[2].pos.y = floorf(g_QuadVertices[2].pos.y + 0.5f);
#endif
        g_QuadVertices[1].pos.y = g_QuadVertices[0].pos.y;
        g_QuadVertices[2].pos.x = g_QuadVertices[0].pos.x;
        g_QuadVertices[3].pos.x = g_QuadVertices[1].pos.x;
        g_QuadVertices[3].pos.y = g_QuadVertices[2].pos.y;
    }

    g_QuadVertices[0].textureUV.x = g_QuadVertices[2].textureUV.x =
        vm->sprite->uvStart.x + vm->uvScrollPos.x;
    g_QuadVertices[1].textureUV.x = g_QuadVertices[3].textureUV.x =
        vm->sprite->uvEnd.x + vm->uvScrollPos.x;
    g_QuadVertices[0].textureUV.y = g_QuadVertices[1].textureUV.y =
        vm->sprite->uvStart.y + vm->uvScrollPos.y;
    g_QuadVertices[2].textureUV.y = g_QuadVertices[3].textureUV.y =
        vm->sprite->uvEnd.y + vm->uvScrollPos.y;

#if defined(TH07_PSP)
    // The PSP projection maps the logical bottom to the physical top.  A
    // dialogue portrait extending below y=480 can therefore rasterize its
    // bottom edge into the display's first row.  Callers opt into this small
    // geometry/UV clip; ordinary sprites retain original behavior.
    const f32 top = g_QuadVertices[0].pos.y;
    const f32 bottom = g_QuadVertices[2].pos.y;
    if (pspClipBottom < 999999.0f && bottom > pspClipBottom && bottom > top)
    {
        if (top >= pspClipBottom)
        {
            return ZUN_SUCCESS;
        }
        const f32 ratio = (pspClipBottom - top) / (bottom - top);
        const f32 clippedV = g_QuadVertices[0].textureUV.y +
                             (g_QuadVertices[2].textureUV.y -
                              g_QuadVertices[0].textureUV.y) * ratio;
        g_QuadVertices[2].pos.y = g_QuadVertices[3].pos.y = pspClipBottom;
        g_QuadVertices[2].textureUV.y = g_QuadVertices[3].textureUV.y = clippedV;
    }
#else
    (void)pspClipBottom;
#endif

    triangleX1 = std::max(g_QuadVertices[0].pos.x, g_QuadVertices[1].pos.x);
    triangleX1 = std::max(g_QuadVertices[2].pos.x, triangleX1);
    triangleX1 = std::max(g_QuadVertices[3].pos.x, triangleX1);

    triangleY1 = std::max(g_QuadVertices[0].pos.y, g_QuadVertices[1].pos.y);
    triangleY1 = std::max(g_QuadVertices[2].pos.y, triangleY1);
    triangleY1 = std::max(g_QuadVertices[3].pos.y, triangleY1);

    triangleX2 = std::min(g_QuadVertices[0].pos.x, g_QuadVertices[1].pos.x);
    triangleX2 = std::min(g_QuadVertices[2].pos.x, triangleX2);
    triangleX2 = std::min(g_QuadVertices[3].pos.x, triangleX2);

    triangleY2 = std::min(g_QuadVertices[0].pos.y, g_QuadVertices[1].pos.y);
    triangleY2 = std::min(g_QuadVertices[2].pos.y, triangleY2);
    triangleY2 = std::min(g_QuadVertices[3].pos.y, triangleY2);

    if (triangleX1 < g_Supervisor.viewport.x || triangleY1 < g_Supervisor.viewport.y ||
        triangleX2 > g_Supervisor.viewport.x + g_Supervisor.viewport.width ||
        triangleY2 > g_Supervisor.viewport.y + g_Supervisor.viewport.height)
    {
        return ZUN_SUCCESS;
    }

    if (this->currentTexture != this->textures[vm->sprite->sourceFileIndex])
    {
        this->currentTexture = this->textures[vm->sprite->sourceFileIndex];
        this->Flush();
        g_Supervisor.gfxDevice->BindTexture(this->currentTexture);
    }
    if (this->currentVertexShader != 1)
    {
        this->Flush();
        this->currentVertexShader = 1;
    }
    if ((drawFlags & 2) == 0)
    {
        color.color = vm->useColor2 ? vm->color2.color : vm->color.color;
        if (this->colorMulEnabled)
        {
            color.bytes.r = ZunColor::Multiply(color.bytes.r, this->color.bytes.r);
            color.bytes.g = ZunColor::Multiply(color.bytes.g, this->color.bytes.g);
            color.bytes.b = ZunColor::Multiply(color.bytes.b, this->color.bytes.b);
            color.bytes.a = ZunColor::Multiply(color.bytes.a, this->color.bytes.a);
        }
        g_QuadVertices[0].color = color;
        g_QuadVertices[1].color = color;
        g_QuadVertices[2].color = color;
        g_QuadVertices[3].color = color;
    }
    SyncRenderState(vm);
    PushSprite(g_QuadVertices);
    return ZUN_SUCCESS;
}

void AnmManager::ResetVertexBuffer()
{
#if defined(TH07_PSP) && defined(TH07_PSP_PERF_M3)
    if (this->spritesToDraw != 0 &&
        (gPspM3BulletBatchCarryPending ||
         gPspM3FrontBatchOrigin == TH07_PSP_PERF_M3_BATCH_BULLET ||
         gPspM3FrontBatchOrigin == TH07_PSP_PERF_M3_BATCH_MIXED))
    {
        // A normal frame flushes the front batch before ResetVertexBuffer.
        // If it did not, preserve that ownership failure in the window latch
        // instead of silently discarding the range metadata.
        Th07PspPerfM3LatchUnresolved();
    }
#endif
    this->spritesToDraw = 0;
    this->vertexBufferCurPtr = this->spriteVertexBuffer;
    this->vertexBufferStartPtr = this->vertexBufferCurPtr;
#if defined(TH07_PSP)
    this->pspSpriteBatchUsesPairs = 0;
    this->pspPreferSpritePairs = 0;
#if defined(TH07_PSP_BULLET_UNIFIED_QUADS)
    this->pspUnifiedBulletGeneralMode = 0;
    this->pspForceSpriteQuads = 0;
#endif
#if defined(TH07_PSP_PERF_M3)
    PspM3ResetFrontBatchTracking();
#endif
#endif
}

void AnmManager::Flush()
{
    if (!this->spritesToDraw)
    {
        return;
    }

    g_Supervisor.gfxDevice->SetTextureArg(TEX_ARG_DIFFUSE);
    g_Supervisor.gfxDevice->SetColorOp(COMPONENT_ALPHA, COLOR_OP_MODULATE);
    g_Supervisor.gfxDevice->SetColorOp(COMPONENT_RGB, COLOR_OP_MODULATE);

#if defined(TH07_PSP)
#if defined(TH07_PSP_PERF_M3)
    int m3BatchOrigin = gPspM3FrontBatchOrigin;
    if (m3BatchOrigin == TH07_PSP_PERF_M3_BATCH_NONE)
    {
        m3BatchOrigin = TH07_PSP_PERF_M3_BATCH_PRE;
    }
    if (gPspM3BulletBatchCarryPending &&
        (m3BatchOrigin != TH07_PSP_PERF_M3_BATCH_BULLET ||
         this->vertexBufferCurPtr != gPspM3BulletBatchEnd ||
         this->spritesToDraw != gPspM3BulletBatchSprites))
    {
        // Another owner appended to the bullet range before it reached the
        // backend.  Do not estimate a byte/time split: mark it unprovable.
        m3BatchOrigin = TH07_PSP_PERF_M3_BATCH_MIXED;
    }
    Th07PspPerfSetM3BatchOrigin(m3BatchOrigin);
#endif
    if (this->pspSpriteBatchUsesPairs)
    {
        Th07PspDrawSpritePairs(this->vertexBufferStartPtr, this->spritesToDraw);
    }
    else
    {
#if defined(TH07_PSP_BULLET_UNIFIED_QUADS)
        if (this->pspForceSpriteQuads)
        {
            Th07PspDrawSpriteQuadsUnified(this->vertexBufferStartPtr,
                                          this->spritesToDraw);
        }
        else
#endif
        {
            Th07PspDrawSpriteQuads(this->vertexBufferStartPtr,
                                   this->spritesToDraw);
        }
    }
#if defined(TH07_PSP_BULLET_UNIFIED_QUADS)
    this->pspForceSpriteQuads = 0;
#endif
#if defined(TH07_PSP_PERF_M3)
    Th07PspPerfSetM3BatchOrigin(TH07_PSP_PERF_M3_BATCH_NONE);
    PspM3ResetFrontBatchTracking();
#endif
#else
    g_Supervisor.gfxDevice->DrawPrimitiveUP(PRIM_TRIANGLES, this->spritesToDraw << 1,
                                            this->vertexBufferStartPtr,
                                            sizeof(VertexTex1DiffuseXyzrhw));
#endif
    this->vertexBufferStartPtr = this->vertexBufferCurPtr;
    this->spritesToDraw = 0;
    this->flushesThisFrame++;
}

ZunResult AnmManager::PushSprite(VertexTex1DiffuseXyzrhw *spriteVertex)
{
#if defined(TH07_PSP)
    if (this->pspSpriteBatchUsesPairs)
    {
        this->Flush();
        this->pspSpriteBatchUsesPairs = 0;
    }
    for (i32 i = 0; i < 4; ++i)
    {
        WritePspSpriteVertex(this->vertexBufferCurPtr[i], spriteVertex[i].pos.x,
                             spriteVertex[i].pos.y, spriteVertex[i].pos.z,
                             spriteVertex[i].textureUV.x, spriteVertex[i].textureUV.y,
                             spriteVertex[i].color);
    }
    this->vertexBufferCurPtr += 4;
#else
    this->vertexBufferCurPtr[0] = spriteVertex[0];
    this->vertexBufferCurPtr[1] = spriteVertex[1];
    this->vertexBufferCurPtr[2] = spriteVertex[2];
    this->vertexBufferCurPtr[3] = spriteVertex[1];
    this->vertexBufferCurPtr[4] = spriteVertex[2];
    this->vertexBufferCurPtr[5] = spriteVertex[3];
    this->vertexBufferCurPtr += 6;
#endif
    this->spritesToDraw++;
    return ZUN_SUCCESS;
}

ZunResult AnmManager::DrawNoRotation(AnmVm *vm, f32 pspClipBottom)
{
    f32 centerY;
    f32 centerX;

    if (!vm->visible)
    {
        return ZUN_ERROR;
    }

    if (!vm->active)
    {
        return ZUN_ERROR;
    }

    if (!vm->color.bytes.a)
    {
        return ZUN_ERROR;
    }

#if defined(TH07_PSP)
    if (!vm->sprite)
    {
        return ZUN_ERROR;
    }
    // Loading sprites can be drawn from a scene lifecycle callback before
    // GameWindow reaches its first regular draw frame.  Start the native
    // batch on demand in that narrow startup/transition window.
    if (!this->vertexBufferCurPtr)
    {
        this->ResetVertexBuffer();
    }

    // Most UI, item, effect and non-rotating enemy sprites used to construct
    // four generic PC vertices, rescan all four for clipping, then copy them
    // into the native GU buffer.  Emit the final PSP quad directly, as the
    // dense-bullet path already does.
    const f32 halfWidth = vm->sprite->widthPx * vm->scale.x * 0.5f;
    const f32 halfHeight = vm->sprite->heightPx * vm->scale.y * 0.5f;
    const f32 rawLeft = (vm->anchor & 1) ? vm->pos.x : vm->pos.x - halfWidth;
    const f32 rawRight =
        (vm->anchor & 1) ? vm->pos.x + halfWidth * 2.0f : vm->pos.x + halfWidth;
    const f32 rawTop = (vm->anchor & 2) ? vm->pos.y : vm->pos.y - halfHeight;
    const f32 rawBottom =
        (vm->anchor & 2) ? vm->pos.y + halfHeight * 2.0f : vm->pos.y + halfHeight;
    const f32 left = PspRenderFloor(rawLeft + this->offset.x + 0.5f);
    const f32 right = PspRenderFloor(rawRight + this->offset.x + 0.5f);
    f32 top = PspRenderFloor(rawTop + this->offset.y + 0.5f);
    f32 bottom = PspRenderFloor(rawBottom + this->offset.y + 0.5f);

    const f32 minX = std::min(left, right);
    const f32 maxX = std::max(left, right);
    const f32 minY = std::min(top, bottom);
    const f32 maxY = std::max(top, bottom);
    if (maxX < g_Supervisor.viewport.x || maxY < g_Supervisor.viewport.y ||
        minX > g_Supervisor.viewport.x + g_Supervisor.viewport.width ||
        minY > g_Supervisor.viewport.y + g_Supervisor.viewport.height)
    {
        return ZUN_SUCCESS;
    }

    f32 u0 = vm->sprite->uvStart.x + vm->uvScrollPos.x;
    const f32 u1 = vm->sprite->uvEnd.x + vm->uvScrollPos.x;
    f32 v0 = vm->sprite->uvStart.y + vm->uvScrollPos.y;
    f32 v1 = vm->sprite->uvEnd.y + vm->uvScrollPos.y;
    if (pspClipBottom < 999999.0f && bottom > pspClipBottom && bottom > top)
    {
        if (top >= pspClipBottom)
        {
            return ZUN_SUCCESS;
        }
        const f32 ratio = (pspClipBottom - top) / (bottom - top);
        v1 = v0 + (v1 - v0) * ratio;
        bottom = pspClipBottom;
    }

    const GfxTextureHandle texture = this->textures[vm->sprite->sourceFileIndex];
    if (this->currentTexture != texture)
    {
        this->currentTexture = texture;
        this->Flush();
        g_Supervisor.gfxDevice->BindTexture(this->currentTexture);
    }
    if (this->currentVertexShader != 1)
    {
        this->Flush();
        this->currentVertexShader = 1;
    }

    ZunColor color = vm->useColor2 ? vm->color2 : vm->color;
    if (this->colorMulEnabled)
    {
        color.bytes.r = ZunColor::Multiply(color.bytes.r, this->color.bytes.r);
        color.bytes.g = ZunColor::Multiply(color.bytes.g, this->color.bytes.g);
        color.bytes.b = ZunColor::Multiply(color.bytes.b, this->color.bytes.b);
        color.bytes.a = ZunColor::Multiply(color.bytes.a, this->color.bytes.a);
    }
    SyncRenderState(vm);

    if (this->pspSpriteBatchUsesPairs != this->pspPreferSpritePairs)
    {
        this->Flush();
        this->pspSpriteBatchUsesPairs = this->pspPreferSpritePairs;
    }
    Th07PspSpriteVertex *out = this->vertexBufferCurPtr;
    WritePspSpriteVertex(out[0], left, top, vm->pos.z, u0, v0, color);
    if (this->pspSpriteBatchUsesPairs)
    {
        WritePspSpriteVertex(out[1], right, bottom, vm->pos.z, u1, v1, color);
        this->vertexBufferCurPtr += 2;
    }
    else
    {
        WritePspSpriteVertex(out[1], right, top, vm->pos.z, u1, v0, color);
        WritePspSpriteVertex(out[2], left, bottom, vm->pos.z, u0, v1, color);
        WritePspSpriteVertex(out[3], right, bottom, vm->pos.z, u1, v1, color);
        this->vertexBufferCurPtr += 4;
    }
    ++this->spritesToDraw;
    return ZUN_SUCCESS;
#endif

    centerX = vm->sprite->widthPx * vm->scale.x / 2.0f;
    centerY = vm->sprite->heightPx * vm->scale.y / 2.0f;

    if ((vm->anchor & 1) == 0)
    {
        g_QuadVertices[0].pos.x = g_QuadVertices[2].pos.x = vm->pos.x - centerX;
        g_QuadVertices[1].pos.x = g_QuadVertices[3].pos.x = centerX + vm->pos.x;
    }
    else
    {
        g_QuadVertices[0].pos.x = g_QuadVertices[2].pos.x = vm->pos.x;
        g_QuadVertices[1].pos.x = g_QuadVertices[3].pos.x = centerX + vm->pos.x + centerX;
    }

    if ((vm->anchor & 2) == 0)
    {
        g_QuadVertices[0].pos.y = g_QuadVertices[1].pos.y = vm->pos.y - centerY;
        g_QuadVertices[2].pos.y = g_QuadVertices[3].pos.y = centerY + vm->pos.y;
    }
    else
    {
        g_QuadVertices[0].pos.y = g_QuadVertices[1].pos.y = vm->pos.y;
        g_QuadVertices[2].pos.y = g_QuadVertices[3].pos.y = centerY + vm->pos.y + centerY;
    }

    g_QuadVertices[0].pos.z = g_QuadVertices[1].pos.z = g_QuadVertices[2].pos.z =
        g_QuadVertices[3].pos.z = vm->pos.z;

    return DrawInner(vm, 1, pspClipBottom);
}

#if defined(TH07_PSP_GUI_TILE_BATCH)
ZunResult AnmManager::DrawPspNoRotationGrid(
    AnmVm *vm, f32 xStart, f32 xEnd, f32 xStep, f32 yStart, f32 yEnd,
    f32 yStep, f32 z)
{
    if (!vm || xStep <= 0.0f || yStep <= 0.0f)
    {
        return ZUN_ERROR;
    }

    // Preserve the canonical observable vm->pos even when this VM is not
    // drawable.  The ordinary GUI loops still assign every grid position in
    // that case; only the redundant DrawNoRotation front-end work is skipped.
    const bool drawable = vm->visible && vm->active && vm->color.bytes.a &&
                          vm->sprite;
    if (!drawable)
    {
        for (f32 x = xStart; x < xEnd; x = x + xStep)
        {
            for (f32 y = yStart; y < yEnd; y = y + yStep)
            {
                vm->pos = ZunVec3(x, y, z);
            }
        }
        return ZUN_ERROR;
    }

    if (!this->vertexBufferCurPtr)
    {
        this->ResetVertexBuffer();
    }

    const f32 halfWidth = vm->sprite->widthPx * vm->scale.x * 0.5f;
    const f32 halfHeight = vm->sprite->heightPx * vm->scale.y * 0.5f;
    const f32 u0 = vm->sprite->uvStart.x + vm->uvScrollPos.x;
    const f32 u1 = vm->sprite->uvEnd.x + vm->uvScrollPos.x;
    const f32 v0 = vm->sprite->uvStart.y + vm->uvScrollPos.y;
    const f32 v1 = vm->sprite->uvEnd.y + vm->uvScrollPos.y;
    ZunColor color = vm->useColor2 ? vm->color2 : vm->color;
    if (this->colorMulEnabled)
    {
        color.bytes.r = ZunColor::Multiply(color.bytes.r, this->color.bytes.r);
        color.bytes.g = ZunColor::Multiply(color.bytes.g, this->color.bytes.g);
        color.bytes.b = ZunColor::Multiply(color.bytes.b, this->color.bytes.b);
        color.bytes.a = ZunColor::Multiply(color.bytes.a, this->color.bytes.a);
    }

    bool frontendReady = false;
    u32 visibleCopies = 0u;
    for (f32 x = xStart; x < xEnd; x = x + xStep)
    {
        for (f32 y = yStart; y < yEnd; y = y + yStep)
        {
            vm->pos = ZunVec3(x, y, z);
            const f32 rawLeft =
                (vm->anchor & 1) ? vm->pos.x : vm->pos.x - halfWidth;
            const f32 rawRight = (vm->anchor & 1)
                                     ? vm->pos.x + halfWidth * 2.0f
                                     : vm->pos.x + halfWidth;
            const f32 rawTop =
                (vm->anchor & 2) ? vm->pos.y : vm->pos.y - halfHeight;
            const f32 rawBottom = (vm->anchor & 2)
                                      ? vm->pos.y + halfHeight * 2.0f
                                      : vm->pos.y + halfHeight;
            const f32 left =
                PspRenderFloor(rawLeft + this->offset.x + 0.5f);
            const f32 right =
                PspRenderFloor(rawRight + this->offset.x + 0.5f);
            const f32 top =
                PspRenderFloor(rawTop + this->offset.y + 0.5f);
            const f32 bottom =
                PspRenderFloor(rawBottom + this->offset.y + 0.5f);

            const f32 minX = std::min(left, right);
            const f32 maxX = std::max(left, right);
            const f32 minY = std::min(top, bottom);
            const f32 maxY = std::max(top, bottom);
            if (maxX < g_Supervisor.viewport.x ||
                maxY < g_Supervisor.viewport.y ||
                minX > g_Supervisor.viewport.x +
                           g_Supervisor.viewport.width ||
                minY > g_Supervisor.viewport.y +
                           g_Supervisor.viewport.height)
            {
                continue;
            }

            if (!frontendReady)
            {
                const GfxTextureHandle texture =
                    this->textures[vm->sprite->sourceFileIndex];
                if (this->currentTexture != texture)
                {
                    this->currentTexture = texture;
                    this->Flush();
                    g_Supervisor.gfxDevice->BindTexture(this->currentTexture);
                }
                if (this->currentVertexShader != 1)
                {
                    this->Flush();
                    this->currentVertexShader = 1;
                }
                SyncRenderState(vm);
                if (this->pspSpriteBatchUsesPairs !=
                    this->pspPreferSpritePairs)
                {
                    this->Flush();
                    this->pspSpriteBatchUsesPairs =
                        this->pspPreferSpritePairs;
                }
                frontendReady = true;
            }

            Th07PspSpriteVertex *out = this->vertexBufferCurPtr;
            WritePspSpriteVertex(out[0], left, top, vm->pos.z, u0, v0,
                                 color);
            if (this->pspSpriteBatchUsesPairs)
            {
                WritePspSpriteVertex(out[1], right, bottom, vm->pos.z, u1,
                                     v1, color);
                this->vertexBufferCurPtr += 2;
            }
            else
            {
                WritePspSpriteVertex(out[1], right, top, vm->pos.z, u1, v0,
                                     color);
                WritePspSpriteVertex(out[2], left, bottom, vm->pos.z, u0,
                                     v1, color);
                WritePspSpriteVertex(out[3], right, bottom, vm->pos.z, u1,
                                     v1, color);
                this->vertexBufferCurPtr += 4;
            }
            ++this->spritesToDraw;
            ++visibleCopies;
        }
    }

    // SyncRenderState's counter is observable to diagnostics. Preserve the
    // canonical one-call-per-visible-copy value without repeating its state
    // comparisons and timer probes.
    if (visibleCopies > 1u)
    {
        this->renderStateChangesThisFrame += visibleCopies - 1u;
    }
    return ZUN_SUCCESS;
}
#endif

void AnmManager::TranslateRotation(VertexTex1DiffuseXyzrhw *vertex, f32 width, f32 height, f32 sine,
                                   f32 cosine, f32 xOffset, f32 yOffset)
{
    vertex->pos.x = width * cosine - height * sine + xOffset;
    vertex->pos.y = width * sine + height * cosine + yOffset;
}

ZunResult AnmManager::Draw(AnmVm *vm)
{
    f32 cosZ;
    f32 sinZ;
    f32 xOffset;
    f32 yOffset;
    f32 z;
    f32 width;
    f32 height;

    if (vm->rotation.z == 0.0f)
    {
        return DrawNoRotation(vm);
    }
    if (!vm->visible)
    {
        return ZUN_ERROR;
    }
    if (!vm->active)
    {
        return ZUN_ERROR;
    }
    if (!vm->color.bytes.a)
    {
        return ZUN_ERROR;
    }

    z = vm->rotation.z;
#if defined(TH07_PSP)
    PspRenderSinCos(z, &sinZ, &cosZ);
#else
    sincosf(&sinZ, &cosZ, z);
#endif
    xOffset = vm->pos.x;
    yOffset = vm->pos.y;
    width = vm->sprite->widthPx * vm->scale.x / 2.0f;
    height = vm->sprite->heightPx * vm->scale.y / 2.0f;

    TranslateRotation(&g_QuadVertices[0], -width, -height, sinZ, cosZ, xOffset, yOffset);
    TranslateRotation(&g_QuadVertices[1], width, -height, sinZ, cosZ, xOffset, yOffset);
    TranslateRotation(&g_QuadVertices[2], -width, height, sinZ, cosZ, xOffset, yOffset);
    TranslateRotation(&g_QuadVertices[3], width, height, sinZ, cosZ, xOffset, yOffset);

    g_QuadVertices[0].pos.z = g_QuadVertices[1].pos.z = g_QuadVertices[2].pos.z =
        g_QuadVertices[3].pos.z = vm->pos.z;
    if ((vm->anchor & 1) != 0)
    {
        g_QuadVertices[0].pos.x += width;
        g_QuadVertices[1].pos.x += width;
        g_QuadVertices[2].pos.x += width;
        g_QuadVertices[3].pos.x += width;
    }
    if ((vm->anchor & 2) != 0)
    {
        g_QuadVertices[0].pos.y += height;
        g_QuadVertices[1].pos.y += height;
        g_QuadVertices[2].pos.y += height;
        g_QuadVertices[3].pos.y += height;
    }

    return DrawInner(vm, 0);
}

#if defined(TH07_PSP)
ZunResult AnmManager::DrawPspFastSprite(AnmVm *vm)
{
    if (!vm)
    {
        return ZUN_ERROR;
    }
    if (vm->rotation.z != 0.0f)
    {
        return Draw(vm);
    }
    this->pspPreferSpritePairs = vm->scale.x >= 0.0f && vm->scale.y >= 0.0f;
    const ZunResult result = DrawNoRotation(vm);
    this->pspPreferSpritePairs = 0;
    return result;
}

#if defined(TH07_PSP_ASCII_POPUP_BATCH)
ZunResult AnmManager::DrawPspAsciiPopupBatch(AnmVm *vm, AsciiManagerPopup *popups,
                                             i32 popupCount, f32 playerX, f32 playerY)
{
    if (!vm || !popups || popupCount < 0 || !vm->visible || !vm->active ||
        !std::isfinite(vm->scale.x) || vm->scale.x < 0.0f ||
        !std::isfinite(vm->scale.y) || vm->scale.y < 0.0f)
    {
#if defined(TH07_PSP_PERF_M2)
        ++gPspAsciiPopupBatchFallbacks;
#endif
        return ZUN_ERROR;
    }

    // Validate the entire frame before touching renderer or VM state.  A bad
    // popup must fall back atomically to the proven per-digit path; partial
    // fast-path output followed by fallback would double-draw the prefix.
    i32 sourceFileIndex = -1;
    unsigned int digitCount = 0;
    for (i32 i = 0; i < popupCount; ++i)
    {
        const AsciiManagerPopup &popup = popups[i];
        if (!popup.inUse)
        {
            continue;
        }
        if (popup.characterCount == 0 || popup.characterCount > sizeof(popup.digits))
        {
#if defined(TH07_PSP_PERF_M2)
            ++gPspAsciiPopupBatchFallbacks;
#endif
            return ZUN_ERROR;
        }
        digitCount += popup.characterCount;
        for (i32 j = 0; j < popup.characterCount; ++j)
        {
            const u8 digit = popup.digits[j];
            if (digit > 10)
            {
#if defined(TH07_PSP_PERF_M2)
                ++gPspAsciiPopupBatchFallbacks;
#endif
                return ZUN_ERROR;
            }
            i32 spriteIndex = digit;
            if (digit != 10 && popup.timer.current >= 52)
            {
                spriteIndex += popup.timer.current < 56 ? 11 : 21;
            }
            const AnmLoadedSprite &candidate = this->sprites[spriteIndex];
            const i32 candidateSource = candidate.sourceFileIndex;
            if (candidateSource < 0 ||
                (sourceFileIndex >= 0 && candidateSource != sourceFileIndex) ||
                !std::isfinite(candidate.widthPx) || candidate.widthPx <= 0.0f ||
                !std::isfinite(candidate.heightPx) || candidate.heightPx <= 0.0f)
            {
#if defined(TH07_PSP_PERF_M2)
                ++gPspAsciiPopupBatchFallbacks;
#endif
                return ZUN_ERROR;
            }
            sourceFileIndex = candidateSource;
        }
    }

    if (digitCount == 0)
    {
        return ZUN_SUCCESS;
    }

    if (!this->vertexBufferCurPtr)
    {
        this->ResetVertexBuffer();
    }

    bool batchStarted = false;
    for (i32 i = 0; i < popupCount; ++i)
    {
        AsciiManagerPopup &popup = popups[i];
        if (!popup.inUse)
        {
            continue;
        }

        vm->pos.x = popup.position.x - static_cast<f32>(popup.characterCount << 2);
        vm->pos.y = popup.position.y;
        vm->color.color = popup.color;

        const f32 dx = playerX - popup.position.x;
        const f32 dy = playerY - popup.position.y;
        i32 alpha = static_cast<i32>(dx * dx + dy * dy);
        if (alpha > 4096)
        {
            alpha = 208;
        }
        else if (alpha > 1024)
        {
            alpha = (alpha - 1024) * 128 / 3072 + 80;
        }
        else
        {
            alpha = 80;
        }

        u8 *digit = &popup.digits[popup.characterCount - 1];
        for (i32 j = popup.characterCount; j > 0; --j, --digit)
        {
            i32 spriteIndex = *digit;
            if (*digit != 10 && popup.timer.current >= 52)
            {
                spriteIndex += popup.timer.current < 56 ? 11 : 21;
            }
            vm->sprite = &this->sprites[spriteIndex];
            vm->color.bytes.a = alpha;

            const f32 halfWidth = vm->sprite->widthPx * vm->scale.x * 0.5f;
            const f32 halfHeight = vm->sprite->heightPx * vm->scale.y * 0.5f;
            const f32 rawLeft = (vm->anchor & 1) ? vm->pos.x : vm->pos.x - halfWidth;
            const f32 rawRight = (vm->anchor & 1)
                                     ? vm->pos.x + halfWidth * 2.0f
                                     : vm->pos.x + halfWidth;
            const f32 rawTop = (vm->anchor & 2) ? vm->pos.y : vm->pos.y - halfHeight;
            const f32 rawBottom = (vm->anchor & 2)
                                      ? vm->pos.y + halfHeight * 2.0f
                                      : vm->pos.y + halfHeight;
            const f32 left = PspRenderFloor(rawLeft + this->offset.x + 0.5f);
            const f32 right = PspRenderFloor(rawRight + this->offset.x + 0.5f);
            const f32 top = PspRenderFloor(rawTop + this->offset.y + 0.5f);
            const f32 bottom = PspRenderFloor(rawBottom + this->offset.y + 0.5f);

            if (right >= g_Supervisor.viewport.x &&
                bottom >= g_Supervisor.viewport.y &&
                left <= g_Supervisor.viewport.x + g_Supervisor.viewport.width &&
                top <= g_Supervisor.viewport.y + g_Supervisor.viewport.height)
            {
                if (!batchStarted)
                {
                    const GfxTextureHandle texture = this->textures[sourceFileIndex];
                    if (this->currentTexture != texture)
                    {
                        this->currentTexture = texture;
                        this->Flush();
                        g_Supervisor.gfxDevice->BindTexture(this->currentTexture);
                    }
                    if (this->currentVertexShader != 1)
                    {
                        this->Flush();
                        this->currentVertexShader = 1;
                    }
                    this->SyncRenderState(vm);
                    if (this->pspSpriteBatchUsesPairs != 1)
                    {
                        this->Flush();
                        this->pspSpriteBatchUsesPairs = 1;
                    }
                    batchStarted = true;
                }

                ZunColor color = vm->useColor2 ? vm->color2 : vm->color;
                if (this->colorMulEnabled)
                {
                    color.bytes.r = ZunColor::Multiply(color.bytes.r, this->color.bytes.r);
                    color.bytes.g = ZunColor::Multiply(color.bytes.g, this->color.bytes.g);
                    color.bytes.b = ZunColor::Multiply(color.bytes.b, this->color.bytes.b);
                    color.bytes.a = ZunColor::Multiply(color.bytes.a, this->color.bytes.a);
                }
                Th07PspSpriteVertex *out = this->vertexBufferCurPtr;
                WritePspSpriteVertex(out[0], left, top, vm->pos.z,
                                     vm->sprite->uvStart.x + vm->uvScrollPos.x,
                                     vm->sprite->uvStart.y + vm->uvScrollPos.y, color);
                WritePspSpriteVertex(out[1], right, bottom, vm->pos.z,
                                     vm->sprite->uvEnd.x + vm->uvScrollPos.x,
                                     vm->sprite->uvEnd.y + vm->uvScrollPos.y, color);
                this->vertexBufferCurPtr += 2;
                ++this->spritesToDraw;
            }
            vm->pos.x += 8.0f;
        }
    }

#if defined(TH07_PSP_PERF_M2)
    ++gPspAsciiPopupBatchCalls;
    gPspAsciiPopupBatchDigits += digitCount;
#endif
    return ZUN_SUCCESS;
}
#endif

#if defined(TH07_PSP_BULLET_UNIFIED_QUADS)
void AnmManager::BeginPspUnifiedBulletBatch()
{
    this->pspUnifiedBulletGeneralMode = 0;
}
#endif

#if defined(TH07_PSP_BULLET_ROTATED_DIRECT)
void AnmManager::BeginPspRotatedBulletBatch()
{
    // ZunViewport uses u32 members.  Preserve the legacy expression order:
    // add x+width/y+height in integer space, then perform the float conversion
    // once for the whole bullet draw callback instead of once per bullet.
    const ZunViewport &viewport = g_Supervisor.viewport;
    gPspRotatedViewportLeft = static_cast<float>(viewport.x);
    gPspRotatedViewportTop = static_cast<float>(viewport.y);
    gPspRotatedViewportRight = static_cast<float>(viewport.x + viewport.width);
    gPspRotatedViewportBottom = static_cast<float>(viewport.y + viewport.height);
}

__attribute__((noinline)) ZunResult
AnmManager::DrawPspRotatedBullet(AnmVm *vm, f32 cachedSin, f32 cachedCos)
{
    // Bullet::Draw routes only non-zero auto-rotation VMs here.  Keep all
    // observable validation, culling, renderer-state and vertex semantics of
    // DrawPspBullet, but place the pure corner math after the last possible
    // renderer call so its eight results never live across a call boundary.
    if (!vm || !vm->sprite || !vm->visible || !vm->active || !vm->color.bytes.a)
    {
        return ZUN_ERROR;
    }

    const float halfWidth = vm->sprite->widthPx * vm->scale.x * 0.5f;
    const float halfHeight = vm->sprite->heightPx * vm->scale.y * 0.5f;
#if defined(TH07_PSP_PERF_M2)
    ++gPspBulletFallbackEligible;
#endif
    const float centerX =
        vm->pos.x + this->offset.x + ((vm->anchor & 1) ? halfWidth : 0.0f);
    const float centerY =
        vm->pos.y + this->offset.y + ((vm->anchor & 2) ? halfHeight : 0.0f);
    const float bound = fabsf(halfWidth) + fabsf(halfHeight);
    if (centerX + bound < gPspRotatedViewportLeft ||
        centerY + bound < gPspRotatedViewportTop ||
        centerX - bound > gPspRotatedViewportRight ||
        centerY - bound > gPspRotatedViewportBottom)
    {
#if defined(TH07_PSP_PERF_M2)
        ++gPspBulletCullRejects;
#endif
        return ZUN_SUCCESS;
    }

    const GfxTextureHandle texture = this->textures[vm->sprite->sourceFileIndex];
    if (this->currentTexture != texture)
    {
        this->currentTexture = texture;
        this->Flush();
        g_Supervisor.gfxDevice->BindTexture(this->currentTexture);
    }
    if (this->currentVertexShader != 1)
    {
        this->Flush();
        this->currentVertexShader = 1;
    }

    ZunColor color = vm->useColor2 ? vm->color2 : vm->color;
    if (this->colorMulEnabled)
    {
        color.bytes.r = ZunColor::Multiply(color.bytes.r, this->color.bytes.r);
        color.bytes.g = ZunColor::Multiply(color.bytes.g, this->color.bytes.g);
        color.bytes.b = ZunColor::Multiply(color.bytes.b, this->color.bytes.b);
        color.bytes.a = ZunColor::Multiply(color.bytes.a, this->color.bytes.a);
    }

#if defined(TH07_PSP_PERF_M2)
    // M2 owns the state timer boundary; retain it in attribution builds.
    SyncRenderState(vm);
#else
    // Dense bullet runs almost always retain identical blend/depth state.
    // Preserve SyncRenderState's unconditional accounting on the hit path and
    // enter the existing slow function only when it has real work to do.
    const bool renderStateMatches =
        static_cast<u32>(this->currentBlendMode) == vm->blendMode &&
        (g_Supervisor.cfg.disableZBuffer ||
         static_cast<u32>(this->currentZWriteDisable) == vm->zWriteDisable);
    if (__builtin_expect(renderStateMatches, 1))
    {
        ++this->renderStateChangesThisFrame;
    }
    else
    {
        SyncRenderState(vm);
    }
#endif

    // rotation.z is non-zero at the only call site, so the legacy pair test is
    // always false.  Keep its flush before appending the first rotated quad.
    if (this->pspSpriteBatchUsesPairs != 0)
    {
        this->Flush();
        this->pspSpriteBatchUsesPairs = 0;
    }
#if defined(TH07_PSP_BULLET_UNIFIED_QUADS)
    // A drawable rotated bullet starts the general-quad portion of this
    // callback.  Keep later axis-aligned bullets in the same stream instead
    // of alternating pair/general batches.  Set the per-buffer bit only after
    // the possible pair flush above, because Flush() clears that bit.
    this->pspUnifiedBulletGeneralMode = 1;
    this->pspForceSpriteQuads = 1;
#endif

    const float u0 = vm->sprite->uvStart.x + vm->uvScrollPos.x;
    const float u1 = vm->sprite->uvEnd.x + vm->uvScrollPos.x;
    const float v0 = vm->sprite->uvStart.y + vm->uvScrollPos.y;
    const float v1 = vm->sprite->uvEnd.y + vm->uvScrollPos.y;
    const float z = vm->pos.z;
    const float posX = vm->pos.x;
    const float posY = vm->pos.y;
    const float offsetX = this->offset.x;
    const float offsetY = this->offset.y;
    const u32 anchor = vm->anchor;
    Th07PspSpriteVertex *out = this->vertexBufferCurPtr;

    // Do not reassociate these expressions.  Each corner deliberately keeps
    // DrawPspBullet's mul/sub-or-add/+pos/+offset/+anchor rounding order.
    const float localX0 = -halfWidth;
    const float localY0 = -halfHeight;
    float x0 = localX0 * cachedCos - localY0 * cachedSin + posX + offsetX;
    float y0 = localX0 * cachedSin + localY0 * cachedCos + posY + offsetY;
    if (anchor & 1)
    {
        x0 += halfWidth;
    }
    if (anchor & 2)
    {
        y0 += halfHeight;
    }
    WritePspSpriteVertex(out[0], x0, y0, z, u0, v0, color);

    const float localX1 = halfWidth;
    const float localY1 = -halfHeight;
    float x1 = localX1 * cachedCos - localY1 * cachedSin + posX + offsetX;
    float y1 = localX1 * cachedSin + localY1 * cachedCos + posY + offsetY;
    if (anchor & 1)
    {
        x1 += halfWidth;
    }
    if (anchor & 2)
    {
        y1 += halfHeight;
    }
    WritePspSpriteVertex(out[1], x1, y1, z, u1, v0, color);

    const float localX2 = -halfWidth;
    const float localY2 = halfHeight;
    float x2 = localX2 * cachedCos - localY2 * cachedSin + posX + offsetX;
    float y2 = localX2 * cachedSin + localY2 * cachedCos + posY + offsetY;
    if (anchor & 1)
    {
        x2 += halfWidth;
    }
    if (anchor & 2)
    {
        y2 += halfHeight;
    }
    WritePspSpriteVertex(out[2], x2, y2, z, u0, v1, color);

    const float localX3 = halfWidth;
    const float localY3 = halfHeight;
    float x3 = localX3 * cachedCos - localY3 * cachedSin + posX + offsetX;
    float y3 = localX3 * cachedSin + localY3 * cachedCos + posY + offsetY;
    if (anchor & 1)
    {
        x3 += halfWidth;
    }
    if (anchor & 2)
    {
        y3 += halfHeight;
    }
    WritePspSpriteVertex(out[3], x3, y3, z, u1, v1, color);

    this->vertexBufferCurPtr += 4;
    ++this->spritesToDraw;
    return ZUN_SUCCESS;
}
#endif

#if defined(TH07_PSP_BULLET_AXIS_FAST)
ZunResult AnmManager::DrawPspBullet(AnmVm *vm, const f32 *cachedSin, const f32 *cachedCos)
{
    // The dense Stage 4 boss patterns are dominated by non-rotated, positive-scale
    // bullets. Keep that common path out of the large rotated/mirrored function so it
    // does not reserve or materialize four-corner scratch arrays on every call.
    if (!vm || !vm->sprite || !vm->visible || !vm->active || !vm->color.bytes.a)
    {
        return ZUN_ERROR;
    }

    const float halfWidth = vm->sprite->widthPx * vm->scale.x * 0.5f;
    const float halfHeight = vm->sprite->heightPx * vm->scale.y * 0.5f;
    const bool axisEligible =
        vm->rotation.z == 0.0f && halfWidth >= 0.0f && halfHeight >= 0.0f;
#if defined(TH07_PSP_PERF_M2)
    if (axisEligible)
    {
        ++gPspBulletAxisEligible;
    }
    else
    {
        ++gPspBulletFallbackEligible;
    }
#endif
    // Keep the R19 visibility rule byte-for-byte equivalent in this first A/B.
    // Exact axis culling is a separate experiment if the direct-pair split is
    // insufficient.
    const float centerX =
        vm->pos.x + this->offset.x + ((vm->anchor & 1) ? halfWidth : 0.0f);
    const float centerY =
        vm->pos.y + this->offset.y + ((vm->anchor & 2) ? halfHeight : 0.0f);
    const float bound = fabsf(halfWidth) + fabsf(halfHeight);
    if (centerX + bound < g_Supervisor.viewport.x ||
        centerY + bound < g_Supervisor.viewport.y ||
        centerX - bound > g_Supervisor.viewport.x + g_Supervisor.viewport.width ||
        centerY - bound > g_Supervisor.viewport.y + g_Supervisor.viewport.height)
    {
#if defined(TH07_PSP_PERF_M2)
        ++gPspBulletCullRejects;
#endif
        return ZUN_SUCCESS;
    }
    if (__builtin_expect(!axisEligible, 0))
    {
        return DrawPspBulletFallback(vm, cachedSin, cachedCos);
    }

    const GfxTextureHandle texture = this->textures[vm->sprite->sourceFileIndex];
    if (this->currentTexture != texture)
    {
        this->currentTexture = texture;
        this->Flush();
        g_Supervisor.gfxDevice->BindTexture(this->currentTexture);
    }
    if (this->currentVertexShader != 1)
    {
        this->Flush();
        this->currentVertexShader = 1;
    }

    ZunColor color = vm->useColor2 ? vm->color2 : vm->color;
    if (this->colorMulEnabled)
    {
        color.bytes.r = ZunColor::Multiply(color.bytes.r, this->color.bytes.r);
        color.bytes.g = ZunColor::Multiply(color.bytes.g, this->color.bytes.g);
        color.bytes.b = ZunColor::Multiply(color.bytes.b, this->color.bytes.b);
        color.bytes.a = ZunColor::Multiply(color.bytes.a, this->color.bytes.a);
    }
    SyncRenderState(vm);

    if (this->pspSpriteBatchUsesPairs != 1)
    {
        this->Flush();
        this->pspSpriteBatchUsesPairs = 1;
    }

    // No calls remain after this point. Keep the four rounded endpoints in FPU
    // registers and write the final 48-byte GU_SPRITES pair directly.
    const float rawLeft = (vm->anchor & 1) ? vm->pos.x : vm->pos.x - halfWidth;
    const float rawRight =
        (vm->anchor & 1) ? vm->pos.x + halfWidth * 2.0f : vm->pos.x + halfWidth;
    const float rawTop = (vm->anchor & 2) ? vm->pos.y : vm->pos.y - halfHeight;
    const float rawBottom =
        (vm->anchor & 2) ? vm->pos.y + halfHeight * 2.0f : vm->pos.y + halfHeight;
    const float left = PspBulletFloor(rawLeft + this->offset.x + 0.5f);
    const float right = PspBulletFloor(rawRight + this->offset.x + 0.5f);
    const float top = PspBulletFloor(rawTop + this->offset.y + 0.5f);
    const float bottom = PspBulletFloor(rawBottom + this->offset.y + 0.5f);
    const float u0 = vm->sprite->uvStart.x + vm->uvScrollPos.x;
    const float u1 = vm->sprite->uvEnd.x + vm->uvScrollPos.x;
    const float v0 = vm->sprite->uvStart.y + vm->uvScrollPos.y;
    const float v1 = vm->sprite->uvEnd.y + vm->uvScrollPos.y;
    Th07PspSpriteVertex *out = this->vertexBufferCurPtr;
    WritePspSpriteVertex(out[0], left, top, vm->pos.z, u0, v0, color);
    WritePspSpriteVertex(out[1], right, bottom, vm->pos.z, u1, v1, color);
    this->vertexBufferCurPtr += 2;
    ++this->spritesToDraw;
    return ZUN_SUCCESS;
}

__attribute__((noinline)) ZunResult
AnmManager::DrawPspBulletFallback(AnmVm *vm, const f32 *cachedSin, const f32 *cachedCos)
#else
ZunResult AnmManager::DrawPspBullet(AnmVm *vm, const f32 *cachedSin, const f32 *cachedCos)
#endif
{
#if defined(TH07_PSP_PERF_M3)
    PspM3EmitterSample m3Sample;
#endif
    // TH06's largest SC-side win was avoiding the generic four-vertex
    // temporary plus a second six-vertex copy for every bullet.  TH07's
    // bullet VMs use the same simple screen-space sprite contract, so emit
    // their final triangle stream directly into the existing Anm batch.
    if (!vm || !vm->sprite || !vm->visible || !vm->active || !vm->color.bytes.a)
    {
        return ZUN_ERROR;
    }

    const float halfWidth = vm->sprite->widthPx * vm->scale.x * 0.5f;
    const float halfHeight = vm->sprite->heightPx * vm->scale.y * 0.5f;
#if defined(TH07_PSP_PERF_M3)
    m3Sample.Advance();
#endif
#if defined(TH07_PSP_PERF_M2) && !defined(TH07_PSP_BULLET_AXIS_FAST)
    const bool axisEligible =
        vm->rotation.z == 0.0f && halfWidth >= 0.0f && halfHeight >= 0.0f;
    if (axisEligible)
    {
        ++gPspBulletAxisEligible;
    }
    else
    {
        ++gPspBulletFallbackEligible;
    }
#endif
    // Special bullet commands retain some off-screen bullets for many frames.
    // Reject those using a conservative rotated bound before constructing and
    // scanning four corners.
    const float centerX =
        vm->pos.x + this->offset.x + ((vm->anchor & 1) ? halfWidth : 0.0f);
    const float centerY =
        vm->pos.y + this->offset.y + ((vm->anchor & 2) ? halfHeight : 0.0f);
    const float bound = fabsf(halfWidth) + fabsf(halfHeight);
    if (centerX + bound < g_Supervisor.viewport.x ||
        centerY + bound < g_Supervisor.viewport.y ||
        centerX - bound > g_Supervisor.viewport.x + g_Supervisor.viewport.width ||
        centerY - bound > g_Supervisor.viewport.y + g_Supervisor.viewport.height)
    {
#if defined(TH07_PSP_PERF_M3)
        m3Sample.NoteCull();
#endif
#if defined(TH07_PSP_PERF_M2) && !defined(TH07_PSP_BULLET_AXIS_FAST)
        ++gPspBulletCullRejects;
#endif
        return ZUN_SUCCESS;
    }
    float x[4];
    float y[4];
    if (vm->rotation.z == 0.0f)
    {
        const float rawLeft = (vm->anchor & 1) ? vm->pos.x : vm->pos.x - halfWidth;
        const float rawRight = (vm->anchor & 1) ? vm->pos.x + halfWidth * 2.0f
                                                : vm->pos.x + halfWidth;
        const float rawTop = (vm->anchor & 2) ? vm->pos.y : vm->pos.y - halfHeight;
        const float rawBottom = (vm->anchor & 2) ? vm->pos.y + halfHeight * 2.0f
                                                 : vm->pos.y + halfHeight;
        const float left = PspRenderFloor(rawLeft + this->offset.x + 0.5f);
        const float right = PspRenderFloor(rawRight + this->offset.x + 0.5f);
        const float top = PspRenderFloor(rawTop + this->offset.y + 0.5f);
        const float bottom = PspRenderFloor(rawBottom + this->offset.y + 0.5f);
        x[0] = x[2] = left;
        x[1] = x[3] = right;
        y[0] = y[1] = top;
        y[2] = y[3] = bottom;
    }
    else
    {
        float sine;
        float cosine;
        if (cachedSin && cachedCos)
        {
            sine = *cachedSin;
            cosine = *cachedCos;
        }
        else
        {
            PspRenderSinCos(vm->rotation.z, &sine, &cosine);
        }
        const float localX[4] = {-halfWidth, halfWidth, -halfWidth, halfWidth};
        const float localY[4] = {-halfHeight, -halfHeight, halfHeight, halfHeight};
        for (int i = 0; i < 4; ++i)
        {
            x[i] = localX[i] * cosine - localY[i] * sine + vm->pos.x + this->offset.x;
            y[i] = localX[i] * sine + localY[i] * cosine + vm->pos.y + this->offset.y;
            if (vm->anchor & 1)
            {
                x[i] += halfWidth;
            }
            if (vm->anchor & 2)
            {
                y[i] += halfHeight;
            }
        }
    }

#if defined(TH07_PSP_PERF_M3)
    m3Sample.Advance();
#endif

    const GfxTextureHandle texture = this->textures[vm->sprite->sourceFileIndex];
    if (this->currentTexture != texture)
    {
        this->currentTexture = texture;
        this->Flush();
        g_Supervisor.gfxDevice->BindTexture(this->currentTexture);
    }
    if (this->currentVertexShader != 1)
    {
        this->Flush();
        this->currentVertexShader = 1;
    }

    ZunColor color = vm->useColor2 ? vm->color2 : vm->color;
    if (this->colorMulEnabled)
    {
        color.bytes.r = ZunColor::Multiply(color.bytes.r, this->color.bytes.r);
        color.bytes.g = ZunColor::Multiply(color.bytes.g, this->color.bytes.g);
        color.bytes.b = ZunColor::Multiply(color.bytes.b, this->color.bytes.b);
        color.bytes.a = ZunColor::Multiply(color.bytes.a, this->color.bytes.a);
    }
    SyncRenderState(vm);

    const float u0 = vm->sprite->uvStart.x + vm->uvScrollPos.x;
    const float u1 = vm->sprite->uvEnd.x + vm->uvScrollPos.x;
    const float v0 = vm->sprite->uvStart.y + vm->uvScrollPos.y;
    const float v1 = vm->sprite->uvEnd.y + vm->uvScrollPos.y;
    const float z = vm->pos.z;
    const bool pairEligible =
        vm->rotation.z == 0.0f && x[0] <= x[3] && y[0] <= y[3];
#if defined(TH07_PSP_BULLET_UNIFIED_QUADS)
    if (!pairEligible)
    {
        this->pspUnifiedBulletGeneralMode = 1;
    }
    const bool usePairs = pairEligible && !this->pspUnifiedBulletGeneralMode;
#else
    const bool usePairs = pairEligible;
#endif
    if (this->pspSpriteBatchUsesPairs != usePairs)
    {
        this->Flush();
        this->pspSpriteBatchUsesPairs = usePairs;
    }
#if defined(TH07_PSP_PERF_M3)
    m3Sample.Advance();
    PspM3NoteBulletAppend(this);
#endif
    Th07PspSpriteVertex *out = this->vertexBufferCurPtr;
    WritePspSpriteVertex(out[0], x[0], y[0], z, u0, v0, color);
    if (usePairs)
    {
        WritePspSpriteVertex(out[1], x[3], y[3], z, u1, v1, color);
        this->vertexBufferCurPtr += 2;
    }
    else
    {
#if defined(TH07_PSP_BULLET_UNIFIED_QUADS)
        if (this->pspUnifiedBulletGeneralMode)
        {
            // Tell the backend that the mixed bullet stream is deliberately
            // one indexed-quad run. Without this bit it would rediscover each
            // axis sub-run and recreate the same submit storm downstream.
            this->pspForceSpriteQuads = 1;
        }
#endif
        WritePspSpriteVertex(out[1], x[1], y[1], z, u1, v0, color);
        WritePspSpriteVertex(out[2], x[2], y[2], z, u0, v1, color);
        WritePspSpriteVertex(out[3], x[3], y[3], z, u1, v1, color);
        this->vertexBufferCurPtr += 4;
    }
    ++this->spritesToDraw;
#if defined(TH07_PSP_PERF_M3)
    if (gPspM3FrontBatchOrigin == TH07_PSP_PERF_M3_BATCH_BULLET)
    {
        gPspM3BulletBatchEnd = this->vertexBufferCurPtr;
        gPspM3BulletBatchSprites = this->spritesToDraw;
    }
#endif
    return ZUN_SUCCESS;
}

#if defined(TH07_PSP_BULLET_SNAPSHOT_EMITTER)
void AnmManager::DrawPspBulletRecords(const PspBulletRenderRecord *records, u32 count)
{
    if (!records)
    {
        return;
    }

    // Records arrive in the exact collision-bucket/linked-list order used by
    // BulletManager::OnDraw.  Never sort by texture or state: alpha blending
    // makes that order part of the rendered result.
    for (u32 recordIndex = 0; recordIndex < count; ++recordIndex)
    {
        const PspBulletRenderRecord &record = records[recordIndex];
        if (!(record.flags & PSP_BULLET_RECORD_DRAWABLE))
        {
            continue;
        }

        const float halfWidth = record.halfWidth;
        const float halfHeight = record.halfHeight;
        const float rotationZ = record.rotationZ;
#if defined(TH07_PSP_PERF_M2)
        const bool axisEligible =
            rotationZ == 0.0f && halfWidth >= 0.0f && halfHeight >= 0.0f;
        if (axisEligible)
        {
            ++gPspBulletAxisEligible;
        }
        else
        {
            ++gPspBulletFallbackEligible;
        }
#endif
        const u32 anchor =
            (record.flags & PSP_BULLET_RECORD_ANCHOR_MASK) >>
            PSP_BULLET_RECORD_ANCHOR_SHIFT;
        const float centerX =
            record.posX + this->offset.x + ((anchor & 1u) ? halfWidth : 0.0f);
        const float centerY =
            record.posY + this->offset.y + ((anchor & 2u) ? halfHeight : 0.0f);
        const float bound = fabsf(halfWidth) + fabsf(halfHeight);
        if (centerX + bound < g_Supervisor.viewport.x ||
            centerY + bound < g_Supervisor.viewport.y ||
            centerX - bound > g_Supervisor.viewport.x + g_Supervisor.viewport.width ||
            centerY - bound > g_Supervisor.viewport.y + g_Supervisor.viewport.height)
        {
#if defined(TH07_PSP_PERF_M2)
            ++gPspBulletCullRejects;
#endif
            continue;
        }

        float x[4];
        float y[4];
        if (rotationZ == 0.0f)
        {
            const float rawLeft =
                (anchor & 1u) ? record.posX : record.posX - halfWidth;
            const float rawRight =
                (anchor & 1u) ? record.posX + halfWidth * 2.0f
                              : record.posX + halfWidth;
            const float rawTop =
                (anchor & 2u) ? record.posY : record.posY - halfHeight;
            const float rawBottom =
                (anchor & 2u) ? record.posY + halfHeight * 2.0f
                              : record.posY + halfHeight;
            const float left = PspRenderFloor(rawLeft + this->offset.x + 0.5f);
            const float right = PspRenderFloor(rawRight + this->offset.x + 0.5f);
            const float top = PspRenderFloor(rawTop + this->offset.y + 0.5f);
            const float bottom = PspRenderFloor(rawBottom + this->offset.y + 0.5f);
            x[0] = x[2] = left;
            x[1] = x[3] = right;
            y[0] = y[1] = top;
            y[2] = y[3] = bottom;
        }
        else
        {
            float sine;
            float cosine;
            if (record.flags & PSP_BULLET_RECORD_CACHED_SINCOS)
            {
                sine = record.sine;
                cosine = record.cosine;
            }
            else
            {
                PspRenderSinCos(rotationZ, &sine, &cosine);
            }
            const float localX[4] = {-halfWidth, halfWidth, -halfWidth, halfWidth};
            const float localY[4] = {-halfHeight, -halfHeight, halfHeight, halfHeight};
            for (int corner = 0; corner < 4; ++corner)
            {
                x[corner] = localX[corner] * cosine - localY[corner] * sine +
                            record.posX + this->offset.x;
                y[corner] = localX[corner] * sine + localY[corner] * cosine +
                            record.posY + this->offset.y;
                if (anchor & 1u)
                {
                    x[corner] += halfWidth;
                }
                if (anchor & 2u)
                {
                    y[corner] += halfHeight;
                }
            }
        }

        const GfxTextureHandle texture = this->textures[record.sourceFileIndex];
        if (this->currentTexture != texture)
        {
            this->currentTexture = texture;
            this->Flush();
            g_Supervisor.gfxDevice->BindTexture(this->currentTexture);
        }
        if (this->currentVertexShader != 1)
        {
            this->Flush();
            this->currentVertexShader = 1;
        }

        ZunColor color = record.color;
        if (this->colorMulEnabled)
        {
            color.bytes.r = ZunColor::Multiply(color.bytes.r, this->color.bytes.r);
            color.bytes.g = ZunColor::Multiply(color.bytes.g, this->color.bytes.g);
            color.bytes.b = ZunColor::Multiply(color.bytes.b, this->color.bytes.b);
            color.bytes.a = ZunColor::Multiply(color.bytes.a, this->color.bytes.a);
        }

#if defined(TH07_PSP_PERF_M2)
        Th07PspPerfInternalBegin(TH07_PSP_PERF_INTERNAL_STATE);
#endif
        const u32 blendMode =
            (record.flags & PSP_BULLET_RECORD_BLEND_ADD) ? 1u : 0u;
        if ((u32)this->currentBlendMode != blendMode)
        {
            this->Flush();
            this->currentBlendMode = blendMode;
            if (!this->currentBlendMode)
            {
                g_Supervisor.gfxDevice->SetBlendMode(BLEND_ALPHA, BLEND_ALPHA);
            }
            else
            {
                g_Supervisor.gfxDevice->SetBlendMode(BLEND_ALPHA, BLEND_ONE);
            }
        }
        const u32 zWriteDisable =
            (record.flags & PSP_BULLET_RECORD_ZWRITE_DISABLE) ? 1u : 0u;
        if (!g_Supervisor.cfg.disableZBuffer &&
            (u32)this->currentZWriteDisable != zWriteDisable)
        {
            this->Flush();
            this->currentZWriteDisable = zWriteDisable;
            g_Supervisor.gfxDevice->SetDepthMask(this->currentZWriteDisable == 0);
        }
        ++this->renderStateChangesThisFrame;
#if defined(TH07_PSP_PERF_M2)
        Th07PspPerfInternalEnd(TH07_PSP_PERF_INTERNAL_STATE);
#endif

        const bool usePairs = rotationZ == 0.0f && x[0] <= x[3] && y[0] <= y[3];
        if (this->pspSpriteBatchUsesPairs != usePairs)
        {
            this->Flush();
            this->pspSpriteBatchUsesPairs = usePairs;
        }
        Th07PspSpriteVertex *out = this->vertexBufferCurPtr;
        WritePspSpriteVertex(out[0], x[0], y[0], record.posZ, record.u0, record.v0,
                             color);
        if (usePairs)
        {
            WritePspSpriteVertex(out[1], x[3], y[3], record.posZ, record.u1,
                                 record.v1, color);
            this->vertexBufferCurPtr += 2;
        }
        else
        {
            WritePspSpriteVertex(out[1], x[1], y[1], record.posZ, record.u1,
                                 record.v0, color);
            WritePspSpriteVertex(out[2], x[2], y[2], record.posZ, record.u0,
                                 record.v1, color);
            WritePspSpriteVertex(out[3], x[3], y[3], record.posZ, record.u1,
                                 record.v1, color);
            this->vertexBufferCurPtr += 4;
        }
        ++this->spritesToDraw;
    }
}
#endif
#endif

ZunResult AnmManager::DrawFacingCamera(AnmVm *vm)
{
    f32 centerY;
    f32 centerX;

    if (!vm->visible)
    {
        return ZUN_ERROR;
    }
    if (!vm->active)
    {
        return ZUN_ERROR;
    }
    if (!vm->color.bytes.a)
    {
        return ZUN_ERROR;
    }

    centerX = vm->sprite->widthPx * vm->scale.x / 2.0f;
    centerY = vm->sprite->heightPx * vm->scale.y / 2.0f;

    if ((vm->anchor & 1) == 0)
    {
        g_QuadVertices[0].pos.x = g_QuadVertices[2].pos.x = vm->pos.x - centerX;
        g_QuadVertices[1].pos.x = g_QuadVertices[3].pos.x = centerX + vm->pos.x;
    }
    else
    {
        g_QuadVertices[0].pos.x = g_QuadVertices[2].pos.x = vm->pos.x;
        g_QuadVertices[1].pos.x = g_QuadVertices[3].pos.x = centerX + vm->pos.x + centerX;
    }

    if ((vm->anchor & 2) == 0)
    {
        g_QuadVertices[0].pos.y = g_QuadVertices[1].pos.y = vm->pos.y - centerY;
        g_QuadVertices[2].pos.y = g_QuadVertices[3].pos.y = centerY + vm->pos.y;
    }
    else
    {
        g_QuadVertices[0].pos.y = g_QuadVertices[1].pos.y = vm->pos.y;
        g_QuadVertices[2].pos.y = g_QuadVertices[3].pos.y = centerY + vm->pos.y + centerY;
    }

    g_QuadVertices[0].pos.z = g_QuadVertices[1].pos.z = g_QuadVertices[2].pos.z =
        g_QuadVertices[3].pos.z = vm->pos.z;

    return DrawInner(vm, 0);
}

ZunResult AnmManager::CalcBillboardTransform(AnmVm *vm)
{
    f32 halfWidth;
    f32 halfHeight;
    f32 screenCenterY;
    f32 halfLength; // also used as screen center x
    f32 sinZ;
    ZunMatrix matrix;
    f32 z = vm->rotation.z;
    ZunVec3 projectRight;
    ZunVec3 projectCenter;
    ZunVec3 projectRightOffset;
    f32 cosZ;

#if defined(TH07_PSP)
    PspRenderSinCos(z, &sinZ, &cosZ);
#else
    sincosf(&sinZ, &cosZ, z);
#endif

    ZunVec3 origin(0.0f, 0.0f, 0.0f);

    matrix.Identity();
    matrix.m[3][0] = vm->pos.x;
    matrix.m[3][1] = vm->pos.y;
    matrix.m[3][2] = vm->pos.z;

    projectCenter.Project(&origin, &g_Supervisor.viewport, &g_Supervisor.projectionMatrix,
                          &g_Supervisor.viewMatrix, &matrix);

    if (projectCenter.z < 0.0f || projectCenter.z > 1.0f)
    {
        return ZUN_ERROR;
    }

    projectRight.Project(&g_Stage.cam.right, &g_Supervisor.viewport, &g_Supervisor.projectionMatrix,
                         &g_Supervisor.viewMatrix, &matrix);

    projectRightOffset = projectRight - projectCenter;

    halfLength = projectRightOffset.Length() * 0.5f;
    halfWidth = halfLength * vm->sprite->widthPx * vm->scale.x;
    halfHeight = halfLength * vm->sprite->heightPx * vm->scale.y;

    halfLength = projectCenter.x; // used as screen center x here
    screenCenterY = projectCenter.y;

    TranslateRotation(&g_QuadVertices[0], -halfWidth, -halfHeight, sinZ, cosZ, halfLength,
                      screenCenterY);
    TranslateRotation(&g_QuadVertices[1], halfWidth, -halfHeight, sinZ, cosZ, halfLength,
                      screenCenterY);
    TranslateRotation(&g_QuadVertices[2], -halfWidth, halfHeight, sinZ, cosZ, halfLength,
                      screenCenterY);
    TranslateRotation(&g_QuadVertices[3], halfWidth, halfHeight, sinZ, cosZ, halfLength,
                      screenCenterY);

    g_QuadVertices[0].pos.z = g_QuadVertices[1].pos.z = g_QuadVertices[2].pos.z =
        g_QuadVertices[3].pos.z = projectCenter.z;

    if ((vm->anchor & 1) != 0)
    {
        g_QuadVertices[0].pos.x += halfWidth;
        g_QuadVertices[1].pos.x += halfWidth;
        g_QuadVertices[2].pos.x += halfWidth;
        g_QuadVertices[3].pos.x += halfWidth;
    }
    if ((vm->anchor & 2) != 0)
    {
        g_QuadVertices[0].pos.y += halfHeight;
        g_QuadVertices[1].pos.y += halfHeight;
        g_QuadVertices[2].pos.y += halfHeight;
        g_QuadVertices[3].pos.y += halfHeight;
    }
    return ZUN_SUCCESS;
}

ZunResult AnmManager::DrawBillboard(AnmVm *vm)
{
    if (!vm->visible)
    {
        return ZUN_ERROR;
    }

    if (!vm->active)
    {
        return ZUN_ERROR;
    }

    if (!vm->color.bytes.a)
    {
        return ZUN_ERROR;
    }

    if (CalcBillboardTransform(vm) != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }

    return DrawInner(vm, 0);
}

void AnmManager::CalcProjectedTransform(AnmVm *vm)
{
    ZunMatrix world;
    ZunMatrix rot;

    if (vm->skipTransform == 0 && (vm->updateScale || vm->updateRotation))
    {
        vm->worldTransformMatrix = vm->matrix;
        vm->worldTransformMatrix.m[0][0] *= vm->scale.x;
        vm->worldTransformMatrix.m[1][1] *= vm->scale.y;
        vm->updateScale = 0;
        if (vm->rotation.x != 0.0)
        {
            rot.RotateX(vm->rotation.x);
            vm->worldTransformMatrix *= rot;
        }
        if (vm->rotation.y != 0.0)
        {
            rot.RotateY(vm->rotation.y);
            vm->worldTransformMatrix *= rot;
        }
        if (vm->rotation.z != 0.0)
        {
            rot.RotateZ(vm->rotation.z);
            vm->worldTransformMatrix *= rot;
        }
        vm->updateRotation = 0;
    }

    world = vm->worldTransformMatrix;
    if ((vm->anchor & 1) == 0)
    {
        world.m[3][0] = vm->pos.x;
    }
    else
    {
        world.m[3][0] = fabsf(vm->sprite->widthPx * vm->scale.x / 2.0f) + vm->pos.x;
    }

    if ((vm->anchor & 2) == 0)
    {
        world.m[3][1] = vm->pos.y;
    }
    else
    {
        world.m[3][1] = fabsf(vm->sprite->heightPx * vm->scale.y / 2.0f) + vm->pos.y;
    }
    world.m[3][2] = vm->pos.z;

    g_QuadVertices[0].pos.Project(&this->vertexBufferContents[0].position, &g_Supervisor.viewport,
                                  &g_Supervisor.projectionMatrix, &g_Supervisor.viewMatrix, &world);
    g_QuadVertices[1].pos.Project(&this->vertexBufferContents[1].position, &g_Supervisor.viewport,
                                  &g_Supervisor.projectionMatrix, &g_Supervisor.viewMatrix, &world);
    g_QuadVertices[2].pos.Project(&this->vertexBufferContents[2].position, &g_Supervisor.viewport,
                                  &g_Supervisor.projectionMatrix, &g_Supervisor.viewMatrix, &world);
    g_QuadVertices[3].pos.Project(&this->vertexBufferContents[3].position, &g_Supervisor.viewport,
                                  &g_Supervisor.projectionMatrix, &g_Supervisor.viewMatrix, &world);

    this->matrix = world;
}

ZunResult AnmManager::DrawProjected(AnmVm *vm)
{
    if (!vm->visible)
    {
        return ZUN_ERROR;
    }

    if (!vm->active)
    {
        return ZUN_ERROR;
    }

    if (!vm->color.bytes.a)
    {
        return ZUN_ERROR;
    }

    CalcProjectedTransform(vm);
    return DrawInner(vm, 0);
}

ZunResult AnmManager::Draw3(AnmVm *vm)
{
    ZunMatrix world;
    ZunMatrix rot;
    ZunMatrix uv;

    if (!vm->visible)
    {
        return ZUN_ERROR;
    }

    if (!vm->active)
    {
        return ZUN_ERROR;
    }

    if (!vm->color.bytes.a)
    {
        return ZUN_ERROR;
    }

    if (this->spritesToDraw != 0)
    {
        this->Flush();
    }

    if (vm->skipTransform == 0 && (vm->updateScale || vm->updateRotation))
    {
        vm->worldTransformMatrix = vm->matrix;
        vm->worldTransformMatrix.m[0][0] *= vm->scale.x;
        vm->worldTransformMatrix.m[1][1] *= vm->scale.y;
        vm->updateScale = 0;

        // double intentionally used here
        if (vm->rotation.x != 0.0)
        {
            rot.RotateX(vm->rotation.x);
            vm->worldTransformMatrix *= rot;
        }
        if (vm->rotation.y != 0.0)
        {
            rot.RotateY(vm->rotation.y);
            vm->worldTransformMatrix *= rot;
        }
        if (vm->rotation.z != 0.0)
        {
            rot.RotateZ(vm->rotation.z);
            vm->worldTransformMatrix *= rot;
        }
        vm->updateRotation = 0;
    }

    world = vm->worldTransformMatrix;
    if ((vm->anchor & 1) == 0)
    {
        world.m[3][0] = vm->pos.x;
    }
    else
    {
        world.m[3][0] = fabsf(vm->sprite->widthPx * vm->scale.x / 2.0f) + vm->pos.x;
    }

    if ((vm->anchor & 2) == 0)
    {
        world.m[3][1] = vm->pos.y;
    }
    else
    {
        world.m[3][1] = fabsf(vm->sprite->heightPx * vm->scale.y / 2.0f) + vm->pos.y;
    }

    world.m[3][0] += this->offset.x;
    world.m[3][1] += this->offset.y;

    SetRenderStateForVm(vm);
    world.m[3][2] = vm->pos.z;

    g_Supervisor.gfxDevice->SetTransformMatrix(MATRIX_MODEL, world);

    if (this->currentSprite != vm->sprite)
    {
        this->currentSprite = vm->sprite;
        uv = vm->uvMatrix;
        uv.m[2][0] = vm->sprite->uvStart.x + vm->uvScrollPos.x;
        uv.m[2][1] = vm->sprite->uvStart.y + vm->uvScrollPos.y;
        g_Supervisor.gfxDevice->SetTransformMatrix(MATRIX_TEXTURE, uv);

        if (this->currentTexture != this->textures[vm->sprite->sourceFileIndex])
        {
            this->currentTexture = this->textures[vm->sprite->sourceFileIndex];
            g_Supervisor.gfxDevice->BindTexture(this->currentTexture);
        }
    }

    if (this->currentVertexShader != 2)
    {
        g_Supervisor.gfxDevice->SetColorOp(COMPONENT_ALPHA, COLOR_OP_MODULATE);
        g_Supervisor.gfxDevice->SetColorOp(COMPONENT_RGB, COLOR_OP_MODULATE);
        // The no-vertex-buffer fallback carries each VM's colour in the
        // submitted vertices.  Using TFACTOR here would replace that alpha
        // with the backend's stale constant and make fading 3D backgrounds
        // opaque, erasing Stage 3's preserved-frame afterimage.
        g_Supervisor.gfxDevice->SetTextureArg(g_Supervisor.cfg.noVertexBuffers
                                                  ? TEX_ARG_DIFFUSE
                                                  : TEX_ARG_TFACTOR);
        this->currentVertexShader = 2;
    }

    if (!g_Supervisor.cfg.noVertexBuffers)
    {
        g_Supervisor.gfxDevice->DrawPrimitive(PRIM_TRIANGLE_STRIP, 0, 2);
    }
    else
    {
        g_Supervisor.gfxDevice->DrawPrimitiveUP(PRIM_TRIANGLE_STRIP, 2, g_Quad3DFallback,
                                                sizeof(VertexTex1DiffuseXyz));
    }
    return ZUN_SUCCESS;
}

f32 AnmVm::GetFloatVarValue(f32 arg)
{
    switch ((i32)arg)
    {
    case 10000:
        return (f32)this->intVars1[0];
    case 10001:
        return (f32)this->intVars1[1];
    case 10002:
        return (f32)this->intVars1[2];
    case 10003:
        return (f32)this->intVars1[3];
    case 10004:
        return this->floatVars[0];
    case 10005:
        return this->floatVars[1];
    case 10006:
        return this->floatVars[2];
    case 10007:
        return this->floatVars[3];
    case 10008:
        return (f32)this->intVars2[0];
    case 10009:
        return (f32)this->intVars2[1];
    default:
        return arg;
    }
}

i32 AnmVm::GetVarValue(i32 arg)
{
    switch (arg)
    {
    case 10000:
        return this->intVars1[0];
    case 10001:
        return this->intVars1[1];
    case 10002:
        return this->intVars1[2];
    case 10003:
        return this->intVars1[3];
    case 10004:
        return this->floatVars[0];
    case 10005:
        return this->floatVars[1];
    case 10006:
        return this->floatVars[2];
    case 10007:
        return this->floatVars[3];
    case 10008:
        return this->intVars2[0];
    case 10009:
        return this->intVars2[1];
    default:
        return arg;
    }
}

f32 *AnmVm::GetFloatVar(f32 *paramId, u16 mask, u32 idx)
{
    if (((u32)mask & 1 << idx) == 0)
    {
        return paramId;
    }

    switch ((u32)*paramId)
    {
    case 10004:
        return &this->floatVars[0];
    case 10005:
        return &this->floatVars[1];
    case 10006:
        return &this->floatVars[2];
    case 10007:
        return &this->floatVars[3];
    default:
        return paramId;
    }
}

i32 *AnmVm::GetVar(i32 *paramId, u16 mask, u32 idx)
{
    if (((u32)mask & 1 << idx) == 0)
    {
        return paramId;
    }

    switch (*paramId)
    {
    case 10000:
        return &this->intVars1[0];
    case 10001:
        return &this->intVars1[1];
    case 10002:
        return &this->intVars1[2];
    case 10003:
        return &this->intVars1[3];
    case 10008:
        return &this->intVars2[0];
    case 10009:
        return &this->intVars2[1];
    default:
        return paramId;
    }
}

i32 AnmManager::ExecuteScript(AnmVm *vm)
{
    AnmRawInstr *instr;
    AnmRawInstr *nextInstr;
    i32 i;
    f32 t;

#define GET_INT_PTR(argIdx) vm->GetVar(&instr->args[argIdx].i, instr->flags, argIdx)

#define GET_FLOAT_PTR(argIdx) vm->GetFloatVar(&instr->args[argIdx].f, instr->flags, argIdx)

#define GET_INT_VALUE(argIdx)                                                                      \
    (((instr->flags & (1 << argIdx)) != 0) ? vm->GetVarValue(instr->args[argIdx].i)                \
                                           : instr->args[argIdx].i)

#define GET_FLOAT_VALUE(argIdx)                                                                    \
    (((instr->flags & (1 << argIdx)) != 0) ? vm->GetFloatVarValue(instr->args[argIdx].f)           \
                                           : instr->args[argIdx].f)

    if (!vm->currentInstruction)
    {
        return 1;
    }

    if (vm->pendingInterrupt != 0)
    {
        goto handle_interrupt;
    }

WHY_NOT_JUST_CONTINUE:
    instr = vm->currentInstruction;
    while (instr->time <= vm->currentTimeInScript.GetCurrent())
    {
        switch (instr->opcode)
        {
        case ANM_EXIT_HIDE:
        case ANM_EXIT_HIDE2:
            vm->visible = 0;
        case ANM_EXIT:
            vm->currentInstruction = NULL;
            return 1;
        case ANM_SET_ACTIVE_SPRITE:
            vm->visible = 1;
            SetActiveSprite(vm, GET_INT_VALUE(0) + this->spriteIndices[vm->anmFileIdx]);
            vm->timeOfLastSpriteSet = vm->currentTimeInScript.GetCurrent();
            break;
        case ANM_SET_SCALE:
            vm->scale.x = GET_FLOAT_VALUE(0);
            vm->scale.y = GET_FLOAT_VALUE(1);
            vm->updateScale = 1;
            break;
        case ANM_SET_ALPHA:
            vm->color.bytes.a = instr->args[0].i & 255;
            break;
        case ANM_SET_COLOR:
            vm->color.color = (vm->color.color & 0xff000000) | (instr->args[0].i & 0xffffff);
            break;
        case ANM_JUMP:
            vm->currentTimeInScript = instr->args[1].i;
            vm->currentInstruction =
                (AnmRawInstr *)((u8 *)vm->beginningOfScript + instr->args[0].i);
            goto WHY_NOT_JUST_CONTINUE;
        case ANM_DEC_JUMP:
            (*GET_INT_PTR(0))--;
            if (GET_INT_VALUE(0) > 0)
            {
                vm->currentTimeInScript = instr->args[2].i;
                vm->currentInstruction =
                    (AnmRawInstr *)((u8 *)vm->beginningOfScript + instr->args[1].i);
                goto WHY_NOT_JUST_CONTINUE;
            }
            break;
        case ANM_FLIP_X:
            vm->flip ^= 1;
            vm->scale.x *= -1.0f;
            vm->updateScale = 1;
            break;
        case ANM_SET_USE_OFFSET:
            vm->useOffset = instr->args[0].i;
            break;
        case ANM_FLIP_Y:
            vm->flip ^= 2;
            vm->scale.y *= -1.0f;
            vm->updateScale = 1;
            break;
        case ANM_SET_ROTATION:
            vm->rotation.x = GET_FLOAT_VALUE(0);
            vm->rotation.y = GET_FLOAT_VALUE(1);
            vm->rotation.z = GET_FLOAT_VALUE(2);
            vm->updateRotation = 1;
            break;
        case ANM_SET_ANGLE_VEL:
            vm->angleVel.x = GET_FLOAT_VALUE(0);
            vm->angleVel.y = GET_FLOAT_VALUE(1);
            vm->angleVel.z = GET_FLOAT_VALUE(2);
            vm->updateRotation = 1;
            break;
        case ANM_SET_SCALE_SPEED:
            vm->scaleGrowth.x = GET_FLOAT_VALUE(0);
            vm->scaleGrowth.y = GET_FLOAT_VALUE(1);
            break;
        case ANM_INTERP_SCALE:
            vm->interpStartTimes[4] = 0;
            vm->interpEndTimes[4] = GET_INT_VALUE(2);
            vm->interpModes[4] = 0;
            vm->scaleInterpInitial = vm->scale;
            vm->scaleInterpFinal.x = GET_FLOAT_VALUE(0);
            vm->scaleInterpFinal.y = GET_FLOAT_VALUE(1);
            break;
        case ANM_FADE:
            vm->colorInterpInitialColor.bytes.a = vm->color.bytes.a;
            vm->colorInterpFinalColor.bytes.a = instr->args[0].b[0];
            vm->interpStartTimes[2] = 0;
            vm->interpEndTimes[2] = GET_INT_VALUE(1);
            vm->interpModes[2] = 0;
            break;
        case ANM_SET_BLEND:
            vm->blendMode = instr->args[0].i;
            break;
        case ANM_SET_TRANSLATION:
            if (!vm->useOffset)
            {
                vm->pos = ZunVec3(GET_FLOAT_VALUE(0), GET_FLOAT_VALUE(1), GET_FLOAT_VALUE(2));
            }
            else
            {
                vm->offset = ZunVec3(GET_FLOAT_VALUE(0), GET_FLOAT_VALUE(1), GET_FLOAT_VALUE(2));
            }
            break;
        case ANM_POS_TIME_ACCEL:
            vm->interpModes[0] = 6;
            goto interp_pos;
        case ANM_POS_TIME_DECEL:
            vm->interpModes[0] = 4;
            goto interp_pos;
        case ANM_POS_TIME_LINEAR:
            vm->interpModes[0] = 0;
        interp_pos:
            if (!vm->useOffset)
            {
                vm->posInterpInitial = vm->pos;
            }
            else
            {
                vm->posInterpInitial = vm->offset;
            }
            vm->posInterpFinal =
                ZunVec3(GET_FLOAT_VALUE(0), GET_FLOAT_VALUE(1), GET_FLOAT_VALUE(2));
            vm->interpEndTimes[0] = GET_INT_VALUE(3);
            vm->interpStartTimes[0] = 0;
            break;
        case ANM_WAIT:
            if (vm->waitTimer == 0)
            {
                vm->waitTimer = GET_INT_VALUE(0);
            }
            else
            {
                vm->waitTimer--;
            }
            if (vm->waitTimer <= 0)
            {
                vm->waitTimer = 0;
                break;
            }
            vm->currentTimeInScript--;
            goto stop;
        case ANM_STOP_HIDE:
            vm->visible = 0;
        case ANM_STOP:
            if (!vm->pendingInterrupt)
            {
                vm->isStopped = 1;
                vm->currentTimeInScript--;
                goto stop;
            }
        handle_interrupt:
            nextInstr = NULL;
            instr = vm->beginningOfScript;
            while ((instr->opcode != ANM_INTERRUPT_LABEL ||
                    (i32)vm->pendingInterrupt != instr->args[0].i) &&
                   instr->opcode != ANM_EXIT_HIDE)
            {
                if (instr->opcode == ANM_INTERRUPT_LABEL && instr->args[0].i == -1)
                {
                    nextInstr = instr;
                }
                instr = (AnmRawInstr *)((u8 *)instr + instr->size);
            }
            vm->pendingInterrupt = 0;
            vm->isStopped = 0;
            if (instr->opcode != ANM_INTERRUPT_LABEL)
            {
                if (!nextInstr)
                {
                    vm->currentTimeInScript--;
                    goto stop;
                }
                instr = nextInstr;
            }

            instr = (AnmRawInstr *)((u8 *)instr + instr->size);
            vm->currentInstruction = instr;
            vm->currentTimeInScript = vm->currentInstruction->time;
            vm->visible = 1;
            goto WHY_NOT_JUST_CONTINUE;
        case ANM_SET_VISIBILITY:
            vm->visible = instr->args[0].i;
            break;
        case ANM_22:
            vm->anchor = 3;
            break;
        case ANM_SET_AUTO_ROTATE:
            vm->autoRotate = instr->args[0].us[0];
            break;
        case ANM_SET_SCROLL_POS_X:
            vm->uvScrollPos.x += GET_FLOAT_VALUE(0);
            if (vm->uvScrollPos.x >= 1.0f)
            {
                vm->uvScrollPos.x -= 1.0f;
            }
            else
            {
                if (vm->uvScrollPos.x < 0.0f)
                {
                    vm->uvScrollPos.x += 1.0f;
                }
            }
            break;
        case ANM_SET_SCROLL_POS_Y:
            vm->uvScrollPos.y += GET_FLOAT_VALUE(0);
            if (vm->uvScrollPos.y >= 1.0f)
            {
                vm->uvScrollPos.y -= 1.0f;
            }
            else
            {
                if (vm->uvScrollPos.y < 0.0f)
                {
                    vm->uvScrollPos.y += 1.0f;
                }
            }
            break;
        case ANM_SET_SCROLLVEL_X:
            vm->uvScrollVel.x = GET_FLOAT_VALUE(0);
            break;
        case ANM_SET_SCROLLVEL_Y:
            vm->uvScrollVel.y = GET_FLOAT_VALUE(0);
            break;
        case ANM_SET_ZWRITE_DISABLE:
            vm->zWriteDisable = instr->args[0].i;
            break;
        case ANM_SET_CAMERA_MODE:
            vm->cameraMode = instr->args[0].i;
            break;
        case ANM_INTERP_POS:
            vm->interpStartTimes[0] = 0;
            vm->interpEndTimes[0] = GET_INT_VALUE(0);
            vm->interpModes[0] = instr->args[1].b[0];
            if (!vm->useOffset)
            {
                vm->posInterpInitial = vm->pos;
            }
            else
            {
                vm->posInterpInitial = vm->offset;
            }
            vm->posInterpFinal.x = GET_FLOAT_VALUE(2);
            vm->posInterpFinal.y = GET_FLOAT_VALUE(3);
            vm->posInterpFinal.z = GET_FLOAT_VALUE(4);
            break;
        case ANM_INTERP_COLOR:
            vm->interpStartTimes[1] = 0;
            vm->interpEndTimes[1] = GET_INT_VALUE(0);
            vm->interpModes[1] = instr->args[1].b[0];
            vm->colorInterpInitialColor.bytes.r = vm->color.bytes.r;
            vm->colorInterpInitialColor.bytes.g = vm->color.bytes.g;
            vm->colorInterpInitialColor.bytes.b = vm->color.bytes.b;
            vm->colorInterpFinalColor.bytes.r = instr->args[2].b[0];
            vm->colorInterpFinalColor.bytes.g = instr->args[2].b[1];
            vm->colorInterpFinalColor.bytes.b = instr->args[2].b[2];
            break;
        case ANM_INTERP_ALPHA:
            vm->interpStartTimes[2] = 0;
            vm->interpEndTimes[2] = GET_INT_VALUE(0);
            vm->interpModes[2] = instr->args[1].b[0];
            vm->colorInterpInitialColor.bytes.a = vm->color.bytes.a;
            vm->colorInterpFinalColor.bytes.a = instr->args[2].b[0];
            break;
        case ANM_INTERP_ROTATE:
            vm->interpStartTimes[3] = 0;
            vm->interpEndTimes[3] = GET_INT_VALUE(0);
            vm->interpModes[3] = instr->args[1].b[0];
            vm->rotateInterpInitial = vm->rotation;
            vm->rotateInterpFinal.x = GET_FLOAT_VALUE(2);
            vm->rotateInterpFinal.y = GET_FLOAT_VALUE(3);
            vm->rotateInterpFinal.z = GET_FLOAT_VALUE(4);
            vm->updateRotation = 1;
            break;
        case ANM_INTERP_SCALE_2:
            vm->interpStartTimes[4] = 0;
            vm->interpEndTimes[4] = GET_INT_VALUE(0);
            vm->interpModes[4] = instr->args[1].b[0];
            vm->scaleInterpInitial = vm->scale;
            vm->scaleInterpFinal.x = GET_FLOAT_VALUE(2);
            vm->scaleInterpFinal.y = GET_FLOAT_VALUE(3);
            vm->updateScale = 1;
            break;
        case ANM_MOV:
            *GET_INT_PTR(0) = GET_INT_VALUE(1);
            break;
        case ANM_MOV_FLOAT:
            *GET_FLOAT_PTR(0) = GET_FLOAT_VALUE(1);
            break;
        case ANM_ADD_2:
            *GET_INT_PTR(0) = GET_INT_VALUE(1) + GET_INT_VALUE(2);
            break;
        case ANM_ADD_FLOAT_2:
            *GET_FLOAT_PTR(0) = GET_FLOAT_VALUE(1) + GET_FLOAT_VALUE(2);
            break;
        case ANM_SUB_2:
            *GET_INT_PTR(0) = GET_INT_VALUE(1) - GET_INT_VALUE(2);
            break;
        case ANM_SUB_FLOAT_2:
            *GET_FLOAT_PTR(0) = GET_FLOAT_VALUE(1) - GET_FLOAT_VALUE(2);
            break;
        case ANM_MUL_2:
            *GET_INT_PTR(0) = GET_INT_VALUE(1) * GET_INT_VALUE(2);
            break;
        case ANM_MUL_FLOAT_2:
            *GET_FLOAT_PTR(0) = GET_FLOAT_VALUE(1) * GET_FLOAT_VALUE(2);
            break;
        case ANM_DIV_2:
            *GET_INT_PTR(0) = GET_INT_VALUE(1) / GET_INT_VALUE(2);
            break;
        case ANM_DIV_FLOAT_2:
            *GET_FLOAT_PTR(0) = GET_FLOAT_VALUE(1) / GET_FLOAT_VALUE(2);
            break;
        case ANM_MOD_2:
            *GET_INT_PTR(0) = GET_INT_VALUE(1) % GET_INT_VALUE(2);
            break;
        case ANM_MOD_FLOAT_2:
            *GET_FLOAT_PTR(0) = fmodf(GET_FLOAT_VALUE(1), GET_FLOAT_VALUE(2));
            break;
        case ANM_ADD:
            *GET_INT_PTR(0) += GET_INT_VALUE(1);
            break;
        case ANM_ADD_FLOAT:
            *GET_FLOAT_PTR(0) += GET_FLOAT_VALUE(1);
            break;
        case ANM_SUB:
            *GET_INT_PTR(0) -= GET_INT_VALUE(1);
            break;
        case ANM_SUB_FLOAT:
            *GET_FLOAT_PTR(0) -= GET_FLOAT_VALUE(1);
            break;
        case ANM_MUL:
            *GET_INT_PTR(0) *= GET_INT_VALUE(1);
            break;
        case ANM_MUL_FLOAT:
            *GET_FLOAT_PTR(0) *= GET_FLOAT_VALUE(1);
            break;
        case ANM_DIV:
            *GET_INT_PTR(0) /= GET_INT_VALUE(1);
            break;
        case ANM_DIV_FLOAT:
            *GET_FLOAT_PTR(0) /= GET_FLOAT_VALUE(1);
            break;
        case ANM_MOD:
            *GET_INT_PTR(0) %= GET_INT_VALUE(1);
            break;
        case ANM_MOD_FLOAT:
            *GET_FLOAT_PTR(0) = fmodf(GET_FLOAT_VALUE(0), GET_FLOAT_VALUE(1));
            break;
        case ANM_RAND:
            *GET_INT_PTR(0) = g_Rng.GetRandomU32InRange(GET_INT_VALUE(1));
            break;
        case ANM_RAND_FLOAT:
            *GET_FLOAT_PTR(0) = g_Rng.GetRandomFloatInRange(GET_FLOAT_VALUE(1));
            break;
        case ANM_SIN:
            *GET_FLOAT_PTR(0) = sinf(GET_FLOAT_VALUE(1));
            break;
        case ANM_COS:
            *GET_FLOAT_PTR(0) = cosf(GET_FLOAT_VALUE(1));
            break;
        case ANM_TAN:
            *GET_FLOAT_PTR(0) = tanf(GET_FLOAT_VALUE(1));
            break;
        case ANM_ACOS:
            *GET_FLOAT_PTR(0) = acosf(GET_FLOAT_VALUE(1));
            break;
        case ANM_ATAN:
            *GET_FLOAT_PTR(0) = atanf(GET_FLOAT_VALUE(1));
            break;
        case ANM_ADD_NORMALIZE_ANGLE:
            *GET_FLOAT_PTR(0) = utils::AddNormalizeAngle(GET_FLOAT_VALUE(0), 0.0f);
            break;
        case ANM_JUMP_IF_EQ:
            if (GET_INT_VALUE(0) == GET_INT_VALUE(1))
            {
                goto jump;
            }
            break;
        case ANM_JUMP_IF_EQ_FLOAT:
            if (GET_FLOAT_VALUE(0) == GET_FLOAT_VALUE(1))
            {
                goto jump;
            }
            break;
        case ANM_JUMP_IF_NEQ:
            if (GET_INT_VALUE(0) != GET_INT_VALUE(1))
            {
                goto jump;
            }
            break;
        case ANM_JUMP_IF_NEQ_FLOAT:
            if (GET_FLOAT_VALUE(0) != GET_FLOAT_VALUE(1))
            {
                goto jump;
            }
            break;
        case ANM_JUMP_IF_LT:
            if (GET_INT_VALUE(0) < GET_INT_VALUE(1))
            {
                goto jump;
            }
            break;
        case ANM_JUMP_IF_LT_FLOAT:
            if (GET_FLOAT_VALUE(0) < GET_FLOAT_VALUE(1))
            {
                goto jump;
            }
            break;
        case ANM_JUMP_IF_LEQ:
            if (GET_INT_VALUE(0) <= GET_INT_VALUE(1))
            {
                goto jump;
            }
            break;
        case ANM_JUMP_IF_LEQ_FLOAT:
            if (GET_FLOAT_VALUE(0) <= GET_FLOAT_VALUE(1))
            {
                goto jump;
            }
            break;
        case ANM_JUMP_IF_GT:
            if (GET_INT_VALUE(0) > GET_INT_VALUE(1))
            {
                goto jump;
            }
            break;
        case ANM_JUMP_IF_GT_FLOAT:
            if (GET_FLOAT_VALUE(0) > GET_FLOAT_VALUE(1))
            {
                goto jump;
            }
            break;
        case ANM_JUMP_IF_GEQ:
            if (GET_INT_VALUE(0) >= GET_INT_VALUE(1))
            {
                goto jump;
            }
            break;
        case ANM_JUMP_IF_GEQ_FLOAT:
            if (GET_FLOAT_VALUE(0) >= GET_FLOAT_VALUE(1))
            {
                goto jump;
            }
            break;
        jump:
            vm->currentTimeInScript = instr->args[3].i;
            vm->currentInstruction =
                (AnmRawInstr *)((u8 *)vm->beginningOfScript + instr->args[2].i);
            goto WHY_NOT_JUST_CONTINUE;
        default:
            break;
        }
        vm->currentInstruction = (AnmRawInstr *)((u8 *)instr + instr->size);
        goto WHY_NOT_JUST_CONTINUE;
    }

stop:
    if (vm->angleVel.x != 0.0f)
    {
        vm->rotation.x = utils::AddNormalizeAngle(
            vm->rotation.x, g_Supervisor.effectiveFramerateMultiplier * vm->angleVel.x);
        vm->updateRotation = 1;
    }
    if (vm->angleVel.y != 0.0f)
    {
        vm->rotation.y = utils::AddNormalizeAngle(
            vm->rotation.y, g_Supervisor.effectiveFramerateMultiplier * vm->angleVel.y);
        vm->updateRotation = 1;
    }
    if (vm->angleVel.z != 0.0f)
    {
        vm->rotation.z = utils::AddNormalizeAngle(
            vm->rotation.z, g_Supervisor.effectiveFramerateMultiplier * vm->angleVel.z);
        vm->updateRotation = 1;
    }
    for (i = 0; i < 5; i++)
    {
        if (vm->interpEndTimes[i] > 0)
        {
            vm->interpStartTimes[i]++;
            if (vm->interpStartTimes[i] >= vm->interpEndTimes[i].GetCurrent())
            {
                t = 1.0f;
                vm->interpEndTimes[i] = 0;
            }
            else
            {
                t = vm->interpStartTimes[i].AsFloat() / vm->interpEndTimes[i].AsFloat();
            }
            switch (vm->interpModes[i])
            {
            case 1:
                t = t * t;
                break;
            case 2:
                t = t * t * t;
                break;
            case 3:
                t = t * t;
                t = t * t;
                break;
            case 4:
                t = 1.0f - t;
                t = t * t;
                t = 1.0f - t;
                break;
            case 5:
                t = 1.0f - t;
                t = t * t * t;
                t = 1.0f - t;
                break;
            case 6:
                t = 1.0f - t;
                t = t * t;
                t = t * t;
                t = 1.0f - t;
                break;
            }
            switch (i)
            {
            case 0:
                if (!vm->useOffset)
                {
                    vm->pos.x = (vm->posInterpFinal.x - vm->posInterpInitial.x) * t +
                                vm->posInterpInitial.x;
                    vm->pos.y = (vm->posInterpFinal.y - vm->posInterpInitial.y) * t +
                                vm->posInterpInitial.y;
                    vm->pos.z = (vm->posInterpFinal.z - vm->posInterpInitial.z) * t +
                                vm->posInterpInitial.z;
                }
                else
                {
                    vm->offset.x = (vm->posInterpFinal.x - vm->posInterpInitial.x) * t +
                                   vm->posInterpInitial.x;
                    vm->offset.y = (vm->posInterpFinal.y - vm->posInterpInitial.y) * t +
                                   vm->posInterpInitial.y;
                    vm->offset.z = (vm->posInterpFinal.z - vm->posInterpInitial.z) * t +
                                   vm->posInterpInitial.z;
                }
                break;
            case 1:
                vm->color.bytes.r = (u8)((f32)((i32)vm->colorInterpFinalColor.bytes.r -
                                               (i32)vm->colorInterpInitialColor.bytes.r) *
                                             t +
                                         (f32)vm->colorInterpInitialColor.bytes.r);
                vm->color.bytes.g = (u8)((f32)((i32)vm->colorInterpFinalColor.bytes.g -
                                               (i32)vm->colorInterpInitialColor.bytes.g) *
                                             t +
                                         (f32)vm->colorInterpInitialColor.bytes.g);
                vm->color.bytes.b = (u8)((f32)((i32)vm->colorInterpFinalColor.bytes.b -
                                               (i32)vm->colorInterpInitialColor.bytes.b) *
                                             t +
                                         (f32)vm->colorInterpInitialColor.bytes.b);
                break;
            case 2:
                vm->color.bytes.a = (u8)((f32)((i32)vm->colorInterpFinalColor.bytes.a -
                                               (i32)vm->colorInterpInitialColor.bytes.a) *
                                             t +
                                         (f32)vm->colorInterpInitialColor.bytes.a);
                break;
            case 3:
                vm->rotation.x = utils::AddNormalizeAngle(
                    (vm->rotateInterpFinal.x - vm->rotateInterpInitial.x) * t,
                    vm->rotateInterpInitial.x);
                vm->rotation.y = utils::AddNormalizeAngle(
                    (vm->rotateInterpFinal.y - vm->rotateInterpInitial.y) * t,
                    vm->rotateInterpInitial.y);
                vm->rotation.z = utils::AddNormalizeAngle(
                    (vm->rotateInterpFinal.z - vm->rotateInterpInitial.z) * t,
                    vm->rotateInterpInitial.z);
                vm->updateRotation = 1;
                break;
            case 4:
                vm->scale.x = (vm->scaleInterpFinal.x - vm->scaleInterpInitial.x) * t +
                              vm->scaleInterpInitial.x;
                vm->scale.y = (vm->scaleInterpFinal.y - vm->scaleInterpInitial.y) * t +
                              vm->scaleInterpInitial.y;
                vm->updateScale = 1;
                break;
            }
        }
    }
    if (vm->scaleGrowth.y != 0.0f)
    {
        vm->scale.y += g_Supervisor.effectiveFramerateMultiplier * vm->scaleGrowth.y;
        vm->updateScale = 1;
    }
    if (vm->scaleGrowth.x != 0.0f)
    {
        vm->scale.x += g_Supervisor.effectiveFramerateMultiplier * vm->scaleGrowth.x;
        vm->updateScale = 1;
        vm->updateRotation = 1;
    }
    vm->uvScrollPos.x += vm->uvScrollVel.x;
    if (vm->uvScrollPos.x >= 1.0f)
    {
        vm->uvScrollPos.x -= 1.0f;
    }
    else if (vm->uvScrollPos.x < 0.0f)
    {
        vm->uvScrollPos.x += 1.0f;
    }
    vm->uvScrollPos.y += vm->uvScrollVel.y;
    if (vm->uvScrollPos.y >= 1.0f)
    {
        vm->uvScrollPos.y -= 1.0f;
    }
    else if (vm->uvScrollPos.y < 0.0f)
    {
        vm->uvScrollPos.y += 1.0f;
    }
    vm->currentTimeInScript++;
    this->scriptTicksThisFrame++;
    return 0;
}

void AnmManager::DrawTextToSprite(u32 spriteDstIdx, i32 x, i32 y, i32 width, i32 height,
                                  i32 fontWidth, i32 fontHeight, u32 textColor, u32 outlineType,
                                  char *strToPrint, f32 scaleY, f32 scaleX)
{
    if (fontWidth <= 0)
    {
        fontWidth = 15;
    }
    if (fontHeight <= 0)
    {
        fontHeight = 15;
    }
    TextHelper::RenderTextToTextureBold(x, y, width, height, (f32)fontWidth * scaleY,
                                        (f32)fontHeight * scaleX, textColor, outlineType,
                                        strToPrint, this->textures[spriteDstIdx]);
}

void AnmManager::DrawVmTextFmt(AnmManager *manager, AnmVm *vm, u32 textColor, u32 outlineType,
                               const char *str, ...)
{
    u32 fontWidth;
    char text[256];
    va_list args;

    fontWidth = vm->fontWidth;

    va_start(args, str);
    vsprintf(text, str, args);
    va_end(args);

    manager->DrawTextToSprite(vm->sprite->sourceFileIndex, vm->sprite->startPixelInclusive.x,
                              vm->sprite->startPixelInclusive.y, vm->sprite->textureWidth,
                              vm->sprite->textureHeight, fontWidth, vm->fontHeight, textColor,
                              outlineType, text, vm->sprite->cols, vm->sprite->rows);

    vm->visible = 1;
}

bool AnmManager::PreRenderVmText(AnmVm *vm, u32 textColor, u32 outlineType, const char *text)
{
    if (!vm || !vm->sprite || !text)
    {
        return false;
    }
    const i32 fontWidth = vm->fontWidth <= 0 ? 15 : vm->fontWidth;
    const i32 fontHeight = vm->fontHeight <= 0 ? 15 : vm->fontHeight;
    return TextHelper::PreRenderTextToCacheBold(
        vm->sprite->startPixelInclusive.x, vm->sprite->startPixelInclusive.y,
        vm->sprite->textureWidth, vm->sprite->textureHeight,
        static_cast<f32>(fontWidth) * vm->sprite->cols,
        static_cast<f32>(fontHeight) * vm->sprite->rows, textColor, outlineType, text);
}

bool AnmManager::PreRenderString(AnmVm *vm, u32 textColor, u32 outlineType, const char *text)
{
    if (!vm || !vm->sprite || !text)
    {
        return false;
    }
    const i32 fontWidth = vm->fontWidth <= 0 ? 15 : vm->fontWidth;
    const i32 x = vm->sprite->startPixelInclusive.x + vm->sprite->widthPx * vm->sprite->cols -
                  static_cast<f32>(TextHelper::GetLogicalStringWidth(text)) * fontWidth *
                      vm->sprite->cols / 2.0f;
    const i32 fontHeight = vm->fontHeight <= 0 ? 15 : vm->fontHeight;
    return TextHelper::PreRenderTextToCacheBold(
        x, vm->sprite->startPixelInclusive.y, vm->sprite->textureWidth,
        vm->sprite->textureHeight, static_cast<f32>(fontWidth) * vm->sprite->cols,
        static_cast<f32>(fontHeight) * vm->sprite->rows, textColor, outlineType, text);
}

void AnmManager::DrawStringFormat(AnmVm *vm, u32 textColor, u32 outlineType, const char *text, ...)
{
    i32 fontWidth;
    char buf[256];
    i32 x;
    va_list args;

    fontWidth = vm->fontWidth <= 0 ? 15 : (u32)vm->fontWidth;
    va_start(args, text);
    vsprintf(buf, text, args);
    va_end(args);

#if !defined(TH07_PSP)
    this->DrawTextToSprite(vm->sprite->sourceFileIndex, vm->sprite->startPixelInclusive.x,
                           vm->sprite->startPixelInclusive.y, vm->sprite->textureWidth,
                           vm->sprite->textureHeight, fontWidth, vm->fontHeight, textColor,
                           outlineType, (char *)" ", vm->sprite->cols, vm->sprite->rows);
#endif

    x = vm->sprite->startPixelInclusive.x + vm->sprite->widthPx * vm->sprite->cols -
        (f32)TextHelper::GetLogicalStringWidth(buf) * (f32)fontWidth * vm->sprite->cols / 2.0f;

#if defined(TH07_PSP)
    // TextHelper clears and republishes the complete 512x16 destination band
    // for this draw. The old leading space pass therefore produced an
    // identical intermediate blank band that was immediately overwritten,
    // doubling spell-name rasterisation, synchronization and upload traffic.
#endif
    this->DrawTextToSprite(vm->sprite->sourceFileIndex, x, vm->sprite->startPixelInclusive.y,
                           vm->sprite->textureWidth, vm->sprite->textureHeight, fontWidth,
                           vm->fontHeight, textColor, outlineType, buf, vm->sprite->cols,
                           vm->sprite->rows);

    vm->visible = 1;
}

void AnmManager::DrawStringFormat2(AnmVm *vm, u32 textColor, u32 outlineType, const char *text, ...)
{
    i32 fontWidth;
    char buf[256];
    i32 x;
    va_list args;

    fontWidth = vm->fontWidth <= 0 ? 15 : (i32)vm->fontWidth;
    va_start(args, text);
    vsprintf(buf, text, args);
    va_end(args);

    this->DrawTextToSprite(vm->sprite->sourceFileIndex, vm->sprite->startPixelInclusive.x,
                           vm->sprite->startPixelInclusive.y, vm->sprite->textureWidth,
                           vm->sprite->textureHeight, fontWidth, vm->fontHeight, textColor,
                           outlineType, (char *)" ", vm->sprite->cols, vm->sprite->rows);

    x = (i32)(vm->sprite->startPixelInclusive.x + vm->sprite->widthPx * vm->sprite->cols / 2.0f -
              (f32)TextHelper::GetLogicalStringWidth(buf) * fontWidth * vm->sprite->cols / 4.0f);

    this->DrawTextToSprite(vm->sprite->sourceFileIndex, x, vm->sprite->startPixelInclusive.y,
                           vm->sprite->textureWidth, vm->sprite->textureHeight, fontWidth,
                           vm->fontHeight, textColor, outlineType, buf, vm->sprite->cols,
                           vm->sprite->rows);

    vm->visible = 1;
}

ZunResult AnmManager::LoadSurface(i32 surfaceIdx, const char *path)
{
    if (this->surfaces[surfaceIdx] || this->surfacesBis[surfaceIdx])
    {
        ReleaseSurface(surfaceIdx);
    }
    SDL_Surface *converted = nullptr;
#if defined(TH07_PSP_1000)
    th07_psp_boot_notef("surface load %s", path ? path : "?");
    th07_psp_heap_note("surface load begin");
#endif
#if defined(TH07_PSP)
    if (strcmp(path, "data/result/music.jpg") == 0)
    {
        converted = LoadPspMusicRawSurface();
    }
#endif
    if (!converted)
    {
        u8 *data = FileSystem::OpenFile(path, 0);
        if (!data)
        {
#if defined(TH07_PSP_1000)
            th07_psp_boot_note("surface source load failed");
#endif
            g_GameErrorContext.Fatal("%sが読み込めないです。\n", path);
            return ZUN_ERROR;
        }
        SDL_RWops *rw = SDL_RWFromMem(data, g_LastFileSize);
        SDL_Surface *surf = IMG_Load_RW(rw, 1);
        if (!surf)
        {
#if defined(TH07_PSP_1000)
            th07_psp_boot_notef("surface decode failed %s", IMG_GetError());
#endif
            free(data);
            return ZUN_ERROR;
        }
#if defined(TH07_PSP_1000)
        // SDL_image normally returns JPEGs as packed RGB24.  Keep that decoded
        // image directly: converting it to PNG cannot restore information
        // already lost in the original JPEG, while pre-quantizing it to RGB565
        // before the 640x480 -> LCD reduction adds visible banding and edge
        // noise.  The GU backend now downsamples RGB24 first and quantizes only
        // once at the final LCD-sized texture.  If a decoder returns another
        // layout, retain the low-memory RGB565 fallback rather than allocating
        // a simultaneous 1.2 MiB RGBA copy on the 32 MiB model.
        if (surf->format->format == SDL_PIXELFORMAT_RGB24)
        {
            converted = surf;
            surf = nullptr;
        }
        else
        {
            converted = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGB565, 0);
        }
#else
        converted = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA32, 0);
#endif
        if (surf)
        {
            SDL_FreeSurface(surf);
        }
        free(data);
#if defined(TH07_PSP)
        if (strcmp(path, "data/result/music.jpg") == 0)
        {
            th07_psp_boot_note("music jpeg SDL fallback");
        }
#endif
    }
    if (!converted)
    {
#if defined(TH07_PSP_1000)
        th07_psp_boot_note("surface conversion failed");
        th07_psp_heap_note("surface conversion failed");
#endif
        return ZUN_ERROR;
    }

    this->surfaceSourceInfo[surfaceIdx].width = converted->w;
    this->surfaceSourceInfo[surfaceIdx].height = converted->h;

#if defined(TH07_PSP)
    // The portable reconstruction keeps two identical copies of every JPEG.
    // PSP never mutates either copy, so keep only the surface consumed by
    // CopySurfaceToBackBuffer/DrawEndingRect.  The standard 64 MiB profile
    // retains its established RGBA32 representation; PSP-1000 keeps the
    // decoder's packed RGB24 image (or an RGB565 fallback) to make the title
    // -> selection decode fit 32 MiB without an extra quality-losing pass.
    this->surfaces[surfaceIdx] = nullptr;
    this->surfacesBis[surfaceIdx] = converted;
#if defined(TH07_PSP_1000)
    th07_psp_boot_notef("surface ready %dx%d %dbpp", converted->w, converted->h,
                        static_cast<int>(converted->format->BytesPerPixel));
    th07_psp_heap_note("surface ready");
#endif
#else
    this->surfaces[surfaceIdx] = converted;
    this->surfacesBis[surfaceIdx] =
        SDL_ConvertSurfaceFormat(this->surfaces[surfaceIdx], SDL_PIXELFORMAT_RGBA32, 0);
    if (!this->surfacesBis[surfaceIdx])
    {
        ReleaseSurface(surfaceIdx);
        return ZUN_ERROR;
    }
#endif

    return ZUN_SUCCESS;
}

void AnmManager::ReleaseSurface(i32 surfaceIdx)
{
#if defined(TH07_PSP)
    if (this->pspSurfaceTextures[surfaceIdx])
    {
        g_Supervisor.gfxDevice->DeleteTexture(this->pspSurfaceTextures[surfaceIdx]);
        this->pspSurfaceTextures[surfaceIdx] = {};
        this->pspSurfaceTextureSources[surfaceIdx] = nullptr;
    }
#endif
    if (this->surfaces[surfaceIdx])
    {
#if defined(TH07_PSP)
        Th07PspForgetSurface(this->surfaces[surfaceIdx]->pixels);
#endif
        SDL_FreeSurface(this->surfaces[surfaceIdx]);
        this->surfaces[surfaceIdx] = nullptr;
    }
    if (this->surfacesBis[surfaceIdx])
    {
#if defined(TH07_PSP)
        Th07PspForgetSurface(this->surfacesBis[surfaceIdx]->pixels);
#endif
        SDL_FreeSurface(this->surfacesBis[surfaceIdx]);
        this->surfacesBis[surfaceIdx] = nullptr;
    }
}

#if defined(TH07_PSP)
namespace
{
GfxTextureHandle GetPersistentSurfaceTexture(AnmManager *manager, i32 surfaceIdx,
                                             SDL_Surface *surface)
{
    if (manager->pspSurfaceTextures[surfaceIdx] &&
        manager->pspSurfaceTextureSources[surfaceIdx] == surface->pixels)
    {
        return manager->pspSurfaceTextures[surfaceIdx];
    }

    // The PSP backend deliberately owns only one converted full-screen
    // surface allocation.  Retire any handle which borrowed its previous
    // contents before filling it with another JPEG.
    for (i32 i = 0; i < 32; ++i)
    {
        if (manager->pspSurfaceTextures[i])
        {
            g_Supervisor.gfxDevice->DeleteTexture(manager->pspSurfaceTextures[i]);
            manager->pspSurfaceTextures[i] = {};
            manager->pspSurfaceTextureSources[i] = nullptr;
        }
    }

    GfxTextureHandle texture = g_Supervisor.gfxDevice->CreateTexture();
    g_Supervisor.gfxDevice->BindTexture(texture);
    const bool rgb565 = surface->format->format == SDL_PIXELFORMAT_RGB565;
    const PixelDataType uploadType = rgb565 ? PIXEL_UNSIGNED_SHORT_5_6_5
                                            : PIXEL_UNSIGNED_BYTE;
    const PixelFormat uploadFormat =
        !rgb565 && surface->format->BytesPerPixel == 3 ? PIXEL_RGB : PIXEL_RGBA;
    g_Supervisor.gfxDevice->SetTextureImage(surface->w, surface->h, uploadFormat,
                                            uploadType, surface->pixels);
    manager->pspSurfaceTextures[surfaceIdx] = texture;
    manager->pspSurfaceTextureSources[surfaceIdx] = surface->pixels;
    return texture;
}
} // namespace
#endif

void AnmManager::CopySurfaceToBackBuffer(i32 surfaceIdx, i32 left, i32 top, i32 x, i32 y)
{
    if (!this->surfacesBis[surfaceIdx])
    {
        return;
    }

    SDL_Surface *surf = this->surfacesBis[surfaceIdx];

#if defined(TH07_PSP)
    GfxTextureHandle tex = GetPersistentSurfaceTexture(this, surfaceIdx, surf);
    g_Supervisor.gfxDevice->BindTexture(tex);
#else
    GfxTextureHandle tex = g_Supervisor.gfxDevice->CreateTexture();
    g_Supervisor.gfxDevice->BindTexture(tex);
    g_Supervisor.gfxDevice->SetTextureImage(surf->w, surf->h, PIXEL_RGBA, PIXEL_UNSIGNED_BYTE,
                                            surf->pixels);
#endif

    VertexTex1DiffuseXyzrhw vertices[4];
    f32 width = (f32)this->surfaceSourceInfo[surfaceIdx].width;
    f32 height = (f32)this->surfaceSourceInfo[surfaceIdx].height;

    vertices[0].pos = ZunVec3(x, y, 0.0f);
    vertices[1].pos = ZunVec3(x + width, y, 0.0f);
    vertices[2].pos = ZunVec3(x, y + height, 0.0f);
    vertices[3].pos = ZunVec3(x + width, y + height, 0.0f);
    vertices[0].w = vertices[1].w = vertices[2].w = vertices[3].w = 1.0f;

    f32 u0 = (f32)left / surf->w;
    f32 v0 = (f32)top / surf->h;
    f32 u1 = (f32)this->surfaceSourceInfo[surfaceIdx].width / surf->w;
    f32 v1 = (f32)this->surfaceSourceInfo[surfaceIdx].height / surf->h;

    vertices[0].textureUV = {u0, v0};
    vertices[1].textureUV = {u1, v0};
    vertices[2].textureUV = {u0, v1};
    vertices[3].textureUV = {u1, v1};

    vertices[0].color.color = vertices[1].color.color = vertices[2].color.color =
        vertices[3].color.color = 0xFFFFFFFF;

    g_Supervisor.gfxDevice->SetDepthMask(false);
    g_Supervisor.gfxDevice->SetBlendMode(BLEND_NONE, BLEND_NONE);
    g_Supervisor.gfxDevice->SetColorOp(COMPONENT_RGB, COLOR_OP_MODULATE);
    g_Supervisor.gfxDevice->SetColorOp(COMPONENT_ALPHA, COLOR_OP_MODULATE);
    g_Supervisor.gfxDevice->Disable(CAPS_ALPHA_TEST);
    g_Supervisor.gfxDevice->DrawPrimitiveUP(PRIM_TRIANGLE_STRIP, 2, vertices,
                                            sizeof(VertexTex1DiffuseXyzrhw));
    g_Supervisor.gfxDevice->Enable(CAPS_ALPHA_TEST);
#if !defined(TH07_PSP)
    g_Supervisor.gfxDevice->DeleteTexture(tex);
#endif
}

void AnmManager::DrawEndingRect(i32 surfaceIdx, i32 rectX, i32 rectY, i32 rectLeft, i32 rectTop,
                                i32 width, i32 height)
{
    if (!this->surfacesBis[surfaceIdx])
    {
        return;
    }

    SDL_Surface *surf =
        this->surfaces[surfaceIdx] ? this->surfaces[surfaceIdx] : this->surfacesBis[surfaceIdx];

#if defined(TH07_PSP)
    GfxTextureHandle tex = GetPersistentSurfaceTexture(this, surfaceIdx, surf);
    g_Supervisor.gfxDevice->BindTexture(tex);
#else
    GfxTextureHandle tex = g_Supervisor.gfxDevice->CreateTexture();
    g_Supervisor.gfxDevice->BindTexture(tex);
    g_Supervisor.gfxDevice->SetTextureImage(surf->w, surf->h, PIXEL_RGBA, PIXEL_UNSIGNED_BYTE,
                                            surf->pixels);
#endif

    VertexTex1DiffuseXyzrhw vertices[4];
    f32 drawWidth = width;
    f32 drawHeight = height;

    vertices[0].pos = ZunVec3(rectX, rectY, 0.0f);
    vertices[1].pos = ZunVec3(rectX + drawWidth, rectY, 0.0f);
    vertices[2].pos = ZunVec3(rectX, rectY + drawHeight, 0.0f);
    vertices[3].pos = ZunVec3(rectX + drawWidth, rectY + drawHeight, 0.0f);
    vertices[0].w = vertices[1].w = vertices[2].w = vertices[3].w = 1.0f;

    f32 u0 = (f32)rectLeft / surf->w;
    f32 v0 = (f32)rectTop / surf->h;
    f32 u1 = (f32)(rectLeft + width) / surf->w;
    f32 v1 = (f32)(rectTop + height) / surf->h;

    vertices[0].textureUV = {u0, v0};
    vertices[1].textureUV = {u1, v0};
    vertices[2].textureUV = {u0, v1};
    vertices[3].textureUV = {u1, v1};

    vertices[0].color.color = vertices[1].color.color = vertices[2].color.color =
        vertices[3].color.color = 0xFFFFFFFF;

    g_Supervisor.gfxDevice->SetDepthMask(false);
    g_Supervisor.gfxDevice->SetBlendMode(BLEND_NONE, BLEND_NONE);
    g_Supervisor.gfxDevice->SetColorOp(COMPONENT_RGB, COLOR_OP_MODULATE);
    g_Supervisor.gfxDevice->SetColorOp(COMPONENT_ALPHA, COLOR_OP_MODULATE);
    g_Supervisor.gfxDevice->DrawPrimitiveUP(PRIM_TRIANGLE_STRIP, 2, vertices,
                                            sizeof(VertexTex1DiffuseXyzrhw));

#if !defined(TH07_PSP)
    g_Supervisor.gfxDevice->DeleteTexture(tex);
#endif
}

bool AnmManager::TakeScreenshot(i32 textureId, i32 srcLeft, i32 srcTop, i32 srcWidth,
                                i32 srcHeight, i32 dstLeft, i32 dstTop, i32 dstWidth,
                                i32 dstHeight)
{
    if (!this->textures[textureId])
    {
        return false;
    }

    Flush();

#if defined(TH07_PSP)
    const bool captured = Th07PspCaptureFramebufferToTexture(
        this->textures[textureId], srcLeft, srcTop, srcWidth, srcHeight, dstLeft, dstTop, dstWidth,
        dstHeight);
    if (!captured)
    {
        // A missing capture must never turn the pause button into a process
        // exit. The menu remains usable without its animated background if
        // the texture was unavailable.
        th07_psp_boot_note("pause capture skipped");
    }
    return captured;
#else
    bool captured = false;
    u32 *pixelData = new u32[srcWidth * srcHeight];
    g_Supervisor.gfxDevice->ReadPixels(srcLeft, srcTop, srcWidth, srcHeight, pixelData);

    if (srcWidth == dstWidth && srcHeight == dstHeight)
    {
        g_Supervisor.gfxDevice->BindTexture(this->textures[textureId]);
        g_Supervisor.gfxDevice->SetTextureSubImage(dstLeft, dstTop, dstWidth, dstHeight, pixelData);
        captured = true;
    }
    else
    {
        SDL_Surface *srcSurf =
            SDL_CreateRGBSurfaceFrom(pixelData, srcWidth, srcHeight, 32, srcWidth * 4, 0x000000FF,
                                     0x0000FF00, 0x00FF0000, 0xFF000000);
        SDL_Surface *dstSurf =
            SDL_CreateRGBSurfaceWithFormat(0, dstWidth, dstHeight, 32, SDL_PIXELFORMAT_RGBA32);

        if (srcSurf && dstSurf)
        {
            SDL_BlitScaled(srcSurf, NULL, dstSurf, NULL);
            g_Supervisor.gfxDevice->BindTexture(this->textures[textureId]);
            g_Supervisor.gfxDevice->SetTextureSubImage(dstLeft, dstTop, dstWidth, dstHeight,
                                                       dstSurf->pixels);
            captured = true;
        }

        if (srcSurf)
        {
            SDL_FreeSurface(srcSurf);
        }
        if (dstSurf)
        {
            SDL_FreeSurface(dstSurf);
        }
    }

    delete[] pixelData;
    return captured;
#endif
}

void AnmManager::CopyTexture(i32 dstIdx, i32 srcIdx, SDL_Rect *dstRect, SDL_Rect *srcRect)
{
    if (!this->textures[dstIdx])
    {
        return;
    }
    if (!this->textures[srcIdx])
    {
        return;
    }

    this->Flush();

    u8 *dstPixels = (u8 *)this->imageDataArray[dstIdx];
    u8 *srcPixels = (u8 *)this->imageDataArray[srcIdx];

    if (!srcPixels || !dstPixels)
    {
        return;
    }

    SDL_Surface *srcSurface = SDL_CreateRGBSurfaceWithFormatFrom(
        srcPixels, this->textureWidths[srcIdx], this->textureHeights[srcIdx], 32,
        this->texturePitches[srcIdx], SDL_PIXELFORMAT_RGBA32);

    SDL_Surface *dstSurface = SDL_CreateRGBSurfaceWithFormatFrom(
        dstPixels, this->textureWidths[dstIdx], this->textureHeights[dstIdx], 32,
        this->texturePitches[dstIdx], SDL_PIXELFORMAT_RGBA32);

    if (srcSurface && dstSurface)
    {
        SDL_SetSurfaceBlendMode(srcSurface, SDL_BLENDMODE_NONE);

        SDL_BlitScaled(srcSurface, srcRect, dstSurface, dstRect);

        g_Supervisor.gfxDevice->BindTexture(this->textures[dstIdx]);
        g_Supervisor.gfxDevice->SetTextureImage(this->textureWidths[dstIdx],
                                                this->textureHeights[dstIdx], PIXEL_RGBA,
                                                PIXEL_UNSIGNED_BYTE, dstPixels);
    }

    if (srcSurface)
    {
        SDL_FreeSurface(srcSurface);
    }
    if (dstSurface)
    {
        SDL_FreeSurface(dstSurface);
    }
}

void AnmManager::SetInterruptActiveVms(AnmVm *vm, i32 vmCount, i16 interrupt)
{
    i32 shouldSetInterrupt;

    while (vmCount != 0)
    {
        if (!vm->sprite)
        {
            shouldSetInterrupt = false;
        }
        else if (vm->sprite->sourceFileIndex < 0)
        {
            shouldSetInterrupt = false;
        }
        else
        {
            shouldSetInterrupt = g_AnmManager->textures[vm->sprite->sourceFileIndex].id != 0;
        }
        if (shouldSetInterrupt)
        {
            vm->pendingInterrupt = interrupt;
        }
        vm++;
        vmCount--;
    }
}

void AnmManager::ExecuteScripts(AnmVm *startVm, i32 count)
{
    while (count != 0)
    {
        if (startVm->anmFileIdx >= 0)
        {
            g_AnmManager->ExecuteScript(startVm);
        }
        startVm++;
        count--;
    }
}

void AnmManager::ExecuteVmsAnms(AnmVm *vm, i32 idx, i32 vmCount)
{
    while (vmCount != 0)
    {
        g_AnmManager->ExecuteAnmIdx(vm, idx);
        vm->baseSpriteIdx = vm->activeSpriteIdx;
        idx++;
        vm++;
        vmCount--;
    }
}

ZunResult AnmManager::UpdateTrail(AnmVm *vm, VertexTex1DiffuseXyzrhw *vertices, i32 count)
{
    f32 num;
    f32 fVar4;
    f32 uvX;
    f32 startuvX;
    VertexTex1DiffuseXyzrhw *vertex;
    i32 i;
    f32 uvY;
    if (count < 3)
    {
        return ZUN_ERROR;
    }

    startuvX = vm->sprite->uvEnd.x + vm->uvScrollPos.x;
    num = vm->sprite->uvEnd.x - vm->sprite->uvStart.x;
    uvY = vm->sprite->uvStart.y + vm->uvScrollPos.y;
    vertex = vertices;
    fVar4 = num / (f32)((i32)((count + 1) / 2) - 1);

    for (i = 0, uvX = startuvX; i < count; i += 2, vertex += 2, uvX = uvX - fVar4)
    {
        vertex->textureUV.x = uvX;
        vertex->textureUV.y = uvY;
        vertex->color.color = vm->color.color;
        vertex->w = 1.0f;
    }

    uvY = vm->sprite->uvEnd.y + vm->uvScrollPos.y;
    vertex = vertices + 1;

    for (i = 1, uvX = startuvX; i < count; i += 2, vertex += 2, uvX = uvX - fVar4)
    {
        vertex->textureUV.x = uvX;
        vertex->textureUV.y = uvY;
        vertex->color.color = vm->color.color;
        vertex->w = 1.0f;
    }

    return ZUN_SUCCESS;
}

ZunResult AnmManager::DrawTriangleStrip(AnmVm *vm, VertexTex1DiffuseXyzrhw *vertices, i32 count)
{
    if (!vm->visible)
    {
        return ZUN_ERROR;
    }

    if (!vm->active)
    {
        return ZUN_ERROR;
    }

    if (!vm->color.bytes.a)
    {
        return ZUN_ERROR;
    }

    if (this->spritesToDraw != 0)
    {
        this->Flush();
    }

    if (this->currentTexture != this->textures[vm->sprite->sourceFileIndex])
    {
        this->currentTexture = this->textures[vm->sprite->sourceFileIndex];
        g_Supervisor.gfxDevice->BindTexture(this->currentTexture);
    }

    if (this->currentVertexShader != 3)
    {
        this->currentVertexShader = 3;
    }

    SetRenderStateForVm(vm);
    g_Supervisor.gfxDevice->DrawPrimitiveUP(PRIM_TRIANGLE_STRIP, count - 2, vertices,
                                            sizeof(VertexTex1DiffuseXyzrhw));
    return ZUN_SUCCESS;
}

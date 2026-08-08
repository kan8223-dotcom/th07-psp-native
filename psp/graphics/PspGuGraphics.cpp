#include "graphics/PspGuGraphics.hpp"

#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include "AnmManager.hpp"
#if defined(TH07_PSP_PERF_DIAG)
#include "BulletManager.hpp"
#include "EffectManager.hpp"
#endif
#include "GameManager.hpp"
#include "Supervisor.hpp"
#include "audio_me.h"
#include "fileio.hpp"

namespace
{
constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 272;
constexpr int kLogicalWidth = 640;
constexpr int kLogicalHeight = 480;
constexpr int kFitWidth = 362;
constexpr int kFitLeft = (kScreenWidth - kFitWidth) / 2;
constexpr int kBufferWidth = 512;
// The LCD accepts RGB565 natively.  Two 16-bit framebuffers halve full-screen
// fill/blend traffic for every stage and leave more EDRAM headroom for the
// persistent full-screen surface cache.
constexpr int kFramePsm = GU_PSM_5650;
constexpr unsigned int kFrameBytesPerPixel = 2;
constexpr unsigned int kFrameBytes = kBufferWidth * kScreenHeight * kFrameBytesPerPixel;
constexpr unsigned int kDepthOffset = kFrameBytes * 2;
constexpr unsigned int kDepthBytes = kBufferWidth * kScreenHeight * 2;
constexpr unsigned int kEdramBytes = kDepthOffset + kDepthBytes;
constexpr unsigned int kListBytes = 1024 * 1024;
constexpr unsigned int kListReserve = 16 * 1024;
constexpr unsigned int kMaxTextures = 256;
constexpr unsigned int kCachedQuadIndexCount = 2048;
// As in TH06, reserve the high end of AnmManager's per-frame sprite buffer for
// GE-ready vertices.  Consecutive engine flushes with identical render state
// can then become one primitive command without allocating another large PSP
// heap block.
constexpr unsigned int kDeferredSpriteArenaBytes = 384 * 1024;
// Keep the first real-hardware text-quality build on the texture allocation
// path which already completed all six stages.  Static-atlas RGBA8888 and
// whole-texture swizzling are reintroduced separately after boot stability is
// measured; combining them hid which optimization caused a kernel shutdown
// while title.anm was loading.
constexpr bool kFastSurfaceTextures = true;
constexpr bool kFastStaticTextureFormats = false;
constexpr bool kSwizzleSurfaceTextures = true;
constexpr bool kSwizzleStaticTextures = true;
#if defined(TH07_PSP_PERF_DIAG)
constexpr unsigned int kPerfWindowFrames = 120;
#endif

const ScePspFMatrix4 kIdentityMatrix = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
};

alignas(16) unsigned int gCommandList[kListBytes / sizeof(unsigned int)];
alignas(64) u16 gQuadIndices[kCachedQuadIndexCount * 6];
#if defined(TH07_PSP_PERF_DIAG)
unsigned long long gPerfCalcChainUs = 0;
unsigned long long gPerfDrawChainUs = 0;
unsigned long long gPerfStageDrawUs = 0;
unsigned long long gPerfCalcJobUs[18] = {};
unsigned long long gPerfDrawJobUs[18] = {};
#endif

struct GuTexture
{
    void *pixels = nullptr;
    // Dynamic text atlases alternate backing addresses.  PPSSPP (and the
    // earlier TH07 PS3 backend) can otherwise keep sampling the first cached
    // image even after a sub-image upload and cache flush.
    void *updatePixels = nullptr;
    int psm = GU_PSM_8888;
    unsigned int logicalWidth = 0;
    unsigned int logicalHeight = 0;
    unsigned int storageWidth = 0;
    unsigned int storageHeight = 0;
    unsigned int contentWidth = 0;
    unsigned int contentHeight = 0;
    unsigned int bytes = 0;
    unsigned int allocationBytes = 0;
    unsigned int updateAllocationBytes = 0;
    float sampleScaleX = 1.0f;
    float sampleScaleY = 1.0f;
    bool used = false;
    bool swizzled = false;
    bool textAtlas = false;
    bool surfaceCandidate = false;
    bool borrowedSurfaceCache = false;
    bool textBatchPending = false;
};

struct SurfaceCache
{
    void *pixels = nullptr;
    const void *source = nullptr;
    unsigned int bytes = 0;
    unsigned int allocationBytes = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    bool valid = false;
    bool edram = false;
};

struct FreeTextureBlock
{
    void *pixels = nullptr;
    unsigned int bytes = 0;
};

struct GuVertexTexColor
{
    float u;
    float v;
    unsigned int color;
    float x;
    float y;
    float z;
};

struct GuVertexTex
{
    float u;
    float v;
    float x;
    float y;
    float z;
};

struct GuVertexColor
{
    unsigned int color;
    float x;
    float y;
    float z;
};

struct GuVertexPosition
{
    float x;
    float y;
    float z;
};

static_assert(sizeof(GuVertexTexColor) == 24, "unexpected PSP textured-color vertex size");
static_assert(sizeof(Th07PspSpriteVertex) == sizeof(GuVertexTexColor),
              "TH07 sprite/deferred vertex layouts must remain the same size");
static_assert(sizeof(GuVertexTex) == 20, "unexpected PSP textured vertex size");
static_assert(sizeof(GuVertexColor) == 16, "unexpected PSP color vertex size");
static_assert(sizeof(GuVertexPosition) == 12, "unexpected PSP position vertex size");

unsigned int NextPowerOfTwo(unsigned int value)
{
    unsigned int result = 1;
    while (result < value)
    {
        result <<= 1;
    }
    return result;
}

// PSPSDK's GU samples store textures as consecutive 16-byte by 8-row blocks.
// SetTextureImage uses this fast path for complete RGBA8 textures just as the
// final TH06 backend does.
void SwizzleCopy(unsigned char *destination, const unsigned char *source,
                 unsigned int widthBytes, unsigned int height)
{
    const unsigned int widthBlocks = widthBytes / 16u;
    const unsigned int heightBlocks = height / 8u;
    for (unsigned int blockY = 0; blockY < heightBlocks; ++blockY)
    {
        for (unsigned int blockX = 0; blockX < widthBlocks; ++blockX)
        {
            unsigned char *block =
                destination + (blockY * widthBlocks + blockX) * 128u;
            const unsigned char *row =
                source + blockY * 8u * widthBytes + blockX * 16u;
            for (unsigned int y = 0; y < 8u; ++y)
            {
                std::memcpy(block + y * 16u, row + y * widthBytes, 16u);
            }
        }
    }
}

struct SurfaceAxisSample
{
    unsigned short first;
    unsigned short second;
    unsigned short fraction;
};

// Match the proven TH06 PSP surface path: filter an immutable 640-wide image
// once to the selected LCD output size instead of asking GE to minify the
// original 480 logical rows every frame.
void BuildSurfaceAxisSamples(SurfaceAxisSample *samples, unsigned int destinationSize,
                             unsigned int sourceSize)
{
    if (!samples || destinationSize == 0 || sourceSize == 0)
    {
        return;
    }
    for (unsigned int i = 0; i < destinationSize; ++i)
    {
        long long fixed = ((static_cast<long long>(i) * 2 + 1) * sourceSize * 128) /
                              destinationSize -
                          128;
        const long long last = static_cast<long long>(sourceSize - 1u) * 256;
        fixed = std::max(0ll, std::min(last, fixed));
        samples[i].first = static_cast<unsigned short>(fixed >> 8);
        samples[i].second = static_cast<unsigned short>(
            std::min(sourceSize - 1u, static_cast<unsigned int>(samples[i].first) + 1u));
        samples[i].fraction = static_cast<unsigned short>(fixed & 255);
    }
}

unsigned int ToGuColor(ZunColor color)
{
    return (color.color & 0xff00ff00u) | ((color.color & 0x00ff0000u) >> 16) |
           ((color.color & 0x000000ffu) << 16);
}

ScePspFMatrix4 ToGuMatrix(const ZunMatrix &matrix)
{
    ScePspFMatrix4 result;
    float *dst = &result.x.x;
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            dst[column * 4 + row] = matrix.m[column][row];
        }
    }
    return result;
}

unsigned int ToHardwareDepth(float value)
{
    value = std::max(0.0f, std::min(1.0f, value));
    return static_cast<unsigned int>((1.0f - value) * 65535.0f);
}

class PspGuGraphics;
PspGuGraphics *gPspGuBackend = nullptr;

class PspGuGraphics final : public ZunGraphics
{
  public:
    PspGuGraphics()
    {
        for (ZunMatrix &matrix : mTransforms)
        {
            matrix.Identity();
        }
        mViewport = {0, 0, kLogicalWidth, kLogicalHeight, 0.0f, 1.0f};
    }

    ~PspGuGraphics() override
    {
        Exit();
    }

    bool Init()
    {
        if (kEdramBytes > sceGeEdramGetSize() || sceGuInit() < 0)
        {
            return false;
        }

        // A quad's topology never changes.  Keep one cache-coherent index table
        // instead of allocating and filling thousands of identical u16 values
        // in the display-list arena every frame.
        for (unsigned int sprite = 0; sprite < kCachedQuadIndexCount; ++sprite)
        {
            const u16 base = static_cast<u16>(sprite * 4);
            gQuadIndices[sprite * 6 + 0] = base;
            gQuadIndices[sprite * 6 + 1] = base + 1;
            gQuadIndices[sprite * 6 + 2] = base + 2;
            gQuadIndices[sprite * 6 + 3] = base + 1;
            gQuadIndices[sprite * 6 + 4] = base + 2;
            gQuadIndices[sprite * 6 + 5] = base + 3;
        }
        sceKernelDcacheWritebackRange(gQuadIndices, sizeof(gQuadIndices));

        sceGuStart(GU_DIRECT, gCommandList);
        mListOpen = true;
        sceGuDrawBuffer(kFramePsm, reinterpret_cast<void *>(0), kBufferWidth);
        sceGuDispBuffer(kScreenWidth, kScreenHeight, reinterpret_cast<void *>(kFrameBytes),
                        kBufferWidth);
        sceGuDepthBuffer(reinterpret_cast<void *>(kDepthOffset), kBufferWidth);
        ApplyViewport();
        sceGuDepthRange(65535, 0);
        sceGuClearDepth(0);
        sceGuShadeModel(GU_SMOOTH);
        sceGuFrontFace(GU_CW);
        sceGuDisable(GU_CULL_FACE);
        sceGuDisable(GU_LIGHTING);
        sceGuDisable(GU_DITHER);
        sceGuDisable(GU_STENCIL_TEST);
        sceGuEnable(GU_CLIP_PLANES);
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(GU_GEQUAL, 4, 0xff);
        sceGuDisable(GU_DEPTH_TEST);
        sceGuDepthMask(GU_FALSE);
        sceGuDepthFunc(GU_GEQUAL);
        sceGuTexWrap(GU_REPEAT, GU_REPEAT);
        sceGuTexFilter(GU_LINEAR, GU_LINEAR);
        sceGuTexMapMode(GU_TEXTURE_COORDS, 0, 0);
        sceGuTexProjMapMode(GU_UV);
        sceGuDisable(GU_TEXTURE_2D);
        sceGuDisable(GU_FOG);
        sceGuClearColor(0xff000000u);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

        // TH07 alternates 2D and 3D matrices several times inside one draw
        // chain.  Keep the backend independent from libGUM's global matrix
        // stack and submit matrices directly to GU.
        for (int mode = GU_PROJECTION; mode <= GU_TEXTURE; ++mode)
        {
            sceGuSetMatrix(mode, &kIdentityMatrix);
        }

        sceGuFinish();
        mListOpen = false;
        sceGuSync(0, 0);

        const unsigned int edram = reinterpret_cast<unsigned int>(sceGeEdramGetAddr());
        auto *display = reinterpret_cast<volatile u16 *>((0x40000000u | edram) + kFrameBytes);
        for (int i = 0; i < kBufferWidth * kScreenHeight; ++i)
        {
            display[i] = 0;
        }

        sceDisplayWaitVblankStart();
        sceGuDisplay(GU_TRUE);
        StartList();
#if defined(TH07_PSP_PERF_DIAG)
        mFrameStartUs = sceKernelGetSystemTimeWide();
#endif
        th07_psp_boot_note(g_Supervisor.cfg.windowed ? "display 4:3 fit 362x272 rgb565"
                                                     : "display full stretch 480x272 rgb565");
        g_Supervisor.cfg.noVertexBuffers = true;
        mInitialized = true;
        return true;
    }

    void Exit() override
    {
        if (!mInitialized && !mListOpen)
        {
            return;
        }
        if (mListOpen)
        {
            FlushDeferredSpriteDraw();
            sceGuFinish();
            sceGuSync(0, 0);
            mListOpen = false;
        }
        for (GuTexture &texture : mTextures)
        {
            if (!texture.borrowedSurfaceCache)
            {
                std::free(texture.pixels);
            }
            std::free(texture.updatePixels);
            texture = GuTexture{};
        }
        for (FreeTextureBlock &block : mFreeTextureBlocks)
        {
            std::free(block.pixels);
            block = FreeTextureBlock{};
        }
        if (!mSurfaceCache.edram)
        {
            std::free(mSurfaceCache.pixels);
        }
        mSurfaceCache = SurfaceCache{};
        sceGuTerm();
        if (gPspGuBackend == this)
        {
            gPspGuBackend = nullptr;
        }
        mInitialized = false;
    }

    RendererType GetType() override
    {
        return RENDERER_OPENGLES;
    }

    void SetFogRange(f32 nearPlane, f32 farPlane) override
    {
        if (mFogParamsKnown && mFogNear == nearPlane && mFogFar == farPlane)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mFogNear = nearPlane;
        mFogFar = farPlane;
        sceGuFog(-mFogNear, -mFogFar, ToGuColor(mFogColor));
        mFogParamsKnown = true;
    }

    void SetFogColor(ZunColor color) override
    {
        if (mFogParamsKnown && mFogColor.color == color.color)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mFogColor = color;
        sceGuFog(-mFogNear, -mFogFar, ToGuColor(mFogColor));
        mFogParamsKnown = true;
    }

    void SetColorOp(TextureOpComponent component, ColorOp op) override
    {
        if (component == COMPONENT_RGB)
        {
            if (mColorOpRgb == op)
            {
                return;
            }
            FlushDeferredSpriteDraw();
            mColorOpRgb = op;
        }
        else
        {
            if (mColorOpAlpha == op)
            {
                return;
            }
            FlushDeferredSpriteDraw();
            mColorOpAlpha = op;
        }
    }

    void SetTextureFactor(ZunColor factor) override
    {
        if (mTextureFactor.color == factor.color)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mTextureFactor = factor;
    }

    void SetTextureArg(TextureArg arg) override
    {
        if (mTextureArg == arg)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mTextureArg = arg;
    }

    void SetTransformMatrix(TransformMatrix type, const ZunMatrix &matrix) override
    {
        if (type < MATRIX_MODEL || type > MATRIX_TEXTURE)
        {
            char message[64];
            std::snprintf(message, sizeof(message), "invalid matrix slot %d", type);
            th07_psp_boot_note(message);
            mError = true;
            return;
        }
        if (std::memcmp(&mTransforms[type], &matrix, sizeof(matrix)) == 0)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mTransforms[type] = matrix;
        // Texture coordinates are transformed while packing CPU vertices.
        // Only model/view/projection correspond to GE matrix state.
        if (type <= MATRIX_PROJECTION)
        {
            mMatrixDirtyMask |= 1u << static_cast<unsigned int>(type);
        }
    }

    void SetTextureFilter() override
    {
        FlushDeferredSpriteDraw();
        sceGuTexFilter(GU_LINEAR, GU_LINEAR);
        mAppliedTexture = ~0u;
    }

    void GetViewport(ZunViewport &viewport) override
    {
        viewport = mViewport;
    }

    void SetViewport(const ZunViewport &viewport) override
    {
        if (std::memcmp(&mViewport, &viewport, sizeof(viewport)) == 0)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mViewport = viewport;
        ApplyViewport();
        // The screen-space projection is derived from the logical viewport.
        mAppliedMatrixMode = -1;
        mMatrixDirtyMask |= 0x7u;
    }

    void Enable(Capabilities cap) override
    {
        switch (cap)
        {
        case CAPS_BLEND:
            if (mBlendEnabled) return;
            FlushDeferredSpriteDraw();
            mBlendEnabled = true;
            sceGuEnable(GU_BLEND);
            break;
        case CAPS_ALPHA_TEST:
            if (mAlphaTestEnabled) return;
            FlushDeferredSpriteDraw();
            mAlphaTestEnabled = true;
            sceGuEnable(GU_ALPHA_TEST);
            break;
        case CAPS_DEPTH_TEST:
            if (mDepthTestEnabled) return;
            FlushDeferredSpriteDraw();
            mDepthTestEnabled = true;
            sceGuEnable(GU_DEPTH_TEST);
            break;
        case CAPS_FOG:
            if (mFogEnabled) return;
            FlushDeferredSpriteDraw();
            mFogEnabled = true;
            sceGuEnable(GU_FOG);
            break;
        }
    }

    void Disable(Capabilities cap) override
    {
        switch (cap)
        {
        case CAPS_BLEND:
            if (!mBlendEnabled) return;
            FlushDeferredSpriteDraw();
            mBlendEnabled = false;
            sceGuDisable(GU_BLEND);
            break;
        case CAPS_ALPHA_TEST:
            if (!mAlphaTestEnabled) return;
            FlushDeferredSpriteDraw();
            mAlphaTestEnabled = false;
            sceGuDisable(GU_ALPHA_TEST);
            break;
        case CAPS_DEPTH_TEST:
            if (!mDepthTestEnabled) return;
            FlushDeferredSpriteDraw();
            mDepthTestEnabled = false;
            sceGuDisable(GU_DEPTH_TEST);
            break;
        case CAPS_FOG:
            if (!mFogEnabled) return;
            FlushDeferredSpriteDraw();
            mFogEnabled = false;
            sceGuDisable(GU_FOG);
            break;
        }
    }

    void SetBlendMode(BlendMode srcMode, BlendMode dstMode) override
    {
        if (mBlendModeKnown && mBlendSrc == srcMode && mBlendDst == dstMode)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        // BLEND_NONE means GL_ONE on the source side in the portable
        // renderer.  Surface-to-backbuffer copies use NONE/NONE, so treating
        // it as SRC_ALPHA subtly changes what is meant to be a plain blit.
        const bool sourceIsOne = srcMode == BLEND_ONE || srcMode == BLEND_NONE;
        int src = sourceIsOne ? GU_FIX : GU_SRC_ALPHA;
        int dst = GU_ONE_MINUS_SRC_ALPHA;
        unsigned int srcFix = sourceIsOne ? 0xffffffffu : 0;
        unsigned int dstFix = 0;
        if (dstMode == BLEND_ONE)
        {
            dst = GU_FIX;
            dstFix = 0xffffffffu;
        }
        else if (dstMode == BLEND_NONE)
        {
            dst = GU_FIX;
            dstFix = 0;
        }
        sceGuBlendFunc(GU_ADD, src, dst, srcFix, dstFix);
        mBlendSrc = srcMode;
        mBlendDst = dstMode;
        mBlendModeKnown = true;
    }

    void SetDepthMask(bool enable) override
    {
        if (mDepthWrite == enable)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mDepthWrite = enable;
        sceGuDepthMask(enable ? GU_FALSE : GU_TRUE);
    }

    void SetDepthFunc(DepthFunc func) override
    {
        if (mDepthFuncKnown && mDepthFunc == func)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        sceGuDepthFunc(func == DEPTH_FUNC_LEQUAL ? GU_GEQUAL : GU_ALWAYS);
        mDepthFunc = func;
        mDepthFuncKnown = true;
    }

    void SetClearDepth(f32 depth) override
    {
        FlushDeferredSpriteDraw();
        mClearDepth = depth;
        sceGuClearDepth(ToHardwareDepth(depth));
    }

    void SetClearColor(ZunColor color) override
    {
        FlushDeferredSpriteDraw();
        mClearColor = color;
        sceGuClearColor(ToGuColor(color));
    }

    void SetAlphaTestRef(u8 ref) override
    {
        if (mAlphaRefKnown && mAlphaRef == ref)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        sceGuAlphaFunc(GU_GEQUAL, ref, 0xff);
        mAlphaRef = ref;
        mAlphaRefKnown = true;
    }

    void Clear(u32 clearBits) override
    {
        FlushDeferredSpriteDraw();
        unsigned int bits = 0;
        if (clearBits & CLEAR_COLOR_BUFFER)
        {
            bits |= GU_COLOR_BUFFER_BIT;
        }
        if (clearBits & CLEAR_DEPTH_BUFFER)
        {
            bits |= GU_DEPTH_BUFFER_BIT;
        }
        sceGuClearColor(ToGuColor(mClearColor));
        sceGuClearDepth(ToHardwareDepth(mClearDepth));
        sceGuClear(bits);
    }

    GfxTextureHandle CreateTexture() override
    {
        for (unsigned int i = 1; i < kMaxTextures; ++i)
        {
            if (!mTextures[i].used)
            {
                mTextures[i].used = true;
                return GfxTextureHandle(i);
            }
        }
        mError = true;
        return {};
    }

    void BindTexture(GfxTextureHandle handle) override
    {
        CheckBoundTexture("bind");
        const unsigned int texture = handle.id < kMaxTextures ? handle.id : 0;
        if (mBoundTexture == texture)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        mBoundTexture = texture;
    }

    void DeleteTexture(GfxTextureHandle handle) override
    {
        if (handle.id == 0 || handle.id >= kMaxTextures || !mTextures[handle.id].used)
        {
            return;
        }
        GuTexture &texture = mTextures[handle.id];
        if (texture.pixels && !texture.borrowedSurfaceCache)
        {
            // Owned texture memory can be returned only after its queued draw
            // has completed.  Cached surface memory remains stable across
            // frames and needs no per-frame GE synchronization.
            SubmitAndRestart();
            RecycleTexturePixels(texture);
        }
        RecyclePixels(texture.updatePixels, texture.updateAllocationBytes);
        texture = GuTexture{};
        if (mAppliedTexture == handle.id)
        {
            mAppliedTexture = ~0u;
        }
        if (mBoundTexture == handle.id)
        {
            mBoundTexture = 0;
        }
    }

    void SetTextureImage(u32 width, u32 height, PixelFormat fmt, PixelDataType type,
                         const void *data) override
    {
        if (mBoundTexture == 0 || width == 0 || height == 0)
        {
            mError = true;
            return;
        }
        GuTexture &texture = mTextures[mBoundTexture];
        if (mAppliedTexture == mBoundTexture)
        {
            mAppliedTexture = ~0u;
        }
        if (texture.pixels && !texture.borrowedSurfaceCache)
        {
            SubmitAndRestart();
            RecycleTexturePixels(texture);
        }
        RecyclePixels(texture.updatePixels, texture.updateAllocationBytes);
        texture.pixels = nullptr;
        texture.updatePixels = nullptr;
        texture.borrowedSurfaceCache = false;

        texture.logicalWidth = static_cast<unsigned int>(width);
        texture.logicalHeight = static_cast<unsigned int>(height);
        texture.storageWidth = NextPowerOfTwo(std::min(texture.logicalWidth, 512u));
        texture.storageHeight = NextPowerOfTwo(std::min(texture.logicalHeight, 512u));
        texture.contentWidth = std::min(texture.logicalWidth, texture.storageWidth);
        texture.contentHeight = std::min(texture.logicalHeight, texture.storageHeight);
        texture.sampleScaleX = static_cast<float>(texture.contentWidth) / texture.storageWidth;
        texture.sampleScaleY = static_cast<float>(texture.contentHeight) / texture.storageHeight;
        // Full-screen RGB565 and static-atlas RGBA8888 are desirable for
        // bandwidth/quality, but the combined fast-path build shut a real PSP
        // down inside title.anm registration.  Use the previously proven
        // allocation formats here; dynamic text is promoted independently by
        // MarkTextTexture after boot.
        const bool surfaceCandidate = data && fmt == PIXEL_RGBA && width == 640 &&
                                      height >= 480 &&
                                      (type == PIXEL_UNSIGNED_BYTE ||
                                       type == PIXEL_UNSIGNED_SHORT_5_6_5);
        if (surfaceCandidate)
        {
            texture.contentWidth = std::min(
                texture.storageWidth,
                static_cast<unsigned int>(g_Supervisor.cfg.windowed ? kFitWidth : kScreenWidth));
            const unsigned int scaledHeight =
                (static_cast<unsigned int>(height) * kScreenHeight + kLogicalHeight - 1u) /
                kLogicalHeight;
            texture.contentHeight = std::min(texture.storageHeight, scaledHeight);
            texture.sampleScaleX =
                static_cast<float>(texture.contentWidth) / texture.storageWidth;
            texture.sampleScaleY =
                static_cast<float>(texture.contentHeight) / texture.storageHeight;
        }
        if (type == PIXEL_UNSIGNED_SHORT_5_6_5)
        {
            texture.psm = GU_PSM_5650;
        }
        else if (type == PIXEL_UNSIGNED_SHORT_5_5_5_1)
        {
            texture.psm = GU_PSM_5551;
        }
        else if (type == PIXEL_UNSIGNED_SHORT_4_4_4_4)
        {
            texture.psm = GU_PSM_4444;
        }
        else if (surfaceCandidate && kFastSurfaceTextures)
        {
            texture.psm = GU_PSM_5650;
        }
        else if (type == PIXEL_UNSIGNED_BYTE)
        {
            texture.psm = surfaceCandidate
                              ? GU_PSM_8888
                              : (kFastStaticTextureFormats ? GU_PSM_8888 : GU_PSM_4444);
        }
        else
        {
            texture.psm = GU_PSM_8888;
        }
        const unsigned int bytesPerPixel = texture.psm == GU_PSM_8888 ? 4u : 2u;
        texture.bytes = texture.storageWidth * texture.storageHeight * bytesPerPixel;
        texture.swizzled = (surfaceCandidate ? kSwizzleSurfaceTextures
                                             : kSwizzleStaticTextures) &&
                           texture.storageWidth * bytesPerPixel >= 16u &&
                           texture.storageHeight >= 8u;
        texture.surfaceCandidate = surfaceCandidate;
        // Surface frames reuse one handle every draw; tracing those would add
        // synchronous Memory Stick writes to the title loop.  The shutdown we
        // are isolating occurs in static title.anm registration.
        const bool traceTexture = !surfaceCandidate && mTextureCreateTraces < 64;
        if (traceTexture)
        {
            char message[144];
            std::snprintf(message, sizeof(message),
                          "teximg %u begin h%u %ux%u -> %ux%u p%d b%u free%uK",
                          mTextureCreateTraces, mBoundTexture,
                          static_cast<unsigned int>(width),
                          static_cast<unsigned int>(height), texture.storageWidth,
                          texture.storageHeight, texture.psm, texture.bytes,
                          sceKernelTotalFreeMemSize() / 1024u);
            th07_psp_boot_note(message);
            ++mTextureCreateTraces;
        }
        // Only the game's ending/title JPEG and backbuffer-sized copies may
        // borrow the one-entry surface cache.  Ending story images are
        // 640x480, while staff00.jpg is 640x627 and is scrolled through a
        // 640x480 viewport.  Excluding the latter caused all ~400k source
        // pixels to be resampled, allocated and freed on every staff frame.
        // The dialogue atlas is 1024 pixels wide
        // and starts as an RGBA zero buffer, so the old broad `width > 512`
        // test accidentally aliased it with that cache.  Subsequent text
        // uploads then had no private/update backing and PPSSPP kept showing
        // the first rendered line ("さむ～").
        if (surfaceCandidate && mSurfaceCache.valid && mSurfaceCache.source == data &&
            mSurfaceCache.width == width && mSurfaceCache.height == height &&
            mSurfaceCache.bytes == texture.bytes)
        {
#if defined(TH07_PSP_PERF_DIAG)
            ++mSurfaceCacheHits;
#endif
            texture.pixels = mSurfaceCache.pixels;
            texture.borrowedSurfaceCache = true;
            return;
        }

        if (surfaceCandidate)
        {
#if defined(TH07_PSP_PERF_DIAG)
            ++mSurfaceCacheMisses;
#endif
            // A previous frame may still sample this allocation.  Synchronize
            // only when replacing the cached image, never on cache hits.
            SubmitAndRestart();
            if (mSurfaceCache.bytes != texture.bytes)
            {
                if (!mSurfaceCache.edram)
                {
                    RecyclePixels(mSurfaceCache.pixels, mSurfaceCache.allocationBytes);
                }
                else
                {
                    mSurfaceCache.pixels = nullptr;
                    mSurfaceCache.allocationBytes = 0;
                }
                mSurfaceCache.edram = false;

                // The two framebuffers and depth buffer leave room for one
                // 512x512 RGB565 full-screen image.  sceGuTexImage expects an
                // absolute texture address here (unlike the relative buffer
                // offsets passed to sceGuDrawBuffer/sceGuDepthBuffer).
                if (kEdramBytes + texture.bytes <= sceGeEdramGetSize())
                {
                    const uintptr_t edram = reinterpret_cast<uintptr_t>(sceGeEdramGetAddr());
                    mSurfaceCache.pixels = reinterpret_cast<void *>(
                        (0x40000000u | edram) + kEdramBytes);
                    mSurfaceCache.edram = true;
                    th07_psp_boot_note("surface cache EDRAM");
                }
                else
                {
                    mSurfaceCache.pixels =
                        AcquireTexturePixels(texture.bytes, &mSurfaceCache.allocationBytes);
                }
                mSurfaceCache.bytes = mSurfaceCache.pixels ? texture.bytes : 0;
            }
            texture.pixels = mSurfaceCache.pixels;
            texture.borrowedSurfaceCache = true;
        }
        else
        {
            texture.pixels = AcquireTexturePixels(texture.bytes, &texture.allocationBytes);
        }
        if (!texture.pixels)
        {
            const struct mallinfo heap = mallinfo();
            char message[176];
            std::snprintf(message, sizeof(message),
                          "texture allocation failed %u %ux%u bytes %u KFREE%u HFREE%u TOP%u",
                          mBoundTexture, texture.storageWidth, texture.storageHeight,
                          texture.bytes, sceKernelTotalFreeMemSize(),
                          static_cast<unsigned int>(heap.fordblks),
                          static_cast<unsigned int>(heap.keepcost));
            th07_psp_boot_note(message);
            mError = true;
            return;
        }
        std::memset(texture.pixels, 0, texture.bytes);

        if (data)
        {
            if (surfaceCandidate)
            {
                SurfaceAxisSample xSamples[512];
                SurfaceAxisSample ySamples[512];
                BuildSurfaceAxisSamples(xSamples, texture.contentWidth,
                                        static_cast<unsigned int>(width));
                BuildSurfaceAxisSamples(ySamples, texture.contentHeight,
                                        static_cast<unsigned int>(height));
                for (unsigned int y = 0; y < texture.contentHeight; ++y)
                {
                    const SurfaceAxisSample sy = ySamples[y];
                    const unsigned int wy0 = 256u - sy.fraction;
                    const unsigned int wy1 = sy.fraction;
                    for (unsigned int x = 0; x < texture.contentWidth; ++x)
                    {
                        const SurfaceAxisSample sx = xSamples[x];
                        const unsigned int wx0 = 256u - sx.fraction;
                        const unsigned int wx1 = sx.fraction;
                        unsigned int p00[4], p10[4], p01[4], p11[4];
                        ReadSourcePixel(fmt, type, data,
                                        static_cast<unsigned int>(sy.first) * width + sx.first,
                                        p00[0], p00[1], p00[2], p00[3]);
                        ReadSourcePixel(fmt, type, data,
                                        static_cast<unsigned int>(sy.first) * width + sx.second,
                                        p10[0], p10[1], p10[2], p10[3]);
                        ReadSourcePixel(fmt, type, data,
                                        static_cast<unsigned int>(sy.second) * width + sx.first,
                                        p01[0], p01[1], p01[2], p01[3]);
                        ReadSourcePixel(fmt, type, data,
                                        static_cast<unsigned int>(sy.second) * width + sx.second,
                                        p11[0], p11[1], p11[2], p11[3]);
                        unsigned int rgba[4];
                        for (unsigned int component = 0; component < 4; ++component)
                        {
                            const unsigned int top =
                                p00[component] * wx0 + p10[component] * wx1;
                            const unsigned int bottom =
                                p01[component] * wx0 + p11[component] * wx1;
                            rgba[component] =
                                (top * wy0 + bottom * wy1 + 32768u) >> 16;
                        }
                        WriteTexturePixel(texture, y * texture.storageWidth + x,
                                          rgba[0], rgba[1], rgba[2], rgba[3]);
                    }
                }
                char surfaceMessage[96];
                std::snprintf(surfaceMessage, sizeof(surfaceMessage),
                              "surface scaled %ux%u>%ux%u rgb565",
                              static_cast<unsigned int>(width),
                              static_cast<unsigned int>(height), texture.contentWidth,
                              texture.contentHeight);
                th07_psp_boot_note(surfaceMessage);
            }
            else if (fmt == PIXEL_RGBA && type == PIXEL_UNSIGNED_BYTE &&
                texture.psm == GU_PSM_8888 && texture.contentWidth == width &&
                texture.contentHeight == height && texture.storageWidth == width &&
                (!texture.swizzled || (height & 7u) == 0))
            {
                // RGBA8 already has the exact byte layout sampled by GE.  For
                // complete swizzle blocks, rearrange 16-byte x 8-row tiles;
                // otherwise the linear case is one memcpy.
                const auto *source = static_cast<const unsigned char *>(data);
                if (texture.swizzled)
                {
                    SwizzleCopy(static_cast<unsigned char *>(texture.pixels), source, width * 4u,
                                height);
                }
                else
                {
                    std::memcpy(texture.pixels, source, width * height * 4u);
                }
            }
            else
            {
                const unsigned int sourceStepX =
                    static_cast<unsigned int>((width << 16) / texture.contentWidth);
                const unsigned int sourceStepY =
                    static_cast<unsigned int>((height << 16) / texture.contentHeight);
            for (unsigned int y = 0; y < texture.contentHeight; ++y)
            {
                const unsigned int sourceY = (y * sourceStepY) >> 16;
                for (unsigned int x = 0; x < texture.contentWidth; ++x)
                {
                    const unsigned int sourceX = (x * sourceStepX) >> 16;
                    unsigned int r, g, b, a;
                    ReadSourcePixel(fmt, type, data, sourceY * width + sourceX, r, g, b, a);
                    WriteTexturePixel(texture, y * texture.storageWidth + x, r, g, b, a);
                }
            }
            }
        }
        if (!mSurfaceCache.edram || !surfaceCandidate)
        {
            sceKernelDcacheWritebackRange(texture.pixels, texture.bytes);
        }
        sceGuTexFlush();
        if (traceTexture)
        {
            char message[96];
            std::snprintf(message, sizeof(message), "teximg end h%u free%uK",
                          mBoundTexture, sceKernelTotalFreeMemSize() / 1024u);
            th07_psp_boot_note(message);
        }
#if defined(TH07_PSP_PERF_DIAG)
        mTextureUploadBytes += texture.bytes;
#endif
        if (surfaceCandidate)
        {
            mSurfaceCache.source = data;
            mSurfaceCache.width = width;
            mSurfaceCache.height = height;
            mSurfaceCache.valid = true;
        }
    }

    void ForgetSurface(const void *pixels)
    {
        if (!pixels || !mSurfaceCache.valid || mSurfaceCache.source != pixels)
        {
            return;
        }
        // ReleaseSurface can run immediately after a frame which referenced
        // the cache.  Finish that list before the allocation is made eligible
        // for a different JPEG at the same SDL heap address.
        SubmitAndRestart();
        mSurfaceCache.source = nullptr;
        mSurfaceCache.valid = false;

        // TH06 PSP hit the same title -> attract-demo boundary: retaining a
        // full-screen conversion after the SDL surface was gone left about
        // 1 MiB unavailable while the stage, GUI and replay were registered.
        // A temporary surface texture is deleted before ReleaseSurface calls
        // this hook, so reclaim the unborrowed cache at that lifetime edge.
        for (const GuTexture &texture : mTextures)
        {
            if (texture.used && texture.borrowedSurfaceCache &&
                texture.pixels == mSurfaceCache.pixels)
            {
                return;
            }
        }
        if (!mSurfaceCache.edram)
        {
            RecyclePixels(mSurfaceCache.pixels, mSurfaceCache.allocationBytes);
        }
        mSurfaceCache = SurfaceCache{};
        th07_psp_boot_note("surface cache released");
    }

    unsigned int TrimTextureCache()
    {
        // Deleted textures are normally retained for quick reuse.  That is
        // useful inside a stage, but after several large 3D stages the mix of
        // atlas sizes can leave enough total memory free while no single heap
        // block is large enough for the next stage.  All blocks in this list
        // are detached from live textures and safe to return to libc.
        SubmitAndRestart();
        unsigned int releasedBytes = 0;
        for (FreeTextureBlock &block : mFreeTextureBlocks)
        {
            releasedBytes += block.bytes;
            std::free(block.pixels);
            block = FreeTextureBlock{};
        }
        return releasedBytes;
    }

    void MarkTextTexture(GfxTextureHandle handle)
    {
        if (handle.id == 0 || handle.id >= kMaxTextures)
        {
            return;
        }
        GuTexture &texture = mTextures[handle.id];
        if (!texture.used || texture.textAtlas)
        {
            return;
        }

        if (!texture.pixels || texture.borrowedSurfaceCache)
        {
            texture.textAtlas = true;
            mAppliedTexture = ~0u;
            return;
        }

        if (texture.psm == GU_PSM_8888)
        {
            // TH06 keeps its dynamic RGBA atlas swizzled and patches glyphs in
            // swizzled address order.  TH07's previous linear promotion made
            // the 512x512 Music Room atlas dominate steady-state GE time.
            const bool canSwizzle = texture.storageWidth * 4u >= 16u &&
                                    texture.storageHeight >= 8u;
            if (canSwizzle && !texture.swizzled)
            {
                SubmitAndRestart();
                const unsigned int swizzledBytes = texture.bytes;
                unsigned int swizzledAllocationBytes = 0;
                void *swizzledPixels =
                    AcquireTexturePixels(swizzledBytes, &swizzledAllocationBytes);
                if (!swizzledPixels)
                {
                    th07_psp_boot_note("text atlas swizzle allocation failed");
                    mError = true;
                    return;
                }
                SwizzleCopy(static_cast<unsigned char *>(swizzledPixels),
                            static_cast<const unsigned char *>(texture.pixels),
                            texture.storageWidth * 4u, texture.storageHeight);
                sceKernelDcacheWritebackRange(swizzledPixels, swizzledBytes);
                RecycleTexturePixels(texture);
                texture.pixels = swizzledPixels;
                texture.allocationBytes = swizzledAllocationBytes;
                texture.bytes = swizzledBytes;
                texture.swizzled = true;
                sceGuTexFlush();
            }
            texture.textAtlas = true;
            mAppliedTexture = ~0u;
            return;
        }

        // TH06's final text path promotes its initially empty dynamic atlas to
        // RGBA8888 before the first glyph upload.  TH07 previously kept the
        // ANM-declared RGBA4444 allocation, reducing the antialiased outline
        // to sixteen alpha levels before the 640x480 -> PSP shrink.
        SubmitAndRestart();
        const unsigned int rgbaBytes = texture.storageWidth * texture.storageHeight * 4u;
        unsigned int rgbaAllocationBytes = 0;
        void *rgbaPixels = AcquireTexturePixels(rgbaBytes, &rgbaAllocationBytes);
        if (!rgbaPixels)
        {
            th07_psp_boot_note("text atlas RGBA8888 allocation failed");
            mError = true;
            return;
        }
        std::memset(rgbaPixels, 0, rgbaBytes);
        RecycleTexturePixels(texture);
        RecyclePixels(texture.updatePixels, texture.updateAllocationBytes);
        texture.pixels = rgbaPixels;
        texture.allocationBytes = rgbaAllocationBytes;
        texture.updatePixels = nullptr;
        texture.psm = GU_PSM_8888;
        texture.bytes = rgbaBytes;
        texture.textAtlas = true;
        texture.swizzled = texture.storageWidth * 4u >= 16u &&
                           texture.storageHeight >= 8u;
        sceKernelDcacheWritebackRange(texture.pixels, texture.bytes);
        sceGuTexFlush();
        mAppliedTexture = ~0u;

        char message[96];
        std::snprintf(message, sizeof(message), "text atlas %u RGBA8888 %ux%u swz%d",
                      static_cast<unsigned int>(handle.id),
                      texture.storageWidth, texture.storageHeight,
                      texture.swizzled ? 1 : 0);
        th07_psp_boot_note(message);
    }

    void CompactTextTexture(GfxTextureHandle handle)
    {
        if (handle.id == 0 || handle.id >= kMaxTextures)
        {
            return;
        }
        GuTexture &texture = mTextures[handle.id];
        if (!texture.used || !texture.textAtlas || !texture.updatePixels)
        {
            return;
        }

        // SetTextureSubImage synchronizes before writing and leaves the newest
        // atlas in `pixels`; the alternate allocation exists only to force a
        // new address on the next dynamic update.  Static menu text can return
        // that often-1-MiB buffer now and allocate it again only when the user
        // actually selects another track.
        std::free(texture.updatePixels);
        texture.updatePixels = nullptr;
        texture.updateAllocationBytes = 0;
    }

    void BeginTextUploadBatch()
    {
        if (mTextUploadBatchActive)
        {
            EndTextUploadBatch();
        }

        // No previously queued draw may still sample an atlas that is about
        // to become the source of the batch's alternate copy.  The music
        // room then performs only CPU writes until EndTextUploadBatch.
        SubmitAndRestart();
        mTextUploadBatchActive = true;
    }

    void EndTextUploadBatch()
    {
        if (!mTextUploadBatchActive)
        {
            return;
        }

        bool updated = false;
        for (GuTexture &texture : mTextures)
        {
            if (!texture.textBatchPending)
            {
                continue;
            }

            void *writePixels = texture.updatePixels ? texture.updatePixels : texture.pixels;
            if (writePixels)
            {
                sceKernelDcacheWritebackRange(writePixels, texture.bytes);
                if (texture.updatePixels)
                {
                    std::swap(texture.pixels, texture.updatePixels);
                    std::swap(texture.allocationBytes, texture.updateAllocationBytes);
                }
                updated = true;
            }
            texture.textBatchPending = false;
        }
        if (updated)
        {
            sceGuTexFlush();
            mAppliedTexture = ~0u;
        }
        mTextUploadBatchActive = false;
    }

    void SetTextureSubImage(i32 xoffset, i32 yoffset, i32 width, i32 height,
                            const void *data) override
    {
        if (mBoundTexture == 0 || !data || width <= 0 || height <= 0)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        GuTexture &texture = mTextures[mBoundTexture];
        if (mAppliedTexture == mBoundTexture)
        {
            mAppliedTexture = ~0u;
        }
        if (!texture.pixels || xoffset < 0 || yoffset < 0 ||
            xoffset + width > static_cast<i32>(texture.logicalWidth) ||
            yoffset + height > static_cast<i32>(texture.logicalHeight))
        {
            mError = true;
            return;
        }
#if defined(TH07_PSP_DIRECT_GAME)
        {
            static unsigned int subImageLogCount;
            if (subImageLogCount < 80)
            {
                const auto *hashBytes = static_cast<const unsigned char *>(data);
                const unsigned int hashSize = static_cast<unsigned int>(width * height * 4);
                unsigned int hash = 2166136261u;
                for (unsigned int i = 0; i < hashSize; ++i)
                {
                    hash = (hash ^ hashBytes[i]) * 16777619u;
                }
                char message[160];
                std::snprintf(message, sizeof(message),
                              "subimg %u tex %u %u,%u %dx%d hash %08x dst %p alt %p",
                              subImageLogCount, mBoundTexture, xoffset, yoffset, width, height,
                              hash, texture.pixels, texture.updatePixels);
                th07_psp_boot_note(message);
                ++subImageLogCount;
            }
        }
#endif
        const bool batchedTextUpdate =
            mTextUploadBatchActive && texture.textAtlas && !texture.borrowedSurfaceCache;
        const bool synchronizedTextUpdate =
            !batchedTextUpdate && texture.textAtlas && !texture.borrowedSurfaceCache;
        if (!batchedTextUpdate)
        {
            SubmitAndRestart();
        }
        const auto *src = static_cast<const unsigned char *>(data);
        void *writePixels = texture.pixels;
        bool initializedAlternate = false;
        if (!texture.borrowedSurfaceCache)
        {
            if (batchedTextUpdate)
            {
                if (!texture.textBatchPending)
                {
                    if (!texture.updatePixels)
                    {
                        texture.updatePixels =
                            AcquireTexturePixels(texture.bytes, &texture.updateAllocationBytes);
                    }
                    if (texture.updatePixels)
                    {
                        std::memcpy(texture.updatePixels, texture.pixels, texture.bytes);
                    }
                    texture.textBatchPending = true;
                }
                writePixels = texture.updatePixels ? texture.updatePixels : texture.pixels;
            }
            else
            {
                if (!texture.updatePixels)
                {
                    texture.updatePixels =
                        AcquireTexturePixels(texture.bytes, &texture.updateAllocationBytes);
                    initializedAlternate = texture.updatePixels != nullptr;
                }
                if (texture.updatePixels)
                {
                    // Text atlases keep both alternating addresses in sync.
                    // Only the first allocation needs a whole-atlas copy;
                    // later glyph uploads patch the same rows in both copies.
                    if (!synchronizedTextUpdate || initializedAlternate)
                    {
                        std::memcpy(texture.updatePixels, texture.pixels, texture.bytes);
                    }
                    writePixels = texture.updatePixels;
                }
            }
        }
        const int dstLeft = static_cast<int>(static_cast<unsigned long long>(xoffset) *
                                             texture.contentWidth / texture.logicalWidth);
        const int dstTop = static_cast<int>(static_cast<unsigned long long>(yoffset) *
                                            texture.contentHeight / texture.logicalHeight);
        const int dstRight = static_cast<int>((static_cast<unsigned long long>(xoffset + width) *
                                               texture.contentWidth + texture.logicalWidth - 1) /
                                              texture.logicalWidth);
        const int dstBottom = static_cast<int>((static_cast<unsigned long long>(yoffset + height) *
                                                texture.contentHeight + texture.logicalHeight - 1) /
                                               texture.logicalHeight);
        const auto writeRegion = [&](void *destination) {
            // Dynamic text atlases are RGBA8888 with a 1:1 logical/content
            // mapping.  Avoid two 64-bit coordinate divisions and a format
            // repack for every pixel; the SDL source bytes already match the
            // little-endian GU_PSM_8888 layout exactly.
            if (texture.psm == GU_PSM_8888 && !texture.swizzled &&
                texture.contentWidth == texture.logicalWidth &&
                texture.contentHeight == texture.logicalHeight)
            {
                auto *dstBytes = static_cast<unsigned char *>(destination);
                const unsigned int dstStride = texture.storageWidth * 4u;
                const unsigned int rowBytes = static_cast<unsigned int>(width) * 4u;
                for (int row = 0; row < height; ++row)
                {
                    std::memcpy(dstBytes +
                                    (static_cast<unsigned int>(yoffset + row) * dstStride +
                                     static_cast<unsigned int>(xoffset) * 4u),
                                src + static_cast<unsigned int>(row) * rowBytes, rowBytes);
                }
                return;
            }

            GuTexture writeTexture = texture;
            writeTexture.pixels = destination;
            for (int y = dstTop; y < dstBottom; ++y)
            {
                const int sourceY = std::min(height - 1, std::max(0, static_cast<int>(
                    static_cast<unsigned long long>(y) * texture.logicalHeight /
                        texture.contentHeight - yoffset)));
                for (int x = dstLeft; x < dstRight; ++x)
                {
                    const int sourceX = std::min(width - 1, std::max(0, static_cast<int>(
                        static_cast<unsigned long long>(x) * texture.logicalWidth /
                            texture.contentWidth - xoffset)));
                    const unsigned char *pixel = src + (sourceY * width + sourceX) * 4;
                    WriteTexturePixel(writeTexture, y * texture.storageWidth + x,
                                      pixel[0], pixel[1], pixel[2], pixel[3]);
                }
            }
        };
        writeRegion(writePixels);
        if (synchronizedTextUpdate && texture.updatePixels &&
            texture.pixels != writePixels)
        {
            writeRegion(texture.pixels);
        }
        if (!batchedTextUpdate)
        {
            if (synchronizedTextUpdate)
            {
                if (texture.swizzled)
                {
                    // A logical row spans several 16x8 swizzle blocks.  A
                    // contiguous row range is therefore not a valid cache
                    // range; publish the fixed atlas as one object after an
                    // edit, just as TH06 does.
                    sceKernelDcacheWritebackRange(texture.pixels, texture.bytes);
                    if (texture.updatePixels)
                    {
                        sceKernelDcacheWritebackRange(texture.updatePixels, texture.bytes);
                    }
                }
                else
                {
                    const unsigned int dirtyOffset =
                        static_cast<unsigned int>(dstTop) * texture.storageWidth * 4u;
                    const unsigned int dirtyBytes =
                        static_cast<unsigned int>(dstBottom - dstTop) * texture.storageWidth * 4u;
                    auto *currentBytes = static_cast<unsigned char *>(texture.pixels);
                    sceKernelDcacheWritebackRange(currentBytes + dirtyOffset, dirtyBytes);
                    if (texture.updatePixels)
                    {
                        if (initializedAlternate)
                        {
                            sceKernelDcacheWritebackRange(texture.updatePixels, texture.bytes);
                        }
                        else
                        {
                            auto *alternateBytes =
                                static_cast<unsigned char *>(texture.updatePixels);
                            sceKernelDcacheWritebackRange(alternateBytes + dirtyOffset, dirtyBytes);
                        }
                    }
                }
            }
            else
            {
                sceKernelDcacheWritebackRange(writePixels, texture.bytes);
            }
        }
#if defined(TH07_PSP_PERF_DIAG)
        mTextureUploadBytes += static_cast<unsigned long long>(width) * height * 4u;
#endif
        if (!batchedTextUpdate && writePixels == texture.updatePixels)
        {
            std::swap(texture.pixels, texture.updatePixels);
            std::swap(texture.allocationBytes, texture.updateAllocationBytes);
        }
        if (!batchedTextUpdate)
        {
            sceGuTexFlush();
        }
    }

    void ReadPixels(i32 x, i32 y, i32 width, i32 height, void *pixels) override
    {
        if (!pixels || width <= 0 || height <= 0)
        {
            return;
        }
        SubmitAndRestart();
        const unsigned int edram = reinterpret_cast<unsigned int>(sceGeEdramGetAddr());
        const auto *frame = reinterpret_cast<const u16 *>(
            0x40000000u | edram | (mCurrentDrawBuffer ? kFrameBytes : 0u));
        auto *dst = static_cast<unsigned char *>(pixels);
        for (int row = 0; row < height; ++row)
        {
            const int logicalY = y + row;
            const int sourceY = kScreenHeight - 1 -
                                logicalY * kScreenHeight / kLogicalHeight;
            for (int column = 0; column < width; ++column)
            {
                const int logicalX = x + column;
            const int contentWidth = g_Supervisor.cfg.windowed ? kFitWidth : kScreenWidth;
            const int contentLeft = g_Supervisor.cfg.windowed ? kFitLeft : 0;
            const int sourceX = contentLeft + logicalX * contentWidth / kLogicalWidth;
                u16 color = 0;
                if (sourceX >= 0 && sourceX < kScreenWidth && sourceY >= 0 &&
                    sourceY < kScreenHeight)
                {
                    color = frame[sourceY * kBufferWidth + sourceX];
                }
                unsigned char *out = dst + (row * width + column) * 4;
                const unsigned int r = color & 0x1fu;
                const unsigned int g = (color >> 5) & 0x3fu;
                const unsigned int b = (color >> 11) & 0x1fu;
                out[0] = static_cast<unsigned char>((r << 3) | (r >> 2));
                out[1] = static_cast<unsigned char>((g << 2) | (g >> 4));
                out[2] = static_cast<unsigned char>((b << 3) | (b >> 2));
                out[3] = 0xff;
            }
        }
    }

    void DrawPrimitive(PrimitiveType, i32, i32) override
    {
        // PSP forces the engine's noVertexBuffers path, which supplies the
        // actual vertices to DrawPrimitiveUP.
        mError = true;
    }

    void DrawPrimitiveUP(PrimitiveType type, i32 primitiveCount, const void *vertexData,
                         i32 vertexStride) override
    {
        // A general primitive must not overtake a delayed AnmManager batch.
        FlushDeferredSpriteDraw();
        if (!vertexData || primitiveCount <= 0)
        {
            return;
        }
        int vertexCount = 0;
        int primitive = GU_TRIANGLES;
        const bool quadBatch = type == PRIM_QUADS;
        if (quadBatch)
        {
            vertexCount = primitiveCount * 4;
        }
        else if (type == PRIM_TRIANGLES)
        {
            vertexCount = primitiveCount * 3;
        }
        else if (type == PRIM_TRIANGLE_STRIP)
        {
            vertexCount = primitiveCount + 2;
            primitive = GU_TRIANGLE_STRIP;
        }
        else
        {
            vertexCount = primitiveCount + 2;
            primitive = GU_TRIANGLE_FAN;
        }

        const bool screenSpace = vertexStride == static_cast<i32>(sizeof(VertexTex1DiffuseXyzrhw)) ||
                                 vertexStride == static_cast<i32>(sizeof(VertexDiffuseXyzrhw));
        const bool textured = vertexStride == static_cast<i32>(sizeof(VertexTex1DiffuseXyzrhw)) ||
                              vertexStride == static_cast<i32>(sizeof(VertexTex1DiffuseXyz));
        if (!screenSpace && !textured)
        {
            mError = true;
            return;
        }

        ApplyMatrices(screenSpace);
        ApplyTexture(textured, screenSpace);

        const auto *source = static_cast<const unsigned char *>(vertexData);
        const auto colorAt = [&](int index) {
            const unsigned char *raw = source + index * vertexStride;
            ZunColor diffuse;
            if (textured)
            {
                if (screenSpace)
                {
                    diffuse = reinterpret_cast<const VertexTex1DiffuseXyzrhw *>(raw)->color;
                }
                else
                {
                    diffuse = reinterpret_cast<const VertexTex1DiffuseXyz *>(raw)->diffuse;
                }
            }
            else
            {
                diffuse = reinterpret_cast<const VertexDiffuseXyzrhw *>(raw)->diffuse;
            }
            return SelectVertexColor(diffuse);
        };

        // Most TH07 sprite batches carry a diffuse field even though every
        // vertex is the same colour.  GE can take that colour once as state,
        // reducing the native vertex from 24 to 20 bytes (16 to 12 without a
        // texture).  Keep the full layout for genuinely varying batches.
        unsigned int constantColor = colorAt(0);
        bool constantColorBatch = true;
        const int colorStep = quadBatch ? 4 : 1;
        for (int i = colorStep; i < vertexCount; i += colorStep)
        {
            if (colorAt(i) != constantColor)
            {
                constantColorBatch = false;
                break;
            }
        }

        // PSP AnmManager submits four unique quad corners. Axis-aligned batches
        // become two-corner GU sprites; any rotated quad keeps those four
        // vertices and uses a compact shared index stream for two triangles.
        bool collapseSprites =
            quadBatch && screenSpace && textured && vertexCount >= 4 &&
            vertexStride == static_cast<i32>(sizeof(VertexTex1DiffuseXyzrhw));
        const VertexTex1DiffuseXyzrhw *spriteSource = nullptr;
        if (collapseSprites)
        {
            spriteSource = static_cast<const VertexTex1DiffuseXyzrhw *>(vertexData);
            const int spriteCount = primitiveCount;
            for (int sprite = 0; sprite < spriteCount; ++sprite)
            {
                const VertexTex1DiffuseXyzrhw *q = spriteSource + sprite * 4;
                if (q[0].pos.x != q[2].pos.x || q[1].pos.x != q[3].pos.x ||
                    q[0].pos.y != q[1].pos.y || q[2].pos.y != q[3].pos.y ||
                    q[0].pos.z != q[1].pos.z || q[0].pos.z != q[2].pos.z ||
                    q[0].pos.z != q[3].pos.z || q[0].pos.x > q[3].pos.x ||
                    q[0].pos.y > q[3].pos.y ||
                    q[0].textureUV.x != q[2].textureUV.x ||
                    q[1].textureUV.x != q[3].textureUV.x ||
                    q[0].textureUV.y != q[1].textureUV.y ||
                    q[2].textureUV.y != q[3].textureUV.y)
                {
                    collapseSprites = false;
                    spriteSource = nullptr;
                    break;
                }
            }
        }

        const bool indexedQuads = quadBatch && !collapseSprites;
        const int submittedVertexCount = collapseSprites ? primitiveCount * 2 : vertexCount;
        const int indexCount = indexedQuads ? primitiveCount * 6 : 0;
        const unsigned int nativeVertexBytes =
            textured ? (constantColorBatch ? sizeof(GuVertexTex) : sizeof(GuVertexTexColor))
                     : (constantColorBatch ? sizeof(GuVertexPosition) : sizeof(GuVertexColor));
        const unsigned int vertexBytes =
            static_cast<unsigned int>(submittedVertexCount) * nativeVertexBytes;
        const bool cachedQuadIndices =
            indexedQuads && primitiveCount <= static_cast<int>(kCachedQuadIndexCount);
        const unsigned int dynamicIndexBytes =
            cachedQuadIndices ? 0u : static_cast<unsigned int>(indexCount) * sizeof(u16);
        if (!EnsureListSpace(vertexBytes + dynamicIndexBytes))
        {
            mError = true;
            return;
        }
        void *packed = sceGuGetMemory(vertexBytes);
        if (!packed)
        {
            mError = true;
            return;
        }
        u16 *indices = nullptr;
        if (indexedQuads)
        {
            if (cachedQuadIndices)
            {
                indices = gQuadIndices;
#if defined(TH07_PSP_PERF_DIAG)
                ++mCachedQuadIndexBatches;
#endif
            }
            else
            {
                indices = static_cast<u16 *>(sceGuGetMemory(dynamicIndexBytes));
                if (!indices)
                {
                    mError = true;
                    return;
                }
                for (int sprite = 0; sprite < primitiveCount; ++sprite)
                {
                    const u16 base = static_cast<u16>(sprite * 4);
                    indices[sprite * 6 + 0] = base;
                    indices[sprite * 6 + 1] = base + 1;
                    indices[sprite * 6 + 2] = base + 2;
                    indices[sprite * 6 + 3] = base + 1;
                    indices[sprite * 6 + 4] = base + 2;
                    indices[sprite * 6 + 5] = base + 3;
                }
            }
        }

        const GuTexture *texture = mBoundTexture ? &mTextures[mBoundTexture] : nullptr;
        if (constantColorBatch &&
            (!mPrimitiveColorKnown || mAppliedPrimitiveColor != constantColor))
        {
            sceGuColor(constantColor);
            mPrimitiveColorKnown = true;
            mAppliedPrimitiveColor = constantColor;
        }

        if (textured)
        {
            if (collapseSprites && constantColorBatch)
            {
                auto *out = static_cast<GuVertexTex *>(packed);
                const int spriteCount = primitiveCount;
                for (int sprite = 0; sprite < spriteCount; ++sprite)
                {
                    const VertexTex1DiffuseXyzrhw *q = spriteSource + sprite * 4;
                    const VertexTex1DiffuseXyzrhw *corners[2] = {&q[0], &q[3]};
                    for (int corner = 0; corner < 2; ++corner)
                    {
                        const int outIndex = sprite * 2 + corner;
                        out[outIndex].u = texture && texture->pixels ? corners[corner]->textureUV.x
                                                                     : 0.0f;
                        out[outIndex].v = texture && texture->pixels ? corners[corner]->textureUV.y
                                                                     : 0.0f;
                        out[outIndex].x = corners[corner]->pos.x;
                        out[outIndex].y = corners[corner]->pos.y;
                        out[outIndex].z = corners[corner]->pos.z;
                    }
                }
            }
            else if (collapseSprites)
            {
                auto *out = static_cast<GuVertexTexColor *>(packed);
                const int spriteCount = primitiveCount;
                for (int sprite = 0; sprite < spriteCount; ++sprite)
                {
                    const VertexTex1DiffuseXyzrhw *q = spriteSource + sprite * 4;
                    const VertexTex1DiffuseXyzrhw *corners[2] = {&q[0], &q[3]};
                    const unsigned int color = colorAt(sprite * 4);
                    for (int corner = 0; corner < 2; ++corner)
                    {
                        const int outIndex = sprite * 2 + corner;
                        out[outIndex].u = texture && texture->pixels ? corners[corner]->textureUV.x
                                                                     : 0.0f;
                        out[outIndex].v = texture && texture->pixels ? corners[corner]->textureUV.y
                                                                     : 0.0f;
                        out[outIndex].color = color;
                        out[outIndex].x = corners[corner]->pos.x;
                        out[outIndex].y = corners[corner]->pos.y;
                        out[outIndex].z = corners[corner]->pos.z;
                    }
                }
            }
            else if (constantColorBatch)
            {
                auto *out = static_cast<GuVertexTex *>(packed);
                for (int i = 0; i < vertexCount; ++i)
                {
                    const unsigned char *raw = source + i * vertexStride;
                    float u;
                    float v;
                    if (screenSpace)
                    {
                        const auto *vertex =
                            reinterpret_cast<const VertexTex1DiffuseXyzrhw *>(raw);
                        out[i].x = vertex->pos.x;
                        out[i].y = vertex->pos.y;
                        out[i].z = vertex->pos.z;
                        u = vertex->textureUV.x;
                        v = vertex->textureUV.y;
                    }
                    else
                    {
                        const auto *vertex = reinterpret_cast<const VertexTex1DiffuseXyz *>(raw);
                        out[i].x = vertex->position.x;
                        out[i].y = vertex->position.y;
                        out[i].z = vertex->position.z;
                        u = vertex->textureUV.x * mTransforms[MATRIX_TEXTURE].m[0][0] +
                            vertex->textureUV.y * mTransforms[MATRIX_TEXTURE].m[1][0] +
                            mTransforms[MATRIX_TEXTURE].m[2][0];
                        v = vertex->textureUV.x * mTransforms[MATRIX_TEXTURE].m[0][1] +
                            vertex->textureUV.y * mTransforms[MATRIX_TEXTURE].m[1][1] +
                            mTransforms[MATRIX_TEXTURE].m[2][1];
                    }
                    out[i].u = texture && texture->pixels ? u : 0.0f;
                    out[i].v = texture && texture->pixels ? v : 0.0f;
                }
            }
            else
            {
                auto *out = static_cast<GuVertexTexColor *>(packed);
                for (int i = 0; i < vertexCount; ++i)
                {
                    const unsigned char *raw = source + i * vertexStride;
                    float u;
                    float v;
                    if (screenSpace)
                    {
                        const auto *vertex =
                            reinterpret_cast<const VertexTex1DiffuseXyzrhw *>(raw);
                        out[i].x = vertex->pos.x;
                        out[i].y = vertex->pos.y;
                        out[i].z = vertex->pos.z;
                        u = vertex->textureUV.x;
                        v = vertex->textureUV.y;
                    }
                    else
                    {
                        const auto *vertex = reinterpret_cast<const VertexTex1DiffuseXyz *>(raw);
                        out[i].x = vertex->position.x;
                        out[i].y = vertex->position.y;
                        out[i].z = vertex->position.z;
                        u = vertex->textureUV.x * mTransforms[MATRIX_TEXTURE].m[0][0] +
                            vertex->textureUV.y * mTransforms[MATRIX_TEXTURE].m[1][0] +
                            mTransforms[MATRIX_TEXTURE].m[2][0];
                        v = vertex->textureUV.x * mTransforms[MATRIX_TEXTURE].m[0][1] +
                            vertex->textureUV.y * mTransforms[MATRIX_TEXTURE].m[1][1] +
                            mTransforms[MATRIX_TEXTURE].m[2][1];
                    }
                    out[i].u = texture && texture->pixels ? u : 0.0f;
                    out[i].v = texture && texture->pixels ? v : 0.0f;
                    out[i].color = colorAt(i);
                }
            }
            // D3D's XYZRHW/screen-space path does not participate in vertex
            // fog.  TH07's title JPEG reaches the backbuffer through exactly
            // this path; leaving GU_FOG enabled replaces it with the default
            // grey fog colour.  The PS3 native backend makes the same split:
            // CopyRects/native_blit has fog=false and only clip-space draws
            // can select its fog fragment program.
            if (screenSpace)
            {
                sceGuDisable(GU_FOG);
            }
            const int vertexType = GU_TEXTURE_32BITF |
                                   (constantColorBatch ? 0 : GU_COLOR_8888) |
                                   GU_VERTEX_32BITF | GU_TRANSFORM_3D |
                                   (indexedQuads ? GU_INDEX_16BIT : 0);
            sceGuDrawArray(collapseSprites ? GU_SPRITES : primitive, vertexType,
                           indexedQuads ? indexCount : submittedVertexCount,
                           indices, packed);
#if defined(TH07_PSP_PERF_DIAG)
            ++mDraws;
            mInputVertices += static_cast<unsigned int>(vertexCount);
            mVertices += static_cast<unsigned int>(submittedVertexCount);
#endif
            if (screenSpace && mFogEnabled)
            {
                sceGuEnable(GU_FOG);
            }
        }
        else
        {
            if (constantColorBatch)
            {
                auto *out = static_cast<GuVertexPosition *>(packed);
                for (int i = 0; i < vertexCount; ++i)
                {
                    const auto *vertex = reinterpret_cast<const VertexDiffuseXyzrhw *>(
                        source + i * vertexStride);
                    out[i].x = vertex->pos.x;
                    out[i].y = vertex->pos.y;
                    out[i].z = vertex->pos.z;
                }
            }
            else
            {
                auto *out = static_cast<GuVertexColor *>(packed);
                for (int i = 0; i < vertexCount; ++i)
                {
                    const auto *vertex = reinterpret_cast<const VertexDiffuseXyzrhw *>(
                        source + i * vertexStride);
                    out[i].x = vertex->pos.x;
                    out[i].y = vertex->pos.y;
                    out[i].z = vertex->pos.z;
                    out[i].color = colorAt(i);
                }
            }
            if (screenSpace)
            {
                sceGuDisable(GU_FOG);
            }
            sceGuDrawArray(primitive,
                           (constantColorBatch ? 0 : GU_COLOR_8888) | GU_VERTEX_32BITF |
                               GU_TRANSFORM_3D | (indexedQuads ? GU_INDEX_16BIT : 0),
                           indexedQuads ? indexCount : vertexCount, indices, packed);
#if defined(TH07_PSP_PERF_DIAG)
            ++mDraws;
            mInputVertices += static_cast<unsigned int>(vertexCount);
            mVertices += static_cast<unsigned int>(vertexCount);
#endif
            if (screenSpace && mFogEnabled)
            {
                sceGuEnable(GU_FOG);
            }
        }
    }

    void DrawSpriteQuads(const Th07PspSpriteVertex *vertices, unsigned int spriteCount)
    {
        if (!vertices || spriteCount == 0)
        {
            return;
        }
        if (!EnsureListSpace(0))
        {
            mError = true;
            return;
        }

        ApplyMatrices(true);
        ApplyTexture(true, true);

        const auto canCollapse = [](const Th07PspSpriteVertex *q) {
            return q[0].x == q[2].x && q[1].x == q[3].x &&
                   q[0].y == q[1].y && q[2].y == q[3].y &&
                   q[0].z == q[1].z && q[0].z == q[2].z && q[0].z == q[3].z &&
                   q[0].x <= q[3].x && q[0].y <= q[3].y &&
                   q[0].u == q[2].u && q[1].u == q[3].u &&
                   q[0].v == q[1].v && q[2].v == q[3].v;
        };

        unsigned int sprite = 0;
        while (sprite < spriteCount)
        {
            const bool collapsed = canCollapse(vertices + sprite * 4u);
            unsigned int runEnd = sprite + 1u;
            while (runEnd < spriteCount &&
                   canCollapse(vertices + runEnd * 4u) == collapsed)
            {
                ++runEnd;
            }

            unsigned int remaining = runEnd - sprite;
            const Th07PspSpriteVertex *batch = vertices + sprite * 4u;

            while (remaining)
            {
                const unsigned int batchSprites =
                    std::min(remaining, kCachedQuadIndexCount);
                if (collapsed)
                {
                    unsigned char *arenaBase = DeferredSpriteArenaBase();
                    const auto *engineWrite = g_AnmManager
                                                  ? reinterpret_cast<const unsigned char *>(
                                                        g_AnmManager->vertexBufferCurPtr)
                                                  : nullptr;
                    const unsigned int packedBytes =
                        batchSprites * 2u * sizeof(GuVertexTexColor);
                    const bool arenaSafe = arenaBase && engineWrite && engineWrite < arenaBase;
                    if (arenaSafe &&
                        mDeferredSpriteArenaUsed + packedBytes <= kDeferredSpriteArenaBytes)
                    {
                        auto *packed = reinterpret_cast<GuVertexTexColor *>(
                            arenaBase + mDeferredSpriteArenaUsed);
                        if (mDeferredSpriteVertices &&
                            mDeferredSpriteVertices + mDeferredSpriteVertexCount != packed)
                        {
                            FlushDeferredSpriteDraw();
                        }
                        // TH06's delayed path publishes render state when a
                        // group begins, then folds later engine flushes into
                        // the same GE primitive.  Every state mutation below
                        // flushes this group before it can change that state.
                        if (!mDeferredSpriteVertices)
                        {
                            if (!EnsureListSpace(0))
                            {
                                mError = true;
                                return;
                            }
                            ApplyMatrices(true);
                            ApplyTexture(true, true);
                            sceGuDisable(GU_FOG);
                            mDeferredSpriteVertices =
                                reinterpret_cast<GuVertexTexColor *>(
                                    arenaBase + mDeferredSpriteArenaUsed);
                        }

                        for (unsigned int i = 0; i < batchSprites; ++i)
                        {
                            const Th07PspSpriteVertex *q = batch + i * 4u;
                            const Th07PspSpriteVertex *corners[2] = {&q[0], &q[3]};
                            for (unsigned int corner = 0; corner < 2u; ++corner)
                            {
                                GuVertexTexColor &out = packed[i * 2u + corner];
                                out.u = corners[corner]->u;
                                out.v = corners[corner]->v;
                                out.color = corners[corner]->color;
                                out.x = corners[corner]->x;
                                out.y = corners[corner]->y;
                                out.z = corners[corner]->z;
                            }
                        }
                        mDeferredSpriteArenaUsed += packedBytes;
                        mDeferredSpriteVertexCount += batchSprites * 2u;
                        mDeferredSpriteInputVertexCount += batchSprites * 4u;
                    }
                    else
                    {
                        // The engine grew into the reserved tail or this
                        // frame exhausted it.  Preserve correctness with the
                        // previous live-list packing path for this batch.
                        FlushDeferredSpriteDraw();
                        if (!EnsureListSpace(packedBytes))
                        {
                            mError = true;
                            return;
                        }
                        auto *packed = static_cast<GuVertexTexColor *>(
                            sceGuGetMemory(static_cast<int>(packedBytes)));
                        if (!packed)
                        {
                            mError = true;
                            return;
                        }
                        for (unsigned int i = 0; i < batchSprites; ++i)
                        {
                            const Th07PspSpriteVertex *q = batch + i * 4u;
                            const Th07PspSpriteVertex *corners[2] = {&q[0], &q[3]};
                            for (unsigned int corner = 0; corner < 2u; ++corner)
                            {
                                GuVertexTexColor &out = packed[i * 2u + corner];
                                out.u = corners[corner]->u;
                                out.v = corners[corner]->v;
                                out.color = corners[corner]->color;
                                out.x = corners[corner]->x;
                                out.y = corners[corner]->y;
                                out.z = corners[corner]->z;
                            }
                        }
                        sceGuDisable(GU_FOG);
                        sceGuDrawArray(GU_SPRITES,
                                       GU_TEXTURE_32BITF | GU_COLOR_8888 |
                                           GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                                       static_cast<int>(batchSprites * 2u), nullptr, packed);
#if defined(TH07_PSP_PERF_DIAG)
                        ++mDraws;
                        mInputVertices += batchSprites * 4u;
                        mVertices += batchSprites * 2u;
                        ++mCachedQuadIndexBatches;
#endif
                        if (mFogEnabled)
                        {
                            sceGuEnable(GU_FOG);
                        }
                    }
                }
                else
                {
                    // General rotated or mirrored quads retain the shared
                    // indexed path and delimit delayed axis-aligned groups.
                    FlushDeferredSpriteDraw();
                    sceKernelDcacheWritebackRange(
                        const_cast<Th07PspSpriteVertex *>(batch),
                        batchSprites * sizeof(*vertices) * 4u);
                    sceGuDisable(GU_FOG);
                    sceGuDrawArray(GU_TRIANGLES,
                                   GU_TEXTURE_32BITF | GU_COLOR_8888 |
                                       GU_VERTEX_32BITF | GU_TRANSFORM_3D |
                                       GU_INDEX_16BIT,
                                   static_cast<int>(batchSprites * 6u),
                                   gQuadIndices, batch);
#if defined(TH07_PSP_PERF_DIAG)
                    ++mDraws;
                    mInputVertices += batchSprites * 4u;
                    mVertices += batchSprites * 4u;
#endif
                    if (mFogEnabled)
                    {
                        sceGuEnable(GU_FOG);
                    }
                }
                batch += batchSprites * 4u;
                remaining -= batchSprites;
            }
            sprite = runEnd;
        }
    }

    void DrawSpritePairs(const Th07PspSpriteVertex *vertices, unsigned int spriteCount)
    {
        if (!vertices || spriteCount == 0)
        {
            return;
        }
        if (!EnsureListSpace(0))
        {
            mError = true;
            return;
        }

        ApplyMatrices(true);
        ApplyTexture(true, true);

        auto *pairs = reinterpret_cast<GuVertexTexColor *>(
            const_cast<Th07PspSpriteVertex *>(vertices));
        if (mDeferredSpriteVertices &&
            mDeferredSpriteVertices + mDeferredSpriteVertexCount != pairs)
        {
            FlushDeferredSpriteDraw();
        }
        if (!mDeferredSpriteVertices)
        {
            // AnmManager owns this per-frame memory until SwapBuffers has
            // synchronized the GE.  Publish it directly and let consecutive
            // compatible bullet/effect flushes extend one GU_SPRITES command.
            ApplyMatrices(true);
            ApplyTexture(true, true);
            sceGuDisable(GU_FOG);
            mDeferredSpriteVertices = pairs;
        }
        mDeferredSpriteVertexCount += spriteCount * 2u;
        mDeferredSpriteInputVertexCount += spriteCount * 2u;
    }

    void SwapBuffers() override
    {
        if (!mListOpen)
        {
            return;
        }
        FlushDeferredSpriteDraw();
#if defined(TH07_PSP_PERF_DIAG)
        const unsigned long long cpuEndUs = sceKernelGetSystemTimeWide();
#endif
        const int listBytes = sceGuFinish();
        mListOpen = false;
#if defined(TH07_PSP_PERF_DIAG)
        if (listBytes > 0)
        {
            mMaxListBytes = std::max(mMaxListBytes, static_cast<unsigned int>(listBytes));
        }
        const unsigned long long geStartUs = sceKernelGetSystemTimeWide();
#endif
        sceGuSync(0, 0);
#if defined(TH07_PSP_PERF_DIAG)
        const unsigned long long geEndUs = sceKernelGetSystemTimeWide();
#endif
        sceDisplayWaitVblankStart();
#if defined(TH07_PSP_PERF_DIAG)
        const unsigned long long vblankEndUs = sceKernelGetSystemTimeWide();
#endif
        sceGuSwapBuffers();
        mCurrentDrawBuffer ^= 1;
#if defined(TH07_PSP_PERF_DIAG)
        AccumulateAndReportPerf(cpuEndUs, geStartUs, geEndUs, vblankEndUs);
        mListsThisFrame = 0;
        mFrameBlockingGeUs = 0;
#endif
        StartList();
        ClearPillarboxes();
#if defined(TH07_PSP_PERF_DIAG)
        mFrameStartUs = sceKernelGetSystemTimeWide();
#endif
    }

  private:
    GuTexture mTextures[kMaxTextures];
    FreeTextureBlock mFreeTextureBlocks[kMaxTextures];
    SurfaceCache mSurfaceCache;
    ZunMatrix mTransforms[4];
    ZunViewport mViewport{};
    ZunColor mTextureFactor{0xffffffffu};
    ZunColor mFogColor{0xffa0a0a0u};
    ZunColor mClearColor{0xff000000u};
    unsigned int mBoundTexture = 0;
    float mFogNear = 1000.0f;
    float mFogFar = 5000.0f;
    float mClearDepth = 1.0f;
    TextureArg mTextureArg = TEX_ARG_DIFFUSE;
    ColorOp mColorOpRgb = COLOR_OP_MODULATE;
    ColorOp mColorOpAlpha = COLOR_OP_MODULATE;
    int mAppliedMatrixMode = -1;
    int mCurrentDrawBuffer = 0;
    unsigned int mMatrixDirtyMask = 0x7u;
    bool mDepthWrite = true;
    bool mDepthFuncKnown = false;
    DepthFunc mDepthFunc = DEPTH_FUNC_LEQUAL;
    bool mBlendEnabled = true;
    bool mAlphaTestEnabled = true;
    bool mDepthTestEnabled = false;
    bool mFogEnabled = false;
    bool mFogParamsKnown = false;
    bool mBlendModeKnown = false;
    BlendMode mBlendSrc = BLEND_ALPHA;
    BlendMode mBlendDst = BLEND_NONE;
    bool mAlphaRefKnown = true;
    u8 mAlphaRef = 4;
    bool mTextureEnableKnown = false;
    bool mTextureEnabled = false;
    unsigned int mAppliedTexture = ~0u;
    unsigned int mTextureCreateTraces = 0;
    bool mAppliedScreenSpace = false;
    bool mAppliedColorOpKnown = false;
    ColorOp mAppliedColorOpRgb = COLOR_OP_MODULATE;
    bool mPrimitiveColorKnown = false;
    unsigned int mAppliedPrimitiveColor = 0xffffffffu;
    bool mListOpen = false;
    bool mInitialized = false;
    bool mError = false;
    bool mTextUploadBatchActive = false;
    GuVertexTexColor *mDeferredSpriteVertices = nullptr;
    unsigned int mDeferredSpriteVertexCount = 0;
    unsigned int mDeferredSpriteInputVertexCount = 0;
    unsigned int mDeferredSpriteArenaUsed = 0;
#if defined(TH07_PSP_PERF_DIAG)
    unsigned long long mFrameStartUs = 0;
    unsigned long long mPerfStartUs = 0;
    unsigned long long mPerfCpuUs = 0;
    unsigned long long mPerfGeUs = 0;
    unsigned long long mPerfVblankUs = 0;
    unsigned long long mPerfMaxFrameUs = 0;
    unsigned long long mFrameBlockingGeUs = 0;
    unsigned long long mTextureUploadBytes = 0;
    unsigned int mPerfFrames = 0;
    unsigned int mDraws = 0;
    unsigned int mInputVertices = 0;
    unsigned int mVertices = 0;
    unsigned int mPerfStartDraws = 0;
    unsigned int mPerfStartInputVertices = 0;
    unsigned int mPerfStartVertices = 0;
    unsigned int mListsThisFrame = 0;
    unsigned int mPerfLists = 0;
    unsigned int mMaxListBytes = 0;
    unsigned int mSurfaceCacheHits = 0;
    unsigned int mSurfaceCacheMisses = 0;
    unsigned int mPerfMaxBullets = 0;
    unsigned int mPerfMaxEffects = 0;
    unsigned int mMatrixSubmissions = 0;
    unsigned int mCachedQuadIndexBatches = 0;
#endif

    void RecyclePixels(void *&pixels, unsigned int &allocationBytes)
    {
        if (!pixels)
        {
            allocationBytes = 0;
            return;
        }
        for (FreeTextureBlock &block : mFreeTextureBlocks)
        {
            if (!block.pixels)
            {
                block.pixels = pixels;
                block.bytes = allocationBytes;
                pixels = nullptr;
                allocationBytes = 0;
                return;
            }
        }
        std::free(pixels);
        pixels = nullptr;
        allocationBytes = 0;
    }

    void RecycleTexturePixels(GuTexture &texture)
    {
        if (!texture.pixels || texture.borrowedSurfaceCache)
        {
            return;
        }
        RecyclePixels(texture.pixels, texture.allocationBytes);
        texture.bytes = 0;
    }

    void *AcquireTexturePixels(unsigned int bytes, unsigned int *allocationBytes)
    {
        int best = -1;
        for (unsigned int i = 0; i < kMaxTextures; ++i)
        {
            if (!mFreeTextureBlocks[i].pixels || mFreeTextureBlocks[i].bytes < bytes)
            {
                continue;
            }
            if (best < 0 || mFreeTextureBlocks[i].bytes < mFreeTextureBlocks[best].bytes)
            {
                best = static_cast<int>(i);
            }
        }
        if (best >= 0)
        {
            void *pixels = mFreeTextureBlocks[best].pixels;
            *allocationBytes = mFreeTextureBlocks[best].bytes;
            mFreeTextureBlocks[best] = FreeTextureBlock{};
            return pixels;
        }

        void *pixels = memalign(16, bytes);
        *allocationBytes = pixels ? bytes : 0;
        return pixels;
    }

    unsigned char *DeferredSpriteArenaBase() const
    {
        if (!g_AnmManager)
        {
            return nullptr;
        }
        auto *end = reinterpret_cast<unsigned char *>(
            g_AnmManager->spriteVertexBuffer +
            sizeof(g_AnmManager->spriteVertexBuffer) /
                sizeof(g_AnmManager->spriteVertexBuffer[0]));
        return end - kDeferredSpriteArenaBytes;
    }

    void FlushDeferredSpriteDraw()
    {
        if (!mDeferredSpriteVertices || mDeferredSpriteVertexCount == 0)
        {
            return;
        }

        const unsigned int bytes =
            mDeferredSpriteVertexCount * sizeof(GuVertexTexColor);
        sceKernelDcacheWritebackRange(mDeferredSpriteVertices, bytes);
        sceGuDrawArray(GU_SPRITES,
                       GU_TEXTURE_32BITF | GU_COLOR_8888 |
                           GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                       static_cast<int>(mDeferredSpriteVertexCount), nullptr,
                       mDeferredSpriteVertices);
#if defined(TH07_PSP_PERF_DIAG)
        ++mDraws;
        mInputVertices += mDeferredSpriteInputVertexCount;
        mVertices += mDeferredSpriteVertexCount;
        ++mCachedQuadIndexBatches;
#endif
        mDeferredSpriteVertices = nullptr;
        mDeferredSpriteVertexCount = 0;
        mDeferredSpriteInputVertexCount = 0;
        if (mFogEnabled)
        {
            sceGuEnable(GU_FOG);
        }
    }

    void StartList()
    {
        mDeferredSpriteVertices = nullptr;
        mDeferredSpriteVertexCount = 0;
        mDeferredSpriteInputVertexCount = 0;
        mDeferredSpriteArenaUsed = 0;
        sceGuStart(GU_DIRECT, gCommandList);
        mListOpen = true;
#if defined(TH07_PSP_PERF_DIAG)
        ++mListsThisFrame;
#endif
        mAppliedMatrixMode = -1;
        mMatrixDirtyMask |= 0x7u;
    }

    void SubmitAndRestart()
    {
        if (!mListOpen)
        {
            return;
        }
        FlushDeferredSpriteDraw();
        const int listBytes = sceGuFinish();
        mListOpen = false;
#if defined(TH07_PSP_PERF_DIAG)
        if (listBytes > 0)
        {
            mMaxListBytes = std::max(mMaxListBytes, static_cast<unsigned int>(listBytes));
        }
        const unsigned long long geStartUs = sceKernelGetSystemTimeWide();
#endif
        sceGuSync(0, 0);
#if defined(TH07_PSP_PERF_DIAG)
        mFrameBlockingGeUs += sceKernelGetSystemTimeWide() - geStartUs;
#endif
        StartList();
    }

#if defined(TH07_PSP_PERF_DIAG)
    void AccumulateAndReportPerf(unsigned long long cpuEndUs, unsigned long long geStartUs,
                                 unsigned long long geEndUs,
                                 unsigned long long vblankEndUs)
    {
        if (mPerfStartUs == 0)
        {
            mPerfStartUs = mFrameStartUs ? mFrameStartUs : cpuEndUs;
            mPerfStartDraws = mDraws;
            mPerfStartInputVertices = mInputVertices;
            mPerfStartVertices = mVertices;
        }
        if (mFrameStartUs)
        {
            mPerfCpuUs += cpuEndUs - mFrameStartUs;
            mPerfMaxFrameUs = std::max(mPerfMaxFrameUs, vblankEndUs - mFrameStartUs);
        }
        mPerfGeUs += mFrameBlockingGeUs + (geEndUs - geStartUs);
        mPerfVblankUs += vblankEndUs - geEndUs;
        mPerfLists += mListsThisFrame;
        mPerfMaxBullets = std::max(mPerfMaxBullets,
                                   static_cast<unsigned int>(g_BulletManager.bulletCount));
        mPerfMaxEffects = std::max(mPerfMaxEffects,
                                   static_cast<unsigned int>(g_EffectManager.activeEffectsCount));
        ++mPerfFrames;
        if (mPerfFrames < kPerfWindowFrames)
        {
            return;
        }

        const unsigned long long elapsedUs = vblankEndUs - mPerfStartUs;
        const unsigned int fps10 = elapsedUs
                                       ? static_cast<unsigned int>(mPerfFrames * 10000000ull /
                                                                   elapsedUs)
                                       : 0;
        const unsigned int cpu10 = static_cast<unsigned int>(mPerfCpuUs / mPerfFrames / 100u);
        const unsigned int ge10 = static_cast<unsigned int>(mPerfGeUs / mPerfFrames / 100u);
        const unsigned int vb10 = static_cast<unsigned int>(mPerfVblankUs / mPerfFrames / 100u);
        const unsigned int max10 = static_cast<unsigned int>(mPerfMaxFrameUs / 100u);
        const unsigned int draws = (mDraws - mPerfStartDraws) / mPerfFrames;
        const unsigned int inputVertices =
            (mInputVertices - mPerfStartInputVertices) / mPerfFrames;
        const unsigned int vertices = (mVertices - mPerfStartVertices) / mPerfFrames;
        const unsigned int lists10 = mPerfLists * 10u / mPerfFrames;
        const unsigned int calc10 =
            static_cast<unsigned int>(gPerfCalcChainUs / mPerfFrames / 100u);
        const unsigned int draw10 =
            static_cast<unsigned int>(gPerfDrawChainUs / mPerfFrames / 100u);
        const unsigned int stage10 =
            static_cast<unsigned int>(gPerfStageDrawUs / mPerfFrames / 100u);
        const auto calcJob10 = [this](unsigned int priority) {
            return static_cast<unsigned int>(gPerfCalcJobUs[priority] / mPerfFrames / 100u);
        };
        const unsigned int menu10 = calcJob10(3);
        const unsigned int stageUpdate10 = calcJob10(7);
        const unsigned int player10 = calcJob10(8);
        const unsigned int enemy10 = calcJob10(10);
        const unsigned int effect10 = calcJob10(11);
        const unsigned int bulletItem10 = calcJob10(12);
        const auto drawJob10 = [this](unsigned int priority) {
            return static_cast<unsigned int>(gPerfDrawJobUs[priority] / mPerfFrames / 100u);
        };
        const unsigned int matrices10 = mMatrixSubmissions * 10u / mPerfFrames;
        const unsigned int cachedQuads10 = mCachedQuadIndexBatches * 10u / mPerfFrames;
        unsigned int meJobs = 0;
        unsigned int meFallbacks = 0;
        unsigned int meTimeouts = 0;
        unsigned int meMaxWaitUs = 0;
        th07_psp_me_audio_diag_window(&meJobs, &meFallbacks, &meTimeouts,
                                      &meMaxWaitUs);
        const struct mallinfo heap = mallinfo();
        char message[384];
        std::snprintf(message, sizeof(message),
                      "PERF S%d ST%d B%d/%u E%d/%u %u.%uFPS CPU%u.%u GE%u.%u VB%u.%u MAX%u.%u "
                      "C%u.%u R%u.%u BG%u.%u D%u VI%u V%u L%u.%u M%u.%u Q%u.%u "
                      "J3%u.%u J7%u.%u J8%u.%u J10%u.%u J11%u.%u J12%u.%u "
                      "LK%u UP%lluK TC%u/%u HF%uK "
                      "ME%u SC%u TO%u MW%u",
                      g_Supervisor.curState, g_GameManager.currentStage,
                      g_BulletManager.bulletCount, mPerfMaxBullets,
                      g_EffectManager.activeEffectsCount, mPerfMaxEffects,
                      fps10 / 10, fps10 % 10,
                      cpu10 / 10, cpu10 % 10, ge10 / 10, ge10 % 10, vb10 / 10, vb10 % 10,
                      max10 / 10, max10 % 10, calc10 / 10, calc10 % 10,
                      draw10 / 10, draw10 % 10, stage10 / 10, stage10 % 10,
                      draws, inputVertices, vertices, lists10 / 10, lists10 % 10,
                      matrices10 / 10, matrices10 % 10,
                      cachedQuads10 / 10, cachedQuads10 % 10,
                      menu10 / 10, menu10 % 10, stageUpdate10 / 10, stageUpdate10 % 10,
                      player10 / 10, player10 % 10, enemy10 / 10, enemy10 % 10,
                      effect10 / 10, effect10 % 10, bulletItem10 / 10,
                      bulletItem10 % 10,
                      mMaxListBytes / 1024u, mTextureUploadBytes / 1024u, mSurfaceCacheHits,
                      mSurfaceCacheMisses, static_cast<unsigned int>(heap.fordblks) / 1024u,
                      meJobs, meFallbacks, meTimeouts, meMaxWaitUs);
        th07_psp_boot_note(message);
        char drawMessage[192];
        std::snprintf(drawMessage, sizeof(drawMessage),
                      "PERF DRAW ST%u.%u/%u.%u EN%u.%u/%u.%u PL%u.%u/%u.%u "
                      "FX%u.%u BU%u.%u GUI%u.%u",
                      drawJob10(3) / 10, drawJob10(3) % 10,
                      drawJob10(4) / 10, drawJob10(4) % 10,
                      drawJob10(5) / 10, drawJob10(5) % 10,
                      drawJob10(7) / 10, drawJob10(7) % 10,
                      drawJob10(6) / 10, drawJob10(6) % 10,
                      drawJob10(8) / 10, drawJob10(8) % 10,
                      drawJob10(9) / 10, drawJob10(9) % 10,
                      drawJob10(10) / 10, drawJob10(10) % 10,
                      drawJob10(12) / 10, drawJob10(12) % 10);
        th07_psp_boot_note(drawMessage);

        mPerfStartUs = 0;
        mPerfCpuUs = 0;
        mPerfGeUs = 0;
        mPerfVblankUs = 0;
        mPerfMaxFrameUs = 0;
        mPerfFrames = 0;
        mPerfLists = 0;
        mMaxListBytes = 0;
        mTextureUploadBytes = 0;
        mSurfaceCacheHits = 0;
        mSurfaceCacheMisses = 0;
        mPerfMaxBullets = 0;
        mPerfMaxEffects = 0;
        mMatrixSubmissions = 0;
        mCachedQuadIndexBatches = 0;
        gPerfCalcChainUs = 0;
        gPerfDrawChainUs = 0;
        gPerfStageDrawUs = 0;
        for (unsigned long long &jobUs : gPerfCalcJobUs)
        {
            jobUs = 0;
        }
        for (unsigned long long &jobUs : gPerfDrawJobUs)
        {
            jobUs = 0;
        }
    }
#endif

    bool EnsureListSpace(unsigned int vertexBytes)
    {
        if (vertexBytes + kListReserve >= kListBytes)
        {
            return false;
        }
        const unsigned int used = static_cast<unsigned int>(std::max(sceGuCheckList(), 0));
        if (used + ((vertexBytes + 3u) & ~3u) + kListReserve >= kListBytes)
        {
            SubmitAndRestart();
        }
        return true;
    }

    void ApplyViewport()
    {
        const int logicalX = static_cast<int>(mViewport.x);
        const int logicalY = static_cast<int>(mViewport.y);
        const int logicalWidth = std::max(1, static_cast<int>(mViewport.width));
        const int logicalHeight = std::max(1, static_cast<int>(mViewport.height));
        const int contentWidth = g_Supervisor.cfg.windowed ? kFitWidth : kScreenWidth;
        const int contentLeft = g_Supervisor.cfg.windowed ? kFitLeft : 0;
        int x = contentLeft + logicalX * contentWidth / kLogicalWidth;
        int right = contentLeft +
                    (logicalX + logicalWidth) * contentWidth / kLogicalWidth;
        int top = kScreenHeight -
                  (logicalY + logicalHeight) * kScreenHeight / kLogicalHeight;
        int bottom = kScreenHeight - logicalY * kScreenHeight / kLogicalHeight;
        x = std::max(0, std::min(kScreenWidth - 1, x));
        right = std::max(x + 1, std::min(kScreenWidth, right));
        top = std::max(0, std::min(kScreenHeight - 1, top));
        bottom = std::max(top + 1, std::min(kScreenHeight, bottom));
        const int width = right - x;
        const int height = bottom - top;
        sceGuOffset(2048 - (x + width / 2), 2048 - (top + height / 2));
        sceGuViewport(2048, 2048, width, height);
        sceGuScissor(x, top, right, bottom);
        sceGuEnable(GU_SCISSOR_TEST);
    }

    void ClearPillarboxes()
    {
        if (!g_Supervisor.cfg.windowed)
        {
            return;
        }
        sceGuOffset(2048 - kScreenWidth / 2, 2048 - kScreenHeight / 2);
        sceGuViewport(2048, 2048, kScreenWidth, kScreenHeight);
        sceGuClearColor(0xff000000u);
        sceGuEnable(GU_SCISSOR_TEST);
        sceGuScissor(0, 0, kFitLeft, kScreenHeight);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        sceGuScissor(kFitLeft + kFitWidth, 0, kScreenWidth, kScreenHeight);
        sceGuClear(GU_COLOR_BUFFER_BIT);
        sceGuClearColor(ToGuColor(mClearColor));
        ApplyViewport();
    }

    void ApplyMatrices(bool screenSpace)
    {
        const int mode = screenSpace ? 1 : 0;
        if (screenSpace && mAppliedMatrixMode == mode)
        {
            // Pending 3D transform changes do not affect XYZRHW vertices.
            return;
        }
        if (!screenSpace && mAppliedMatrixMode == mode && mMatrixDirtyMask == 0)
        {
            return;
        }
        if (screenSpace)
        {
            sceGuSetMatrix(GU_MODEL, &kIdentityMatrix);
            sceGuSetMatrix(GU_VIEW, &kIdentityMatrix);
#if defined(TH07_PSP_PERF_DIAG)
            mMatrixSubmissions += 2;
#endif

            const float left = static_cast<float>(mViewport.x);
            const float right = static_cast<float>(mViewport.x + mViewport.width);
            const float bottom = static_cast<float>(mViewport.y + mViewport.height);
            const float top = static_cast<float>(mViewport.y);
            constexpr float nearPlane = -10000.0f;
            constexpr float farPlane = 10000.0f;
            const float dx = right - left;
            const float dy = top - bottom;
            const float dz = farPlane - nearPlane;
            ScePspFMatrix4 projection;
            projection.x.x = 2.0f / dx;
            projection.x.y = 0.0f;
            projection.x.z = 0.0f;
            projection.x.w = 0.0f;
            projection.y.x = 0.0f;
            projection.w.x = -(right + left) / dx;
            projection.y.y = 2.0f / dy;
            projection.y.z = 0.0f;
            projection.y.w = 0.0f;
            projection.z.x = 0.0f;
            projection.z.y = 0.0f;
            projection.w.y = -(top + bottom) / dy;
            projection.z.z = -2.0f / dz;
            projection.z.w = 0.0f;
            projection.w.x = -(right + left) / dx;
            projection.w.y = -(top + bottom) / dy;
            projection.w.z = -(farPlane + nearPlane) / dz;
            projection.w.w = 1.0f;
            sceGuSetMatrix(GU_PROJECTION, &projection);
#if defined(TH07_PSP_PERF_DIAG)
            ++mMatrixSubmissions;
#endif
        }
        else
        {
            if (mAppliedMatrixMode != mode)
            {
                // Screen-space rendering installed identity model/view and an
                // orthographic projection, so all 3D matrices must be restored.
                mMatrixDirtyMask |= 0x7u;
            }
            static const int modes[3] = {GU_MODEL, GU_VIEW, GU_PROJECTION};
            for (int i = 0; i < 3; ++i)
            {
                if ((mMatrixDirtyMask & (1u << i)) != 0)
                {
                    const ScePspFMatrix4 matrix = ToGuMatrix(mTransforms[i]);
                    sceGuSetMatrix(modes[i], &matrix);
#if defined(TH07_PSP_PERF_DIAG)
                    ++mMatrixSubmissions;
#endif
                }
            }
            mMatrixDirtyMask = 0;
        }
        mAppliedMatrixMode = mode;
    }

    void ApplyTexture(bool textured, bool screenSpace)
    {
        CheckBoundTexture("draw");
        const bool enabled = textured && mBoundTexture != 0 &&
                             mTextures[mBoundTexture].pixels &&
                             !(mColorOpRgb == COLOR_OP_DISABLE &&
                               mColorOpAlpha == COLOR_OP_DISABLE);
        if (!enabled)
        {
            if (!mTextureEnableKnown || mTextureEnabled)
            {
                sceGuDisable(GU_TEXTURE_2D);
                mTextureEnableKnown = true;
                mTextureEnabled = false;
            }
            return;
        }
        const GuTexture &texture = mTextures[mBoundTexture];
        if (!mTextureEnableKnown || !mTextureEnabled)
        {
            sceGuEnable(GU_TEXTURE_2D);
            mTextureEnableKnown = true;
            mTextureEnabled = true;
        }
        if (mAppliedTexture != mBoundTexture || mAppliedScreenSpace != screenSpace)
        {
            sceGuTexMode(texture.psm, 0, 0, texture.swizzled ? GU_TRUE : GU_FALSE);
            sceGuTexImage(0, texture.storageWidth, texture.storageHeight, texture.storageWidth,
                          texture.pixels);
            sceGuTexScale(texture.sampleScaleX, texture.sampleScaleY);
            sceGuTexOffset(0.0f, 0.0f);
            // Match the final TH06 PSP backend and TH07's portable GLES
            // renderer.  The atlas UVs are inset to texel centers in
            // AnmManager::LoadSprite, which prevents linear filtering from
            // leaking adjacent HUD cells while retaining antialiased text
            // during the 640x480 -> PSP viewport shrink.
            sceGuTexFilter(GU_LINEAR, GU_LINEAR);
            mAppliedTexture = mBoundTexture;
            mAppliedScreenSpace = screenSpace;
        }
        int function = GU_TFX_MODULATE;
        if (mColorOpRgb == COLOR_OP_ADD)
        {
            function = GU_TFX_ADD;
        }
        else if (mColorOpRgb == COLOR_OP_REPLACE)
        {
            function = GU_TFX_REPLACE;
        }
        if (!mAppliedColorOpKnown || mAppliedColorOpRgb != mColorOpRgb)
        {
            sceGuTexFunc(function, GU_TCC_RGBA);
            mAppliedColorOpRgb = mColorOpRgb;
            mAppliedColorOpKnown = true;
        }
    }

    void CheckBoundTexture(const char *where)
    {
        if (mBoundTexture < kMaxTextures)
        {
            return;
        }

        const unsigned int corrupted = mBoundTexture;
        const unsigned int expected =
            g_AnmManager && g_AnmManager->currentTexture.id < kMaxTextures
                ? g_AnmManager->currentTexture.id
                : 0;
        char message[96];
        std::snprintf(message, sizeof(message), "texture bound corrupt %s %08x -> %u", where,
                      corrupted, expected);
        th07_psp_boot_note(message);
        mBoundTexture = expected;
        mError = true;
    }

    unsigned int SelectVertexColor(ZunColor diffuse) const
    {
        if (mTextureArg == TEX_ARG_TFACTOR)
        {
            return ToGuColor(mTextureFactor);
        }
        if (mTextureArg == TEX_ARG_TEXTURE)
        {
            return 0xffffffffu;
        }
        return ToGuColor(diffuse);
    }

    static void ReadSourcePixel(PixelFormat fmt, PixelDataType type, const void *data,
                                unsigned int index, unsigned int &r, unsigned int &g,
                                unsigned int &b, unsigned int &a)
    {
        const auto *bytes = static_cast<const unsigned char *>(data);
        const auto *words = static_cast<const unsigned short *>(data);
        r = g = b = 0;
        a = 255;
        if (type == PIXEL_UNSIGNED_BYTE)
        {
            const unsigned int components = fmt == PIXEL_RGB ? 3u : 4u;
            r = bytes[index * components + 0];
            g = bytes[index * components + 1];
            b = bytes[index * components + 2];
            if (components == 4)
            {
                a = bytes[index * components + 3];
            }
        }
        else if (type == PIXEL_UNSIGNED_SHORT_5_6_5)
        {
            const unsigned int value = words[index];
            r = ((value >> 11) & 31u) * 255u / 31u;
            g = ((value >> 5) & 63u) * 255u / 63u;
            b = (value & 31u) * 255u / 31u;
        }
        else if (type == PIXEL_UNSIGNED_SHORT_5_5_5_1)
        {
            const unsigned int value = words[index];
            r = ((value >> 11) & 31u) * 255u / 31u;
            g = ((value >> 6) & 31u) * 255u / 31u;
            b = ((value >> 1) & 31u) * 255u / 31u;
            a = (value & 1u) ? 255u : 0u;
        }
        else
        {
            const unsigned int value = words[index];
            // TH07 embeds D3DFMT_A4R4G4B4 words, not GL's RGBA4444
            // ordering implied by the portable enum name.
            a = ((value >> 12) & 15u) * 17u;
            r = ((value >> 8) & 15u) * 17u;
            g = ((value >> 4) & 15u) * 17u;
            b = (value & 15u) * 17u;
        }
    }

    static void WriteTexturePixel(GuTexture &texture, unsigned int index, unsigned int r,
                                  unsigned int g, unsigned int b, unsigned int a)
    {
        const unsigned int bytesPerPixel = texture.psm == GU_PSM_8888 ? 4u : 2u;
        const unsigned int x = index % texture.storageWidth;
        const unsigned int y = index / texture.storageWidth;
        const unsigned int rowBytes = texture.storageWidth * bytesPerPixel;
        const unsigned int byteX = x * bytesPerPixel;
        const unsigned int byteOffset = texture.swizzled
                                            ? (((y >> 3) * (rowBytes >> 4) + (byteX >> 4)) << 7) +
                                                  ((y & 7u) << 4) + (byteX & 15u)
                                            : y * rowBytes + byteX;
        auto *destination = static_cast<unsigned char *>(texture.pixels) + byteOffset;
        if (texture.psm == GU_PSM_8888)
        {
            *reinterpret_cast<unsigned int *>(destination) = r | (g << 8) | (b << 16) | (a << 24);
        }
        else if (texture.psm == GU_PSM_5650)
        {
            *reinterpret_cast<unsigned short *>(destination) = static_cast<unsigned short>(
                (r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11));
        }
        else if (texture.psm == GU_PSM_5551)
        {
            *reinterpret_cast<unsigned short *>(destination) = static_cast<unsigned short>(
                (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | ((a >= 128u) << 15));
        }
        else
        {
            *reinterpret_cast<unsigned short *>(destination) = static_cast<unsigned short>(
                (r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8) | ((a >> 4) << 12));
        }
    }
};
} // namespace

ZunGraphics *Th07CreatePspGuBackend()
{
    auto *backend = new PspGuGraphics();
    if (!backend->Init())
    {
        delete backend;
        return nullptr;
    }
    gPspGuBackend = backend;
    return backend;
}

void Th07PspDrawSpriteQuads(const Th07PspSpriteVertex *vertices, unsigned int spriteCount)
{
    if (gPspGuBackend)
    {
        gPspGuBackend->DrawSpriteQuads(vertices, spriteCount);
    }
}

void Th07PspDrawSpritePairs(const Th07PspSpriteVertex *vertices, unsigned int spriteCount)
{
    if (gPspGuBackend)
    {
        gPspGuBackend->DrawSpritePairs(vertices, spriteCount);
    }
}

void Th07PspForgetSurface(const void *pixels)
{
    if (gPspGuBackend)
    {
        gPspGuBackend->ForgetSurface(pixels);
    }
}

void Th07PspMarkTextTexture(GfxTextureHandle texture)
{
    if (gPspGuBackend)
    {
        gPspGuBackend->MarkTextTexture(texture);
    }
}

void Th07PspBeginTextUploadBatch()
{
    if (gPspGuBackend)
    {
        gPspGuBackend->BeginTextUploadBatch();
    }
}

void Th07PspEndTextUploadBatch()
{
    if (gPspGuBackend)
    {
        gPspGuBackend->EndTextUploadBatch();
    }
}

void Th07PspCompactTextTexture(GfxTextureHandle texture)
{
    if (gPspGuBackend)
    {
        gPspGuBackend->CompactTextTexture(texture);
    }
}

unsigned int Th07PspTrimTextureCache()
{
    return gPspGuBackend ? gPspGuBackend->TrimTextureCache() : 0;
}

#if defined(TH07_PSP_PERF_DIAG)
void Th07PspPerfAddCalcTime(unsigned long long elapsedUs)
{
    gPerfCalcChainUs += elapsedUs;
}

void Th07PspPerfAddDrawTime(unsigned long long elapsedUs)
{
    gPerfDrawChainUs += elapsedUs;
}

void Th07PspPerfAddStageTime(unsigned long long elapsedUs)
{
    gPerfStageDrawUs += elapsedUs;
}

void Th07PspPerfAddCalcJobTime(int priority, unsigned long long elapsedUs)
{
    if (priority >= 0 && priority < static_cast<int>(sizeof(gPerfCalcJobUs) /
                                                     sizeof(gPerfCalcJobUs[0])))
    {
        gPerfCalcJobUs[priority] += elapsedUs;
    }
}

void Th07PspPerfAddDrawJobTime(int priority, unsigned long long elapsedUs)
{
    if (priority >= 0 && priority < static_cast<int>(sizeof(gPerfDrawJobUs) /
                                                     sizeof(gPerfDrawJobUs[0])))
    {
        gPerfDrawJobUs[priority] += elapsedUs;
    }
}
#endif

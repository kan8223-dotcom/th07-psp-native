#include "TextHelper.hpp"

#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_endian.h>
#include <SDL2/SDL_rwops.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "GameErrorContext.hpp"
#include "Supervisor.hpp"
#include "graphics/ZunGraphics.hpp"
#include "inttypes.hpp"
#include "thirdparty/sjis_converter.h"
#if defined(TH07_PSP)
#include "fileio.hpp"
#include "graphics/PspGuGraphics.hpp"
#include "optional_ram_budget.hpp"
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
#include <pspkernel.h>
#endif
#endif

static TTF_Font *g_Font = nullptr;
static TextHelper g_TextWorkBuffer;

#if defined(TH07_PSP) && !defined(TH07_PSP_1000)
namespace
{
// A complete gameplay text update is normally 512 * 16 * 4 = 32 KiB. Keep a
// losslessly zero-run-compressed copy of the already filtered upload row rather
// than SDL_ttf's internal glyph state: a cache hit is bit-identical and only
// pays decode plus the unavoidable texture update. One contiguous arena avoids
// hundreds of medium-sized heap blocks and makes releasing a stage deterministic.
constexpr u32 kStageTextCacheMaxBytes = 1536u * 1024u;
constexpr u32 kStageTextCacheMinBytes = 256u * 1024u;
constexpr u32 kStageTextCacheDecodeBytes = 512u * 16u * 4u;

struct StageTextCacheKey
{
    i32 xPos;
    i32 yPos;
    i32 spriteWidth;
    i32 spriteHeight;
    i32 fontHeight;
    i32 fontWidth;
    u32 textColor;
    u32 outlineType;
};

struct StageTextCacheEntry
{
    StageTextCacheKey key;
    u32 hash;
    u32 stringOffset;
    u32 stringBytes;
    u32 payloadOffset;
    u32 encodedBytes;
    u32 rawBytes;
    u32 encoding;
};

enum StageTextCacheEncoding : u32
{
    STAGE_TEXT_CACHE_RAW = 0,
    STAGE_TEXT_CACHE_ZERO_RLE = 1,
};

struct StageTextCacheState
{
    u8 *arena;
    u32 capacityBytes;
    u32 dataBegin;
    u32 entryCount;
    u32 preRenderCount;
    u32 hitCount;
    u32 missCount;
    u32 fullCount;
    u32 expectedKeyCount;
    u32 coveredKeyCount;
    bool prewarming;
    bool ready;
};

StageTextCacheState g_StageTextCache = {};
// Text rendering and the shared work surface are already main-thread-only.
// Reuse that established serialization so the public pre-render API can run
// the exact production raster path without introducing a second copy of it.
bool g_StageTextPreRenderOnly = false;
bool g_StageTextPreRenderSucceeded = false;
#if !defined(TH07_PSP_1000)
i32 g_LastPreRenderFontSize = 0;
TTF_Font *g_LastPreRenderFont = nullptr;
#endif

#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
StageTextPrewarmTiming g_StageTextPrewarmTiming = {};

u32 StageTextPrewarmNowUs()
{
    return sceKernelGetSystemTimeLow();
}

u32 StageTextPrewarmElapsedUs(u32 startUs)
{
    return StageTextPrewarmNowUs() - startUs;
}

struct StageTextPrewarmRowScope
{
    explicit StageTextPrewarmRowScope(bool enabled)
        : active(enabled), succeeded(false), startUs(enabled ? StageTextPrewarmNowUs() : 0u)
    {
        if (active)
        {
            ++g_StageTextPrewarmTiming.uniqueRowCount;
        }
    }

    ~StageTextPrewarmRowScope()
    {
        if (!active)
        {
            return;
        }
        g_StageTextPrewarmTiming.uniqueTotalUs += StageTextPrewarmElapsedUs(startUs);
        if (!succeeded)
        {
            ++g_StageTextPrewarmTiming.failureCount;
        }
    }

    void Complete(bool success)
    {
        succeeded = success;
    }

    bool active;
    bool succeeded;
    u32 startUs;
};

#define TH07_TEXT_PREWARM_LOOKUP_BEGIN()                                             \
    const bool profilePrewarm = g_StageTextPreRenderOnly && g_StageTextCache.prewarming; \
    const u32 lookupStartUs = profilePrewarm ? StageTextPrewarmNowUs() : 0u;          \
    if (profilePrewarm)                                                               \
    {                                                                                 \
        ++g_StageTextPrewarmTiming.requestCount;                                      \
    }
#else
#define TH07_TEXT_PREWARM_LOOKUP_BEGIN() ((void)0)
#endif

#if defined(TH07_PSP_FONT_MAIN_RAM)
constexpr std::size_t kDefaultFontMaxMainRamBytes = 8u * 1024u * 1024u;
char g_DefaultFontPath[768] = {};
const void *g_DefaultFontMainRamData = nullptr;
std::size_t g_DefaultFontMainRamBytes = 0;
SDL_RWops *g_DefaultFontMainRamStream = nullptr;
bool g_DefaultFontMainRamFailureReported = false;

void RememberDefaultFontPath(const char *path)
{
    if (!path)
    {
        g_DefaultFontPath[0] = '\0';
        return;
    }
    std::snprintf(g_DefaultFontPath, sizeof(g_DefaultFontPath), "%s", path);
}

void ResetDefaultFontRuntimeTracking()
{
    g_LastPreRenderFont = nullptr;
    g_LastPreRenderFontSize = 0;
}

void ReportDefaultFontMainRamFailureOnce(const char *step)
{
    if (!g_DefaultFontMainRamFailureReported)
    {
        th07_psp_boot_notef("font Main RAM fallback step=%s", step ? step : "unknown");
        g_DefaultFontMainRamFailureReported = true;
    }
}

void ReleaseDefaultFontMainRamBacking()
{
    if (g_DefaultFontMainRamStream)
    {
        SDL_RWclose(g_DefaultFontMainRamStream);
    }
    g_DefaultFontMainRamStream = nullptr;
    if (g_DefaultFontMainRamData)
    {
        Th07PspOptionalRamReleaseFontBuffer(g_DefaultFontMainRamData);
    }
    g_DefaultFontMainRamData = nullptr;
    g_DefaultFontMainRamBytes = 0;
}
#endif

u32 AlignDownStageTextCache(u32 value, u32 alignment)
{
    return value & ~(alignment - 1u);
}

u32 AlignUpStageTextCache(u32 value, u32 alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

u32 HashStageTextCacheWord(u32 hash, u32 value)
{
    for (u32 shift = 0; shift < 32u; shift += 8u)
    {
        hash = (hash ^ ((value >> shift) & 0xffu)) * 16777619u;
    }
    return hash;
}

u32 HashStageTextCacheKey(const StageTextCacheKey &key, const char *string)
{
    u32 hash = 2166136261u;
    hash = HashStageTextCacheWord(hash, static_cast<u32>(key.xPos));
    hash = HashStageTextCacheWord(hash, static_cast<u32>(key.yPos));
    hash = HashStageTextCacheWord(hash, static_cast<u32>(key.spriteWidth));
    hash = HashStageTextCacheWord(hash, static_cast<u32>(key.spriteHeight));
    hash = HashStageTextCacheWord(hash, static_cast<u32>(key.fontHeight));
    hash = HashStageTextCacheWord(hash, static_cast<u32>(key.fontWidth));
    hash = HashStageTextCacheWord(hash, key.textColor);
    hash = HashStageTextCacheWord(hash, key.outlineType);
    for (const u8 *cursor = reinterpret_cast<const u8 *>(string); *cursor; ++cursor)
    {
        hash = (hash ^ *cursor) * 16777619u;
    }
    return hash;
}

bool StageTextCacheKeysEqual(const StageTextCacheKey &lhs, const StageTextCacheKey &rhs)
{
    return lhs.xPos == rhs.xPos && lhs.yPos == rhs.yPos &&
           lhs.spriteWidth == rhs.spriteWidth && lhs.spriteHeight == rhs.spriteHeight &&
           lhs.fontHeight == rhs.fontHeight && lhs.fontWidth == rhs.fontWidth &&
           lhs.textColor == rhs.textColor && lhs.outlineType == rhs.outlineType;
}

const StageTextCacheEntry *FindStageTextCache(const StageTextCacheKey &key, const char *string,
                                              bool recordHit)
{
    if (!g_StageTextCache.arena || !string || (recordHit && !g_StageTextCache.ready))
    {
        return nullptr;
    }

    const u32 hash = HashStageTextCacheKey(key, string);
    const u32 stringBytes = static_cast<u32>(std::strlen(string)) + 1u;
    const StageTextCacheEntry *entries = reinterpret_cast<const StageTextCacheEntry *>(
        g_StageTextCache.arena + kStageTextCacheDecodeBytes);
    for (u32 i = 0; i < g_StageTextCache.entryCount; ++i)
    {
        const StageTextCacheEntry &entry = entries[i];
        if (entry.hash == hash && entry.stringBytes == stringBytes &&
            StageTextCacheKeysEqual(entry.key, key) &&
            std::memcmp(g_StageTextCache.arena + entry.stringOffset, string, stringBytes) == 0)
        {
            if (recordHit)
            {
                ++g_StageTextCache.hitCount;
            }
            return &entry;
        }
    }
    if (recordHit)
    {
        ++g_StageTextCache.missCount;
        // Never continue as a partial runtime cache.  The owner's allocation
        // remains resident until stage teardown, but every later request uses
        // the established raster fallback as well.
        g_StageTextCache.ready = false;
    }
    return nullptr;
}

const u8 *StageTextSurfacePixel(const SDL_Surface *surface, u32 pixelIdx)
{
    const u32 y = pixelIdx / static_cast<u32>(surface->w);
    const u32 x = pixelIdx - y * static_cast<u32>(surface->w);
    return static_cast<const u8 *>(surface->pixels) + y * surface->pitch + x * 4u;
}

bool StageTextPixelIsTransparent(const u8 *pixel)
{
    return (pixel[0] | pixel[1] | pixel[2] | pixel[3]) == 0;
}

u32 MeasureStageTextCachePayload(const SDL_Surface *surface)
{
    const u32 pixelCount = static_cast<u32>(surface->w) * static_cast<u32>(surface->h);
    u32 payloadBytes = 0;
    for (u32 pixelIdx = 0; pixelIdx < pixelCount;)
    {
        const bool transparent = StageTextPixelIsTransparent(
            StageTextSurfacePixel(surface, pixelIdx));
        u32 runPixels = 1;
        while (runPixels < 128u && pixelIdx + runPixels < pixelCount &&
               StageTextPixelIsTransparent(
                   StageTextSurfacePixel(surface, pixelIdx + runPixels)) == transparent)
        {
            ++runPixels;
        }
        payloadBytes += 1u + (transparent ? 0u : runPixels * 4u);
        pixelIdx += runPixels;
    }
    return payloadBytes;
}

void EncodeStageTextCachePayload(const SDL_Surface *surface, u8 *out)
{
    const u32 pixelCount = static_cast<u32>(surface->w) * static_cast<u32>(surface->h);
    for (u32 pixelIdx = 0; pixelIdx < pixelCount;)
    {
        const bool transparent = StageTextPixelIsTransparent(
            StageTextSurfacePixel(surface, pixelIdx));
        u32 runPixels = 1;
        while (runPixels < 128u && pixelIdx + runPixels < pixelCount &&
               StageTextPixelIsTransparent(
                   StageTextSurfacePixel(surface, pixelIdx + runPixels)) == transparent)
        {
            ++runPixels;
        }
        *out++ = static_cast<u8>((transparent ? 0x80u : 0u) | (runPixels - 1u));
        if (!transparent)
        {
            for (u32 i = 0; i < runPixels; ++i)
            {
                const u8 *pixel = StageTextSurfacePixel(surface, pixelIdx + i);
                std::memcpy(out, pixel, 4u);
                out += 4u;
            }
        }
        pixelIdx += runPixels;
    }
}

const u8 *DecodeStageTextCachePayload(const StageTextCacheEntry &entry)
{
    if (!g_StageTextCache.arena || entry.rawBytes > kStageTextCacheDecodeBytes ||
        entry.payloadOffset > g_StageTextCache.capacityBytes ||
        entry.encodedBytes > g_StageTextCache.capacityBytes - entry.payloadOffset)
    {
        return nullptr;
    }
    const u8 *in = g_StageTextCache.arena + entry.payloadOffset;
    const u8 *const inEnd = in + entry.encodedBytes;
    u8 *out = g_StageTextCache.arena;
    u8 *const outEnd = out + entry.rawBytes;
    if (entry.encoding == STAGE_TEXT_CACHE_RAW)
    {
        if (entry.encodedBytes != entry.rawBytes)
        {
            return nullptr;
        }
        std::memcpy(out, in, entry.rawBytes);
        return out;
    }
    if (entry.encoding != STAGE_TEXT_CACHE_ZERO_RLE)
    {
        return nullptr;
    }
    while (in < inEnd && out < outEnd)
    {
        const u8 control = *in++;
        const u32 runPixels = (control & 0x7fu) + 1u;
        const u32 runBytes = runPixels * 4u;
        if (runBytes > static_cast<u32>(outEnd - out))
        {
            return nullptr;
        }
        if (control & 0x80u)
        {
            std::memset(out, 0, runBytes);
        }
        else
        {
            if (runBytes > static_cast<u32>(inEnd - in))
            {
                return nullptr;
            }
            std::memcpy(out, in, runBytes);
            in += runBytes;
        }
        out += runBytes;
    }
    return in == inEnd && out == outEnd ? g_StageTextCache.arena : nullptr;
}

bool StoreStageTextCache(const StageTextCacheKey &key, const char *string,
                         const SDL_Surface *pixels)
{
    if (!g_StageTextCache.arena || !string || !pixels || !pixels->pixels || pixels->w <= 0 ||
        pixels->h <= 0)
    {
        return false;
    }
    if (FindStageTextCache(key, string, false))
    {
        return true;
    }

    const size_t stringLength = std::strlen(string);
    const size_t pixelBytesWide = static_cast<size_t>(pixels->w) *
                                  static_cast<size_t>(pixels->h) * 4u;
    if (stringLength >= UINT32_MAX || pixelBytesWide > UINT32_MAX)
    {
        return false;
    }
    const u32 stringBytes = static_cast<u32>(stringLength) + 1u;
    const u32 pixelsBytes = static_cast<u32>(pixelBytesWide);
    if (pixelsBytes > kStageTextCacheDecodeBytes)
    {
        return false;
    }
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    const bool profileStore = g_StageTextPreRenderOnly && g_StageTextCache.prewarming;
    const u32 rleMeasureStartUs = profileStore ? StageTextPrewarmNowUs() : 0u;
#endif
    const u32 rleBytes = MeasureStageTextCachePayload(pixels);
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    if (profileStore)
    {
        g_StageTextPrewarmTiming.rleMeasureUs +=
            StageTextPrewarmElapsedUs(rleMeasureStartUs);
    }
#endif
    const bool useRle = rleBytes < pixelsBytes;
    const u32 encodedBytes = useRle ? rleBytes : pixelsBytes;
    const u32 stringStorage = AlignUpStageTextCache(stringBytes, 4u);
    const u32 payloadStorage = AlignUpStageTextCache(encodedBytes, 4u);
    if (stringStorage > g_StageTextCache.dataBegin ||
        payloadStorage > g_StageTextCache.dataBegin - stringStorage)
    {
        ++g_StageTextCache.fullCount;
        return false;
    }
    const u32 storageBytes = stringStorage + payloadStorage;
    const u32 newDataBegin = AlignDownStageTextCache(
        g_StageTextCache.dataBegin - storageBytes, alignof(StageTextCacheEntry));
    const size_t nextEntriesEnd = kStageTextCacheDecodeBytes +
                                  static_cast<size_t>(g_StageTextCache.entryCount + 1u) *
                                      sizeof(StageTextCacheEntry);
    if (nextEntriesEnd > newDataBegin)
    {
        ++g_StageTextCache.fullCount;
        return false;
    }

    const u32 payloadOffset = newDataBegin;
    const u32 stringOffset = payloadOffset + payloadStorage;
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    const u32 rleEncodeStartUs = profileStore ? StageTextPrewarmNowUs() : 0u;
#endif
    if (useRle)
    {
        EncodeStageTextCachePayload(pixels, g_StageTextCache.arena + payloadOffset);
    }
    else
    {
        for (i32 y = 0; y < pixels->h; ++y)
        {
            std::memcpy(g_StageTextCache.arena + payloadOffset + y * pixels->w * 4u,
                        static_cast<const u8 *>(pixels->pixels) + y * pixels->pitch,
                        pixels->w * 4u);
        }
    }
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    if (profileStore)
    {
        g_StageTextPrewarmTiming.rleEncodeUs +=
            StageTextPrewarmElapsedUs(rleEncodeStartUs);
    }
#endif
    std::memcpy(g_StageTextCache.arena + stringOffset, string, stringBytes);

    StageTextCacheEntry *entries = reinterpret_cast<StageTextCacheEntry *>(
        g_StageTextCache.arena + kStageTextCacheDecodeBytes);
    StageTextCacheEntry &entry = entries[g_StageTextCache.entryCount++];
    entry.key = key;
    entry.hash = HashStageTextCacheKey(key, string);
    entry.stringOffset = stringOffset;
    entry.stringBytes = stringBytes;
    entry.payloadOffset = payloadOffset;
    entry.encodedBytes = encodedBytes;
    entry.rawBytes = pixelsBytes;
    entry.encoding = useRle ? STAGE_TEXT_CACHE_ZERO_RLE : STAGE_TEXT_CACHE_RAW;
    g_StageTextCache.dataBegin = newDataBegin;
    if (g_StageTextCache.prewarming)
    {
        ++g_StageTextCache.preRenderCount;
    }
    return true;
}
} // namespace
#endif

static TTF_Font *OpenDefaultFont()
{
#if defined(TH07_PSP)
    char fontPath[768];
    // Match the final TH06 PSP port: a locally supplied MS Gothic is the
    // first choice because its hinted strokes survive an 8-9 pixel physical
    // glyph much better.  Noto remains the redistributable release fallback;
    // msgothic.ttc is never packaged by this project.
    const char *resolvedPath =
        th07_psp_resolve_path("msgothic.ttc", fontPath, sizeof(fontPath));
    TTF_Font *font = TTF_OpenFont(resolvedPath, 10);
#if defined(TH07_PSP_FONT_MAIN_RAM) && !defined(TH07_PSP_1000)
    if (font)
    {
        RememberDefaultFontPath(resolvedPath);
    }
#endif
    if (!font)
    {
        resolvedPath =
            th07_psp_resolve_path("NotoSansJP-Regular.ttf", fontPath, sizeof(fontPath));
        font = TTF_OpenFont(resolvedPath, 10);
#if defined(TH07_PSP_FONT_MAIN_RAM) && !defined(TH07_PSP_1000)
        if (font)
        {
            RememberDefaultFontPath(resolvedPath);
        }
#endif
    }
    return font;
#else
    return TTF_OpenFont("msgothic.ttc", 10);
#endif
}

bool TextHelper::PromoteDefaultFontToMainRam()
{
#if defined(TH07_PSP) && defined(TH07_PSP_FONT_MAIN_RAM) && !defined(TH07_PSP_1000)
    if (g_DefaultFontMainRamData && g_DefaultFontMainRamStream)
    {
        return true;
    }
    if (!g_Font || !g_DefaultFontPath[0])
    {
        ReportDefaultFontMainRamFailureOnce("no-file-font");
        return false;
    }

    SDL_RWops *fileStream = SDL_RWFromFile(g_DefaultFontPath, "rb");
    if (!fileStream)
    {
        ReportDefaultFontMainRamFailureOnce("open");
        return false;
    }
    const Sint64 fileBytes = SDL_RWsize(fileStream);
    if (fileBytes <= 0 || static_cast<Uint64>(fileBytes) > kDefaultFontMaxMainRamBytes ||
        SDL_RWseek(fileStream, 0, RW_SEEK_SET) < 0)
    {
        SDL_RWclose(fileStream);
        ReportDefaultFontMainRamFailureOnce("size");
        return false;
    }

    void *fontData =
        Th07PspOptionalRamAcquireFontBuffer(static_cast<std::size_t>(fileBytes));
    if (!fontData)
    {
        SDL_RWclose(fileStream);
        ReportDefaultFontMainRamFailureOnce("alloc");
        return false;
    }
    const std::size_t bytesRead =
        SDL_RWread(fileStream, fontData, 1u, static_cast<std::size_t>(fileBytes));
    SDL_RWclose(fileStream);
    if (bytesRead != static_cast<std::size_t>(fileBytes))
    {
        Th07PspOptionalRamReleaseFontBuffer(fontData);
        ReportDefaultFontMainRamFailureOnce("read");
        return false;
    }

    SDL_RWops *memoryStream =
        SDL_RWFromConstMem(fontData, static_cast<int>(fileBytes));
    if (!memoryStream)
    {
        Th07PspOptionalRamReleaseFontBuffer(fontData);
        ReportDefaultFontMainRamFailureOnce("rw");
        return false;
    }
    // Keep the RWops and its borrowed backing explicitly alive until the font
    // is closed. TTF_OpenFontRW receives freesrc=0 so teardown order is fully
    // visible and cannot free the owner allocation behind FreeType's back.
    TTF_Font *memoryFont = TTF_OpenFontRW(memoryStream, 0, 10);
    if (!memoryFont)
    {
        SDL_RWclose(memoryStream);
        Th07PspOptionalRamReleaseFontBuffer(fontData);
        ReportDefaultFontMainRamFailureOnce("ttf");
        return false;
    }
    TTF_SetFontStyle(memoryFont, TTF_STYLE_BOLD);

    TTF_CloseFont(g_Font);
    g_Font = memoryFont;
    g_DefaultFontMainRamData = fontData;
    g_DefaultFontMainRamBytes = static_cast<std::size_t>(fileBytes);
    g_DefaultFontMainRamStream = memoryStream;
    ResetDefaultFontRuntimeTracking();
    th07_psp_boot_notef("font Main RAM ready %uK",
                       static_cast<unsigned int>(g_DefaultFontMainRamBytes / 1024u));
    return true;
#else
    return false;
#endif
}

bool TextHelper::DemoteDefaultFontToFile()
{
#if defined(TH07_PSP) && defined(TH07_PSP_FONT_MAIN_RAM) && !defined(TH07_PSP_1000)
    if (!g_Font || !g_DefaultFontMainRamData || !g_DefaultFontMainRamStream ||
        !g_DefaultFontPath[0])
    {
        return false;
    }
    // Open the established selected file before dropping the RAM-backed font.
    // If this fails, keep the known-good memory font and its backing untouched.
    TTF_Font *fileFont = TTF_OpenFont(g_DefaultFontPath, 10);
    if (!fileFont)
    {
        return false;
    }
    TTF_SetFontStyle(fileFont, TTF_STYLE_BOLD);

    TTF_CloseFont(g_Font);
    ReleaseDefaultFontMainRamBacking();
    g_Font = fileFont;
    ResetDefaultFontRuntimeTracking();
    th07_psp_boot_note("font Main RAM released for stage guard");
    return true;
#else
    return false;
#endif
}

bool TextHelper::IsDefaultFontInMainRam()
{
#if defined(TH07_PSP) && defined(TH07_PSP_FONT_MAIN_RAM) && !defined(TH07_PSP_1000)
    return g_Font && g_DefaultFontMainRamData && g_DefaultFontMainRamStream;
#else
    return false;
#endif
}

// stolen from
// https://stackoverflow.com/questions/3404199/how-to-find-out-the-encoding-of-a-file-c-sharp/3404317#3404317
bool IsUtf8(const char *string)
{
    i32 charByteCounter = 1;
    unsigned char curByte;

    size_t len = strlen(string);
    for (size_t i = 0; i < len; i++)
    {
        curByte = string[i];
        if (charByteCounter == 1)
        {
            if (curByte >= 0x80)
            {
                while (((curByte <<= 1) & 0x80) != 0)
                {
                    charByteCounter++;
                }
                if (charByteCounter == 1 || charByteCounter > 6)
                {
                    return false;
                }
            }
        }
        else
        {
            if ((curByte & 0xC0) != 0x80)
            {
                return false;
            }
            charByteCounter--;
        }
    }
    if (charByteCounter > 1)
    {
        return false;
    }

    return true;
}

#if defined(TH07_PSP_TEXT_BLIT_FAST)
namespace
{
// Exact floor(product / 255) for every product of two 8-bit channels.  SDL
// 2.32.8's generic ARGB8888 -> ABGR8888 modulate+blend path uses the same
// truncation at each step; the +1 identity lets Allegrex avoid integer divu.
inline u8 DivideTextChannelBy255(u32 product)
{
    const u32 biased = product + 1u;
    return static_cast<u8>((biased + (biased >> 8)) >> 8);
}

void CompositeBoldTextLayerExact(const SDL_Surface *source, SDL_Surface *destination,
                                 const SDL_Rect &activeClip, i32 destinationX,
                                 i32 destinationY, u8 modR, u8 modG, u8 modB,
                                 bool blackOutline)
{
    const i32 left = std::max(destinationX, activeClip.x);
    const i32 top = std::max(destinationY, activeClip.y);
    const i32 right =
        std::min(destinationX + source->w, activeClip.x + activeClip.w);
    const i32 bottom =
        std::min(destinationY + source->h, activeClip.y + activeClip.h);
    if (left >= right || top >= bottom)
    {
        return;
    }

    for (i32 y = top; y < bottom; ++y)
    {
        const u8 *src = static_cast<const u8 *>(source->pixels) +
                        (y - destinationY) * source->pitch +
                        (left - destinationX) * 4;
        u8 *dst = static_cast<u8 *>(destination->pixels) + y * destination->pitch + left * 4;
        for (i32 x = left; x < right; ++x)
        {
            const u8 srcA = src[3];
            const u8 inverseA = static_cast<u8>(255u - srcA);
            if (!blackOutline)
            {
                // TTF_RenderUTF8_Blended is ARGB8888 on little-endian PSP:
                // memory is B,G,R,A.  The work surface is RGBA32: R,G,B,A.
                const u8 srcR = DivideTextChannelBy255(static_cast<u32>(src[2]) * modR);
                const u8 srcG = DivideTextChannelBy255(static_cast<u32>(src[1]) * modG);
                const u8 srcB = DivideTextChannelBy255(static_cast<u32>(src[0]) * modB);
                dst[0] = static_cast<u8>(
                    DivideTextChannelBy255(static_cast<u32>(srcR) * srcA) +
                    DivideTextChannelBy255(static_cast<u32>(dst[0]) * inverseA));
                dst[1] = static_cast<u8>(
                    DivideTextChannelBy255(static_cast<u32>(srcG) * srcA) +
                    DivideTextChannelBy255(static_cast<u32>(dst[1]) * inverseA));
                dst[2] = static_cast<u8>(
                    DivideTextChannelBy255(static_cast<u32>(srcB) * srcA) +
                    DivideTextChannelBy255(static_cast<u32>(dst[2]) * inverseA));
            }
            // AllocateBuffer cleared the whole active rectangle immediately
            // before these layers.  A black source over RGB zero remains zero,
            // so the four outline passes only need the exact alpha recurrence.
            dst[3] = static_cast<u8>(
                srcA + DivideTextChannelBy255(static_cast<u32>(dst[3]) * inverseA));
            src += 4;
            dst += 4;
        }
    }
}

bool CompositeBoldTextSurfaceExact(SDL_Surface *source, SDL_Surface *destination,
                                   i32 activeWidth, i32 activeHeight, i32 baseX,
                                   u32 textColor, bool compactOutline)
{
    // A white color mod clears SDL_COPY_MODULATE_COLOR and selects a different
    // legacy /256 approximation.  Keep that uncommon path on the original SDL
    // blitter; every stage-prewarm color currently enters the exact /255 path.
    if (!source || !destination || (textColor & 0x00ffffffu) == 0x00ffffffu ||
        SDL_BYTEORDER != SDL_LIL_ENDIAN ||
        source->format->format != SDL_PIXELFORMAT_ARGB8888 ||
        destination->format->format != SDL_PIXELFORMAT_RGBA32 ||
        source->format->BytesPerPixel != 4 || destination->format->BytesPerPixel != 4 ||
        !source->pixels || !destination->pixels || source == destination ||
        source->w <= 0 || source->h <= 0 || activeWidth <= 0 || activeHeight <= 0 ||
        activeWidth > destination->w || activeHeight > destination->h ||
        source->pitch < source->w * 4 || destination->pitch < destination->w * 4)
    {
        return false;
    }

    SDL_BlendMode blendMode = SDL_BLENDMODE_NONE;
    u8 alphaMod = 0;
    u8 colorModR = 0;
    u8 colorModG = 0;
    u8 colorModB = 0;
    u32 colorKey = 0;
    if (SDL_GetSurfaceBlendMode(source, &blendMode) != 0 || blendMode != SDL_BLENDMODE_BLEND ||
        SDL_GetSurfaceAlphaMod(source, &alphaMod) != 0 || alphaMod != 255 ||
        SDL_GetSurfaceColorMod(source, &colorModR, &colorModG, &colorModB) != 0 ||
        colorModR != 255 || colorModG != 255 || colorModB != 255 ||
        SDL_GetColorKey(source, &colorKey) == 0)
    {
        return false;
    }

    SDL_Rect activeBounds = {0, 0, activeWidth, activeHeight};
    SDL_Rect activeClip = {};
    if (SDL_IntersectRect(&activeBounds, &destination->clip_rect, &activeClip) == SDL_FALSE)
    {
        return true;
    }

    const bool lockSource = SDL_MUSTLOCK(source);
    const bool lockDestination = SDL_MUSTLOCK(destination);
    if (lockSource && SDL_LockSurface(source) != 0)
    {
        return false;
    }
    if (lockDestination && SDL_LockSurface(destination) != 0)
    {
        if (lockSource)
        {
            SDL_UnlockSurface(source);
        }
        return false;
    }

    static const i32 normalDx[4] = {4, 0, 2, 2};
    static const i32 normalDy[4] = {2, 2, 0, 4};
    static const i32 compactDx[4] = {3, 1, 2, 2};
    static const i32 compactDy[4] = {2, 2, 1, 3};
    const i32 *dx = compactOutline ? compactDx : normalDx;
    const i32 *dy = compactOutline ? compactDy : normalDy;
    for (i32 layer = 0; layer < 4; ++layer)
    {
        CompositeBoldTextLayerExact(source, destination, activeClip, baseX + dx[layer],
                                    dy[layer], 0, 0, 0, true);
    }
    CompositeBoldTextLayerExact(
        source, destination, activeClip, baseX + 2, 2,
        static_cast<u8>((textColor >> 16) & 0xffu),
        static_cast<u8>((textColor >> 8) & 0xffu), static_cast<u8>(textColor & 0xffu), false);

    if (lockDestination)
    {
        SDL_UnlockSurface(destination);
    }
    if (lockSource)
    {
        SDL_UnlockSurface(source);
    }
    return true;
}
} // namespace
#endif

TextHelper::TextHelper()
{
    this->buffer = NULL;
    this->width = 0;
    this->height = 0;
}

TextHelper::~TextHelper()
{
    ReleaseBuffer();
}

bool TextHelper::ReleaseBuffer()
{
    if (this->buffer)
    {
        SDL_FreeSurface(this->buffer);
        this->buffer = NULL;
        this->width = 0;
        this->height = 0;
        return true;
    }
    return false;
}

bool TextHelper::AllocateBuffer(i32 width, i32 height)
{
    if (this->buffer && this->width >= width && this->height >= height)
    {
        SDL_Rect usedRect = {0, 0, width, height};
        SDL_FillRect(this->buffer, &usedRect, 0);
        return true;
    }
    ReleaseBuffer();
    this->buffer = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (!this->buffer)
    {
        return false;
    }
    SDL_FillRect(this->buffer, NULL, 0);
    this->width = width;
    this->height = height;
    return true;
}

bool TextHelper::InvertAlpha(i32 x, i32 y, i32 spriteWidth, i32 fontHeight, i32 param5)
{
    i32 doubleArea = spriteWidth * fontHeight * 2;
    if (doubleArea == 0 || !this->buffer)
    {
        return false;
    }

    SDL_LockSurface(this->buffer);
    u8 *pixels = (u8 *)this->buffer->pixels;
    i32 pitch = this->buffer->pitch;

    // Music Room always shades a 1024x38 work rectangle with param5 == 0.
    // Keeping its divisor compile-time constant lets Allegrex GCC replace the
    // two runtime divisions on every covered glyph pixel with multiply/shift.
    // The expressions and their integer truncation order match the generic
    // path below exactly.
    if (x == 0 && y == 0 && spriteWidth == 1024 && fontHeight == 38 && !param5)
    {
        constexpr i32 kMusicTextDoubleArea = 1024 * 38 * 2;
        for (i32 py = 0; py < 38; ++py)
        {
            for (i32 px = 0; px < 1024; ++px)
            {
                u8 *p = &pixels[py * pitch + px * 4];
                u8 r = p[0];
                u8 g = p[1];
                u8 b = p[2];
                const u8 a = p[3];

                if (a > 0)
                {
                    const i32 i = (py * 1024 + px) * 2;
                    if (r >= b)
                    {
                        r = r - (r * i * 2) / kMusicTextDoubleArea / 3;
                        g = g - (g * i * 2) / kMusicTextDoubleArea / 3;
                    }
                    else
                    {
                        b = b - (b * i) / kMusicTextDoubleArea / 2;
                        g = g - (g * i) / kMusicTextDoubleArea / 2;
                    }
                    p[0] = r;
                    p[1] = g;
                    p[2] = b;
                    p[3] = a;
                }
                else
                {
                    p[0] = 0;
                    p[1] = 0;
                    p[2] = 0;
                    p[3] = 0;
                }
            }
        }
        SDL_UnlockSurface(this->buffer);
        return true;
    }

    for (i32 py = 0; py < fontHeight; py++)
    {
        for (i32 px = 0; px < spriteWidth; px++)
        {
            u8 *p = &pixels[(py + y) * pitch + (px + x) * 4];
            u8 r = p[0];
            u8 g = p[1];
            u8 b = p[2];
            u8 a = p[3];

            if (a > 0)
            {
                i32 i = (py * spriteWidth + px) * 2;

                if (!param5)
                {
                    if (r >= b)
                    {
                        r = r - (r * i * 2) / doubleArea / 3;
                        g = g - (g * i * 2) / doubleArea / 3;
                    }
                    else
                    {
                        b = b - (b * i) / doubleArea / 2;
                        g = g - (g * i) / doubleArea / 2;
                    }
                }
                else
                {
                    if (r >= b)
                    {
                        r = r - (r * i) / doubleArea / 4;
                        g = g - (g * i) / doubleArea / 4;
                    }
                    else
                    {
                        b = b - (b * i) / doubleArea / 4;
                        g = g - (g * i) / doubleArea / 4;
                    }
                }

                p[0] = r;
                p[1] = g;
                p[2] = b;
                p[3] = a;
            }
            else
            {
                p[0] = 0;
                p[1] = 0;
                p[2] = 0;
                p[3] = 0;
            }
        }
    }

    SDL_UnlockSurface(this->buffer);
    return true;
}

static void CopyTextBufferBoxFiltered(SDL_Surface *src, const SDL_Rect &srcRect,
                                      SDL_Surface *dst)
{
    if (!src || !src->pixels || !dst || !dst->pixels || dst->w <= 0 || dst->h <= 0 ||
        srcRect.w <= 0 || srcRect.h <= 0)
    {
        return;
    }

    SDL_LockSurface(src);
    SDL_LockSurface(dst);
    const bool exactHorizontal2x =
        static_cast<long long>(srcRect.w) == static_cast<long long>(dst->w) * 2;
    const bool unclippedHorizontal2x =
        exactHorizontal2x && srcRect.x >= 0 && srcRect.x + srcRect.w <= src->w;
    for (i32 y = 0; y < dst->h; ++y)
    {
        i32 sy0 = srcRect.y + static_cast<i32>(static_cast<long long>(y) * srcRect.h / dst->h);
        i32 sy1 = srcRect.y + static_cast<i32>(
            (static_cast<long long>(y + 1) * srcRect.h + dst->h - 1) / dst->h);
        sy0 = std::max(0, std::min(src->h - 1, sy0));
        sy1 = std::max(sy0 + 1, std::min(src->h, sy1));

        for (i32 x = 0; x < dst->w; ++x)
        {
            i32 sx0;
            i32 sx1;
            if (exactHorizontal2x)
            {
                // Music-room text is rasterised at exactly twice its upload
                // width.  These are the same floor/ceil bounds as below,
                // without two software 64-bit divisions for every pixel.
                sx0 = srcRect.x + x * 2;
                sx1 = sx0 + 2;
            }
            else
            {
                sx0 = srcRect.x +
                      static_cast<i32>(static_cast<long long>(x) * srcRect.w / dst->w);
                sx1 = srcRect.x + static_cast<i32>(
                    (static_cast<long long>(x + 1) * srcRect.w + dst->w - 1) / dst->w);
            }
            sx0 = std::max(0, std::min(src->w - 1, sx0));
            sx1 = std::max(sx0 + 1, std::min(src->w, sx1));

            u32 sumR = 0;
            u32 sumG = 0;
            u32 sumB = 0;
            u32 sumA = 0;
            u32 count = 0;
            for (i32 sy = sy0; sy < sy1; ++sy)
            {
                const u8 *srcRow = static_cast<const u8 *>(src->pixels) + sy * src->pitch;
                for (i32 sx = sx0; sx < sx1; ++sx)
                {
                    const u8 *pixel = srcRow + sx * 4;
                    sumR += pixel[0];
                    sumG += pixel[1];
                    sumB += pixel[2];
                    sumA += pixel[3];
                    ++count;
                }
            }

            u8 *out = static_cast<u8 *>(dst->pixels) + y * dst->pitch + x * 4;
            if (unclippedHorizontal2x)
            {
                // The x span is exactly two pixels, so the denominator is a
                // small row-constant.  Keeping these constants visible to the
                // compiler removes four Allegrex divu operations per pixel.
                const i32 sampleRows = sy1 - sy0;
                if (sampleRows == 3)
                {
                    out[0] = static_cast<u8>(sumR / 6u);
                    out[1] = static_cast<u8>(sumG / 6u);
                    out[2] = static_cast<u8>(sumB / 6u);
                    out[3] = static_cast<u8>(sumA / 6u);
                }
                else if (sampleRows == 4)
                {
                    out[0] = static_cast<u8>(sumR >> 3);
                    out[1] = static_cast<u8>(sumG >> 3);
                    out[2] = static_cast<u8>(sumB >> 3);
                    out[3] = static_cast<u8>(sumA >> 3);
                }
                else
                {
                    out[0] = static_cast<u8>(sumR / count);
                    out[1] = static_cast<u8>(sumG / count);
                    out[2] = static_cast<u8>(sumB / count);
                    out[3] = static_cast<u8>(sumA / count);
                }
            }
            else
            {
                out[0] = static_cast<u8>(sumR / count);
                out[1] = static_cast<u8>(sumG / count);
                out[2] = static_cast<u8>(sumB / count);
                out[3] = static_cast<u8>(sumA / count);
            }
        }
    }
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
}

static SDL_Surface *CreateTextUploadSurface(TextHelper &textHelper, i32 yPos, i32 spriteWidth,
                                            i32 spriteHeight, i32 fontHeight)
{
    if (spriteWidth <= 0 || yPos < 0 || yPos >= spriteHeight || !textHelper.buffer)
    {
        return nullptr;
    }
    const i32 uploadHeight = std::max(1, std::min(16, spriteHeight - yPos));
    SDL_Surface *outSurface =
        SDL_CreateRGBSurfaceWithFormat(0, spriteWidth, uploadHeight, 32,
                                       SDL_PIXELFORMAT_RGBA32);
    if (!outSurface)
    {
        return nullptr;
    }
    SDL_Rect srcRect;
    srcRect.x = 0;
    srcRect.y = 0;
    srcRect.w = spriteWidth * 2;
    srcRect.h = fontHeight * 2 + 8;
    if (srcRect.w > textHelper.width)
    {
        srcRect.w = textHelper.width;
    }
    if (srcRect.h > textHelper.height)
    {
        srcRect.h = textHelper.height;
    }
    CopyTextBufferBoxFiltered(textHelper.buffer, srcRect, outSurface);
    return outSurface;
}

static void UploadTextPixels(i32 yPos, i32 spriteWidth, i32 spriteHeight,
                             const void *pixels, GfxTextureHandle outTexture)
{
    if (!pixels || spriteWidth <= 0 || yPos < 0 || yPos >= spriteHeight)
    {
        return;
    }
    const i32 uploadHeight = std::max(1, std::min(16, spriteHeight - yPos));
#if defined(TH07_PSP)
    Th07PspMarkTextTexture(outTexture);
#endif
    g_Supervisor.gfxDevice->BindTexture(outTexture);
    g_Supervisor.gfxDevice->SetTextureSubImage(0, yPos, spriteWidth, uploadHeight, pixels);
}

bool TextHelper::CopyTextToTexture(i32 yPos, i32 spriteWidth, i32 spriteHeight, i32 fontHeight,
                                   i32 fontWidth, GfxTextureHandle outTexture)
{
    (void)fontWidth;
    // TH06's proven path scales each 2x raster into the actual 16px text row
    // with an explicit area average.  SDL_SoftStretchLinear sampled only four
    // neighbours and then TH07 scaled the result again into the PSP viewport,
    // which erased narrow outline strokes.
    SDL_Surface *outSurface =
        CreateTextUploadSurface(*this, yPos, spriteWidth, spriteHeight, fontHeight);
    if (!outSurface)
    {
        return false;
    }
    UploadTextPixels(yPos, spriteWidth, spriteHeight, outSurface->pixels, outTexture);
    SDL_FreeSurface(outSurface);
    return true;
}

bool TextHelper::AttachStageTextCache(void *arena, u32 capacityBytes)
{
#if defined(TH07_PSP) && !defined(TH07_PSP_1000)
    DetachStageTextCache();
    if (!arena || capacityBytes < kStageTextCacheMinBytes ||
        capacityBytes > kStageTextCacheMaxBytes ||
        capacityBytes <= kStageTextCacheDecodeBytes + sizeof(StageTextCacheEntry))
    {
        return false;
    }
    g_StageTextCache.arena = static_cast<u8 *>(arena);
    g_StageTextCache.capacityBytes = capacityBytes;
    g_StageTextCache.dataBegin = capacityBytes;
    g_StageTextCache.prewarming = true;
    return true;
#else
    // PSP-1000 and desktop builds cannot attach optional stage storage.  In
    // particular, this branch performs no allocation and retains no pointer.
    (void)arena;
    (void)capacityBytes;
    return false;
#endif
}

bool TextHelper::EndStageTextCache(bool sourceEnumerationComplete)
{
#if defined(TH07_PSP) && !defined(TH07_PSP_1000)
    g_StageTextCache.prewarming = false;
    // SDL_ttf retains every glyph touched while scanning the stage. The final
    // rows now live in our bounded arena, so explicitly discard that temporary
    // glyph cache before gameplay. Re-applying the active size preserves the
    // next miss path's font metrics while SDL_ttf frees the glyph allocations.
    if (g_StageTextCache.preRenderCount && g_Font && g_LastPreRenderFont == g_Font &&
        g_LastPreRenderFontSize > 0)
    {
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
        const u32 flushStartUs = StageTextPrewarmNowUs();
#endif
        TTF_SetFontSize(g_Font, g_LastPreRenderFontSize);
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
        g_StageTextPrewarmTiming.fontFlushUs += StageTextPrewarmElapsedUs(flushStartUs);
#endif
    }
    const bool coverageComplete =
        g_StageTextCache.expectedKeyCount == g_StageTextCache.coveredKeyCount;
    g_StageTextCache.ready = g_StageTextCache.arena && sourceEnumerationComplete &&
                             coverageComplete &&
                             g_StageTextCache.fullCount == 0 &&
                             g_StageTextCache.missCount == 0;
    return g_StageTextCache.ready;
#else
    (void)sourceEnumerationComplete;
    return false;
#endif
}

bool TextHelper::IsStageTextCacheReady()
{
#if defined(TH07_PSP) && !defined(TH07_PSP_1000)
    return g_StageTextCache.ready;
#else
    return false;
#endif
}

bool TextHelper::GetStageTextCacheStats(StageTextCacheStats *outStats)
{
#if defined(TH07_PSP) && !defined(TH07_PSP_1000)
    if (!outStats || !g_StageTextCache.arena)
    {
        return false;
    }
    outStats->capacityBytes = g_StageTextCache.capacityBytes;
    outStats->entryCount = g_StageTextCache.entryCount;
    outStats->expectedKeyCount = g_StageTextCache.expectedKeyCount;
    outStats->coveredKeyCount = g_StageTextCache.coveredKeyCount;
    outStats->hitCount = g_StageTextCache.hitCount;
    outStats->missCount = g_StageTextCache.missCount;
    outStats->fullCount = g_StageTextCache.fullCount;
    outStats->ready = g_StageTextCache.ready;
    return true;
#else
    (void)outStats;
    return false;
#endif
}

#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
bool TextHelper::GetStageTextPrewarmTiming(StageTextPrewarmTiming *outTiming)
{
    if (!outTiming || !g_StageTextCache.arena)
    {
        return false;
    }
    *outTiming = g_StageTextPrewarmTiming;
    return true;
}
#endif

void TextHelper::DetachStageTextCache()
{
#if defined(TH07_PSP) && !defined(TH07_PSP_1000)
    // Borrowed storage is never freed here. optional_ram_budget owns its
    // lifetime and calls this before releasing the pool.
    g_StageTextCache = {};
    g_StageTextPreRenderOnly = false;
    g_StageTextPreRenderSucceeded = false;
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    g_StageTextPrewarmTiming = {};
#endif
#endif
}

bool TextHelper::PreRenderTextToCacheBold(i32 xPos, i32 yPos, i32 spriteWidth,
                                          i32 spriteHeight, i32 fontHeight, i32 fontWidth,
                                          u32 textColor, u32 outlineType, const char *string)
{
#if defined(TH07_PSP) && !defined(TH07_PSP_1000)
    if (!g_StageTextCache.arena || !g_StageTextCache.prewarming || !string)
    {
        return false;
    }
    ++g_StageTextCache.expectedKeyCount;
    if (!g_Font)
    {
        return false;
    }
    g_StageTextPreRenderOnly = true;
    g_StageTextPreRenderSucceeded = false;
    RenderTextToTextureBold(xPos, yPos, spriteWidth, spriteHeight, fontHeight, fontWidth,
                            textColor, outlineType, const_cast<char *>(string),
                            GfxTextureHandle());
    g_StageTextPreRenderOnly = false;
    if (g_StageTextPreRenderSucceeded)
    {
        ++g_StageTextCache.coveredKeyCount;
    }
    return g_StageTextPreRenderSucceeded;
#else
    (void)xPos;
    (void)yPos;
    (void)spriteWidth;
    (void)spriteHeight;
    (void)fontHeight;
    (void)fontWidth;
    (void)textColor;
    (void)outlineType;
    (void)string;
    return false;
#endif
}

ZunResult TextHelper::CreateTextBuffer()
{
    if (TTF_Init() < 0)
    {
        g_GameErrorContext.Log("TTF_Init fail : %s\n", TTF_GetError());
        return ZUN_ERROR;
    }

    g_Font = OpenDefaultFont();
    if (!g_Font)
    {
        g_GameErrorContext.Log("TTF_OpenFont fail : %s\n", TTF_GetError());
        return ZUN_ERROR;
    }
    TTF_SetFontStyle(g_Font, TTF_STYLE_BOLD);
    if (!g_TextWorkBuffer.AllocateBuffer(1024, 64))
    {
        g_GameErrorContext.Log("text work buffer allocation failed\n");
        return ZUN_ERROR;
    }

    // The optional PSP-2000+ profile performs one contiguous read at process
    // startup. Every stage then uses the exact selected font bytes from Main
    // RAM; failure leaves the established file-backed path active.
    PromoteDefaultFontToMainRam();

    // Pay FreeType's one-time Japanese charmap/glyph setup cost while the
    // loading screen is expected, not on the first dialogue frame.
    TTF_SetFontSize(g_Font, 28);
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface *prewarm = TTF_RenderUTF8_Blended(g_Font, u8"さむ～", white);
    SDL_FreeSurface(prewarm);
    return ZUN_SUCCESS;
}

void TextHelper::ReleaseTextBuffer()
{
#if defined(TH07_PSP) && !defined(TH07_PSP_1000)
    // Keep the borrowed allocation alive until its sole owner has sampled the
    // final runtime counters, detached this consumer and released the pool.
    Th07PspOptionalRamEndStage();
#else
    DetachStageTextCache();
#endif
    g_TextWorkBuffer.ReleaseBuffer();
    if (g_Font)
    {
        TTF_CloseFont(g_Font);
    }
    g_Font = nullptr;
#if defined(TH07_PSP) && defined(TH07_PSP_FONT_MAIN_RAM) && !defined(TH07_PSP_1000)
    // FreeType may read the source at any point until TTF_CloseFont returns.
    // Close the borrowed RWops next, then ask the sole owner to free its data.
    ReleaseDefaultFontMainRamBacking();
    ResetDefaultFontRuntimeTracking();
#endif
    TTF_Quit();
}

void TextHelper::RenderTextToTextureBold(i32 xPos, i32 yPos, i32 spriteWidth, i32 spriteHeight,
                                         i32 fontHeight, i32 fontWidth, u32 textColor,
                                         u32 outlineType, char *string, GfxTextureHandle outTexture)
{
    if (!string)
    {
        return;
    }
#if defined(TH07_PSP) && !defined(TH07_PSP_1000)
    const StageTextCacheKey cacheKey = {xPos,       yPos,      spriteWidth, spriteHeight,
                                        fontHeight, fontWidth, textColor,   outlineType};
    TH07_TEXT_PREWARM_LOOKUP_BEGIN();
    const StageTextCacheEntry *cachedEntry =
        FindStageTextCache(cacheKey, string, !g_StageTextPreRenderOnly);
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    if (profilePrewarm)
    {
        g_StageTextPrewarmTiming.lookupUs += StageTextPrewarmElapsedUs(lookupStartUs);
    }
#endif
    if (cachedEntry)
    {
        if (g_StageTextPreRenderOnly)
        {
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
            if (profilePrewarm)
            {
                ++g_StageTextPrewarmTiming.hitCount;
            }
#endif
            g_StageTextPreRenderSucceeded = true;
            return;
        }
        if (const u8 *cachedPixels = DecodeStageTextCachePayload(*cachedEntry))
        {
            UploadTextPixels(yPos, spriteWidth, spriteHeight, cachedPixels, outTexture);
            return;
        }
        // A malformed/truncated cache row is never uploaded. Fall through to
        // the established raster path and disable every later cache hit.  The
        // owner retains the pool until teardown, so this cannot allocate/free
        // optional RAM during gameplay.
        ++g_StageTextCache.missCount;
        g_StageTextCache.ready = false;
    }
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    StageTextPrewarmRowScope prewarmRow(profilePrewarm);
    const u32 fontStartUs = profilePrewarm ? StageTextPrewarmNowUs() : 0u;
#endif
#endif

    i32 fontSize = fontHeight * 2;
    if (fontSize <= 0)
    {
        return;
    }

    if (!g_Font)
    {
        g_Font = OpenDefaultFont();
        if (!g_Font)
        {
            g_GameErrorContext.Fatal("TTF_OpenFont fail : %s\n", TTF_GetError());
            return;
        }
        TTF_SetFontStyle(g_Font, TTF_STYLE_BOLD);
    }

#if defined(TH07_PSP_1000)
    // Intentionally call this for every row. SDL_ttf releases its glyph
    // allocations here; retaining same-size glyphs across a long Story run
    // fragments the PSP-1000 heap until later dialogue textures fail.
    TTF_SetFontSize(g_Font, fontSize);
#else
    // 64 MiB models keep SDL_ttf's glyph cache. Re-setting the size every row
    // flushes it and forces a full FreeType re-raster of every glyph, measured
    // at ~0.5 s per Music Room row on PSP-3000 hardware (R7 boot log).
    // A (re)opened font has an unknown active size, so track it per font.
    static i32 currentFontSize;
    static TTF_Font *currentFont;
    if (currentFont != g_Font || currentFontSize != fontSize)
    {
        TTF_SetFontSize(g_Font, fontSize);
        currentFont = g_Font;
        currentFontSize = fontSize;
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
        if (profilePrewarm)
        {
            ++g_StageTextPrewarmTiming.fontSizeChangeCount;
        }
#endif
    }
#if defined(TH07_PSP) && !defined(TH07_PSP_1000)
    if (g_StageTextPreRenderOnly)
    {
        g_LastPreRenderFont = g_Font;
        g_LastPreRenderFontSize = fontSize;
    }
#endif
#endif

#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    if (profilePrewarm)
    {
        g_StageTextPrewarmTiming.fontUs += StageTextPrewarmElapsedUs(fontStartUs);
    }
    const u32 conversionStartUs = profilePrewarm ? StageTextPrewarmNowUs() : 0u;
#endif

    const char *convStr = string;
    char *convertedText = nullptr;
    if (!IsUtf8(string))
    {
        // PSP newlib/SDL_iconv does not provide a Shift_JIS converter.  It
        // returns NULL here and the old path consequently handed raw SJIS to
        // SDL_ttf as if it were UTF-8.  Use the same self-contained lookup
        // converter as the proven TH06 PSP port.
        convertedText = sjis2utf8(string);
        if (convertedText)
        {
            convStr = convertedText;
        }
    }

#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    if (profilePrewarm)
    {
        g_StageTextPrewarmTiming.conversionUs +=
            StageTextPrewarmElapsedUs(conversionStartUs);
    }
    const u32 ttfStartUs = profilePrewarm ? StageTextPrewarmNowUs() : 0u;
#endif

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface *textSurf = TTF_RenderUTF8_Blended(g_Font, convStr, white);
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    if (profilePrewarm)
    {
        g_StageTextPrewarmTiming.ttfUs += StageTextPrewarmElapsedUs(ttfStartUs);
    }
#endif
#if defined(TH07_PSP_DIRECT_GAME) && !defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    {
        static unsigned int textRenderLogCount;
        if (textRenderLogCount < 64)
        {
            unsigned int hash = 2166136261u;
            for (const unsigned char *cursor =
                     reinterpret_cast<const unsigned char *>(convStr);
                 *cursor; ++cursor)
            {
                hash = (hash ^ *cursor) * 16777619u;
            }
            char message[112];
            std::snprintf(message, sizeof(message), "text render %u hash %08x result %dx%d",
                          textRenderLogCount, hash, textSurf ? textSurf->w : -1,
                          textSurf ? textSurf->h : -1);
            th07_psp_boot_note(message);
            ++textRenderLogCount;
        }
    }
#endif
    free(convertedText);
    if (!textSurf)
    {
        return;
    }

    i32 dWidth = spriteWidth * 2;
    i32 dHeight = fontHeight * 2 + 8;
    if (dWidth > 1024)
    {
        dWidth = 1024;
    }
    if (dHeight > 64)
    {
        dHeight = 64;
    }
    if (dWidth <= 0 || dHeight <= 0)
    {
        SDL_FreeSurface(textSurf);
        return;
    }

#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    const u32 clearStartUs = profilePrewarm ? StageTextPrewarmNowUs() : 0u;
#endif
    const bool workBufferReady = g_TextWorkBuffer.AllocateBuffer(dWidth, dHeight);
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    if (profilePrewarm)
    {
        g_StageTextPrewarmTiming.clearUs += StageTextPrewarmElapsedUs(clearStartUs);
    }
#endif
    if (!workBufferReady)
    {
#if defined(TH07_PSP)
        th07_psp_boot_note("text work surface allocation failed");
#endif
        SDL_FreeSurface(textSurf);
        return;
    }

    TextHelper &textHelper = g_TextWorkBuffer;
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    const u32 blitStartUs = profilePrewarm ? StageTextPrewarmNowUs() : 0u;
#endif
    SDL_SetSurfaceBlendMode(textSurf, SDL_BLENDMODE_BLEND);
#if defined(TH07_PSP_TEXT_BLIT_FAST)
    const bool exactCompositeDone = CompositeBoldTextSurfaceExact(
        textSurf, textHelper.buffer, dWidth, dHeight, xPos * 2, textColor,
        outlineType == 0xffffffffu);
#else
    constexpr bool exactCompositeDone = false;
#endif
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE) && defined(TH07_PSP_TEXT_BLIT_FAST)
    if (profilePrewarm)
    {
        if (exactCompositeDone)
        {
            ++g_StageTextPrewarmTiming.fastBlitCount;
        }
        else
        {
            ++g_StageTextPrewarmTiming.fastBlitFallbackCount;
        }
    }
#endif
    if (!exactCompositeDone)
    {
        // Proven fallback, also retained for white body text because SDL's
        // no-color-mod blitter intentionally uses a different /256 rounding.
        SDL_SetSurfaceColorMod(textSurf, 0, 0, 0);
        SDL_Rect dstRect;
        if (outlineType != 0xffffffff)
        {
            i32 dx[4] = {4, 0, 2, 2};
            i32 dy[4] = {2, 2, 0, 4};
            for (i32 i = 0; i < 4; i++)
            {
                dstRect = {xPos * 2 + dx[i], dy[i], textSurf->w, textSurf->h};
                SDL_BlitSurface(textSurf, NULL, textHelper.buffer, &dstRect);
            }
        }
        else
        {
            i32 dx[4] = {3, 1, 2, 2};
            i32 dy[4] = {2, 2, 1, 3};
            for (i32 i = 0; i < 4; i++)
            {
                dstRect = {xPos * 2 + dx[i], dy[i], textSurf->w, textSurf->h};
                SDL_BlitSurface(textSurf, NULL, textHelper.buffer, &dstRect);
            }
        }

        u8 r = (textColor >> 16) & 0xFF;
        u8 g = (textColor >> 8) & 0xFF;
        u8 b_col = textColor & 0xFF;
        SDL_SetSurfaceColorMod(textSurf, r, g, b_col);
        dstRect = {xPos * 2 + 2, 2, textSurf->w, textSurf->h};
        SDL_BlitSurface(textSurf, NULL, textHelper.buffer, &dstRect);
    }

    SDL_FreeSurface(textSurf);
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    if (profilePrewarm)
    {
        g_StageTextPrewarmTiming.blitUs += StageTextPrewarmElapsedUs(blitStartUs);
    }
    const u32 invertStartUs = profilePrewarm ? StageTextPrewarmNowUs() : 0u;
#endif

    textHelper.InvertAlpha(0, 0, spriteWidth << 1, fontHeight * 2 + 8,
                           (u32)(outlineType == 0xffffffff));
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    if (profilePrewarm)
    {
        g_StageTextPrewarmTiming.invertUs += StageTextPrewarmElapsedUs(invertStartUs);
    }
    const u32 filterStartUs = profilePrewarm ? StageTextPrewarmNowUs() : 0u;
#endif
    SDL_Surface *uploadSurface =
        CreateTextUploadSurface(textHelper, yPos, spriteWidth, spriteHeight, fontHeight);
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    if (profilePrewarm)
    {
        g_StageTextPrewarmTiming.filterUs += StageTextPrewarmElapsedUs(filterStartUs);
    }
#endif
    if (!uploadSurface)
    {
        return;
    }
#if defined(TH07_PSP) && !defined(TH07_PSP_1000)
    // A scanner miss is allowed to populate the arena during loading. A
    // gameplay miss must remain the established path: do not measure/encode
    // or mutate the cache in the frame whose stall we are trying to avoid.
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    const u32 storeStartUs = profilePrewarm ? StageTextPrewarmNowUs() : 0u;
#endif
    const bool stored = g_StageTextPreRenderOnly &&
                        StoreStageTextCache(cacheKey, string, uploadSurface);
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    if (profilePrewarm)
    {
        g_StageTextPrewarmTiming.storeUs += StageTextPrewarmElapsedUs(storeStartUs);
    }
#endif
    if (g_StageTextPreRenderOnly)
    {
        g_StageTextPreRenderSucceeded = stored;
    }
    else
    {
        UploadTextPixels(yPos, spriteWidth, spriteHeight, uploadSurface->pixels, outTexture);
    }
#else
    UploadTextPixels(yPos, spriteWidth, spriteHeight, uploadSurface->pixels, outTexture);
#endif
    SDL_FreeSurface(uploadSurface);
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    prewarmRow.Complete(!profilePrewarm || g_StageTextPreRenderSucceeded);
#endif
}

i32 TextHelper::GetLogicalStringWidth(const char *str)
{
    if (!IsUtf8(str))
    {
        return strlen(str);
    }

    i32 width = 0;
    while (*str)
    {
        if ((*str & 0x80) == 0)
        {
            width += 1;
            str += 1;
        }
        else if ((*str & 0xE0) == 0xC0)
        {
            width += 2;
            str += 2;
        }
        else if ((*str & 0xF0) == 0xE0)
        {
            width += 2;
            str += 3;
        }
        else if ((*str & 0xF8) == 0xF0)
        {
            width += 2;
            str += 4;
        }
        else
        {
            str++;
        }
    }
    return width;
}

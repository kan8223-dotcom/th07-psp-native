#pragma once

#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_surface.h>

#include "ZunResult.hpp"
#include "graphics/ZunGraphics.hpp"
#include "inttypes.hpp"

struct StageTextCacheStats
{
    u32 capacityBytes;
    u32 entryCount;
    u32 expectedKeyCount;
    u32 coveredKeyCount;
    u32 hitCount;
    u32 missCount;
    u32 fullCount;
    bool ready;
};

#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
// Diagnostic-only, stage-aggregate counters. No pixel/glyph loop is timed and
// release builds do not contain this state or API.
struct StageTextPrewarmTiming
{
    u32 requestCount;
    u32 hitCount;
    u32 uniqueRowCount;
    u32 failureCount;
    u32 fontSizeChangeCount;
    u32 fastBlitCount;
    u32 fastBlitFallbackCount;
    u64 lookupUs;
    u64 uniqueTotalUs;
    u64 fontUs;
    u64 conversionUs;
    u64 ttfUs;
    u64 clearUs;
    u64 blitUs;
    u64 invertUs;
    u64 filterUs;
    u64 storeUs;
    u64 rleMeasureUs;
    u64 rleEncodeUs;
    u64 fontFlushUs;
};
#endif

struct TextHelper
{
    TextHelper();
    ~TextHelper();

    bool AllocateBuffer(i32 width, i32 height);
    bool ReleaseBuffer();
    bool InvertAlpha(i32 x, i32 y, i32 spriteWidth, i32 fontHeight, i32 param5);
    bool CopyTextToTexture(i32 yPos, i32 spriteWidth, i32 spriteHeight, i32 fontHeight,
                           i32 fontWidth, GfxTextureHandle outTexture);

    static ZunResult CreateTextBuffer();
    static void ReleaseTextBuffer();
    // PSP-2000+ validation profile only. The font bytes are owned by the
    // process-lifetime optional RAM owner and remain borrowed until the font
    // is closed. A failed promotion leaves the established file-backed font
    // active; demotion is used only to protect the higher-priority stage gate.
    static bool PromoteDefaultFontToMainRam();
    static bool DemoteDefaultFontToFile();
    static bool IsDefaultFontInMainRam();
    // The stage cache stores final 16-pixel-high RGBA upload rows in borrowed
    // Main RAM.  optional_ram_budget is the sole allocator/owner; TextHelper
    // only attaches, validates and detaches the pointer.  A disabled/rejected
    // cache leaves the established RenderTextToTextureBold path intact.
    static bool AttachStageTextCache(void *arena, u32 capacityBytes);
    static bool PreRenderTextToCacheBold(i32 xPos, i32 yPos, i32 spriteWidth,
                                         i32 spriteHeight, i32 fontHeight, i32 fontWidth,
                                         u32 textColor, u32 outlineType, const char *string);
    // sourceEnumerationComplete must only be true after every gameplay text
    // source was parsed to a bounded terminator.  Arena-local key counts cannot
    // prove that a malformed or prematurely-aborted source scan was complete.
    static bool EndStageTextCache(bool sourceEnumerationComplete);
    static bool IsStageTextCacheReady();
    static bool GetStageTextCacheStats(StageTextCacheStats *outStats);
#if defined(TH07_PSP_TEXT_PREWARM_PROFILE)
    static bool GetStageTextPrewarmTiming(StageTextPrewarmTiming *outTiming);
#endif
    static void DetachStageTextCache();
    static void RenderTextToTextureBold(i32 xPos, i32 yPos, i32 spriteWidth, i32 spriteHeight,
                                        i32 fontHeight, i32 fontWidth, u32 textColor,
                                        u32 outlineType, char *string, GfxTextureHandle outTexture);
    static i32 GetLogicalStringWidth(const char* str);

    SDL_Surface *buffer;
    i32 width;
    i32 height;
};

#include "TextHelper.hpp"

#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "GameErrorContext.hpp"
#include "Supervisor.hpp"
#include "graphics/ZunGraphics.hpp"
#include "inttypes.hpp"
#include "thirdparty/sjis_converter.h"
#if defined(TH07_PSP)
#include "fileio.hpp"
#include "graphics/PspGuGraphics.hpp"
#endif

static TTF_Font *g_Font = nullptr;
static TextHelper g_TextWorkBuffer;

static TTF_Font *OpenDefaultFont()
{
#if defined(TH07_PSP)
    char fontPath[768];
    // Match the final TH06 PSP port: a locally supplied MS Gothic is the
    // first choice because its hinted strokes survive an 8-9 pixel physical
    // glyph much better.  Noto remains the redistributable release fallback;
    // msgothic.ttc is never packaged by this project.
    TTF_Font *font = TTF_OpenFont(
        th07_psp_resolve_path("msgothic.ttc", fontPath, sizeof(fontPath)), 10);
    if (!font)
    {
        font = TTF_OpenFont(
            th07_psp_resolve_path("NotoSansJP-Regular.ttf", fontPath, sizeof(fontPath)), 10);
    }
    return font;
#else
    return TTF_OpenFont("msgothic.ttc", 10);
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
        SDL_FillRect(this->buffer, NULL, 0);
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
    for (i32 y = 0; y < dst->h; ++y)
    {
        i32 sy0 = srcRect.y + static_cast<i32>(static_cast<long long>(y) * srcRect.h / dst->h);
        i32 sy1 = srcRect.y + static_cast<i32>(
            (static_cast<long long>(y + 1) * srcRect.h + dst->h - 1) / dst->h);
        sy0 = std::max(0, std::min(src->h - 1, sy0));
        sy1 = std::max(sy0 + 1, std::min(src->h, sy1));

        for (i32 x = 0; x < dst->w; ++x)
        {
            i32 sx0 = srcRect.x +
                      static_cast<i32>(static_cast<long long>(x) * srcRect.w / dst->w);
            i32 sx1 = srcRect.x + static_cast<i32>(
                (static_cast<long long>(x + 1) * srcRect.w + dst->w - 1) / dst->w);
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
            out[0] = static_cast<u8>(sumR / count);
            out[1] = static_cast<u8>(sumG / count);
            out[2] = static_cast<u8>(sumB / count);
            out[3] = static_cast<u8>(sumA / count);
        }
    }
    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(src);
}

bool TextHelper::CopyTextToTexture(i32 yPos, i32 spriteWidth, i32 spriteHeight, i32 fontHeight,
                                   i32 fontWidth, GfxTextureHandle outTexture)
{
    (void)fontWidth;
    if (spriteWidth <= 0 || yPos < 0 || yPos >= spriteHeight)
    {
        return false;
    }
    // TH06's proven path scales each 2x raster into the actual 16px text row
    // with an explicit area average.  SDL_SoftStretchLinear sampled only four
    // neighbours and then TH07 scaled the result again into the PSP viewport,
    // which erased narrow outline strokes.
    const i32 uploadHeight = std::max(1, std::min(16, spriteHeight - yPos));
    SDL_Surface *outSurface =
        SDL_CreateRGBSurfaceWithFormat(0, spriteWidth, uploadHeight, 32,
                                       SDL_PIXELFORMAT_RGBA32);
    if (!outSurface)
    {
        return false;
    }
    SDL_FillRect(outSurface, NULL, 0);

    SDL_Rect srcRect;
    srcRect.x = 0;
    srcRect.y = 0;
    srcRect.w = spriteWidth * 2;
    srcRect.h = fontHeight * 2 + 8;
    if (srcRect.w > this->width)
    {
        srcRect.w = this->width;
    }
    if (srcRect.h > this->height)
    {
        srcRect.h = this->height;
    }

    CopyTextBufferBoxFiltered(this->buffer, srcRect, outSurface);

#if defined(TH07_PSP)
    Th07PspMarkTextTexture(outTexture);
#endif
    g_Supervisor.gfxDevice->BindTexture(outTexture);
    g_Supervisor.gfxDevice->SetTextureSubImage(0, yPos, outSurface->w, uploadHeight,
                                               outSurface->pixels);
    SDL_FreeSurface(outSurface);
    return true;
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
    g_TextWorkBuffer.ReleaseBuffer();
    TTF_CloseFont(g_Font);
    g_Font = nullptr;
    TTF_Quit();
}

void TextHelper::RenderTextToTextureBold(i32 xPos, i32 yPos, i32 spriteWidth, i32 spriteHeight,
                                         i32 fontHeight, i32 fontWidth, u32 textColor,
                                         u32 outlineType, char *string, GfxTextureHandle outTexture)
{
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

    TTF_SetFontSize(g_Font, fontSize);

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

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface *textSurf = TTF_RenderUTF8_Blended(g_Font, convStr, white);
#if defined(TH07_PSP_DIRECT_GAME)
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

    if (!g_TextWorkBuffer.AllocateBuffer(dWidth, dHeight))
    {
#if defined(TH07_PSP)
        th07_psp_boot_note("text work surface allocation failed");
#endif
        SDL_FreeSurface(textSurf);
        return;
    }

    TextHelper &textHelper = g_TextWorkBuffer;
    SDL_SetSurfaceBlendMode(textSurf, SDL_BLENDMODE_BLEND);

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

    SDL_FreeSurface(textSurf);

    textHelper.InvertAlpha(0, 0, spriteWidth << 1, fontHeight * 2 + 8,
                           (u32)(outlineType == 0xffffffff));
    textHelper.CopyTextToTexture(yPos, spriteWidth, spriteHeight, fontHeight, fontWidth,
                                 outTexture);
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

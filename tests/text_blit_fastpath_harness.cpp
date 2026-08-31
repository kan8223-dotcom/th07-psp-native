#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
constexpr int kWorkWidth = 1024;
constexpr int kWorkHeight = 38;
constexpr int kUploadWidth = 512;
constexpr int kUploadHeight = 16;
constexpr uint8_t kPaddingByte = 0xa5;

struct SurfaceStorage
{
    std::vector<uint8_t> bytes;
    SDL_Surface *surface = nullptr;

    SurfaceStorage(int width, int height, int pitch, Uint32 format)
        : bytes(static_cast<size_t>(pitch) * height, kPaddingByte)
    {
        surface = SDL_CreateRGBSurfaceWithFormatFrom(bytes.data(), width, height, 32, pitch,
                                                     format);
    }

    ~SurfaceStorage()
    {
        SDL_FreeSurface(surface);
    }

    SurfaceStorage(const SurfaceStorage &) = delete;
    SurfaceStorage &operator=(const SurfaceStorage &) = delete;
};

struct Case
{
    int glyphWidth;
    int glyphHeight;
    int glyphPitchPadding;
    int workPitchPadding;
    int xPos;
    uint32_t outlineType;
    uint32_t textColor;
    SDL_Rect clip;
    int alphaPattern;
    uint32_t seed;
};

uint32_t NextRandom(uint32_t &state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Exact for every product in [0, 255 * 255]. This is the rounding used by
// SDL's ARGB8888 -> ABGR8888 modulated slow blitter on the PSP SDL build.
uint8_t DivideBy255(uint32_t product)
{
    const uint32_t biased = product + 1u;
    return static_cast<uint8_t>((biased + (biased >> 8)) >> 8);
}

void FillGlyph(SurfaceStorage &storage, const Case &testCase)
{
    uint32_t random = testCase.seed;
    SDL_Surface *surface = storage.surface;
    for (int y = 0; y < surface->h; ++y)
    {
        uint8_t *row = static_cast<uint8_t *>(surface->pixels) + y * surface->pitch;
        for (int x = 0; x < surface->w; ++x)
        {
            uint8_t alpha;
            switch (testCase.alphaPattern)
            {
            case 0:
                alpha = static_cast<uint8_t>(x & 0xff); // exhaustive alpha ramp
                break;
            case 1:
                alpha = ((x / 3 + y / 2) & 1) ? 255 : 0; // thin hard edges
                break;
            case 2:
            {
                const int cx = surface->w / 2;
                const int cy = surface->h / 2;
                const int distance = std::abs(x - cx) * 3 + std::abs(y - cy) * 11;
                alpha = static_cast<uint8_t>(std::max(0, 255 - distance));
                break;
            }
            case 3:
            {
                static constexpr uint8_t edges[] = {0, 1, 2, 63, 127, 128, 129, 253, 254, 255};
                alpha = edges[(x + y * surface->w) % std::size(edges)];
                break;
            }
            default:
                alpha = static_cast<uint8_t>(NextRandom(random));
                break;
            }

            // TTF_RenderUTF8_Blended returns ARGB8888. On the little-endian
            // PSP and x86 test host its byte order is B,G,R,A. Use varying RGB
            // too, so the candidate cannot accidentally assume white glyphs.
            row[x * 4 + 0] = static_cast<uint8_t>(x * 17 + y * 29 + testCase.seed);
            row[x * 4 + 1] = static_cast<uint8_t>(x * 43 + y * 7 + (testCase.seed >> 8));
            row[x * 4 + 2] = static_cast<uint8_t>(x * 5 + y * 61 + (testCase.seed >> 16));
            row[x * 4 + 3] = alpha;
        }
    }
}

void ClearActivePixels(SurfaceStorage &storage, int activeWidth, int activeHeight)
{
    SDL_Surface *surface = storage.surface;
    for (int y = 0; y < activeHeight; ++y)
    {
        std::memset(static_cast<uint8_t *>(surface->pixels) + y * surface->pitch, 0,
                    static_cast<size_t>(activeWidth) * 4);
    }
}

std::array<SDL_Point, 5> LayerOffsets(const Case &testCase)
{
    if (testCase.outlineType != 0xffffffffu)
    {
        return {{{testCase.xPos * 2 + 4, 2},
                 {testCase.xPos * 2 + 0, 2},
                 {testCase.xPos * 2 + 2, 0},
                 {testCase.xPos * 2 + 2, 4},
                 {testCase.xPos * 2 + 2, 2}}};
    }
    return {{{testCase.xPos * 2 + 3, 2},
             {testCase.xPos * 2 + 1, 2},
             {testCase.xPos * 2 + 2, 1},
             {testCase.xPos * 2 + 2, 3},
             {testCase.xPos * 2 + 2, 2}}};
}

bool CompositeReference(SDL_Surface *glyph, SDL_Surface *work, const Case &testCase)
{
    if (SDL_SetSurfaceBlendMode(glyph, SDL_BLENDMODE_BLEND) != 0 ||
        SDL_SetClipRect(work, &testCase.clip) == SDL_FALSE)
    {
        return false;
    }
    const auto offsets = LayerOffsets(testCase);
    if (SDL_SetSurfaceColorMod(glyph, 0, 0, 0) != 0)
    {
        return false;
    }
    for (int layer = 0; layer < 4; ++layer)
    {
        SDL_Rect destination = {offsets[layer].x, offsets[layer].y, glyph->w, glyph->h};
        if (SDL_BlitSurface(glyph, nullptr, work, &destination) != 0)
        {
            return false;
        }
    }
    const uint8_t red = static_cast<uint8_t>(testCase.textColor >> 16);
    const uint8_t green = static_cast<uint8_t>(testCase.textColor >> 8);
    const uint8_t blue = static_cast<uint8_t>(testCase.textColor);
    if (SDL_SetSurfaceColorMod(glyph, red, green, blue) != 0)
    {
        return false;
    }
    SDL_Rect destination = {offsets[4].x, offsets[4].y, glyph->w, glyph->h};
    return SDL_BlitSurface(glyph, nullptr, work, &destination) == 0;
}

void BlendSlowPathPixel(const uint8_t *source, uint8_t *destination, uint8_t modR,
                        uint8_t modG, uint8_t modB)
{
    const uint8_t alpha = source[3];
    const uint8_t inverseAlpha = static_cast<uint8_t>(255u - alpha);
    const uint8_t sourceR = DivideBy255(static_cast<uint32_t>(source[2]) * modR);
    const uint8_t sourceG = DivideBy255(static_cast<uint32_t>(source[1]) * modG);
    const uint8_t sourceB = DivideBy255(static_cast<uint32_t>(source[0]) * modB);
    destination[0] = static_cast<uint8_t>(
        DivideBy255(static_cast<uint32_t>(sourceR) * alpha) +
        DivideBy255(static_cast<uint32_t>(destination[0]) * inverseAlpha));
    destination[1] = static_cast<uint8_t>(
        DivideBy255(static_cast<uint32_t>(sourceG) * alpha) +
        DivideBy255(static_cast<uint32_t>(destination[1]) * inverseAlpha));
    destination[2] = static_cast<uint8_t>(
        DivideBy255(static_cast<uint32_t>(sourceB) * alpha) +
        DivideBy255(static_cast<uint32_t>(destination[2]) * inverseAlpha));
    destination[3] = static_cast<uint8_t>(
        alpha + DivideBy255(static_cast<uint32_t>(destination[3]) * inverseAlpha));
}

void CompositeLayerExact(SDL_Surface *glyph, SDL_Surface *work, const SDL_Rect &activeClip,
                         int destinationX, int destinationY, uint8_t modR, uint8_t modG,
                         uint8_t modB, bool blackOutline)
{
    const int left = std::max(destinationX, activeClip.x);
    const int top = std::max(destinationY, activeClip.y);
    const int right = std::min(destinationX + glyph->w, activeClip.x + activeClip.w);
    const int bottom = std::min(destinationY + glyph->h, activeClip.y + activeClip.h);
    for (int y = top; y < bottom; ++y)
    {
        const uint8_t *source = static_cast<const uint8_t *>(glyph->pixels) +
                                (y - destinationY) * glyph->pitch +
                                (left - destinationX) * 4;
        uint8_t *destination =
            static_cast<uint8_t *>(work->pixels) + y * work->pitch + left * 4;
        for (int x = left; x < right; ++x)
        {
            if (blackOutline)
            {
                const uint8_t alpha = source[3];
                destination[3] = static_cast<uint8_t>(
                    alpha + DivideBy255(static_cast<uint32_t>(destination[3]) *
                                        (255u - alpha)));
            }
            else
            {
                BlendSlowPathPixel(source, destination, modR, modG, modB);
            }
            source += 4;
            destination += 4;
        }
    }
}

// Test model for the intended production API. It validates every fallible
// precondition before touching dst, then replaces the five SDL blits with five
// narrow raw-pixel passes in the same order.
bool CompositeBoldTextSurfaceExact(SDL_Surface *glyph, SDL_Surface *work, int activeWidth,
                                   int activeHeight, int baseX, uint32_t textColor,
                                   bool compactOutline)
{
    if (!glyph || !work || glyph->format->format != SDL_PIXELFORMAT_ARGB8888 ||
        work->format->format != SDL_PIXELFORMAT_RGBA32 || glyph->format->BytesPerPixel != 4 ||
        work->format->BytesPerPixel != 4 || !glyph->pixels || !work->pixels || glyph == work ||
        activeWidth <= 0 || activeHeight <= 0 || activeWidth > work->w ||
        activeHeight > work->h ||
        glyph->pitch < glyph->w * 4 || work->pitch < work->w * 4 ||
        SDL_BYTEORDER != SDL_LIL_ENDIAN || (textColor & 0x00ffffffu) == 0x00ffffffu)
    {
        return false;
    }
    if (SDL_LockSurface(glyph) != 0)
    {
        return false;
    }
    if (SDL_LockSurface(work) != 0)
    {
        SDL_UnlockSurface(glyph);
        return false;
    }

    SDL_Rect activeBounds = {0, 0, activeWidth, activeHeight};
    SDL_Rect activeClip = {};
    SDL_IntersectRect(&activeBounds, &work->clip_rect, &activeClip);

    const std::array<SDL_Point, 4> outlineOffsets = compactOutline
                                                        ? std::array<SDL_Point, 4>{
                                                              {{3, 2}, {1, 2}, {2, 1}, {2, 3}}}
                                                        : std::array<SDL_Point, 4>{
                                                              {{4, 2}, {0, 2}, {2, 0}, {2, 4}}};
    for (const SDL_Point &offset : outlineOffsets)
    {
        CompositeLayerExact(glyph, work, activeClip, baseX + offset.x, offset.y, 0, 0, 0,
                            true);
    }
    CompositeLayerExact(glyph, work, activeClip, baseX + 2, 2,
                        static_cast<uint8_t>(textColor >> 16),
                        static_cast<uint8_t>(textColor >> 8),
                        static_cast<uint8_t>(textColor), false);
    SDL_UnlockSurface(work);
    SDL_UnlockSurface(glyph);
    return true;
}

void InvertAlpha(SDL_Surface *surface, int activeWidth, int activeHeight, uint32_t outlineType)
{
    const int doubleArea = activeWidth * activeHeight * 2;
    const bool alternate = outlineType == 0xffffffffu;
    for (int y = 0; y < activeHeight; ++y)
    {
        uint8_t *row = static_cast<uint8_t *>(surface->pixels) + y * surface->pitch;
        for (int x = 0; x < activeWidth; ++x)
        {
            uint8_t *pixel = row + x * 4;
            uint8_t red = pixel[0];
            uint8_t green = pixel[1];
            uint8_t blue = pixel[2];
            if (pixel[3] == 0)
            {
                std::memset(pixel, 0, 4);
                continue;
            }
            const int index = (y * activeWidth + x) * 2;
            if (!alternate)
            {
                if (red >= blue)
                {
                    red = static_cast<uint8_t>(red - (red * index * 2) / doubleArea / 3);
                    green = static_cast<uint8_t>(green - (green * index * 2) / doubleArea / 3);
                }
                else
                {
                    blue = static_cast<uint8_t>(blue - (blue * index) / doubleArea / 2);
                    green = static_cast<uint8_t>(green - (green * index) / doubleArea / 2);
                }
            }
            else if (red >= blue)
            {
                red = static_cast<uint8_t>(red - (red * index) / doubleArea / 4);
                green = static_cast<uint8_t>(green - (green * index) / doubleArea / 4);
            }
            else
            {
                blue = static_cast<uint8_t>(blue - (blue * index) / doubleArea / 4);
                green = static_cast<uint8_t>(green - (green * index) / doubleArea / 4);
            }
            pixel[0] = red;
            pixel[1] = green;
            pixel[2] = blue;
        }
    }
}

void BoxFilter(SDL_Surface *source, int activeWidth, int activeHeight, SDL_Surface *destination)
{
    for (int y = 0; y < destination->h; ++y)
    {
        const int sourceY0 = y * activeHeight / destination->h;
        const int sourceY1 = ((y + 1) * activeHeight + destination->h - 1) /
                            destination->h;
        uint8_t *destinationRow =
            static_cast<uint8_t *>(destination->pixels) + y * destination->pitch;
        for (int x = 0; x < destination->w; ++x)
        {
            const int sourceX0 = x * activeWidth / destination->w;
            const int sourceX1 =
                ((x + 1) * activeWidth + destination->w - 1) / destination->w;
            uint32_t sums[4] = {};
            uint32_t count = 0;
            for (int sourceY = sourceY0; sourceY < sourceY1; ++sourceY)
            {
                const uint8_t *sourceRow =
                    static_cast<const uint8_t *>(source->pixels) + sourceY * source->pitch;
                for (int sourceX = sourceX0; sourceX < sourceX1; ++sourceX)
                {
                    const uint8_t *pixel = sourceRow + sourceX * 4;
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        sums[channel] += pixel[channel];
                    }
                    ++count;
                }
            }
            for (int channel = 0; channel < 4; ++channel)
            {
                destinationRow[x * 4 + channel] = static_cast<uint8_t>(sums[channel] / count);
            }
        }
    }
}

bool CompareBytes(const std::vector<uint8_t> &expected, const std::vector<uint8_t> &actual,
                  const char *stage, int caseIndex)
{
    if (expected == actual)
    {
        return true;
    }
    const auto mismatch = std::mismatch(expected.begin(), expected.end(), actual.begin());
    const size_t offset = static_cast<size_t>(mismatch.first - expected.begin());
    std::fprintf(stderr, "case %d %s mismatch at byte %zu: %u != %u\n", caseIndex, stage,
                 offset, static_cast<unsigned>(*mismatch.first),
                 static_cast<unsigned>(*mismatch.second));
    return false;
}

bool RunCase(const Case &testCase, int caseIndex)
{
    const int glyphPitch = testCase.glyphWidth * 4 + testCase.glyphPitchPadding;
    constexpr int kExtraWorkWidth = 13;
    constexpr int kExtraWorkHeight = 7;
    const int workPitch = (kWorkWidth + kExtraWorkWidth) * 4 + testCase.workPitchPadding;
    SurfaceStorage glyph(testCase.glyphWidth, testCase.glyphHeight, glyphPitch,
                         SDL_PIXELFORMAT_ARGB8888);
    SurfaceStorage referenceWork(kWorkWidth + kExtraWorkWidth, kWorkHeight + kExtraWorkHeight,
                                 workPitch, SDL_PIXELFORMAT_RGBA32);
    SurfaceStorage candidateWork(kWorkWidth + kExtraWorkWidth, kWorkHeight + kExtraWorkHeight,
                                 workPitch, SDL_PIXELFORMAT_RGBA32);
    if (!glyph.surface || !referenceWork.surface || !candidateWork.surface)
    {
        std::fprintf(stderr, "surface allocation failed: %s\n", SDL_GetError());
        return false;
    }
    FillGlyph(glyph, testCase);
    ClearActivePixels(referenceWork, kWorkWidth, kWorkHeight);
    ClearActivePixels(candidateWork, kWorkWidth, kWorkHeight);
    SDL_SetClipRect(candidateWork.surface, &testCase.clip);
    if (!CompositeReference(glyph.surface, referenceWork.surface, testCase))
    {
        std::fprintf(stderr, "case %d composite setup failed: %s\n", caseIndex,
                     SDL_GetError());
        return false;
    }
    const bool candidateAccepted = CompositeBoldTextSurfaceExact(
        glyph.surface, candidateWork.surface, kWorkWidth, kWorkHeight, testCase.xPos * 2,
        testCase.textColor, testCase.outlineType == 0xffffffffu);
    if ((testCase.textColor & 0x00ffffffu) == 0x00ffffffu)
    {
        // Setting color modulation back to white removes SDL's MODULATE_COLOR
        // flag and changes its body layer to a /256 packed-alpha blitter. The
        // custom /255 slow-path compositor must reject before touching dst.
        SurfaceStorage untouched(kWorkWidth + kExtraWorkWidth,
                                 kWorkHeight + kExtraWorkHeight, workPitch,
                                 SDL_PIXELFORMAT_RGBA32);
        if (!untouched.surface)
        {
            return false;
        }
        ClearActivePixels(untouched, kWorkWidth, kWorkHeight);
        if (candidateAccepted || candidateWork.bytes != untouched.bytes ||
            !CompositeReference(glyph.surface, candidateWork.surface, testCase))
        {
            std::fprintf(stderr, "case %d white-body fallback failed\n", caseIndex);
            return false;
        }
    }
    else if (!candidateAccepted)
    {
        std::fprintf(stderr, "case %d candidate unexpectedly rejected\n", caseIndex);
        return false;
    }
    if (!CompareBytes(referenceWork.bytes, candidateWork.bytes, "RGBA-work", caseIndex))
    {
        return false;
    }

    InvertAlpha(referenceWork.surface, kWorkWidth, kWorkHeight, testCase.outlineType);
    InvertAlpha(candidateWork.surface, kWorkWidth, kWorkHeight, testCase.outlineType);
    SurfaceStorage referenceUpload(kUploadWidth, kUploadHeight, kUploadWidth * 4 + 20,
                                   SDL_PIXELFORMAT_RGBA32);
    SurfaceStorage candidateUpload(kUploadWidth, kUploadHeight, kUploadWidth * 4 + 20,
                                   SDL_PIXELFORMAT_RGBA32);
    if (!referenceUpload.surface || !candidateUpload.surface)
    {
        return false;
    }
    BoxFilter(referenceWork.surface, kWorkWidth, kWorkHeight, referenceUpload.surface);
    BoxFilter(candidateWork.surface, kWorkWidth, kWorkHeight, candidateUpload.surface);
    return CompareBytes(referenceUpload.bytes, candidateUpload.bytes, "512x16-upload",
                        caseIndex);
}

bool RunFallbackCases()
{
    SurfaceStorage validGlyph(17, 9, 17 * 4 + 4, SDL_PIXELFORMAT_ARGB8888);
    SurfaceStorage wrongGlyph(17, 9, 17 * 4 + 4, SDL_PIXELFORMAT_RGBA32);
    SurfaceStorage validWork(32, 16, 32 * 4 + 8, SDL_PIXELFORMAT_RGBA32);
    SurfaceStorage wrongWork(32, 16, 32 * 4 + 8, SDL_PIXELFORMAT_ARGB8888);
    if (!validGlyph.surface || !wrongGlyph.surface || !validWork.surface || !wrongWork.surface)
    {
        return false;
    }
    const auto validBefore = validWork.bytes;
    const auto wrongBefore = wrongWork.bytes;
    const bool rejected =
        !CompositeBoldTextSurfaceExact(nullptr, validWork.surface, 32, 16, 0, 0x123456u,
                                       false) &&
        !CompositeBoldTextSurfaceExact(validGlyph.surface, nullptr, 32, 16, 0, 0x123456u,
                                       false) &&
        !CompositeBoldTextSurfaceExact(wrongGlyph.surface, validWork.surface, 32, 16, 0,
                                       0x123456u, false) &&
        !CompositeBoldTextSurfaceExact(validGlyph.surface, wrongWork.surface, 32, 16, 0,
                                       0x123456u, false) &&
        !CompositeBoldTextSurfaceExact(validWork.surface, validWork.surface, 32, 16, 0,
                                       0x123456u, false) &&
        !CompositeBoldTextSurfaceExact(validGlyph.surface, validWork.surface, 33, 16, 0,
                                       0x123456u, false) &&
        !CompositeBoldTextSurfaceExact(validGlyph.surface, validWork.surface, 32, 17, 0,
                                       0x123456u, false) &&
        !CompositeBoldTextSurfaceExact(validGlyph.surface, validWork.surface, 32, 16, 0,
                                       0xffffffu, false);
    return rejected && validWork.bytes == validBefore && wrongWork.bytes == wrongBefore;
}

std::vector<Case> BuildCases()
{
    std::vector<Case> cases;
    const std::array<uint32_t, 2> outlines = {0u, 0xffffffffu};
    const std::array<uint32_t, 7> colors = {0x000000u, 0xffffffu, 0xff0000u, 0x00ff00u,
                                           0x0000ffu, 0xa5123456u, 0xf17bc3u};
    const std::array<int, 8> xPositions = {-5, -2, 0, 1, 127, 390, 500, 511};
    const std::array<SDL_Rect, 5> clips = {{{0, 0, 1024, 38},
                                           {3, 1, 1018, 36},
                                           {0, 0, 19, 7},
                                           {997, 25, 27, 13},
                                           {211, 9, 613, 21}}};
    int index = 0;
    for (uint32_t outline : outlines)
    {
        for (uint32_t color : colors)
        {
            const int alphaPattern = index % 5;
            cases.push_back({257 + index % 263,
                             11 + index % 23,
                             (index % 5) * 4,
                             (index % 7) * 4,
                             xPositions[index % xPositions.size()],
                             outline,
                             color,
                             clips[index % clips.size()],
                             alphaPattern,
                             0x9e3779b9u ^ static_cast<uint32_t>(index * 0x10203)});
            ++index;
        }
    }

    uint32_t random = 0x74f06a35u;
    for (int i = 0; i < 96; ++i)
    {
        const uint32_t seed = NextRandom(random);
        const int width = 1 + static_cast<int>(NextRandom(random) % 760);
        const int height = 1 + static_cast<int>(NextRandom(random) % 46);
        const int x = -12 + static_cast<int>(NextRandom(random) % 540);
        const int clipX = static_cast<int>(NextRandom(random) % 1024);
        const int clipY = static_cast<int>(NextRandom(random) % 38);
        const int clipW = 1 + static_cast<int>(NextRandom(random) % (1024 - clipX));
        const int clipH = 1 + static_cast<int>(NextRandom(random) % (38 - clipY));
        cases.push_back({width,
                         height,
                         static_cast<int>(NextRandom(random) % 9) * 4,
                         static_cast<int>(NextRandom(random) % 9) * 4,
                         x,
                         (NextRandom(random) & 1) ? 0u : 0xffffffffu,
                         NextRandom(random) & 0x00ffffffu,
                         {clipX, clipY, clipW, clipH},
                         static_cast<int>(NextRandom(random) % 5),
                         seed});
    }
    return cases;
}
} // namespace

int main()
{
    static_assert(SDL_BYTEORDER == SDL_LIL_ENDIAN,
                  "the PSP-equivalence harness requires little-endian RGBA byte order");
    if (SDL_Init(0) != 0)
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }
    for (uint32_t product = 0; product <= 255u * 255u; ++product)
    {
        if (DivideBy255(product) != product / 255u)
        {
            std::fprintf(stderr, "DivideBy255 mismatch at %u\n", product);
            SDL_Quit();
            return 1;
        }
    }
    const std::vector<Case> cases = BuildCases();
    if (!RunFallbackCases())
    {
        std::fprintf(stderr, "fastpath fallback changed destination bytes\n");
        SDL_Quit();
        return 1;
    }
    for (size_t index = 0; index < cases.size(); ++index)
    {
        if (!RunCase(cases[index], static_cast<int>(index)))
        {
            SDL_Quit();
            return 1;
        }
    }
    SDL_Quit();
    std::printf("text blit fastpath: %zu cases byte-exact\n", cases.size());
    return 0;
}

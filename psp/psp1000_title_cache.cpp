#include "psp1000_title_cache.hpp"

#if defined(TH07_PSP_1000)

#include "psp1000_arena.hpp"
#include "fileio.hpp"
#include "../src/AnmManager.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
constexpr char kCacheName[] = "title01.psp1000.cache";
constexpr char kTempName[] = "title01.psp1000.tmp";
constexpr char kMagic[8] = {'T', 'H', '0', '7', '1', 'K', 'A', '7'};
constexpr u32 kVersion = 6;
constexpr u32 kFnvOffset = 2166136261u;
constexpr u32 kFnvPrime = 16777619u;

struct CacheHeader
{
    char magic[8];
    u32 version;
    u32 sourceBytes;
    u32 payloadBytes;
    u32 checksum;
};

u32 UpdateChecksum(u32 checksum, const void *data, std::size_t bytes)
{
    const auto *cursor = static_cast<const unsigned char *>(data);
    while (bytes--)
    {
        checksum ^= *cursor++;
        checksum *= kFnvPrime;
    }
    return checksum;
}

bool WritePayload(FILE *file, const void *data, std::size_t bytes, u32 *checksum)
{
    if (std::fwrite(data, 1, bytes, file) != bytes)
        return false;
    *checksum = UpdateChecksum(*checksum, data, bytes);
    return true;
}

bool ValidatePayload(const unsigned char *payload, std::size_t payloadBytes)
{
    std::size_t offset = 0;
    u32 entries = 0;
    for (;;)
    {
        if (offset > payloadBytes || payloadBytes - offset < sizeof(AnmRawEntry))
            return false;
        const auto *entry = reinterpret_cast<const AnmRawEntry *>(payload + offset);
        const std::size_t span =
            entry->nextOffset > 0 ? static_cast<std::size_t>(entry->nextOffset)
                                  : payloadBytes - offset;
        if (entry->version != 2 || !entry->hasData || entry->textureOffset < 0 ||
            span > payloadBytes - offset ||
            static_cast<std::size_t>(entry->textureOffset) > span ||
            span - static_cast<std::size_t>(entry->textureOffset) <
                sizeof(ZunImageInfoEmbedded))
            return false;
        const auto *image = reinterpret_cast<const ZunImageInfoEmbedded *>(
            payload + offset + static_cast<std::size_t>(entry->textureOffset));
        const bool normalImage = image->unused_c == TH07_PSP_1000_TITLE_IMAGE_MARKER &&
                                 image->width <= 256 && image->height <= 256;
        const bool highResolutionImage =
            image->unused_c == TH07_PSP_1000_TITLE_HIRES_IMAGE_MARKER &&
            image->width <= 512 && image->height <= 256;
        if (image->format != 5 || image->width <= 0 || image->height <= 0 ||
            (!normalImage && !highResolutionImage))
            return false;
        const std::size_t pixels =
            static_cast<std::size_t>(image->width) * static_cast<std::size_t>(image->height);
        if (pixels > (span - static_cast<std::size_t>(entry->textureOffset) -
                      sizeof(ZunImageInfoEmbedded)) /
                         2u)
            return false;
        ++entries;
        if (entries > 50)
            return false;
        if (entry->nextOffset == 0)
            return offset + span == payloadBytes;
        offset += span;
    }
}

void ResolveCachePath(const char *name, char *out, std::size_t outBytes)
{
    th07_psp_resolve_path(name, out, outBytes);
}
} // namespace

unsigned char *th07_psp_1000_load_title_cache(std::size_t sourceBytes,
                                               std::size_t *cacheBytes)
{
    if (cacheBytes)
        *cacheBytes = 0;
    char path[768];
    ResolveCachePath(kCacheName, path, sizeof(path));
    FILE *file = std::fopen(path, "rb");
    if (!file)
        return nullptr;

    CacheHeader header{};
    const bool headerValid =
        std::fread(&header, 1, sizeof(header), file) == sizeof(header) &&
        std::memcmp(header.magic, kMagic, sizeof(kMagic)) == 0 && header.version == kVersion &&
        header.sourceBytes == sourceBytes && header.payloadBytes >= sizeof(AnmRawEntry) &&
        header.payloadBytes <= th07_psp_1000_arena_capacity();
    if (!headerValid)
    {
        std::fclose(file);
        std::remove(path);
        th07_psp_boot_note("PSP1000 title cache header invalid");
        return nullptr;
    }

    auto *payload = static_cast<unsigned char *>(
        th07_psp_1000_acquire_anm(static_cast<std::size_t>(header.payloadBytes)));
    bool loaded = payload &&
                  std::fread(payload, 1, header.payloadBytes, file) == header.payloadBytes;
    std::fclose(file);
    loaded = loaded && UpdateChecksum(kFnvOffset, payload, header.payloadBytes) == header.checksum &&
             ValidatePayload(payload, header.payloadBytes);
    if (!loaded)
    {
        if (payload)
            th07_psp_1000_release_anm(payload);
        std::remove(path);
        th07_psp_boot_note("PSP1000 title cache data invalid");
        return nullptr;
    }
    if (cacheBytes)
        *cacheBytes = header.payloadBytes;
    th07_psp_boot_notef("PSP1000 title cache loaded %uK", header.payloadBytes / 1024u);
    return payload;
}

bool th07_psp_1000_build_title_cache(const void *source, std::size_t sourceBytes)
{
    if (!source || sourceBytes < sizeof(AnmRawEntry))
        return false;

    char finalPath[768];
    char tempPath[768];
    ResolveCachePath(kCacheName, finalPath, sizeof(finalPath));
    ResolveCachePath(kTempName, tempPath, sizeof(tempPath));
    FILE *file = std::fopen(tempPath, "wb");
    if (!file)
    {
        th07_psp_boot_note("PSP1000 title cache create failed");
        return false;
    }

    CacheHeader header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kVersion;
    header.sourceBytes = static_cast<u32>(sourceBytes);
    bool ok = std::fwrite(&header, 1, sizeof(header), file) == sizeof(header);
    std::size_t sourceOffset = 0;
    u32 checksum = kFnvOffset;
    u32 payloadBytes = 0;
    u32 entryCount = 0;
    u16 converted[1024];
    const auto *sourceBase = static_cast<const unsigned char *>(source);

    while (ok)
    {
        if (sourceOffset > sourceBytes || sourceBytes - sourceOffset < sizeof(AnmRawEntry))
        {
            ok = false;
            break;
        }
        const auto *sourceEntry =
            reinterpret_cast<const AnmRawEntry *>(sourceBase + sourceOffset);
        const std::size_t sourceSpan =
            sourceEntry->nextOffset > 0 ? static_cast<std::size_t>(sourceEntry->nextOffset)
                                        : sourceBytes - sourceOffset;
        if (sourceEntry->version != 2 || !sourceEntry->hasData ||
            sourceEntry->textureOffset < static_cast<i32>(sizeof(AnmRawEntry)) ||
            sourceSpan > sourceBytes - sourceOffset ||
            static_cast<std::size_t>(sourceEntry->textureOffset) > sourceSpan ||
            sourceSpan - static_cast<std::size_t>(sourceEntry->textureOffset) <
                sizeof(ZunImageInfoEmbedded))
        {
            ok = false;
            break;
        }
        const auto *sourceImage = reinterpret_cast<const ZunImageInfoEmbedded *>(
            sourceBase + sourceOffset + static_cast<std::size_t>(sourceEntry->textureOffset));
#if defined(TH07_PSP_PERF_DIAG)
        th07_psp_boot_notef("TITLE CACHE E%u F%d %dx%d SP%uK", entryCount,
                            static_cast<int>(sourceImage->format),
                            static_cast<int>(sourceImage->width),
                            static_cast<int>(sourceImage->height),
                            static_cast<unsigned int>(sourceSpan / 1024u));
#endif
        if ((sourceImage->format != 1 && sourceImage->format != 5) ||
            sourceImage->width <= 0 || sourceImage->height <= 0)
        {
            ok = false;
            break;
        }
        const std::size_t sourcePixelCount = static_cast<std::size_t>(sourceImage->width) *
                                             static_cast<std::size_t>(sourceImage->height);
        const std::size_t sourceBytesPerPixel = sourceImage->format == 1 ? 4u : 2u;
        const std::size_t sourcePixelBytes = sourcePixelCount * sourceBytesPerPixel;
        if (sourcePixelBytes > sourceSpan - static_cast<std::size_t>(sourceEntry->textureOffset) -
                                   sizeof(ZunImageInfoEmbedded))
        {
            ok = false;
            break;
        }

        const std::size_t metadataBytes = static_cast<std::size_t>(sourceEntry->textureOffset);
        const bool imageNameInMetadata =
            sourceEntry->nameOffset >= 0 &&
            static_cast<std::size_t>(sourceEntry->nameOffset) < metadataBytes;
        const char *imageName = imageNameInMetadata
                                    ? reinterpret_cast<const char *>(
                                          sourceBase + sourceOffset +
                                          static_cast<std::size_t>(sourceEntry->nameOffset))
                                    : "";
        const bool imageNameTerminated =
            imageNameInMetadata &&
            std::memchr(imageName, '\0',
                        metadataBytes - static_cast<std::size_t>(sourceEntry->nameOffset));
        // Preserve horizontal detail in the three atlases visible in the
        // title and difficulty screenshots.  512x256 costs only 128 KiB more
        // than the old 256x256 copy; retaining full 512x512 selection atlases
        // exceeds the measured PSP-1000 JPEG-decode headroom.
        const bool keepWideUiAtlas =
            imageNameTerminated &&
            (std::strcmp(imageName, "data/title/title02.png") == 0 ||
             std::strcmp(imageName, "data/title/title01.png") == 0 ||
             std::strcmp(imageName, "data/title/select01.png") == 0) &&
            sourceImage->width <= 512;
        const u32 cacheWidth = keepWideUiAtlas
                                   ? static_cast<u32>(sourceImage->width)
                                   : (sourceImage->width > 256 ? 256u : sourceImage->width);
        const u32 cacheHeight =
            sourceImage->height > 256 ? 256u : static_cast<u32>(sourceImage->height);
        const std::size_t cachePixelCount =
            static_cast<std::size_t>(cacheWidth) * static_cast<std::size_t>(cacheHeight);
        const std::size_t cacheSpan =
            (metadataBytes + sizeof(ZunImageInfoEmbedded) + cachePixelCount * 2u + 3u) & ~3u;
        auto *metadata = static_cast<unsigned char *>(std::malloc(metadataBytes));
        if (!metadata)
        {
            ok = false;
            break;
        }
        std::memcpy(metadata, sourceEntry, metadataBytes);
        auto *cacheEntry = reinterpret_cast<AnmRawEntry *>(metadata);
        cacheEntry->format = 5;
        cacheEntry->nextOffset = sourceEntry->nextOffset ? static_cast<i32>(cacheSpan) : 0;
        ok = WritePayload(file, metadata, metadataBytes, &checksum);
        std::free(metadata);

        ZunImageInfoEmbedded cacheImage = *sourceImage;
        cacheImage.format = 5;
        cacheImage.width = static_cast<i16>(cacheWidth);
        cacheImage.height = static_cast<i16>(cacheHeight);
        cacheImage.unused_c = keepWideUiAtlas
                                  ? TH07_PSP_1000_TITLE_HIRES_IMAGE_MARKER
                                  : TH07_PSP_1000_TITLE_IMAGE_MARKER;
        ok = ok && WritePayload(file, &cacheImage, sizeof(cacheImage), &checksum);
        const unsigned char *pixels = sourceImage->data;
        for (std::size_t base = 0; ok && base < cachePixelCount; base += 1024u)
        {
            const std::size_t count =
                cachePixelCount - base < 1024u ? cachePixelCount - base : 1024u;
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::size_t cacheIndex = base + i;
                const std::size_t x = cacheIndex % cacheWidth;
                const std::size_t y = cacheIndex / cacheWidth;
                const u32 sourceWidth = static_cast<u32>(sourceImage->width);
                const u32 sourceHeight = static_cast<u32>(sourceImage->height);
                const u32 sourceX0 = static_cast<u32>(x) * sourceWidth / cacheWidth;
                const u32 sourceY0 = static_cast<u32>(y) * sourceHeight / cacheHeight;
                const u32 sourceX1 = std::max(sourceX0 + 1u,
                                              (static_cast<u32>(x) + 1u) * sourceWidth /
                                                  cacheWidth);
                const u32 sourceY1 = std::max(sourceY0 + 1u,
                                              (static_cast<u32>(y) + 1u) * sourceHeight /
                                                  cacheHeight);
                u32 sumA = 0;
                u32 sumPremultipliedR = 0;
                u32 sumPremultipliedG = 0;
                u32 sumPremultipliedB = 0;
                u32 samples = 0;
                for (u32 sourceY = sourceY0; sourceY < sourceY1; ++sourceY)
                {
                    for (u32 sourceX = sourceX0; sourceX < sourceX1; ++sourceX)
                    {
                        const std::size_t sourceIndex = sourceY * sourceWidth + sourceX;
                        if (sourceImage->format == 5)
                        {
                            u16 argb;
                            std::memcpy(&argb, pixels + sourceIndex * 2u, sizeof(argb));
                            const u32 a = ((argb >> 12) & 15u) * 17u;
                            const u32 r = ((argb >> 8) & 15u) * 17u;
                            const u32 g = ((argb >> 4) & 15u) * 17u;
                            const u32 b = (argb & 15u) * 17u;
                            sumA += a;
                            sumPremultipliedR += r * a;
                            sumPremultipliedG += g * a;
                            sumPremultipliedB += b * a;
                        }
                        else
                        {
                            const unsigned char *bgra = pixels + sourceIndex * 4u;
                            const u32 a = bgra[3];
                            sumA += a;
                            sumPremultipliedR += static_cast<u32>(bgra[2]) * a;
                            sumPremultipliedG += static_cast<u32>(bgra[1]) * a;
                            sumPremultipliedB += static_cast<u32>(bgra[0]) * a;
                        }
                        ++samples;
                    }
                }
                const u32 a = sumA / samples;
                const u32 r = sumA ? sumPremultipliedR / sumA : 0u;
                const u32 g = sumA ? sumPremultipliedG / sumA : 0u;
                const u32 b = sumA ? sumPremultipliedB / sumA : 0u;
                converted[i] = static_cast<u16>(((a >> 4) << 12) | ((r >> 4) << 8) |
                                                ((g >> 4) << 4) | (b >> 4));
            }
            ok = WritePayload(file, converted, count * sizeof(u16), &checksum);
        }
        const std::size_t written =
            metadataBytes + sizeof(ZunImageInfoEmbedded) + cachePixelCount * 2u;
        if (ok && cacheSpan > written)
        {
            const u32 zero = 0;
            ok = WritePayload(file, &zero, cacheSpan - written, &checksum);
        }
        if (!ok || cacheSpan > 0xffffffffu - payloadBytes)
        {
            ok = false;
            break;
        }
        payloadBytes += static_cast<u32>(cacheSpan);
        ++entryCount;
        if (!sourceEntry->nextOffset)
            break;
        sourceOffset += sourceSpan;
    }

    header.payloadBytes = payloadBytes;
    header.checksum = checksum;
#if defined(TH07_PSP_PERF_DIAG)
    th07_psp_boot_notef("TITLE CACHE END OK%u E%u P%uK CAP%uK", ok ? 1u : 0u, entryCount,
                        payloadBytes / 1024u,
                        static_cast<unsigned int>(th07_psp_1000_arena_capacity() / 1024u));
#endif
    ok = ok && entryCount > 0 && payloadBytes <= th07_psp_1000_arena_capacity() &&
         std::fseek(file, 0, SEEK_SET) == 0 &&
         std::fwrite(&header, 1, sizeof(header), file) == sizeof(header);
    std::fclose(file);
    if (!ok)
    {
        std::remove(tempPath);
        th07_psp_boot_note("PSP1000 title cache conversion failed");
        return false;
    }
    std::remove(finalPath);
    if (std::rename(tempPath, finalPath) != 0)
    {
        std::remove(tempPath);
        th07_psp_boot_note("PSP1000 title cache rename failed");
        return false;
    }
    th07_psp_boot_notef("PSP1000 title cache built %uK", payloadBytes / 1024u);
    return true;
}

#endif

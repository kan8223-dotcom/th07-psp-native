#include "xmb_selfwrap.hpp"

#include <pspiofilemgr.h>

#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C"
{
#include <jpeglib.h>
#include <png.h>
#include <zlib.h>
}

namespace
{
constexpr std::uint64_t kTh07DatBytes = 23829135ull;
constexpr std::uint64_t kThBgmDatBytes = 444516656ull;
constexpr std::uint32_t kPbpHeaderBytes = 40u;
constexpr std::uint32_t kMaxSfoBytes = 64u * 1024u;
constexpr std::uint32_t kPsfHeaderBytes = 20u;
constexpr std::uint32_t kPsfEntryBytes = 16u;
constexpr std::uint16_t kPsfBinaryFormat = 0x0004u;
constexpr std::uint32_t kIconSlotBytes = 64u * 1024u;
constexpr std::uint32_t kPictureSlotBytes = 512u * 1024u;
constexpr std::size_t kMaxPath = 640u;
constexpr std::size_t kRuntimeDataPath = 512u;
constexpr std::uint32_t kMaxPbgEntries = 8192u;
constexpr std::uint32_t kMaxPbgTableBytes = 2u * 1024u * 1024u;
constexpr std::uint32_t kMaxTitleBytes = 8u * 1024u * 1024u;
constexpr std::uint32_t kMaxStaffJpegBytes = 2u * 1024u * 1024u;
constexpr std::uint32_t kMaxLogoDimension = 1024u;
constexpr std::uint32_t kMaxJpegDimension = 2048u;
constexpr std::size_t kMaxJpegRgbaBytes = 12u * 1024u * 1024u;

constexpr unsigned char kPbpMagic[4] = {0x00u, 'P', 'B', 'P'};
constexpr unsigned char kPngMagic[8] = {
    0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au};
constexpr char kXmbMarker[8] = {'T', 'H', '0', '7', 'X', 'M', 'B', '2'};
constexpr unsigned char kSelfwrapChunkType[4] = {'t', 'h', 'S', 'b'};
constexpr unsigned char kPlaceholderIdentity[8] = {
    'T', 'H', '0', '7', 'P', 'L', 'N', '2'};
constexpr unsigned char kWrappedIdentity[8] = {
    'T', 'H', '0', '7', 'X', 'M', 'B', '2'};
constexpr char kXmbSfoKey[] = "TH07_XMB_SLOT";

enum SelfwrapError
{
    kSelfwrapBadArgument = -1,
    kSelfwrapBadPbp = -2,
    kSelfwrapSourceFailure = -3,
    kSelfwrapImageFailure = -4,
    kSelfwrapReserveFailure = -5,
    kSelfwrapCommitFailure = -6,
    // The running PBP is readable but its backing VFS refuses a write-open.
    // Leave the canonical file untouched and defer self-wrapping; callers
    // continue launching the neutral executable normally.
    kSelfwrapWriteOpenDenied = -7,
    // The fixed media slots contain complete PNGs without our private
    // ownership identity. Never overwrite user/foreign media implicitly.
    kSelfwrapForeignMedia = -8,
};

static_assert(static_cast<int>(kSelfwrapWriteOpenDenied) ==
                  TH07_UNIFIED_SELFWRAP_DEFERRED,
              "launcher/selfwrap deferred result mismatch");

struct ByteBuffer
{
    unsigned char *data;
    std::size_t size;
    std::size_t capacity;
};

struct Image
{
    std::uint32_t width;
    std::uint32_t height;
    unsigned char *rgba;
};

struct PbgEntry
{
    std::uint32_t offset;
    std::uint32_t compressed_size;
    std::uint32_t decompressed_size;
    bool found;
};

struct PbpInfo
{
    unsigned char header[kPbpHeaderBytes];
    std::uint32_t offsets[8];
    std::uint32_t reserve_start;
    std::uint32_t reserve_end;
    std::uint64_t file_size;
    bool foreign_media;
    bool placeholder;
    bool wrapped;
};

enum class PngIdentity
{
    Invalid,
    Foreign,
    Placeholder,
    Wrapped,
};

void FreeBuffer(ByteBuffer *buffer)
{
    if (!buffer) return;
    std::free(buffer->data);
    buffer->data = nullptr;
    buffer->size = 0u;
    buffer->capacity = 0u;
}

void FreeImage(Image *image)
{
    if (!image) return;
    std::free(image->rgba);
    image->rgba = nullptr;
    image->width = 0u;
    image->height = 0u;
}

std::uint16_t ReadLe16(const unsigned char *data)
{
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(data[1] << 8u);
}

std::uint32_t ReadLe32(const unsigned char *data)
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8u) |
           (static_cast<std::uint32_t>(data[2]) << 16u) |
           (static_cast<std::uint32_t>(data[3]) << 24u);
}

std::uint32_t ReadBe32(const unsigned char *data)
{
    return (static_cast<std::uint32_t>(data[0]) << 24u) |
           (static_cast<std::uint32_t>(data[1]) << 16u) |
           (static_cast<std::uint32_t>(data[2]) << 8u) |
           static_cast<std::uint32_t>(data[3]);
}

void WriteBe32(unsigned char *data, std::uint32_t value)
{
    data[0] = static_cast<unsigned char>(value >> 24u);
    data[1] = static_cast<unsigned char>(value >> 16u);
    data[2] = static_cast<unsigned char>(value >> 8u);
    data[3] = static_cast<unsigned char>(value);
}

bool CheckedImageBytes(std::uint32_t width, std::uint32_t height,
                       std::size_t channels, std::size_t *bytes)
{
    if (!width || !height || !channels) return false;
    const std::uint64_t total = static_cast<std::uint64_t>(width) * height * channels;
    if (total > static_cast<std::uint64_t>(SIZE_MAX)) return false;
    *bytes = static_cast<std::size_t>(total);
    return true;
}

bool ReadExact(SceUID fd, void *destination, std::size_t bytes)
{
    auto *out = static_cast<unsigned char *>(destination);
    std::size_t done = 0u;
    while (done < bytes)
    {
        const std::size_t remaining = bytes - done;
        const unsigned int request = remaining > 0x7fffffffu
                                         ? 0x7fffffffu
                                         : static_cast<unsigned int>(remaining);
        const int got = sceIoRead(fd, out + done, request);
        if (got <= 0) return false;
        done += static_cast<std::size_t>(got);
    }
    return true;
}

bool ReadExactAt(SceUID fd, std::uint64_t offset, void *destination,
                 std::size_t bytes)
{
    if (offset > 0x7fffffffffffffffull) return false;
    const SceOff wanted = static_cast<SceOff>(offset);
    if (sceIoLseek(fd, wanted, PSP_SEEK_SET) != wanted) return false;
    return ReadExact(fd, destination, bytes);
}

bool WriteExact(SceUID fd, const void *source, std::size_t bytes)
{
    const auto *in = static_cast<const unsigned char *>(source);
    std::size_t done = 0u;
    while (done < bytes)
    {
        const std::size_t remaining = bytes - done;
        const unsigned int request = remaining > 0x7fffffffu
                                         ? 0x7fffffffu
                                         : static_cast<unsigned int>(remaining);
        const int wrote = sceIoWrite(fd, in + done, request);
        if (wrote <= 0) return false;
        done += static_cast<std::size_t>(wrote);
    }
    return true;
}

bool WriteExactAt(SceUID fd, std::uint64_t offset, const void *source,
                  std::size_t bytes)
{
    if (offset > 0x7fffffffffffffffull) return false;
    const SceOff wanted = static_cast<SceOff>(offset);
    if (sceIoLseek(fd, wanted, PSP_SEEK_SET) != wanted) return false;
    return WriteExact(fd, source, bytes);
}

bool JoinPath(char *out, std::size_t out_size, const char *root,
              const char *leaf)
{
    if (!out || !out_size || !root || !root[0] || !leaf || !leaf[0]) return false;
    const std::size_t root_length = std::strlen(root);
    const bool has_slash = root_length && root[root_length - 1u] == '/';
    const int length = std::snprintf(out, out_size, "%s%s%s", root,
                                     has_slash ? "" : "/", leaf);
    return length >= 0 && static_cast<std::size_t>(length) < out_size;
}

bool CopyPath(char *out, std::size_t out_size, const char *path)
{
    if (!out || !out_size || !path) return false;
    const int length = std::snprintf(out, out_size, "%s", path);
    return length >= 0 && static_cast<std::size_t>(length) < out_size;
}

bool HasHeaderAndSize(const char *path, const char magic[4],
                      std::uint64_t expected_size)
{
    SceIoStat stat{};
    if (!path || sceIoGetstat(path, &stat) < 0 ||
        static_cast<std::uint64_t>(stat.st_size) != expected_size)
    {
        return false;
    }
    const SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return false;
    char header[4];
    bool ok = ReadExact(fd, header, sizeof(header)) &&
              std::memcmp(header, magic, sizeof(header)) == 0;
    if (sceIoClose(fd) < 0) ok = false;
    return ok;
}

bool IsCompleteDataRoot(const char *root)
{
    char th07_path[kMaxPath];
    char bgm_path[kMaxPath];
    return JoinPath(th07_path, sizeof(th07_path), root, "th07.dat") &&
           JoinPath(bgm_path, sizeof(bgm_path), root, "thbgm.dat") &&
           HasHeaderAndSize(th07_path, "PBG4", kTh07DatBytes) &&
           HasHeaderAndSize(bgm_path, "ZWAV", kThBgmDatBytes);
}

int AcceptCandidate(const char *candidate, char *out, std::size_t out_size)
{
    // Match fileio.cpp's gDataDir/gDataCandidates[...][512] admission bound.
    // An overlong valid folder is skipped there rather than aborting the scan.
    if (!candidate || !candidate[0] ||
        std::strlen(candidate) >= kRuntimeDataPath ||
        !IsCompleteDataRoot(candidate))
    {
        return 0;
    }
    return CopyPath(out, out_size, candidate) ? 1 : kSelfwrapBadArgument;
}

int ScanSiblingInstalls(const char *device, char *out, std::size_t out_size)
{
    char game_root[64];
    if (!JoinPath(game_root, sizeof(game_root), device, "PSP/GAME")) return 0;
    const SceUID directory = sceIoDopen(game_root);
    if (directory < 0) return 0;

    int result = 0;
    SceIoDirent entry{};
    while (sceIoDread(directory, &entry) > 0)
    {
        if (entry.d_name[0] && std::strcmp(entry.d_name, ".") != 0 &&
            std::strcmp(entry.d_name, "..") != 0 &&
            FIO_S_ISDIR(entry.d_stat.st_mode))
        {
            char install_dir[kMaxPath];
            char data_dir[kMaxPath];
            if (JoinPath(install_dir, sizeof(install_dir), game_root, entry.d_name) &&
                JoinPath(data_dir, sizeof(data_dir), install_dir, "th7"))
            {
                result = AcceptCandidate(data_dir, out, out_size);
                if (result != 0) break;
            }
        }
        std::memset(&entry, 0, sizeof(entry));
    }
    sceIoDclose(directory);
    return result;
}

int ScanDeviceRoot(const char *device, char *out, std::size_t out_size)
{
    int result = AcceptCandidate(device, out, out_size);
    if (result != 0) return result;

    constexpr const char *known_names[] = {
        "th7", "TH07", "youyoumu", "PerfectCherryBlossom"};
    for (const char *name : known_names)
    {
        char candidate[kMaxPath];
        if (JoinPath(candidate, sizeof(candidate), device, name))
        {
            result = AcceptCandidate(candidate, out, out_size);
            if (result != 0) return result;
        }
    }

    char root[16];
    // sceIoDopen accepts "ms0:/" but not every driver accepts "ms0:/.".
    const int root_length = std::snprintf(root, sizeof(root), "%s/", device);
    if (root_length < 0 || static_cast<std::size_t>(root_length) >= sizeof(root)) return 0;
    const SceUID directory = sceIoDopen(root);
    if (directory < 0) return 0;
    SceIoDirent entry{};
    while (sceIoDread(directory, &entry) > 0)
    {
        if (entry.d_name[0] && std::strcmp(entry.d_name, ".") != 0 &&
            std::strcmp(entry.d_name, "..") != 0 &&
            FIO_S_ISDIR(entry.d_stat.st_mode))
        {
            char candidate[kMaxPath];
            if (JoinPath(candidate, sizeof(candidate), device, entry.d_name))
            {
                result = AcceptCandidate(candidate, out, out_size);
                if (result != 0) break;
            }
        }
        std::memset(&entry, 0, sizeof(entry));
    }
    sceIoDclose(directory);
    return result;
}

struct LzssInput
{
    SceUID fd;
    std::uint32_t remaining;
    unsigned char io[4096];
    std::size_t cursor;
    std::size_t available;
    unsigned char current;
    unsigned char mask;
};

bool LzssReadByte(LzssInput *input, unsigned char *value)
{
    if (input->cursor == input->available)
    {
        if (!input->remaining) return false;
        const unsigned int request = input->remaining > sizeof(input->io)
                                         ? sizeof(input->io)
                                         : input->remaining;
        const int got = sceIoRead(input->fd, input->io, request);
        if (got <= 0) return false;
        input->cursor = 0u;
        input->available = static_cast<std::size_t>(got);
        input->remaining -= static_cast<std::uint32_t>(got);
    }
    *value = input->io[input->cursor++];
    return true;
}

bool LzssGetBits(LzssInput *input, unsigned int count, std::uint32_t *value)
{
    std::uint32_t result = 0u;
    for (unsigned int i = 0u; i < count; ++i)
    {
        if (!input->mask)
        {
            if (!LzssReadByte(input, &input->current)) return false;
            input->mask = 0x80u;
        }
        result <<= 1u;
        if (input->current & input->mask) result |= 1u;
        input->mask >>= 1u;
    }
    *value = result;
    return true;
}

bool DecompressLzss(SceUID fd, std::uint32_t source_offset,
                    std::uint32_t source_size, unsigned char *destination,
                    std::uint32_t destination_size)
{
    if (!destination || !destination_size ||
        sceIoLseek(fd, static_cast<SceOff>(source_offset), PSP_SEEK_SET) !=
            static_cast<SceOff>(source_offset))
    {
        return false;
    }

    unsigned char dictionary[8192]{};
    std::uint32_t dictionary_head = 1u;
    std::uint32_t produced = 0u;
    LzssInput input{};
    input.fd = fd;
    input.remaining = source_size;

    while (produced < destination_size)
    {
        std::uint32_t flag = 0u;
        if (!LzssGetBits(&input, 1u, &flag)) return false;
        if (flag)
        {
            std::uint32_t literal = 0u;
            if (!LzssGetBits(&input, 8u, &literal)) return false;
            const unsigned char byte = static_cast<unsigned char>(literal);
            destination[produced++] = byte;
            dictionary[dictionary_head] = byte;
            dictionary_head = (dictionary_head + 1u) & 0x1fffu;
        }
        else
        {
            std::uint32_t offset = 0u;
            std::uint32_t encoded_length = 0u;
            if (!LzssGetBits(&input, 13u, &offset) || !offset) return false;
            if (!LzssGetBits(&input, 4u, &encoded_length)) return false;
            const std::uint32_t length = encoded_length + 3u;
            if (length > destination_size - produced) return false;
            for (std::uint32_t i = 0u; i < length; ++i)
            {
                const unsigned char byte = dictionary[(offset + i) & 0x1fffu];
                destination[produced++] = byte;
                dictionary[dictionary_head] = byte;
                dictionary_head = (dictionary_head + 1u) & 0x1fffu;
            }
        }
    }
    return true;
}

bool AsciiNameEquals(const unsigned char *name, std::size_t length,
                     const char *wanted)
{
    const std::size_t wanted_length = std::strlen(wanted);
    if (length != wanted_length) return false;
    for (std::size_t i = 0u; i < length; ++i)
    {
        unsigned char a = name[i];
        unsigned char b = static_cast<unsigned char>(wanted[i]);
        if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

bool LoadArchiveIndex(SceUID fd, PbgEntry *title, PbgEntry *staff)
{
    unsigned char header[16];
    if (!ReadExactAt(fd, 0u, header, sizeof(header)) ||
        std::memcmp(header, "PBG4", 4u) != 0)
    {
        return false;
    }
    const std::uint32_t count = ReadLe32(header + 4u);
    const std::uint32_t table_offset = ReadLe32(header + 8u);
    const std::uint32_t table_size = ReadLe32(header + 12u);
    if (!count || count > kMaxPbgEntries || table_offset < sizeof(header) ||
        table_offset >= kTh07DatBytes || !table_size ||
        table_size > kMaxPbgTableBytes)
    {
        return false;
    }

    auto *table = static_cast<unsigned char *>(std::malloc(table_size));
    auto *entry_offsets = static_cast<std::uint32_t *>(
        std::malloc(static_cast<std::size_t>(count) * sizeof(std::uint32_t)));
    if (!table || !entry_offsets)
    {
        std::free(table);
        std::free(entry_offsets);
        return false;
    }
    if (!DecompressLzss(fd, table_offset,
                        static_cast<std::uint32_t>(kTh07DatBytes - table_offset),
                        table, table_size))
    {
        std::free(table);
        std::free(entry_offsets);
        return false;
    }

    std::size_t cursor = 0u;
    bool valid = true;
    for (std::uint32_t i = 0u; i < count && valid; ++i)
    {
        const std::size_t name_start = cursor;
        while (cursor < table_size && table[cursor]) ++cursor;
        if (cursor >= table_size)
        {
            valid = false;
            break;
        }
        const std::size_t name_length = cursor - name_start;
        ++cursor;
        if (cursor > table_size || table_size - cursor < 12u)
        {
            valid = false;
            break;
        }
        const std::uint32_t data_offset = ReadLe32(table + cursor);
        const std::uint32_t decompressed_size = ReadLe32(table + cursor + 4u);
        cursor += 12u;
        if (data_offset < sizeof(header) || data_offset >= table_offset ||
            !decompressed_size)
        {
            valid = false;
            break;
        }
        entry_offsets[i] = data_offset;

        PbgEntry *selected = nullptr;
        if (AsciiNameEquals(table + name_start, name_length, "title01.anm"))
            selected = title;
        else if (AsciiNameEquals(table + name_start, name_length, "staff00.jpg"))
            selected = staff;
        if (selected)
        {
            if (selected->found)
            {
                valid = false;
                break;
            }
            selected->offset = data_offset;
            selected->decompressed_size = decompressed_size;
            selected->found = true;
        }
    }

    if (valid && title->found && staff->found)
    {
        PbgEntry *wanted[2] = {title, staff};
        for (PbgEntry *selected : wanted)
        {
            std::uint32_t end = table_offset;
            for (std::uint32_t i = 0u; i < count; ++i)
            {
                const std::uint32_t candidate = entry_offsets[i];
                if (candidate > selected->offset && candidate < end) end = candidate;
            }
            if (end <= selected->offset)
            {
                valid = false;
                break;
            }
            selected->compressed_size = end - selected->offset;
        }
    }
    else
    {
        valid = false;
    }

    if (valid &&
        (title->decompressed_size > kMaxTitleBytes ||
         staff->decompressed_size > kMaxStaffJpegBytes))
    {
        valid = false;
    }
    std::free(table);
    std::free(entry_offsets);
    return valid;
}

bool DecompressEntry(SceUID fd, const PbgEntry &entry, ByteBuffer *output)
{
    output->data = static_cast<unsigned char *>(std::malloc(entry.decompressed_size));
    if (!output->data) return false;
    output->capacity = entry.decompressed_size;
    if (!DecompressLzss(fd, entry.offset, entry.compressed_size, output->data,
                        entry.decompressed_size))
    {
        FreeBuffer(output);
        return false;
    }
    output->size = entry.decompressed_size;
    return true;
}

bool CropNonzero(const Image &source, Image *cropped)
{
    std::uint32_t left = source.width;
    std::uint32_t top = source.height;
    std::uint32_t right = 0u;
    std::uint32_t bottom = 0u;
    bool found = false;
    for (std::uint32_t y = 0u; y < source.height; ++y)
    {
        for (std::uint32_t x = 0u; x < source.width; ++x)
        {
            const unsigned char *pixel =
                source.rgba + (static_cast<std::size_t>(y) * source.width + x) * 4u;
            if ((pixel[0] | pixel[1] | pixel[2] | pixel[3]) == 0u) continue;
            if (!found || x < left) left = x;
            if (!found || x > right) right = x;
            if (!found || y < top) top = y;
            if (!found || y > bottom) bottom = y;
            found = true;
        }
    }
    if (!found) return false;
    cropped->width = right - left + 1u;
    cropped->height = bottom - top + 1u;
    std::size_t bytes = 0u;
    if (!CheckedImageBytes(cropped->width, cropped->height, 4u, &bytes)) return false;
    cropped->rgba = static_cast<unsigned char *>(std::malloc(bytes));
    if (!cropped->rgba) return false;
    const std::size_t row_bytes = static_cast<std::size_t>(cropped->width) * 4u;
    for (std::uint32_t y = 0u; y < cropped->height; ++y)
    {
        const unsigned char *source_row = source.rgba +
            (static_cast<std::size_t>(top + y) * source.width + left) * 4u;
        std::memcpy(cropped->rgba + static_cast<std::size_t>(y) * row_bytes,
                    source_row, row_bytes);
    }
    return true;
}

bool DecodeFirstLogo(const ByteBuffer &anm, Image *logo)
{
    std::size_t marker = anm.size;
    for (std::size_t i = 0u; i + 4u <= anm.size; ++i)
    {
        if (std::memcmp(anm.data + i, "THTX", 4u) == 0)
        {
            marker = i;
            break;
        }
    }
    if (marker == anm.size || anm.size - marker < 16u) return false;
    const std::uint16_t format = ReadLe16(anm.data + marker + 6u);
    const std::uint32_t width = ReadLe16(anm.data + marker + 8u);
    const std::uint32_t height = ReadLe16(anm.data + marker + 10u);
    if (!width || !height || width > kMaxLogoDimension ||
        height > kMaxLogoDimension || (format != 1u && format != 5u))
    {
        return false;
    }
    const std::size_t source_bpp = format == 1u ? 4u : 2u;
    std::size_t source_bytes = 0u;
    std::size_t rgba_bytes = 0u;
    if (!CheckedImageBytes(width, height, source_bpp, &source_bytes) ||
        !CheckedImageBytes(width, height, 4u, &rgba_bytes) ||
        source_bytes > anm.size - marker - 16u)
    {
        return false;
    }
    Image decoded{};
    decoded.width = width;
    decoded.height = height;
    decoded.rgba = static_cast<unsigned char *>(std::malloc(rgba_bytes));
    if (!decoded.rgba) return false;
    const unsigned char *pixels = anm.data + marker + 16u;
    const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
    for (std::size_t i = 0u; i < pixel_count; ++i)
    {
        unsigned char *out = decoded.rgba + i * 4u;
        if (format == 1u)
        {
            out[0] = pixels[i * 4u + 2u];
            out[1] = pixels[i * 4u + 1u];
            out[2] = pixels[i * 4u + 0u];
            out[3] = pixels[i * 4u + 3u];
        }
        else
        {
            const std::uint16_t value = ReadLe16(pixels + i * 2u);
            out[0] = static_cast<unsigned char>(((value >> 8u) & 0x0fu) * 17u);
            out[1] = static_cast<unsigned char>(((value >> 4u) & 0x0fu) * 17u);
            out[2] = static_cast<unsigned char>((value & 0x0fu) * 17u);
            out[3] = static_cast<unsigned char>(((value >> 12u) & 0x0fu) * 17u);
        }
    }
    const bool ok = CropNonzero(decoded, logo);
    FreeImage(&decoded);
    return ok;
}

struct JpegContext
{
    // Keep the error manager first: libjpeg gives ErrorExit only common->err.
    jpeg_error_mgr manager;
    jpeg_decompress_struct decoder;
    jmp_buf jump;
    unsigned char *rgba;
    unsigned char *row;
};

void JpegErrorExit(j_common_ptr common)
{
    auto *context = reinterpret_cast<JpegContext *>(common->err);
    longjmp(context->jump, 1);
}

void DestroyJpegContext(JpegContext *context)
{
    if (!context) return;
    if (context->decoder.mem) jpeg_destroy_decompress(&context->decoder);
    std::free(context->rgba);
    std::free(context->row);
    std::free(context);
}

bool DecodeJpeg(const ByteBuffer &jpeg, Image *image)
{
    // libjpeg signals malformed input with longjmp.  Every object that it or
    // the decoder mutates after setjmp lives in this heap context; reading an
    // automatic pointer/state changed after setjmp would be indeterminate in
    // C++ even though it happens to work on Allegrex GCC.
    auto *context = static_cast<JpegContext *>(std::calloc(1u, sizeof(JpegContext)));
    if (!context) return false;
    context->decoder.err = jpeg_std_error(&context->manager);
    context->manager.error_exit = JpegErrorExit;
    if (setjmp(context->jump))
    {
        DestroyJpegContext(context);
        return false;
    }
    jpeg_create_decompress(&context->decoder);
    jpeg_mem_src(&context->decoder, jpeg.data,
                 static_cast<unsigned long>(jpeg.size));
    if (jpeg_read_header(&context->decoder, TRUE) != JPEG_HEADER_OK)
    {
        DestroyJpegContext(context);
        return false;
    }
    context->decoder.out_color_space = JCS_RGB;
    context->decoder.do_fancy_upsampling = TRUE;
    if (!jpeg_start_decompress(&context->decoder) ||
        !context->decoder.output_width || !context->decoder.output_height ||
        context->decoder.output_width > kMaxJpegDimension ||
        context->decoder.output_height > kMaxJpegDimension ||
        context->decoder.output_components != 3u)
    {
        DestroyJpegContext(context);
        return false;
    }
    std::size_t rgba_bytes = 0u;
    if (!CheckedImageBytes(context->decoder.output_width,
                           context->decoder.output_height, 4u,
                           &rgba_bytes) ||
        rgba_bytes > kMaxJpegRgbaBytes)
    {
        DestroyJpegContext(context);
        return false;
    }
    context->rgba = static_cast<unsigned char *>(std::malloc(rgba_bytes));
    context->row = static_cast<unsigned char *>(std::malloc(
        static_cast<std::size_t>(context->decoder.output_width) * 3u));
    if (!context->rgba || !context->row)
    {
        DestroyJpegContext(context);
        return false;
    }
    while (context->decoder.output_scanline < context->decoder.output_height)
    {
        JSAMPROW rows[1] = {context->row};
        if (jpeg_read_scanlines(&context->decoder, rows, 1u) != 1u)
        {
            DestroyJpegContext(context);
            return false;
        }
        const std::uint32_t y = context->decoder.output_scanline - 1u;
        unsigned char *out = context->rgba +
            static_cast<std::size_t>(y) * context->decoder.output_width * 4u;
        for (std::uint32_t x = 0u; x < context->decoder.output_width; ++x)
        {
            out[x * 4u + 0u] = context->row[x * 3u + 0u];
            out[x * 4u + 1u] = context->row[x * 3u + 1u];
            out[x * 4u + 2u] = context->row[x * 3u + 2u];
            out[x * 4u + 3u] = 255u;
        }
    }
    if (!jpeg_finish_decompress(&context->decoder))
    {
        DestroyJpegContext(context);
        return false;
    }
    image->width = context->decoder.output_width;
    image->height = context->decoder.output_height;
    image->rgba = context->rgba;
    context->rgba = nullptr;
    DestroyJpegContext(context);
    return true;
}

bool ResizeRegion(const Image &source, std::uint32_t source_x,
                  std::uint32_t source_y, std::uint32_t source_width,
                  std::uint32_t source_height, std::uint32_t destination_width,
                  std::uint32_t destination_height, Image *destination)
{
    if (!source.rgba || !source_width || !source_height ||
        source_x > source.width || source_y > source.height ||
        source_width > source.width - source_x ||
        source_height > source.height - source_y)
    {
        return false;
    }
    std::size_t bytes = 0u;
    if (!CheckedImageBytes(destination_width, destination_height, 4u, &bytes))
        return false;
    destination->rgba = static_cast<unsigned char *>(std::malloc(bytes));
    if (!destination->rgba) return false;
    destination->width = destination_width;
    destination->height = destination_height;

    for (std::uint32_t y = 0u; y < destination_height; ++y)
    {
        const std::uint64_t y_fixed = destination_height > 1u
            ? static_cast<std::uint64_t>(y) * (source_height - 1u) * 65536u /
                  (destination_height - 1u)
            : 0u;
        const std::uint32_t y0 = static_cast<std::uint32_t>(y_fixed >> 16u);
        const std::uint32_t y1 = y0 + 1u < source_height ? y0 + 1u : y0;
        const std::uint32_t fy = static_cast<std::uint32_t>(y_fixed & 0xffffu);
        for (std::uint32_t x = 0u; x < destination_width; ++x)
        {
            const std::uint64_t x_fixed = destination_width > 1u
                ? static_cast<std::uint64_t>(x) * (source_width - 1u) * 65536u /
                      (destination_width - 1u)
                : 0u;
            const std::uint32_t x0 = static_cast<std::uint32_t>(x_fixed >> 16u);
            const std::uint32_t x1 = x0 + 1u < source_width ? x0 + 1u : x0;
            const std::uint32_t fx = static_cast<std::uint32_t>(x_fixed & 0xffffu);
            const unsigned char *p00 = source.rgba +
                (static_cast<std::size_t>(source_y + y0) * source.width + source_x + x0) * 4u;
            const unsigned char *p10 = source.rgba +
                (static_cast<std::size_t>(source_y + y0) * source.width + source_x + x1) * 4u;
            const unsigned char *p01 = source.rgba +
                (static_cast<std::size_t>(source_y + y1) * source.width + source_x + x0) * 4u;
            const unsigned char *p11 = source.rgba +
                (static_cast<std::size_t>(source_y + y1) * source.width + source_x + x1) * 4u;
            unsigned char *out = destination->rgba +
                (static_cast<std::size_t>(y) * destination_width + x) * 4u;
            for (unsigned int channel = 0u; channel < 4u; ++channel)
            {
                const std::uint64_t top =
                    static_cast<std::uint64_t>(p00[channel]) * (65536u - fx) +
                    static_cast<std::uint64_t>(p10[channel]) * fx;
                const std::uint64_t bottom =
                    static_cast<std::uint64_t>(p01[channel]) * (65536u - fx) +
                    static_cast<std::uint64_t>(p11[channel]) * fx;
                const std::uint64_t value =
                    top * (65536u - fy) + bottom * fy + 0x80000000ull;
                out[channel] = static_cast<unsigned char>(value >> 32u);
            }
        }
    }
    return true;
}

bool MakeThumbnail(const Image &source, std::uint32_t max_width,
                   std::uint32_t max_height, Image *thumbnail)
{
    std::uint32_t width = source.width;
    std::uint32_t height = source.height;
    if (width > max_width || height > max_height)
    {
        if (static_cast<std::uint64_t>(width) * max_height >
            static_cast<std::uint64_t>(height) * max_width)
        {
            width = max_width;
            height = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(source.height) * max_width +
                 source.width / 2u) /
                source.width);
        }
        else
        {
            height = max_height;
            width = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(source.width) * max_height +
                 source.height / 2u) /
                source.height);
        }
    }
    if (!width) width = 1u;
    if (!height) height = 1u;
    return ResizeRegion(source, 0u, 0u, source.width, source.height,
                        width, height, thumbnail);
}

void CompositeOpaque(Image *destination, const Image &source, int left, int top)
{
    for (std::uint32_t y = 0u; y < source.height; ++y)
    {
        const int destination_y = top + static_cast<int>(y);
        if (destination_y < 0 || destination_y >= static_cast<int>(destination->height))
            continue;
        for (std::uint32_t x = 0u; x < source.width; ++x)
        {
            const int destination_x = left + static_cast<int>(x);
            if (destination_x < 0 || destination_x >= static_cast<int>(destination->width))
                continue;
            const unsigned char *in = source.rgba +
                (static_cast<std::size_t>(y) * source.width + x) * 4u;
            unsigned char *out = destination->rgba +
                (static_cast<std::size_t>(destination_y) * destination->width +
                 static_cast<std::uint32_t>(destination_x)) * 4u;
            const unsigned int alpha = in[3];
            for (unsigned int channel = 0u; channel < 3u; ++channel)
            {
                out[channel] = static_cast<unsigned char>(
                    (static_cast<unsigned int>(in[channel]) * alpha +
                     static_cast<unsigned int>(out[channel]) * (255u - alpha) + 127u) /
                    255u);
            }
            out[3] = 255u;
        }
    }
}

bool BuildIcon(const Image &logo, Image *icon)
{
    icon->width = 144u;
    icon->height = 80u;
    std::size_t bytes = 0u;
    if (!CheckedImageBytes(icon->width, icon->height, 4u, &bytes)) return false;
    icon->rgba = static_cast<unsigned char *>(std::malloc(bytes));
    if (!icon->rgba) return false;
    for (std::uint32_t y = 0u; y < icon->height; ++y)
    {
        const unsigned int red = 4u + (8u * y) / 79u;
        const unsigned int green = 2u + (6u * y) / 79u;
        const unsigned int blue = 8u + (16u * y) / 79u;
        for (std::uint32_t x = 0u; x < icon->width; ++x)
        {
            unsigned char *pixel = icon->rgba +
                (static_cast<std::size_t>(y) * icon->width + x) * 4u;
            pixel[0] = static_cast<unsigned char>(red);
            pixel[1] = static_cast<unsigned char>(green);
            pixel[2] = static_cast<unsigned char>(blue);
            pixel[3] = 255u;
        }
    }
    Image scaled{};
    if (!MakeThumbnail(logo, 136u, 72u, &scaled))
    {
        FreeImage(icon);
        return false;
    }
    CompositeOpaque(icon, scaled,
                    (static_cast<int>(icon->width) - static_cast<int>(scaled.width)) / 2,
                    (static_cast<int>(icon->height) - static_cast<int>(scaled.height)) / 2);
    FreeImage(&scaled);
    for (std::uint32_t x = 0u; x < icon->width; ++x)
    {
        unsigned char *top = icon->rgba + static_cast<std::size_t>(x) * 4u;
        unsigned char *bottom = icon->rgba +
            (static_cast<std::size_t>(icon->height - 1u) * icon->width + x) * 4u;
        top[0] = bottom[0] = 150u;
        top[1] = bottom[1] = 145u;
        top[2] = bottom[2] = 175u;
    }
    for (std::uint32_t y = 0u; y < icon->height; ++y)
    {
        unsigned char *left = icon->rgba +
            static_cast<std::size_t>(y) * icon->width * 4u;
        unsigned char *right = left + static_cast<std::size_t>(icon->width - 1u) * 4u;
        left[0] = right[0] = 150u;
        left[1] = right[1] = 145u;
        left[2] = right[2] = 175u;
    }
    return true;
}

bool AddLogoShadow(Image *picture, const Image &logo)
{
    const std::size_t pixels = static_cast<std::size_t>(logo.width) * logo.height;
    auto *horizontal = static_cast<unsigned char *>(std::malloc(pixels));
    auto *blurred = static_cast<unsigned char *>(std::malloc(pixels));
    if (!horizontal || !blurred)
    {
        std::free(horizontal);
        std::free(blurred);
        return false;
    }
    constexpr unsigned int kernel[5] = {1u, 4u, 6u, 4u, 1u};
    for (std::uint32_t y = 0u; y < logo.height; ++y)
    {
        for (std::uint32_t x = 0u; x < logo.width; ++x)
        {
            unsigned int sum = 0u;
            for (int tap = -2; tap <= 2; ++tap)
            {
                const int sample_x = static_cast<int>(x) + tap;
                if (sample_x < 0 || sample_x >= static_cast<int>(logo.width)) continue;
                sum += logo.rgba[(static_cast<std::size_t>(y) * logo.width +
                                  static_cast<std::uint32_t>(sample_x)) * 4u + 3u] *
                       kernel[tap + 2];
            }
            horizontal[static_cast<std::size_t>(y) * logo.width + x] =
                static_cast<unsigned char>((sum + 8u) / 16u);
        }
    }
    for (std::uint32_t y = 0u; y < logo.height; ++y)
    {
        for (std::uint32_t x = 0u; x < logo.width; ++x)
        {
            unsigned int sum = 0u;
            for (int tap = -2; tap <= 2; ++tap)
            {
                const int sample_y = static_cast<int>(y) + tap;
                if (sample_y < 0 || sample_y >= static_cast<int>(logo.height)) continue;
                sum += horizontal[static_cast<std::size_t>(sample_y) * logo.width + x] *
                       kernel[tap + 2];
            }
            blurred[static_cast<std::size_t>(y) * logo.width + x] =
                static_cast<unsigned char>((sum + 8u) / 16u);
        }
    }
    std::free(horizontal);

    for (std::uint32_t y = 0u; y < logo.height; ++y)
    {
        const int destination_y = 22 + static_cast<int>(y);
        if (destination_y < 0 || destination_y >= static_cast<int>(picture->height)) continue;
        for (std::uint32_t x = 0u; x < logo.width; ++x)
        {
            const int destination_x = 26 + static_cast<int>(x);
            if (destination_x < 0 || destination_x >= static_cast<int>(picture->width)) continue;
            const unsigned int alpha =
                (static_cast<unsigned int>(blurred[static_cast<std::size_t>(y) * logo.width + x]) *
                 140u + 127u) /
                255u;
            unsigned char *out = picture->rgba +
                (static_cast<std::size_t>(destination_y) * picture->width +
                 static_cast<std::uint32_t>(destination_x)) * 4u;
            constexpr unsigned int shadow[3] = {20u, 0u, 40u};
            for (unsigned int channel = 0u; channel < 3u; ++channel)
            {
                out[channel] = static_cast<unsigned char>(
                    (shadow[channel] * alpha +
                     static_cast<unsigned int>(out[channel]) * (255u - alpha) + 127u) /
                    255u);
            }
        }
    }
    std::free(blurred);
    return true;
}

bool BuildPicture(const Image &logo, const Image &staff, Image *picture)
{
    std::uint32_t crop_height = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(staff.width) * 272u / 480u);
    if (!crop_height) return false;
    if (crop_height > staff.height) crop_height = staff.height;
    const std::uint32_t crop_top =
        static_cast<std::uint32_t>((staff.height - crop_height) * 45u / 100u);
    if (!ResizeRegion(staff, 0u, crop_top, staff.width, crop_height,
                      480u, 272u, picture))
    {
        return false;
    }
    Image scaled_logo{};
    if (!MakeThumbnail(logo, 300u, 120u, &scaled_logo) ||
        !AddLogoShadow(picture, scaled_logo))
    {
        FreeImage(&scaled_logo);
        FreeImage(picture);
        return false;
    }
    CompositeOpaque(picture, scaled_logo, 24, 20);
    FreeImage(&scaled_logo);
    return true;
}

struct PngSink
{
    unsigned char *data;
    std::size_t size;
    std::size_t capacity;
};

void PngWrite(png_structp png, png_bytep data, png_size_t length)
{
    auto *sink = static_cast<PngSink *>(png_get_io_ptr(png));
    if (!sink || sink->size > sink->capacity ||
        length > sink->capacity - sink->size)
    {
        png_error(png, "TH07 XMB PNG reserve exhausted");
        return;
    }
    std::memcpy(sink->data + sink->size, data, length);
    sink->size += length;
}

void PngFlush(png_structp)
{
}

bool EncodePng(const Image &image, bool with_alpha,
               std::size_t maximum_bytes, ByteBuffer *png_output)
{
    std::size_t row_bytes = 0u;
    const std::size_t channels = with_alpha ? 4u : 3u;
    if (!image.rgba || !CheckedImageBytes(image.width, 1u, channels,
                                          &row_bytes) ||
        row_bytes == SIZE_MAX ||
        image.height > SIZE_MAX / (row_bytes + 1u))
    {
        return false;
    }
    const std::size_t filtered_bytes = (row_bytes + 1u) * image.height;
    const uLong zlib_source = static_cast<uLong>(filtered_bytes);
    if (static_cast<std::size_t>(zlib_source) != filtered_bytes) return false;
    const std::size_t compressed_bound =
        static_cast<std::size_t>(compressBound(zlib_source));
    if (compressed_bound > SIZE_MAX - 1024u) return false;
    const std::size_t conservative_size = compressed_bound + 1024u;
    if (conservative_size > maximum_bytes) return false;

    auto *storage = static_cast<unsigned char *>(std::malloc(conservative_size));
    auto *row = static_cast<unsigned char *>(std::malloc(row_bytes));
    if (!storage || !row)
    {
        std::free(storage);
        std::free(row);
        return false;
    }
    PngSink sink{storage, 0u, conservative_size};
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png)
    {
        std::free(storage);
        std::free(row);
        return false;
    }
    png_infop info = png_create_info_struct(png);
    if (!info)
    {
        png_destroy_write_struct(&png, nullptr);
        std::free(storage);
        std::free(row);
        return false;
    }
    if (setjmp(png_jmpbuf(png)))
    {
        png_destroy_write_struct(&png, &info);
        std::free(storage);
        std::free(row);
        return false;
    }
    png_set_write_fn(png, &sink, PngWrite, PngFlush);
    png_set_IHDR(png, info, image.width, image.height, 8,
                 with_alpha ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_compression_level(png, 6);
    png_write_info(png, info);
    for (std::uint32_t y = 0u; y < image.height; ++y)
    {
        const unsigned char *source = image.rgba +
            static_cast<std::size_t>(y) * image.width * 4u;
        for (std::uint32_t x = 0u; x < image.width; ++x)
        {
            row[x * channels + 0u] = source[x * 4u + 0u];
            row[x * channels + 1u] = source[x * 4u + 1u];
            row[x * channels + 2u] = source[x * 4u + 2u];
            if (with_alpha) row[x * channels + 3u] = source[x * 4u + 3u];
        }
        png_write_row(png, row);
    }
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    std::free(row);
    if (sink.size < sizeof(kPngMagic) ||
        std::memcmp(sink.data, kPngMagic, sizeof(kPngMagic)) != 0 ||
        sink.size > maximum_bytes)
    {
        std::free(storage);
        return false;
    }
    png_output->data = storage;
    png_output->size = sink.size;
    png_output->capacity = conservative_size;
    return true;
}

bool PadPngToSlot(ByteBuffer *png, std::size_t slot_bytes,
                  const unsigned char identity[8], unsigned char role)
{
    // libpng emits a 12-byte zero-length IEND as the final chunk. Insert one
    // private ancillary identity/padding chunk immediately before it, making
    // the complete PNG section exactly the immutable PBP slot length.
    constexpr std::size_t kIendBytes = 12u;
    constexpr std::size_t kChunkOverhead = 12u;
    constexpr std::size_t kIdentityBytes = 9u;
    if (!png || !png->data || png->size < sizeof(kPngMagic) + kIendBytes ||
        slot_bytes > UINT32_MAX || png->size > slot_bytes ||
        slot_bytes - png->size < kChunkOverhead + kIdentityBytes)
    {
        return false;
    }
    const unsigned char *iend = png->data + png->size - kIendBytes;
    if (ReadBe32(iend) != 0u || std::memcmp(iend + 4u, "IEND", 4u) != 0)
        return false;

    const std::size_t payload_bytes =
        slot_bytes - png->size - kChunkOverhead;
    if (payload_bytes > UINT32_MAX) return false;
    auto *fixed = static_cast<unsigned char *>(std::malloc(slot_bytes));
    if (!fixed) return false;
    const std::size_t prefix_bytes = png->size - kIendBytes;
    std::memcpy(fixed, png->data, prefix_bytes);
    unsigned char *chunk = fixed + prefix_bytes;
    WriteBe32(chunk, static_cast<std::uint32_t>(payload_bytes));
    std::memcpy(chunk + 4u, kSelfwrapChunkType,
                sizeof(kSelfwrapChunkType));
    std::memcpy(chunk + 8u, identity, sizeof(kWrappedIdentity));
    chunk[8u + sizeof(kWrappedIdentity)] = role;
    std::memset(chunk + 8u + kIdentityBytes, 0,
                payload_bytes - kIdentityBytes);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, chunk + 4u,
                static_cast<uInt>(sizeof(kSelfwrapChunkType) + payload_bytes));
    WriteBe32(chunk + 8u + payload_bytes, static_cast<std::uint32_t>(crc));
    std::memcpy(chunk + kChunkOverhead + payload_bytes, iend, kIendBytes);

    std::free(png->data);
    png->data = fixed;
    png->size = slot_bytes;
    png->capacity = slot_bytes;
    return true;
}

bool GenerateAssets(const char *data_root, ByteBuffer *icon_png,
                    ByteBuffer *picture_png)
{
    char archive_path[kMaxPath];
    if (!JoinPath(archive_path, sizeof(archive_path), data_root, "th07.dat"))
        return false;
    const SceUID archive = sceIoOpen(archive_path, PSP_O_RDONLY, 0);
    if (archive < 0) return false;
    PbgEntry title{};
    PbgEntry staff_entry{};
    if (!LoadArchiveIndex(archive, &title, &staff_entry))
    {
        sceIoClose(archive);
        return false;
    }

    ByteBuffer title_anm{};
    Image logo{};
    bool ok = DecompressEntry(archive, title, &title_anm) &&
              DecodeFirstLogo(title_anm, &logo);
    FreeBuffer(&title_anm);
    if (!ok)
    {
        FreeImage(&logo);
        sceIoClose(archive);
        return false;
    }

    ByteBuffer staff_jpeg{};
    Image staff_image{};
    ok = DecompressEntry(archive, staff_entry, &staff_jpeg) &&
         DecodeJpeg(staff_jpeg, &staff_image);
    FreeBuffer(&staff_jpeg);
    if (sceIoClose(archive) < 0) ok = false;
    if (!ok)
    {
        FreeImage(&logo);
        FreeImage(&staff_image);
        return false;
    }

    Image icon{};
    Image picture{};
    ok = BuildIcon(logo, &icon) && BuildPicture(logo, staff_image, &picture);
    FreeImage(&logo);
    FreeImage(&staff_image);
    if (!ok)
    {
        FreeImage(&icon);
        FreeImage(&picture);
        return false;
    }
    ok = EncodePng(icon, false, kIconSlotBytes, icon_png) &&
         PadPngToSlot(icon_png, kIconSlotBytes, kWrappedIdentity, 'I');
    if (ok)
    {
        ok = EncodePng(picture, false, kPictureSlotBytes, picture_png) &&
             PadPngToSlot(picture_png, kPictureSlotBytes,
                          kWrappedIdentity, 'P');
    }
    FreeImage(&icon);
    FreeImage(&picture);
    if (!ok)
    {
        FreeBuffer(icon_png);
        FreeBuffer(picture_png);
    }
    return ok;
}

bool GeneratePlaceholderAssets(ByteBuffer *icon_png,
                               ByteBuffer *picture_png)
{
    Image icon{144u, 80u, nullptr};
    Image picture{480u, 272u, nullptr};
    std::size_t icon_bytes = 0u;
    std::size_t picture_bytes = 0u;
    if (!CheckedImageBytes(icon.width, icon.height, 4u, &icon_bytes) ||
        !CheckedImageBytes(picture.width, picture.height, 4u,
                           &picture_bytes))
    {
        return false;
    }
    icon.rgba = static_cast<unsigned char *>(std::calloc(1u, icon_bytes));
    picture.rgba =
        static_cast<unsigned char *>(std::calloc(1u, picture_bytes));
    bool ok = icon.rgba && picture.rgba &&
              EncodePng(icon, true, kIconSlotBytes, icon_png) &&
              PadPngToSlot(icon_png, kIconSlotBytes,
                           kPlaceholderIdentity, 'I') &&
              EncodePng(picture, true, kPictureSlotBytes, picture_png) &&
              PadPngToSlot(picture_png, kPictureSlotBytes,
                           kPlaceholderIdentity, 'P');
    FreeImage(&icon);
    FreeImage(&picture);
    if (!ok)
    {
        FreeBuffer(icon_png);
        FreeBuffer(picture_png);
    }
    return ok;
}

bool PbpOffsetsMonotonic(const std::uint32_t offsets[8], std::uint64_t file_size)
{
    if (offsets[0] != kPbpHeaderBytes) return false;
    for (unsigned int i = 0u; i + 1u < 8u; ++i)
    {
        if (offsets[i] > offsets[i + 1u]) return false;
    }
    return offsets[7] <= file_size;
}

bool FindSfoSlotContract(SceUID fd, const std::uint32_t offsets[8])
{
    if (offsets[1] <= offsets[0]) return false;
    const std::uint32_t section_bytes = offsets[1] - offsets[0];
    if (section_bytes < kPsfHeaderBytes || section_bytes > kMaxSfoBytes)
        return false;
    auto *buffer = static_cast<unsigned char *>(std::malloc(section_bytes));
    if (!buffer) return false;
    if (!ReadExactAt(fd, offsets[0], buffer, section_bytes) ||
        std::memcmp(buffer, "\0PSF", 4u) != 0)
    {
        std::free(buffer);
        return false;
    }
    const std::uint32_t key_table = ReadLe32(buffer + 8u);
    const std::uint32_t data_table = ReadLe32(buffer + 12u);
    const std::uint32_t count = ReadLe32(buffer + 16u);
    const std::uint64_t directory_end = static_cast<std::uint64_t>(
        kPsfHeaderBytes) + static_cast<std::uint64_t>(count) * kPsfEntryBytes;
    if (count > 1024u || directory_end > key_table || key_table > data_table ||
        data_table > section_bytes)
    {
        std::free(buffer);
        return false;
    }

    bool found = false;
    bool valid = true;
    for (std::uint32_t i = 0u; i < count && valid; ++i)
    {
        const unsigned char *entry =
            buffer + kPsfHeaderBytes + i * kPsfEntryBytes;
        const std::uint32_t key_at = key_table + ReadLe16(entry);
        const std::uint16_t format = ReadLe16(entry + 2u);
        const std::uint32_t value_length = ReadLe32(entry + 4u);
        const std::uint32_t value_capacity = ReadLe32(entry + 8u);
        const std::uint32_t value_relative = ReadLe32(entry + 12u);
        if (key_at < key_table || key_at >= data_table ||
            value_length > value_capacity || value_relative > section_bytes - data_table ||
            value_capacity > section_bytes - data_table - value_relative)
        {
            valid = false;
            break;
        }
        std::uint32_t key_end = key_at;
        while (key_end < data_table && buffer[key_end] != 0u) ++key_end;
        if (key_end == data_table)
        {
            valid = false;
            break;
        }
        const std::size_t key_bytes = key_end - key_at;
        if (key_bytes != sizeof(kXmbSfoKey) - 1u ||
            std::memcmp(buffer + key_at, kXmbSfoKey, key_bytes) != 0)
            continue;
        if (found || format != kPsfBinaryFormat || value_length != 16u ||
            value_capacity != 16u)
        {
            valid = false;
            break;
        }
        const unsigned char *value = buffer + data_table + value_relative;
        found = std::memcmp(value, kXmbMarker, sizeof(kXmbMarker)) == 0 &&
                ReadLe32(value + 8u) == kIconSlotBytes &&
                ReadLe32(value + 12u) == kPictureSlotBytes;
        if (!found) valid = false;
    }
    std::free(buffer);
    return valid && found;
}

bool PngChunkTypeEquals(const unsigned char type[4], const char expected[5])
{
    return std::memcmp(type, expected, 4u) == 0;
}

PngIdentity ValidatePngSection(SceUID fd, std::uint32_t begin,
                               std::uint32_t end,
                               std::uint32_t expected_width,
                               std::uint32_t expected_height,
                               unsigned char expected_role)
{
    if (end <= begin || end - begin < 45u ||
        end - begin > kPictureSlotBytes)
        return PngIdentity::Invalid;
    unsigned char signature[sizeof(kPngMagic)];
    if (!ReadExactAt(fd, begin, signature, sizeof(signature)) ||
        std::memcmp(signature, kPngMagic, sizeof(signature)) != 0)
    {
        return PngIdentity::Invalid;
    }

    std::uint32_t cursor = begin + sizeof(kPngMagic);
    unsigned int chunk_index = 0u;
    bool saw_ihdr = false;
    bool saw_idat = false;
    bool saw_selfwrap_chunk = false;
    bool previous_was_identity = false;
    bool contract_shape = false;
    unsigned char color_type = 0xffu;
    PngIdentity identity = PngIdentity::Foreign;
    while (cursor < end)
    {
        unsigned char chunk_header[8];
        if (end - cursor < 12u ||
            !ReadExactAt(fd, cursor, chunk_header, sizeof(chunk_header)))
        {
            return PngIdentity::Invalid;
        }
        const std::uint32_t length = ReadBe32(chunk_header);
        if (length > end - cursor - 12u) return PngIdentity::Invalid;
        const unsigned char *type = chunk_header + 4u;
        const bool is_ihdr = PngChunkTypeEquals(type, "IHDR");
        const bool is_idat = PngChunkTypeEquals(type, "IDAT");
        const bool is_iend = PngChunkTypeEquals(type, "IEND");
        const bool is_identity =
            std::memcmp(type, kSelfwrapChunkType,
                        sizeof(kSelfwrapChunkType)) == 0;
        if ((chunk_index == 0u) != is_ihdr || (is_ihdr && saw_ihdr) ||
            (is_iend && length != 0u))
        {
            return PngIdentity::Invalid;
        }

        uLong crc = crc32(0L, Z_NULL, 0);
        crc = crc32(crc, type, 4u);
        unsigned char ihdr[13]{};
        unsigned char identity_header[9]{};
        bool identity_padding_zero = true;
        unsigned char data[4096];
        std::uint32_t consumed = 0u;
        while (consumed < length)
        {
            const std::uint32_t remaining = length - consumed;
            const std::size_t bytes = remaining < sizeof(data)
                                          ? static_cast<std::size_t>(remaining)
                                          : sizeof(data);
            if (!ReadExactAt(fd, static_cast<std::uint64_t>(cursor) + 8u + consumed,
                             data, bytes))
            {
                return PngIdentity::Invalid;
            }
            if (is_ihdr && consumed < sizeof(ihdr))
            {
                const std::size_t copy = bytes < sizeof(ihdr) - consumed
                                             ? bytes
                                             : sizeof(ihdr) - consumed;
                std::memcpy(ihdr + consumed, data, copy);
            }
            if (is_identity)
            {
                for (std::size_t i = 0u; i < bytes; ++i)
                {
                    const std::uint32_t position =
                        consumed + static_cast<std::uint32_t>(i);
                    if (position < sizeof(identity_header))
                        identity_header[position] = data[i];
                    else if (data[i] != 0u)
                        identity_padding_zero = false;
                }
            }
            crc = crc32(crc, data, static_cast<uInt>(bytes));
            consumed += static_cast<std::uint32_t>(bytes);
        }
        unsigned char stored_crc[4];
        if (!ReadExactAt(fd, static_cast<std::uint64_t>(cursor) + 8u + length,
                         stored_crc, sizeof(stored_crc)) ||
            static_cast<std::uint32_t>(crc) != ReadBe32(stored_crc))
        {
            return PngIdentity::Invalid;
        }
        cursor += 12u + length;
        ++chunk_index;

        if (is_ihdr)
        {
            if (length != sizeof(ihdr) || ihdr[10] != 0u || ihdr[11] != 0u ||
                ihdr[12] > 1u)
                return PngIdentity::Invalid;
            color_type = ihdr[9];
            contract_shape = ReadBe32(ihdr) == expected_width &&
                ReadBe32(ihdr + 4u) == expected_height && ihdr[8] == 8u &&
                (color_type == PNG_COLOR_TYPE_RGB ||
                 color_type == PNG_COLOR_TYPE_RGBA) && ihdr[12] == 0u;
            saw_ihdr = true;
        }
        if (is_idat)
        {
            if (!saw_ihdr) return PngIdentity::Invalid;
            saw_idat = true;
        }
        if (is_identity)
        {
            const bool ownership_shape = !saw_selfwrap_chunk && saw_idat &&
                length >= sizeof(identity_header) && identity_padding_zero &&
                identity_header[8] == expected_role;
            if (ownership_shape &&
                std::memcmp(identity_header, kPlaceholderIdentity,
                            sizeof(kPlaceholderIdentity)) == 0)
            {
                identity = PngIdentity::Placeholder;
            }
            else if (ownership_shape &&
                     std::memcmp(identity_header, kWrappedIdentity,
                                 sizeof(kWrappedIdentity)) == 0)
            {
                identity = PngIdentity::Wrapped;
            }
            else
            {
                identity = PngIdentity::Foreign;
            }
            saw_selfwrap_chunk = true;
        }
        if (is_iend)
        {
            if (!saw_ihdr || !saw_idat)
                return PngIdentity::Invalid;
            if (!contract_shape || cursor != end || !saw_selfwrap_chunk ||
                !previous_was_identity)
                return PngIdentity::Foreign;
            const bool color_matches =
                (identity == PngIdentity::Placeholder &&
                 color_type == PNG_COLOR_TYPE_RGBA) ||
                (identity == PngIdentity::Wrapped &&
                 color_type == PNG_COLOR_TYPE_RGB);
            return color_matches ? identity : PngIdentity::Foreign;
        }
        previous_was_identity = is_identity;
    }
    return PngIdentity::Invalid;
}

bool InspectPbp(const char *path, PbpInfo *info)
{
    if (!path || !info) return false;
    info->foreign_media = false;
    info->placeholder = false;
    info->wrapped = false;
    SceIoStat stat{};
    if (sceIoGetstat(path, &stat) < 0 || stat.st_size < kPbpHeaderBytes)
        return false;
    info->file_size = static_cast<std::uint64_t>(stat.st_size);
    const SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return false;
    bool ok = ReadExact(fd, info->header, sizeof(info->header)) &&
              std::memcmp(info->header, kPbpMagic, sizeof(kPbpMagic)) == 0;
    if (ok)
    {
        for (unsigned int i = 0u; i < 8u; ++i)
            info->offsets[i] = ReadLe32(info->header + 8u + i * 4u);
        ok = PbpOffsetsMonotonic(info->offsets, info->file_size) &&
             FindSfoSlotContract(fd, info->offsets);
        info->reserve_start = info->offsets[1];
        info->reserve_end = info->offsets[6];
    }
    if (ok)
    {
        const std::uint32_t icon_start = info->offsets[1];
        const std::uint64_t picture_start64 =
            static_cast<std::uint64_t>(icon_start) + kIconSlotBytes;
        const std::uint64_t data_start64 = picture_start64 + kPictureSlotBytes;
        ok = data_start64 <= UINT32_MAX &&
             info->offsets[2] == picture_start64 &&
             info->offsets[3] == picture_start64 &&
             info->offsets[4] == picture_start64 &&
             info->offsets[5] == data_start64 &&
             info->offsets[6] == data_start64;
        if (ok)
        {
            const std::uint32_t picture_start =
                static_cast<std::uint32_t>(picture_start64);
            const PngIdentity icon = ValidatePngSection(
                fd, icon_start, picture_start, 144u, 80u, 'I');
            const PngIdentity picture = ValidatePngSection(
                fd, picture_start, info->reserve_end, 480u, 272u, 'P');
            info->foreign_media = icon == PngIdentity::Foreign ||
                                  picture == PngIdentity::Foreign;
            info->placeholder = icon == PngIdentity::Placeholder &&
                                picture == PngIdentity::Placeholder;
            info->wrapped = icon == PngIdentity::Wrapped &&
                            picture == PngIdentity::Wrapped;
        }
    }
    if (sceIoClose(fd) < 0) ok = false;
    return ok;
}

bool RangeMatches(SceUID fd, std::uint32_t offset, const unsigned char *expected,
                  std::size_t bytes)
{
    if (sceIoLseek(fd, static_cast<SceOff>(offset), PSP_SEEK_SET) !=
        static_cast<SceOff>(offset))
    {
        return false;
    }
    unsigned char buffer[4096];
    std::size_t done = 0u;
    while (done < bytes)
    {
        const std::size_t remaining = bytes - done;
        const unsigned int request = remaining > sizeof(buffer)
                                         ? sizeof(buffer)
                                         : static_cast<unsigned int>(remaining);
        if (!ReadExact(fd, buffer, request) ||
            std::memcmp(buffer, expected + done, request) != 0)
        {
            return false;
        }
        done += request;
    }
    return true;
}

bool SyncPathDevice(const char *path)
{
    if (!path) return false;
    const char *colon = std::strchr(path, ':');
    if (!colon) return false;
    const std::size_t length = static_cast<std::size_t>(colon - path + 1);
    if (!length || length >= 8u) return false;
    char device[8];
    std::memcpy(device, path, length);
    device[length] = '\0';
    return sceIoSync(device, 0) >= 0;
}

int OpenExistingForWrite(const char *path, std::uint64_t expected_size,
                         SceUID *opened)
{
    if (!opened) return kSelfwrapCommitFailure;
    *opened = -1;
    SceIoStat stat{};
    if (sceIoGetstat(path, &stat) < 0 || stat.st_size < 0 ||
        static_cast<std::uint64_t>(stat.st_size) != expected_size)
    {
        return kSelfwrapCommitFailure;
    }
    // Never add O_CREAT here.  If the canonical PBP disappeared between stat
    // and open, creating an empty file would turn a write-open failure into a
    // destructive mutation.  PPSSPP's running-file denial is returned as a
    // deferred selfwrap; real PSP storage accepts this existing-only handle.
    const SceUID fd = sceIoOpen(path, PSP_O_WRONLY, 0);
    if (fd < 0) return kSelfwrapWriteOpenDenied;
    SceIoStat after{};
    if (sceIoGetstat(path, &after) < 0 || after.st_size < 0 ||
        static_cast<std::uint64_t>(after.st_size) != expected_size)
    {
        sceIoClose(fd);
        return kSelfwrapCommitFailure;
    }
    *opened = fd;
    return 0;
}

int WriteAndVerifyAssets(const char *path, const PbpInfo &contract,
                         const ByteBuffer &icon,
                         const ByteBuffer &picture,
                         PngIdentity expected_identity)
{
    if (icon.size != kIconSlotBytes || picture.size != kPictureSlotBytes ||
        (expected_identity != PngIdentity::Placeholder &&
         expected_identity != PngIdentity::Wrapped))
    {
        return kSelfwrapReserveFailure;
    }
    const std::uint32_t icon_start = contract.offsets[1];
    const std::uint32_t picture_start = contract.offsets[4];
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return kSelfwrapCommitFailure;
    unsigned char current_header[kPbpHeaderBytes];
    bool ok = ReadExactAt(fd, 0u, current_header, sizeof(current_header)) &&
              std::memcmp(current_header, contract.header,
                          sizeof(current_header)) == 0;
    if (sceIoClose(fd) < 0) ok = false;
    if (!ok) return kSelfwrapCommitFailure;

    const int open_result =
        OpenExistingForWrite(path, contract.file_size, &fd);
    if (open_result < 0) return open_result;
    ok = WriteExactAt(fd, icon_start, icon.data, icon.size) &&
         WriteExactAt(fd, picture_start, picture.data, picture.size);
    if (sceIoClose(fd) < 0) ok = false;
    if (!SyncPathDevice(path)) ok = false;
    if (!ok) return kSelfwrapCommitFailure;

    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return kSelfwrapCommitFailure;
    const PngIdentity icon_identity = ValidatePngSection(
        fd, icon_start, picture_start, 144u, 80u, 'I');
    const PngIdentity picture_identity = ValidatePngSection(
        fd, picture_start, contract.reserve_end, 480u, 272u, 'P');
    ok = RangeMatches(fd, icon_start, icon.data, icon.size) &&
         RangeMatches(fd, picture_start, picture.data, picture.size) &&
         icon_identity == expected_identity &&
         picture_identity == expected_identity;
    if (sceIoClose(fd) < 0) ok = false;
    return ok ? 0 : kSelfwrapCommitFailure;
}

int CommitAssets(const char *path, const PbpInfo &initial,
                 const ByteBuffer &icon, const ByteBuffer &picture,
                 PngIdentity expected_identity)
{
    if (initial.foreign_media) return kSelfwrapForeignMedia;
    const int result = WriteAndVerifyAssets(
        path, initial, icon, picture, expected_identity);
    if (result < 0) return result;

    PbpInfo verified{};
    if (!InspectPbp(path, &verified) ||
        (expected_identity == PngIdentity::Wrapped ? !verified.wrapped
                                                   : !verified.placeholder) ||
        verified.file_size != initial.file_size ||
        verified.reserve_start != initial.reserve_start ||
        verified.reserve_end != initial.reserve_end ||
        std::memcmp(verified.header, initial.header,
                    sizeof(initial.header)) != 0)
    {
        return kSelfwrapCommitFailure;
    }
    return 1;
}
} // namespace

extern "C" int th07_unified_find_original_data(
    const char *appdir, const char *launch_device, char *out,
    std::size_t out_size)
{
    if (!appdir || !appdir[0] || !launch_device || !launch_device[0] ||
        !out || !out_size)
    {
        return kSelfwrapBadArgument;
    }
    out[0] = '\0';
    char candidate[kMaxPath];
    int result = 0;
    if (JoinPath(candidate, sizeof(candidate), appdir, "th7"))
    {
        result = AcceptCandidate(candidate, out, out_size);
        if (result != 0) return result;
    }
    result = AcceptCandidate(appdir, out, out_size);
    if (result != 0) return result;
    result = ScanSiblingInstalls(launch_device, out, out_size);
    if (result != 0) return result;
    result = ScanDeviceRoot(launch_device, out, out_size);
    if (result != 0) return result;

    const char *other_device =
        std::strcmp(launch_device, "ef0:") == 0 ? "ms0:" : "ef0:";
    result = ScanSiblingInstalls(other_device, out, out_size);
    if (result != 0) return result;
    return ScanDeviceRoot(other_device, out, out_size);
}

extern "C" int th07_unified_selfwrap_needs_generation(
    const char *eboot_path, const char *data_root)
{
    if (!eboot_path || !eboot_path[0]) return kSelfwrapBadArgument;

    PbpInfo pbp{};
    if (!InspectPbp(eboot_path, &pbp)) return kSelfwrapBadPbp;
    if (pbp.foreign_media) return kSelfwrapForeignMedia;
    if (pbp.wrapped) return 0;

    return data_root && data_root[0] && IsCompleteDataRoot(data_root) ? 1 : 0;
}

extern "C" int th07_unified_try_selfwrap(
    const char *appdir, const char *eboot_path, const char *data_root)
{
    if (!appdir || !appdir[0] || !eboot_path || !eboot_path[0])
        return kSelfwrapBadArgument;

    PbpInfo pbp{};
    if (!InspectPbp(eboot_path, &pbp)) return kSelfwrapBadPbp;
    if (pbp.foreign_media) return kSelfwrapForeignMedia;
    if (pbp.wrapped) return 0;
    const bool have_original = data_root && data_root[0] &&
                               IsCompleteDataRoot(data_root);
    if (pbp.placeholder && !have_original) return 0;

    // Probe before archive decompression and PNG encoding. PPSSPP refuses a
    // write handle for the currently booted EBOOT; opening existing-only and
    // immediately closing it changes no bytes and lets that environment defer
    // cheaply while real PSP storage continues into the in-place path.
    SceUID probe = -1;
    const int probe_result =
        OpenExistingForWrite(eboot_path, pbp.file_size, &probe);
    if (probe_result < 0) return probe_result;
    if (sceIoClose(probe) < 0) return kSelfwrapCommitFailure;

    ByteBuffer icon{};
    ByteBuffer picture{};
    const bool generated = have_original
                               ? GenerateAssets(data_root, &icon, &picture)
                               : GeneratePlaceholderAssets(&icon, &picture);
    if (!generated)
        return kSelfwrapImageFailure;
    const int result = CommitAssets(
        eboot_path, pbp, icon, picture,
        have_original ? PngIdentity::Wrapped : PngIdentity::Placeholder);
    FreeBuffer(&icon);
    FreeBuffer(&picture);
    return result;
}

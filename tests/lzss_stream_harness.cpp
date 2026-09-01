#include "pbg4/Lzss.hpp"
#include "pbg4/Pbg4File.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>

const u32 g_SeekModes[3] = {0, 1, 2};
const char *g_AccessModes[3] = {"r", "w", "a"};

Pbg4File::Pbg4File() : file(nullptr), access(nullptr)
{
}

Pbg4File::~Pbg4File() = default;
bool Pbg4File::Open(const char *, const char *) { return false; }
void Pbg4File::Close() {}
u32 Pbg4File::Read(void *, u32) { return 0; }
bool Pbg4File::Write(void *, u32) { return false; }
u32 Pbg4File::Tell() { return 0; }
u32 Pbg4File::GetSize() { return 0; }
bool Pbg4File::Seek(u32, u32) { return false; }
void *Pbg4File::ReadRemaining(u32) { return nullptr; }
void Pbg4File::GetFullPath(char *, const char *) {}

namespace
{
struct MemoryPbg4File final : Pbg4File
{
    MemoryPbg4File(const u8 *bytes, u32 size) : bytes(bytes), size(size), cursor(0) {}

    u32 Read(void *out, u32 length) override
    {
        const u32 available = cursor < size ? size - cursor : 0;
        const u32 count = std::min(length, available);
        if (count)
        {
            std::memcpy(out, bytes + cursor, count);
            cursor += count;
        }
        return count;
    }

    bool Seek(u32 offset, u32 origin) override
    {
        if (origin != 0 || offset > size)
            return false;
        cursor = offset;
        return true;
    }

    const u8 *bytes;
    u32 size;
    u32 cursor;
};
} // namespace

int main()
{
    std::vector<u8> input(16384u);
    for (u32 i = 0; i < input.size(); ++i)
        input[i] = static_cast<u8>((i * 37u + (i >> 3u) * 11u) & 0xffu);

    i32 compressedBytes = 0;
    u8 *compressed =
        Lzss::Compress(input.data(), static_cast<i32>(input.size()), &compressedBytes);
    assert(compressed);
    assert(compressedBytes > 8);

    std::vector<u8> memoryOutput(input.size(), 0);
    Lzss::InitializeDictionary();
    assert(Lzss::Decompress(compressed, compressedBytes, memoryOutput.data(),
                            static_cast<u32>(memoryOutput.size())) == memoryOutput.data());
    assert(memoryOutput == input);

    std::vector<u8> streamOutput(input.size(), 0);
    MemoryPbg4File stream(compressed, static_cast<u32>(compressedBytes));
    Lzss::InitializeDictionary();
    assert(Lzss::DecompressFile(&stream, static_cast<u32>(compressedBytes),
                                streamOutput.data(),
                                static_cast<u32>(streamOutput.size())) == streamOutput.data());
    assert(streamOutput == memoryOutput);

    std::vector<u8> truncatedOutput(input.size(), 0);
    MemoryPbg4File truncated(compressed, static_cast<u32>(compressedBytes / 2));
    Lzss::InitializeDictionary();
    assert(!Lzss::DecompressFile(&truncated, static_cast<u32>(compressedBytes / 2),
                                 truncatedOutput.data(),
                                 static_cast<u32>(truncatedOutput.size())));

    std::free(compressed);
    return 0;
}

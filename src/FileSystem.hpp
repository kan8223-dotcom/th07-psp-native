#pragma once

#include "inttypes.hpp"

extern u32 g_LastFileSize;

namespace FileSystem
{
i32 CheckFileExists(const char *file);
u8 *OpenFile(const char *filepath, i32 isExternalResource);
void ReleaseFile(void *buffer);
i32 WriteDataToFile(const char *filename, const void *out, u32 bytesToWrite);
} // namespace FileSystem

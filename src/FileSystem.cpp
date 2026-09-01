#include "FileSystem.hpp"

#include <cstdio>
#include <cstring>

#include "GameErrorContext.hpp"
#include "Supervisor.hpp"
#include "pbg4/Pbg4Archive.hpp"
#if defined(TH07_PSP)
#include "fileio.hpp"
#include "optional_ram_budget.hpp"
#endif
#if defined(TH07_PSP_1000)
#include "../psp/psp1000_arena.hpp"
#endif

#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT) && \
    !defined(TH07_PSP_1000)
namespace
{
bool IsTitleWorkspaceTransientAnm(const char *filename)
{
    if (!filename || std::strncmp(filename, "face_", 5u) != 0)
    {
        return false;
    }
    const std::size_t length = std::strlen(filename);
    return length > 9u && std::strcmp(filename + length - 4u, ".anm") == 0;
}
} // namespace
#endif

u32 g_LastFileSize;

u8 *FileSystem::OpenFile(const char *filepath, i32 isExternalResource)
{
    FILE *file;
    u8 *buf;
    u32 fsize;
    const char *filename;

    if (!isExternalResource)
    {
        filename = strrchr(filepath, '\\');
        if (!filename)
        {
            filename = filepath;
        }
        else
        {
            filename++;
        }

        filename = strrchr(filename, '/');
        if (!filename)
        {
            filename = filepath;
        }
        else
        {
            filename++;
        }
        fsize = g_Pbg4Archive.GetEntrySize(filename);
        g_LastFileSize = fsize;
        if (fsize == 0)
        {
            g_GameErrorContext.Fatal("error : %s is not found in arcfile.\n", filename);
            return NULL;
        }
        if (fsize != 0)
        {
            Supervisor::DebugPrint("%s Decode ... \n", filename);
            buf = NULL;
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE) && \
    !defined(TH07_PSP_1000)
            // title01.anm is a 5.4 MiB temporary source.  Its first load occurs
            // before gameplay fragments the heap, so retain that successful
            // block as a process workspace and decompress every later title
            // return into the same contiguous address.
            if (strcmp(filename, "title01.anm") == 0)
            {
                buf = static_cast<u8 *>(Th07PspOptionalRamAcquireTitleArchive(fsize));
            }
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE_TRANSIENT)
            else if (IsTitleWorkspaceTransientAnm(filename))
            {
                // Every face_*.anm call site enters through LoadAnms.  A6v2's
                // compact path must release this serial whole-workspace loan
                // before returning; any non-compact source is rejected there.
                buf = static_cast<u8 *>(Th07PspOptionalRamAcquireTransientArchive(fsize));
            }
#endif
#endif
#if defined(TH07_PSP_1000)
            const char *extension = strrchr(filename, '.');
            // Small/non-embedded ANMs may intentionally retain their source
            // buffer for the lifetime of the texture.  Reserve the shared
            // scratch block only for the large embedded archives that the PSP
            // compaction path releases immediately after upload.
            // All embedded archives observed at 128 KiB or larger compact and
            // release their source immediately.  Routing stage backgrounds as
            // well as portraits through the arena prevents 1 MiB allocations
            // from failing after the first stage transition.
            if (fsize >= 128u * 1024u && extension && strcmp(extension, ".anm") == 0)
            {
                buf = static_cast<u8 *>(th07_psp_1000_acquire_anm(fsize));
            }
#endif
            if (!buf)
            {
                buf = (u8 *)malloc(fsize);
            }
            if (!buf)
            {
#if defined(TH07_PSP)
                th07_psp_boot_notef("ARC ALLOC NG %s %uK", filename, fsize / 1024u);
                th07_psp_heap_note("arc alloc failed");
#endif
                return NULL;
            }

            if (!g_Pbg4Archive.ReadDecompressEntry(filename, buf))
            {
#if defined(TH07_PSP)
                th07_psp_boot_notef("ARC DECODE NG %s %uK", filename, fsize / 1024u);
#endif
                FileSystem::ReleaseFile(buf);
                return NULL;
            }
            return buf;
        }
    }
    Supervisor::DebugPrint("%s Load ... \n", filepath);
#if defined(TH07_PSP)
    char resolvedPath[768];
    filepath = th07_psp_resolve_path(filepath, resolvedPath, sizeof(resolvedPath));
#endif
    file = fopen(filepath, "rb");
    if (!file)
    {
        Supervisor::DebugPrint("error : %s is not found.\n", filepath);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    fsize = ftell(file);
    buf = (u8 *)malloc(fsize);
    if (!buf)
    {
#if defined(TH07_PSP)
        th07_psp_boot_notef("FILE ALLOC NG %s %uK", filepath, fsize / 1024u);
        th07_psp_heap_note("file alloc failed");
#endif
        fclose(file);
        return NULL;
    }

    fseek(file, 0, SEEK_SET);
    if (fread(buf, 1, fsize, file) != fsize)
    {
        fclose(file);
        free(buf);
        return NULL;
    }
    g_LastFileSize = fsize;
    fclose(file);
    return buf;
}

void FileSystem::ReleaseFile(void *buffer)
{
    if (!buffer)
        return;
#if defined(TH07_PSP_TITLE_ARCHIVE_WORKSPACE) && \
    !defined(TH07_PSP_1000)
    if (Th07PspOptionalRamReleaseArchiveWorkspace(buffer))
        return;
#endif
#if defined(TH07_PSP_1000)
    if (th07_psp_1000_release_anm(buffer))
        return;
#endif
    free(buffer);
}

i32 FileSystem::CheckFileExists(const char *file)
{
    FILE *fp;

#if defined(TH07_PSP)
    char resolvedPath[768];
    file = th07_psp_resolve_path(file, resolvedPath, sizeof(resolvedPath));
#endif
    fp = fopen(file, "rb");
    if (fp)
    {
        fclose(fp);
        return true;
    }
    return false;
}

i32 FileSystem::WriteDataToFile(const char *filename, const void *out, u32 bytesToWrite)
{
    FILE *file;
    u32 bytesWritten;

#if defined(TH07_PSP)
    char resolvedPath[768];
    filename = th07_psp_resolve_path(filename, resolvedPath, sizeof(resolvedPath));
#endif
    file = fopen(filename, "wb");
    if (!file)
    {
        Supervisor::DebugPrint("error : %s write error\n", filename);
        return -1;
    }

    bytesWritten = fwrite(out, 1, bytesToWrite, file);
    if (bytesToWrite != bytesWritten)
    {
        fclose(file);
        Supervisor::DebugPrint("error : %s write error\n", filename);
        return -2;
    }
    fclose(file);
    Supervisor::DebugPrint("%s write ...\n", filename);
    return 0;
}

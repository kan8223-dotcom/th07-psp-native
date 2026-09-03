#include "Pbg4File.hpp"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "inttypes.hpp"
#if defined(TH07_PSP)
#include "fileio.hpp"
#endif

const u32 g_SeekModes[3] = {0, 1, 2};

// would it really not have been simpler to just type the letter where its used
// GLOBAL: TH07 0x0049ea70
const char *g_AccessModes[3] = {
    "r",
    "w",
    "a",
};

Pbg4File::Pbg4File()
{
    this->file = NULL;
    this->access = 0;
}

Pbg4File::~Pbg4File()
{
    Close();
}

bool Pbg4File::Open(const char *path, const char *mode)
{
    char local_114[264];
    i32 local_c;
    const char *local_8;

    local_c = 0;
    this->Close();
    for (local_8 = mode; *local_8 != '\0'; ++local_8)
    {
        if (*local_8 == 'r')
        {
            this->access = "rb";
            break;
        }
        if (*local_8 == 'w')
        {
            remove(path);
            this->access = "wb";
            break;
        }
        if (*local_8 == 'a')
        {
            local_c = 1;
            this->access = "ab";
            break;
        }
    }
    if (*local_8 == '\0')
    {
        return false;
    }
    else
    {
        GetFullPath(local_114, path);
#if defined(TH07_PSP_GO_BOOT_JITTER_DIAG)
        const unsigned long long openStartUs = th07_psp_boot_jitter_now();
#endif
        this->file = fopen(local_114, this->access);
#if defined(TH07_PSP_GO_BOOT_JITTER_DIAG)
        th07_psp_boot_jitter_record_archive_io(
            TH07_PSP_BOOT_JITTER_ARCHIVE_OPEN,
            th07_psp_boot_jitter_now() - openStartUs);
#endif
        if (!this->file)
        {
            return false;
        }

        if (local_c != 0)
        {
            fseek(this->file, 0, SEEK_END);
        }
        return true;
    }
}

void Pbg4File::Close()
{
    if (this->file)
    {
#if defined(TH07_PSP_GO_BOOT_JITTER_DIAG)
        const unsigned long long closeStartUs = th07_psp_boot_jitter_now();
#endif
        fclose(this->file);
#if defined(TH07_PSP_GO_BOOT_JITTER_DIAG)
        th07_psp_boot_jitter_record_archive_io(
            TH07_PSP_BOOT_JITTER_ARCHIVE_CLOSE,
            th07_psp_boot_jitter_now() - closeStartUs);
#endif
        this->file = NULL;
        this->access = 0;
    }
}

u32 Pbg4File::Read(void *data, u32 len)
{
    u32 local_8;

    local_8 = 0;
    if (!this->access || strcmp(this->access, "rb") != 0)
    {
        return 0;
    }

#if defined(TH07_PSP_GO_BOOT_JITTER_DIAG)
    const unsigned long long readStartUs = th07_psp_boot_jitter_now();
#endif
    local_8 = fread(data, 1, len, this->file);
#if defined(TH07_PSP_GO_BOOT_JITTER_DIAG)
    th07_psp_boot_jitter_record_archive_io(
        TH07_PSP_BOOT_JITTER_ARCHIVE_READ,
        th07_psp_boot_jitter_now() - readStartUs);
#endif
    return local_8;
}

bool Pbg4File::Write(void *data, u32 len)
{
    u32 local_8;

    local_8 = 0;
    if (!this->access || strcmp(this->access, "wb") != 0)
    {
        return false;
    }

    local_8 = fwrite(data, 1, len, this->file);
    return len == local_8;
}

u32 Pbg4File::Tell()
{
    if (!this->file)
    {
        return 0;
    }
    else
    {
#if defined(TH07_PSP_GO_BOOT_JITTER_DIAG)
        const unsigned long long metaStartUs = th07_psp_boot_jitter_now();
#endif
        const u32 offset = ftell(this->file);
#if defined(TH07_PSP_GO_BOOT_JITTER_DIAG)
        th07_psp_boot_jitter_record_archive_io(
            TH07_PSP_BOOT_JITTER_ARCHIVE_META,
            th07_psp_boot_jitter_now() - metaStartUs);
#endif
        return offset;
    }
}

u32 Pbg4File::GetSize()
{
    if (!this->file)
    {
        return 0;
    }
    else
    {
#if defined(TH07_PSP_GO_BOOT_JITTER_DIAG)
        const unsigned long long metaStartUs = th07_psp_boot_jitter_now();
#endif
        long cur = ftell(this->file);
        fseek(this->file, 0, SEEK_END);
        u32 size = ftell(this->file);
        fseek(this->file, cur, SEEK_SET);
#if defined(TH07_PSP_GO_BOOT_JITTER_DIAG)
        th07_psp_boot_jitter_record_archive_io(
            TH07_PSP_BOOT_JITTER_ARCHIVE_META,
            th07_psp_boot_jitter_now() - metaStartUs);
#endif
        return size;
    }
}

bool Pbg4File::Seek(u32 offset, u32 seekFrom)
{
    if (!this->file)
    {
        return false;
    }

#if defined(TH07_PSP_GO_BOOT_JITTER_DIAG)
    const unsigned long long seekStartUs = th07_psp_boot_jitter_now();
#endif
    fseek(this->file, offset, seekFrom);
#if defined(TH07_PSP_GO_BOOT_JITTER_DIAG)
    th07_psp_boot_jitter_record_archive_io(
        TH07_PSP_BOOT_JITTER_ARCHIVE_SEEK,
        th07_psp_boot_jitter_now() - seekStartUs);
#endif
    return true;
}

void *Pbg4File::ReadRemaining(u32 max)
{
    void *hMem;
    u32 DVar2;
    u32 DVar3;

    if (!this->access || strcmp(this->access, "rb") != 0)
    {
        return NULL;
    }

    DVar2 = this->GetSize();
    if (DVar2 > max)
    {
        return NULL;
    }

    hMem = calloc(1, DVar2);
    if (!hMem)
    {
        return NULL;
    }

    DVar3 = this->Tell();
    if (!this->Seek(DVar3, g_SeekModes[0]))
    {
        return NULL;
    }

    if (this->Read(hMem, DVar2) == 0)
    {
        if (hMem)
        {
            free(hMem);
            hMem = NULL;
        }
        return NULL;
    }

    this->Seek(DVar3, g_SeekModes[0]);
    return hMem;
}

void Pbg4File::GetFullPath(char *out, const char *filename)
{
#if defined(TH07_PSP)
    th07_psp_resolve_path(filename, out, 260);
    return;
#endif
#ifdef _WIN32
    if (strchr(filename, ':') != nullptr)
    {
        strcpy(out, filename);
        return;
    }
#else
    if (filename[0] == '/')
    {
        snprintf(out, 260, "%s", filename);
        return;
    }
#endif

    char *base = SDL_GetBasePath();
    if (base)
    {
        snprintf(out, 260, "%s%s", base, filename);
        SDL_free(base);
    }
    else
    {
        snprintf(out, 260, "%s", filename);
    }
}

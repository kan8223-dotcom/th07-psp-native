#include "MusicRoom.hpp"

#include <algorithm>
#include <cstdio>

#include "AnmIdx.hpp"
#include "AnmManager.hpp"
#include "AsciiManager.hpp"
#include "Chain.hpp"
#include "Controller.hpp"
#include "FileSystem.hpp"
#include "SoundPlayer.hpp"
#include "Supervisor.hpp"
#if defined(TH07_PSP)
#include "fileio.hpp"
#include "graphics/PspGuGraphics.hpp"

namespace
{
void DrawMusicTitle(MusicRoom *room, i32 idx)
{
    if (idx < 0 || idx >= room->numDescriptors || room->titleRendered[idx])
    {
        return;
    }
    AnmManager::DrawVmTextFmt(g_AnmManager, room->titleSprites + idx, 0xc0e0ff, 0x302080,
                              room->trackDescriptors[idx].title);
    room->titleRendered[idx] = 1;
}

bool DrawNextVisibleMusicTitle(MusicRoom *room)
{
    // Keep the selected row usable first, then fill the rest of the visible
    // window from top to bottom.  Exactly one FreeType raster/upload is done
    // in an update so entering or scrolling never blocks on ten rows at once.
    if (room->cursor >= room->listingOffset && room->cursor < room->listingOffset + 10 &&
        room->cursor < room->numDescriptors && !room->titleRendered[room->cursor])
    {
        DrawMusicTitle(room, room->cursor);
        return true;
    }

    const i32 end = std::min(room->listingOffset + 10, room->numDescriptors);
    for (i32 i = room->listingOffset; i < end; ++i)
    {
        if (!room->titleRendered[i])
        {
            DrawMusicTitle(room, i);
            return true;
        }
    }
    return false;
}

void QueueMusicDescription(MusicRoom *room, i32 trackIdx)
{
    room->selectedIdx = trackIdx;
    room->descriptionRenderIdx = 0;
    for (AnmVm &description : room->descriptionSprites)
    {
        description.active = 0;
        description.pendingInterrupt = 1;
    }
}

void DrawNextMusicDescriptionLine(MusicRoom *room)
{
    const i32 lineIdx = room->descriptionRenderIdx;
    if (lineIdx < 0 || lineIdx >= 8)
    {
        return;
    }

    char line[66] = {};
    memcpy(line, room->trackDescriptors[room->selectedIdx].description[lineIdx], 64);
    if (line[0] != '\0')
    {
        room->descriptionSprites[lineIdx].active = 1;
        AnmManager::DrawVmTextFmt(g_AnmManager, room->descriptionSprites + lineIdx,
                                  0xffe0c0, 0x300000, line);
    }
    room->descriptionSprites[lineIdx].pendingInterrupt = 1;
    room->descriptionRenderIdx = lineIdx == 7 ? -1 : lineIdx + 1;
}

} // namespace
#endif

ZunResult MusicRoom::CheckInputEnable()
{
    i32 i;

    if (this->waitFramesCounter == 0)
    {
        for (i = 0; i < 31; i++)
        {
            if (this->cursor == i)
            {
                this->titleSprites[i].pendingInterrupt = 1;
            }
            else
            {
                this->titleSprites[i].pendingInterrupt = 2;
            }
        }
        for (i = 0; i < 8; i++)
        {
            this->descriptionSprites[i].pendingInterrupt = 1;
        }
    }
    if (this->waitFramesCounter >= 8)
    {
        this->enableInput = 1;
    }
    return ZUN_SUCCESS;
}

i32 MusicRoom::ProcessInput()
{
#if !defined(TH07_PSP)
    char local_54[66];
#endif
    i32 i;

    if (WAS_PRESSED_RAW(TH_BUTTON_UP))
    {
        this->cursor--;
        if (this->cursor < 0)
        {
            this->cursor = this->numDescriptors - 1;
            this->listingOffset = this->numDescriptors - 10;
            if (this->listingOffset < 0)
            {
                this->listingOffset = 0;
            }
        }
        else if (this->listingOffset > this->cursor)
        {
            this->listingOffset = this->cursor;
        }
        for (i = 0; i < 31; i++)
        {
            if (this->cursor == i)
            {
                this->titleSprites[i].pendingInterrupt = 1;
            }
            else
            {
                this->titleSprites[i].pendingInterrupt = 2;
            }
        }
    }
    if (WAS_PRESSED_RAW(TH_BUTTON_DOWN))
    {
        this->cursor = this->cursor + 1;
        if (this->cursor >= this->numDescriptors)
        {
            this->cursor = 0;
            this->listingOffset = 0;
        }
        else
        {
            if (this->listingOffset <= this->cursor - 10)
            {
                this->listingOffset = this->cursor - 9;
            }
        }
        for (i = 0; i < 31; i++)
        {
            if (this->cursor == i)
            {
                this->titleSprites[i].pendingInterrupt = 1;
            }
            else
            {
                this->titleSprites[i].pendingInterrupt = 2;
            }
        }
    }
    if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
    {
        this->selectedIdx = this->cursor;
        if (g_Supervisor.cfg.preloadBgm)
        {
            g_SoundPlayer.StartBGM("thbgm.dat");
        }
        g_Supervisor.PlayAudio(this->trackDescriptors[this->selectedIdx].path);
#if defined(TH07_PSP)
        // Dispatch AUDIO_START before FreeType redraws the eight comment
        // lines.  The producer can fill its ring while those glyphs are
        // rendered, so selecting a track is no longer followed by a long
        // silent pause on Memory Stick hardware.
        g_SoundPlayer.ProcessQueues();
        QueueMusicDescription(this, this->selectedIdx);
#else
        for (i = 0; i < 8; i++)
        {
            memset(local_54, 0, sizeof(local_54));
            memcpy(local_54, this->trackDescriptors[this->selectedIdx].description[i], 64);
            if (local_54[0] != '\0')
            {
                this->descriptionSprites[i].active = 1;
                AnmManager::DrawVmTextFmt(g_AnmManager, this->descriptionSprites + i, 0xffe0c0,
                                          0x300000, local_54);
            }
            else
            {
                this->descriptionSprites[i].active = 0;
            }
            this->descriptionSprites[i].pendingInterrupt = 1;
        }
#endif
    }
    if (WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))
    {
#if defined(TH07_PSP)
        g_Supervisor.StopAudio();
        while (g_SoundPlayer.ProcessQueues())
        {
        }
#endif
        g_Supervisor.curState = 1;
        return 1;
    }

    return 0;
}

u32 MusicRoom::OnUpdate(MusicRoom *arg)
{
    i32 iVar1;
    i32 i;

    iVar1 = arg->enableInput;
recheck:
    switch (arg->enableInput)
    {
    case 0:
        if (!arg->CheckInputEnable())
        {
            break;
        }
        goto recheck;
    case 1:
        if (arg->ProcessInput())
        {
            return CHAIN_CALLBACK_RESULT_CONTINUE_AND_REMOVE_JOB;
        }
    default:
        break;
    }
    if (iVar1 != arg->enableInput)
    {
        arg->waitFramesCounter = 0;
    }
    else
    {
        arg->waitFramesCounter++;
    }
    g_AnmManager->ExecuteScript(&arg->vm[0]);
#if defined(TH07_PSP)
    const i32 titleEnd = std::min(arg->listingOffset + 10, arg->numDescriptors);
    for (i = arg->listingOffset; i < titleEnd; i++)
#else
    for (i = 0; i < 31; i++)
#endif
    {
        g_AnmManager->ExecuteScript(&arg->titleSprites[i]);
    }
    for (i = 0; i < 8; i++)
    {
        g_AnmManager->ExecuteScript(&arg->descriptionSprites[i]);
    }
#if defined(TH07_PSP)
    if (!DrawNextVisibleMusicTitle(arg))
    {
        DrawNextMusicDescriptionLine(arg);
    }
#endif
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

u32 MusicRoom::OnDraw(MusicRoom *arg)
{
    ZunVec3 local_18;
    char local_c[4];
    i32 i;

    local_c[0] = 127;
    local_c[1] = 0;
    g_AnmManager->SetTexture(0);
    g_AnmManager->CopySurfaceToBackBuffer(0, 0, 0, 0, 0);
    g_AnmManager->DrawNoRotation(&arg->vm[0]);
    for (i = arg->listingOffset; i < arg->listingOffset + 10; i++)
    {
        if (i >= arg->numDescriptors)
        {
            break;
        }
        g_AsciiManager.SetColor(arg->titleSprites[i].color.color);
        arg->titleSprites[i].pos.x = 93.0f;
        arg->titleSprites[i].pos.y = (f32)((i + 1 - arg->listingOffset) * 18) + 104.0f - 20.0f;
        arg->titleSprites[i].pos.z = 0.0f;
        g_AnmManager->DrawNoRotation(arg->titleSprites + i);
        local_18 = arg->titleSprites[i].pos;
        local_18.x -= 60.0f;
        if (arg->cursor == i)
        {
            g_AsciiManager.AddString(&local_18, local_c);
        }
        local_18.x += 15.0f;
        AsciiManager::AddFormatText(&g_AsciiManager, &local_18, "%2d.", i + 1);
    }
    i++;
    for (i = 0; i < 8; i++)
    {
        g_AnmManager->DrawNoRotation(&arg->descriptionSprites[i]);
    }
    g_AsciiManager.color = 0xffffffff;
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

ZunResult MusicRoom::AddedCallback(MusicRoom *arg)
{
#if !defined(TH07_PSP)
    char lineCharBuffer[66];
#endif
    char *firstChar;
    i32 charIdx;
    char *curChar;
    i32 lineIdx;
    i32 offset;
#if defined(TH07_PSP)
    th07_psp_boot_note("music added begin");
    memset(arg->titleRendered, 0, sizeof(arg->titleRendered));
#endif
    if (g_AnmManager->LoadSurface(0, "data/result/music.jpg") != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }
#if defined(TH07_PSP)
    th07_psp_boot_note("music background decoded");
#endif
    if (g_AnmManager->LoadAnms(ANM_FILE_MUSIC, "data/music00.anm", ANM_OFFSET_MUSIC) != ZUN_SUCCESS)
    {
        g_AnmManager->ReleaseSurface(0);
        return ZUN_ERROR;
    }
#if defined(TH07_PSP)
    th07_psp_boot_note("music anm loaded");
#endif

    g_AnmManager->SetAnmIdxAndExecuteScript(&arg->vm[0], 2304);
    arg->waitFramesCounter = 0;
    curChar = (char *)FileSystem::OpenFile("data/musiccmt.txt", 0);
    firstChar = curChar;
    if ((u8 *)curChar == NULL)
    {
        g_AnmManager->ReleaseAnm(46);
        g_AnmManager->ReleaseAnm(47);
        g_AnmManager->ReleaseSurface(0);
        return ZUN_ERROR;
    }

    arg->trackDescriptors = new TrackDescriptor[32];
    offset = -1;
    while (((uintptr_t)curChar - (uintptr_t)firstChar) < g_LastFileSize)
    {
        if (*curChar == '@')
        {
            curChar++;
            offset++;
            charIdx = 0;
            while (*curChar != '\n' && *curChar != '\r')
            {
                arg->trackDescriptors[offset].path[charIdx] = *curChar;
                curChar++;
                charIdx++;
                if (((uintptr_t)curChar - (uintptr_t)firstChar) >= g_LastFileSize)
                {
                    goto LAB_0043b195;
                }
            }
            while (*curChar == '\n' || *curChar == '\r')
            {
                curChar++;
                if (((uintptr_t)curChar - (uintptr_t)firstChar) >= g_LastFileSize)
                {
                    goto LAB_0043b195;
                }
            }
            charIdx = 0;
            while (*curChar != '\n' && *curChar != '\r')
            {
                arg->trackDescriptors[offset].title[charIdx] = *curChar;
                curChar++;
                charIdx++;
                if (((uintptr_t)curChar - (uintptr_t)firstChar) >= g_LastFileSize)
                {
                    goto LAB_0043b195;
                }
            }
            while (*curChar == '\n' && *curChar == '\r')
            {
                curChar++;
                if (((uintptr_t)curChar - (uintptr_t)firstChar) >= g_LastFileSize)
                {
                    goto LAB_0043b195;
                }
            }
            for (lineIdx = 0; lineIdx < 8; lineIdx++)
            {
                if (*curChar == '@')
                {
                    break;
                }

                memset(arg->trackDescriptors[offset].description[lineIdx], 0,
                       sizeof(arg->trackDescriptors[offset].description[lineIdx]));
                charIdx = 0;
                while (*curChar != '\n' && *curChar != '\r')
                {
                    arg->trackDescriptors[offset].description[lineIdx][charIdx] = *curChar;
                    curChar++;
                    charIdx++;
                    if (((uintptr_t)curChar - (uintptr_t)firstChar) >= g_LastFileSize)
                    {
                        goto LAB_0043b195;
                    }
                }
                while (*curChar == '\n' || *curChar == '\r')
                {
                    curChar++;
                    if (((uintptr_t)curChar - (uintptr_t)firstChar) >= g_LastFileSize)
                    {
                        goto LAB_0043b195;
                    }
                }
            }
        }
        else
        {
            curChar++;
        }
    }
LAB_0043b195:
    arg->numDescriptors = offset + 1;
#if defined(TH07_PSP)
    th07_psp_boot_note("music comments parsed");
#endif
    for (offset = 0; offset < arg->numDescriptors; offset++)
    {
        g_AnmManager->SetAnmIdxAndExecuteScript(&arg->titleSprites[offset], offset + 2305);
#if !defined(TH07_PSP)
        AnmManager::DrawVmTextFmt(g_AnmManager, arg->titleSprites + offset, 0xc0e0ff, 0x302080,
                                  arg->trackDescriptors[offset].title);
#endif
        arg->titleSprites[offset].pos.x = 93.0f;
        arg->titleSprites[offset].pos.y = (f32)((offset + 1) * 18) + 104.0f - 20.0f;
        arg->titleSprites[offset].pos.z = 0.0f;
        arg->titleSprites[offset].anchor = 3;
    }
    for (offset = 0; offset < 8; offset++)
    {
        g_AnmManager->SetAnmIdxAndExecuteScript(&arg->descriptionSprites[offset], offset + 1799);
#if defined(TH07_PSP)
        // Initial comments are drawn below after every sprite has its script.
        arg->descriptionSprites[offset].active = 0;
#else
        memset(lineCharBuffer, 0, sizeof(lineCharBuffer));
        memcpy(lineCharBuffer, arg->trackDescriptors[arg->selectedIdx].description[offset], 64);
        if (*lineCharBuffer != '\0')
        {
            arg->descriptionSprites[offset].active = 1;
            AnmManager::DrawVmTextFmt(g_AnmManager, arg->descriptionSprites + offset, 0xffe0c0,
                                      0x300000, (char *)&lineCharBuffer);
        }
        else
        {
            arg->descriptionSprites[offset].active = 0;
        }
#endif
    }
#if defined(TH07_PSP)
    QueueMusicDescription(arg, arg->selectedIdx);
    Th07PspTrimTextureCache();
    th07_psp_boot_note("music added ready");
#endif
    free(firstChar);
    return ZUN_SUCCESS;
}

ZunResult MusicRoom::DeletedCallback(MusicRoom *arg)
{
#if defined(TH07_PSP)
    th07_psp_boot_note("music delete begin");
#endif
    g_Chain.Cut(arg->drawChain);
    arg->drawChain = NULL;
    delete[] arg->trackDescriptors;
    arg->trackDescriptors = NULL;
    g_AnmManager->ReleaseSurface(0);
    g_AnmManager->ReleaseAnm(46);
    g_AnmManager->ReleaseAnm(47);
#if defined(TH07_PSP)
    const unsigned int releasedBytes = Th07PspTrimTextureCache();
    char message[80];
    std::snprintf(message, sizeof(message), "music delete ready trim %uK",
                  releasedBytes / 1024u);
    th07_psp_boot_note(message);
#endif
    return ZUN_SUCCESS;
}

ZunResult MusicRoom::RegisterChain()
{
    static MusicRoom g_MusicRoom;
    MusicRoom *musicRoom = &g_MusicRoom;

    musicRoom->calcChain = g_Chain.CreateElem((ChainCallback)OnUpdate);
    musicRoom->calcChain->arg = musicRoom;
    musicRoom->calcChain->addedCallback = (ChainLifecycleCallback)AddedCallback;
    musicRoom->calcChain->deletedCallback = (ChainLifecycleCallback)DeletedCallback;
    if (g_Chain.AddToCalcChain(musicRoom->calcChain, 3))
    {
        g_Chain.Cut(musicRoom->calcChain);
        musicRoom->calcChain = NULL;
        return ZUN_ERROR;
    }

    musicRoom->drawChain = g_Chain.CreateElem((ChainCallback)OnDraw);
    musicRoom->drawChain->arg = musicRoom;
    g_Chain.AddToDrawChain(musicRoom->drawChain, 0);
    return ZUN_SUCCESS;
}

#pragma once

#include <cstring>

#include "AnmVm.hpp"
#include "Chain.hpp"

struct TrackDescriptor
{
    TrackDescriptor()
    {
        memset(this, 0, sizeof(TrackDescriptor));
    }

    char path[64];
    char title[66];
    char description[8][66];
};

struct MusicRoom
{
    MusicRoom()
    {
        memset(this, 0, sizeof(MusicRoom));
    }

    static ZunResult RegisterChain();

    static ZunResult AddedCallback(MusicRoom *arg);
    static ZunResult DeletedCallback(MusicRoom *arg);
    static u32 OnUpdate(MusicRoom *arg);
    static u32 OnDraw(MusicRoom *arg);

    ZunResult CheckInputEnable();
    i32 ProcessInput();

    struct ChainElem *calcChain;
    struct ChainElem *drawChain;
    i32 waitFramesCounter;
    i32 enableInput;
    i32 cursor;
    i32 selectedIdx;
    i32 listingOffset;
    i32 numDescriptors;
    TrackDescriptor *trackDescriptors;
    AnmVm vm[1]; // ZUN quirk: WHY is this an array
    AnmVm titleSprites[31];
    AnmVm descriptionSprites[8];
#if defined(TH07_PSP)
    // PSP fills only the visible rows, one per update.  This keeps room entry
    // and scrolling from rasterising a whole page of Japanese strings in one
    // blocking callback.
    u8 titleRendered[31];
    // Comments remain part of the normal room presentation, but rendering
    // one line per update avoids a single long FreeType/atlas upload stall.
    i32 descriptionRenderIdx;
#endif
};

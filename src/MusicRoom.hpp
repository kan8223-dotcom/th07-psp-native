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
    // The initial page is prepared before the entrance animation; scrolling
    // renders only the newly exposed row.
    u8 titleRendered[31];
    // This occupies the byte which was tail padding after titleRendered, so
    // the PSP MusicRoom object does not grow.
    u8 initialDescriptionState;
#endif
};

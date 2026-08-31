#pragma once

#include "AnmVm.hpp"
#include "Player.hpp"

extern u8 g_ItemDropTable[32];

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
struct Th07PspMeBulletCompactJob;
struct Th07PspMeItemMotionSeed;
struct Th07PspMeItemMotionOutput;
void PspSetMeItemMotionView(
    const Th07PspMeBulletCompactJob *job,
    const Th07PspMeItemMotionSeed *seed,
    const Th07PspMeItemMotionOutput *output);
void PspTakeMeItemMotionFrameStats(
    u32 *active, u32 *candidates, u32 *adopted,
    u32 *slotRejects, u32 *globalRejects);
void PspClearMeItemMotionView();
#endif

void AngleToVector(ZunVec3 *out, f32 angle, f32 speed);

typedef enum ItemType
{
    ITEM_POWER_SMALL = 0,
    ITEM_POINT = 1,
    ITEM_POWER_BIG = 2,
    ITEM_BOMB = 3,
    ITEM_FULL_POWER = 4,
    ITEM_LIFE = 5,
    ITEM_POINT_BULLET = 6,
    ITEM_CHERRY = 7,
    ITEM_CHERRY_SMALL = 8,
    ITEM_STAR = 9,
    ITEM_NO_ITEM = 255
} ItemType;

struct Item
{
    Item();

    i32 IsBelowPoc()
    {
        return this->currentPosition.y < g_Player.shooterData->pocY;
    }

    i32 OffsetFromPoc()
    {
        return this->currentPosition.y - g_Player.shooterData->pocY;
    }

    i32 ShouldAwardMaxScore()
    {
        return this->currentPosition.y < g_Player.shooterData->pocY || this->autoCollect;
    }

    AnmVm sprite;
    ZunVec3 currentPosition;
    ZunVec3 startPosition;
    ZunVec3 targetPosition;
    ZunTimer timer;
    i8 itemType;
    i8 isInUse;
    i8 isOnscreen;
    i8 state;
    i8 autoCollect;
    // pad 3
    struct Item *next;
};

#if defined(TH07_PSP_1000)
static_assert(sizeof(Item) == 648,
              "PSP-1000 Item growth requires re-auditing the stage pool arena");
#endif

struct ItemManager
{
    // The item index parity is gameplay-visible, and replay-compatible spawn
    // order requires all 1,100 original slots even on PSP-1000.
    static constexpr i32 kItemCapacity = 1100;

    ItemManager();

    void Reset();
#if defined(TH07_PSP_1000)
    bool PspEnsureItemPool();
    void PspReleaseItemPool();
#endif

    void ActivateAllItems();
    void DespawnAllItems(i32 param_1);
    void OnUpdate();
    void OnDraw();
    void RemoveAllItems();
    Item *SpawnItem(ZunVec3 *heading, i32 itemType, i32 state);

#if defined(TH07_PSP_1000)
    // Keep the full original slot space in the shared stage arena. The extra
    // payload is the inherited spawn-failure sentinel.
    struct Item *items;
#else
    struct Item items[kItemCapacity + 1];
#endif
    Item *ItemAt(i32 logicalIndex)
    {
#if defined(TH07_PSP_1000)
        if (logicalIndex < 0 || logicalIndex >= kItemCapacity)
        {
            return nullptr;
        }
#endif
        return &items[logicalIndex];
    }

    Item *ItemFailureSentinel()
    {
        return &items[kItemCapacity];
    }
    i32 nextIndex;
    i32 activeItemCount;
    struct Item listHead;
    struct Item *listTail;
#if defined(TH07_PSP)
    // Item embeds an AnmVm; probing empty slots costs far more cache
    // traffic than walking this 140-byte map first.
    u32 pspActiveItemBits[(kItemCapacity + 31) / 32];

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    // I-ME7 leaves gameplay and ANM-script authority on SC. These sidecars
    // describe only the immutable, post-update Item draw list consumed by the
    // existing asynchronous render command. Keeping them manager-owned
    // preserves Item's canonical 648-byte ABI.
    u32 pspMeItemSlotGenerations[kItemCapacity];
    f32 pspMeItemRenderSin[kItemCapacity];
    f32 pspMeItemRenderCos[kItemCapacity];
    u32 pspMeItemPrepareSerial;
    u32 pspMeItemPreparedSerial;
    u32 pspMeItemPreparedCount;
    u32 pspMeItemPreparedPrefixCount;
    Item *pspMeItemPreparedPrefixTail;
    Item *pspMeItemPreparedSuffixHead;
    u32 pspMeItemRequestedPrefixCount;
    // Counted while the canonical list is already being linked.  Adaptive
    // admission can therefore reject Item ME work without another pool walk.
    u32 pspMeItemListCount;

    bool PspPrepareMeItemRenderStream();
    void PspDrawCanonicalItemSuffix(Item *suffixHead);
    bool PspMeItemRenderStreamPrepared() const
    {
        return pspMeItemPreparedSerial != 0u &&
               pspMeItemPreparedSerial == pspMeItemPrepareSerial;
    }
#endif

    bool PspIsItemSlotTracked(i32 index) const
    {
        return (pspActiveItemBits[index >> 5] & (1u << (index & 31))) != 0;
    }

    void PspTrackItemSlot(i32 index)
    {
        pspActiveItemBits[index >> 5] |= 1u << (index & 31);
    }

    void PspForgetItemSlot(i32 index)
    {
        pspActiveItemBits[index >> 5] &= ~(1u << (index & 31));
    }
#endif
};
extern ItemManager g_ItemManager;

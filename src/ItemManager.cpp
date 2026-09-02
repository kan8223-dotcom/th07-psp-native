#include "ItemManager.hpp"

#include "AnmManager.hpp"
#include "AsciiManager.hpp"
#include "BulletManager.hpp"
#include "EffectManager.hpp"
#include "EnemyManager.hpp"
#include "GameManager.hpp"
#include "Gui.hpp"
#include "Player.hpp"
#include "Rng.hpp"
#include "SoundPlayer.hpp"
#include "ZunMath.hpp"

#if defined(TH07_PSP_PERF_A1_SAME)
#include "../psp/graphics/PspGuGraphics.hpp"
#endif

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
#include "../psp/audio_me.h"
#include "Supervisor.hpp"

#include <cstring>
#endif

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
#if !defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
#error TH07_PSP_ME_ITEM_RENDER_STREAM requires the I-ME5 direct-list owner
#endif
#include <pspmath.h>
#include <cmath>
#endif

#if defined(TH07_PSP_1000)
#include "../psp/fileio.hpp"
#include "../psp/psp1000_arena.hpp"

#include <cstdlib>
#endif

i32 g_FullPowerScoreBonus[30] = {10,   20,   30,   40,   50,   60,   70,   80,    90,    100,
                                 200,  300,  400,  500,  600,  700,  800,  900,   1000,  2000,
                                 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000};

i32 g_PowerLevels[9] = {8, 16, 32, 48, 64, 80, 96, 128, 999};

u8 g_ItemDropTable[32] = {0, 0, 1, 0, 1, 0, 0, 7, 1, 1, 0, 0, 7, 1, 1, 0,
                          1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 7, 1, 1, 1, 0, 2};

ItemManager g_ItemManager;

#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
namespace
{
struct PspMeItemMotionScView
{
    bool active;
    bool authorityClosed;
    Th07PspMeBulletCompactJob job;
    const Th07PspMeItemMotionSeed *seed;
    const Th07PspMeItemMotionOutput *output;
    u32 candidates;
    u32 adopted;
    u32 slotRejects;
    u32 globalRejects;
};

PspMeItemMotionScView gPspMeItemMotionScView{};

inline u32 PspMeItemMotionFloatBits(float value)
{
    u32 bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline bool PspMeItemMotionGlobalsMatch(
    const Th07PspMeBulletCompactJob &job)
{
    return g_Player.shooterData && g_GameManager.globals &&
           static_cast<u32>(static_cast<u8>(g_Player.playerState)) ==
               job.playerState &&
           PspMeItemMotionFloatBits(g_Player.positionCenter.x) ==
               job.itemPlayerPosXBits &&
           PspMeItemMotionFloatBits(g_Player.positionCenter.y) ==
               job.itemPlayerPosYBits &&
           PspMeItemMotionFloatBits(
               g_Player.shooterData->itemCollectSpeed) ==
               job.itemCollectSpeedBits &&
           PspMeItemMotionFloatBits(g_Player.shooterData->pocY) ==
               job.itemPocYBits &&
           PspMeItemMotionFloatBits(
               g_Supervisor.effectiveFramerateMultiplier) ==
               job.itemFramerateMultiplierBits &&
           static_cast<i32>(g_GameManager.globals->currentPower) ==
               job.itemCurrentPowerClass &&
           g_GameManager.difficulty == job.itemDifficulty &&
           static_cast<u32>(g_Player.hasBorder == 1) ==
               job.itemHasBorder;
}

enum PspMeItemMotionAdoptRoute
{
    PSP_ME_ITEM_MOTION_CANONICAL = 0,
    PSP_ME_ITEM_MOTION_TO_BOUNDS = 1,
    PSP_ME_ITEM_MOTION_TO_COLLISION = 2
};

PspMeItemMotionAdoptRoute PspTryAdoptMeItemMotion(Item *item, u32 slot)
{
    PspMeItemMotionScView &view = gPspMeItemMotionScView;
    if (!view.active || view.authorityClosed || !item ||
        slot >= TH07_PSP_ME_ITEM_MOTION_MAX_SLOTS ||
        !view.seed || !view.output)
        return PSP_ME_ITEM_MOTION_CANONICAL;

    const u32 bit = 1u << (slot & 31u);
    const u32 word = slot >> 5u;
    if ((view.seed->candidateBits[word] & bit) == 0u ||
        (view.output->candidateBits[word] & bit) == 0u)
        return PSP_ME_ITEM_MOTION_CANONICAL;
    ++view.candidates;

    if (!PspMeItemMotionGlobalsMatch(view.job))
    {
        view.authorityClosed = true;
        ++view.globalRejects;
        return PSP_ME_ITEM_MOTION_CANONICAL;
    }

    const Th07PspMeItemMotionSeedSlot &input = view.seed->slots[slot];
    const Th07PspMeItemMotionSlotResult &result = view.output->slots[slot];
#if defined(TH07_PSP_ME_ITEM_SEED_SLIM)
    const u32 inputState =
        ((view.seed->stateBit0[word] & bit) != 0u ? 1u : 0u) |
        ((view.seed->stateBit1[word] & bit) != 0u ? 2u : 0u);
    const u32 inputAuto =
        (view.seed->autoCollectBits[word] & bit) != 0u ? 1u : 0u;
    // A C2c candidate can only be captured from an Item whose inUse byte is
    // exactly one.  candidateBits is therefore the compact inUse proof.
    const u32 inputInUse = 1u;
#else
    const u32 inputState = input.stateAndFlags &
        TH07_PSP_ME_ITEM_MOTION_INPUT_STATE_MASK;
    const u32 inputAuto =
        (input.stateAndFlags >>
         TH07_PSP_ME_ITEM_MOTION_INPUT_AUTOCOLLECT_SHIFT) & 0xffu;
    const u32 inputInUse =
        (input.stateAndFlags >>
         TH07_PSP_ME_ITEM_MOTION_INPUT_INUSE_SHIFT) & 0xffu;
#endif

    bool exact = item->isInUse == 1 && inputInUse == 1u &&
        g_ItemManager.pspMeItemSlotGenerations[slot] == input.generation &&
        result.generation == input.generation && input.generation != 0u &&
        static_cast<u32>(static_cast<u8>(item->state)) == inputState &&
        static_cast<u32>(static_cast<u8>(item->autoCollect)) == inputAuto &&
        PspMeItemMotionFloatBits(item->currentPosition.x) == input.posXBits &&
        PspMeItemMotionFloatBits(item->currentPosition.y) == input.posYBits &&
        PspMeItemMotionFloatBits(item->currentPosition.z) == input.posZBits &&
        PspMeItemMotionFloatBits(item->startPosition.x) == input.startXBits &&
        PspMeItemMotionFloatBits(item->startPosition.y) == input.startYBits &&
        PspMeItemMotionFloatBits(item->startPosition.z) == input.startZBits;
    if (exact && inputState == 2u)
    {
        exact = PspMeItemMotionFloatBits(item->targetPosition.x) ==
                    input.targetXBits &&
                PspMeItemMotionFloatBits(item->targetPosition.y) ==
                    input.targetYBits &&
                PspMeItemMotionFloatBits(item->targetPosition.z) ==
                    input.targetZBits &&
                item->timer.current == input.timerCurrent &&
                PspMeItemMotionFloatBits(item->timer.subFrame) ==
                    input.timerSubFrameBits;
    }
    if (!exact)
    {
        ++view.slotRejects;
        return PSP_ME_ITEM_MOTION_CANONICAL;
    }

    const u32 resultState = result.stateAndRoute &
        TH07_PSP_ME_ITEM_MOTION_RESULT_STATE_MASK;
    const u32 resultAuto =
        (result.stateAndRoute >>
         TH07_PSP_ME_ITEM_MOTION_RESULT_AUTOCOLLECT_SHIFT) & 0xffu;
    const u32 route = result.stateAndRoute &
        TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_MASK;
    const bool gotoCollision =
        (result.stateAndRoute &
         TH07_PSP_ME_ITEM_MOTION_RESULT_GOTO_COLLISION) != 0u;
    constexpr u32 allowedResultBits =
        TH07_PSP_ME_ITEM_MOTION_RESULT_STATE_MASK |
        (0xffu << TH07_PSP_ME_ITEM_MOTION_RESULT_AUTOCOLLECT_SHIFT) |
        TH07_PSP_ME_ITEM_MOTION_RESULT_CANDIDATE |
        TH07_PSP_ME_ITEM_MOTION_RESULT_GOTO_COLLISION |
        TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_MASK;
    if ((result.stateAndRoute & ~allowedResultBits) != 0u ||
        (result.stateAndRoute &
         TH07_PSP_ME_ITEM_MOTION_RESULT_CANDIDATE) == 0u)
    {
        ++view.slotRejects;
        return PSP_ME_ITEM_MOTION_CANONICAL;
    }
    u32 expectedRoute;
    u32 expectedState = inputState;
    u32 expectedAuto = inputAuto;
    bool expectedCollision = false;
    if (inputState == 2u && input.timerCurrent < 60)
    {
        expectedRoute = TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_INTERP;
        expectedCollision = true;
    }
    else if (inputState == 2u)
    {
        if (input.timerCurrent == 60)
        {
            expectedRoute =
                TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_STATE2_60;
            expectedState = 0u;
        }
        else
        {
            expectedRoute =
                TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_STATE2_LATE;
        }
    }
    else
    {
        const float playerY = g_Player.positionCenter.y;
        const bool pull = inputState == 1u ||
            (((view.job.itemCurrentPowerClass >= 128 ||
               view.job.itemDifficulty >= 4) &&
              playerY < g_Player.shooterData->pocY) ||
             view.job.itemHasBorder == 1u);
        if (pull && view.job.playerState != 1u)
        {
            expectedRoute = TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_HOME;
            expectedState = 1u;
            if (view.job.itemHasBorder == 1u)
                expectedAuto = 1u;
        }
        else if (pull)
        {
            expectedRoute = TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_SPAWN;
            expectedState = 0u;
        }
        else
        {
            expectedRoute = TH07_PSP_ME_ITEM_MOTION_RESULT_ROUTE_FALL;
        }
    }
    if (resultState != expectedState || resultAuto != expectedAuto ||
        route != expectedRoute || gotoCollision != expectedCollision)
    {
        ++view.slotRejects;
        return PSP_ME_ITEM_MOTION_CANONICAL;
    }

    std::memcpy(&item->currentPosition.x, &result.posXBits,
                sizeof(item->currentPosition.x));
    std::memcpy(&item->currentPosition.y, &result.posYBits,
                sizeof(item->currentPosition.y));
    std::memcpy(&item->currentPosition.z, &result.posZBits,
                sizeof(item->currentPosition.z));
    std::memcpy(&item->startPosition.x, &result.startXBits,
                sizeof(item->startPosition.x));
    std::memcpy(&item->startPosition.y, &result.startYBits,
                sizeof(item->startPosition.y));
    std::memcpy(&item->startPosition.z, &result.startZBits,
                sizeof(item->startPosition.z));
    item->state = static_cast<i8>(resultState);
    item->autoCollect = static_cast<i8>(resultAuto);
    ++view.adopted;
    return gotoCollision ? PSP_ME_ITEM_MOTION_TO_COLLISION
                         : PSP_ME_ITEM_MOTION_TO_BOUNDS;
}
}

void PspSetMeItemMotionView(
    const Th07PspMeBulletCompactJob *job,
    const Th07PspMeItemMotionSeed *seed,
    const Th07PspMeItemMotionOutput *output)
{
    gPspMeItemMotionScView = PspMeItemMotionScView{};
    if (!job || !seed || !output ||
        !th07_psp_me_item_motion_available())
        return;
    gPspMeItemMotionScView.active = true;
    gPspMeItemMotionScView.job = *job;
    gPspMeItemMotionScView.seed = seed;
    gPspMeItemMotionScView.output = output;
}

void PspClearMeItemMotionView()
{
    gPspMeItemMotionScView = PspMeItemMotionScView{};
}

void PspTakeMeItemMotionFrameStats(
    u32 *active, u32 *candidates, u32 *adopted,
    u32 *slotRejects, u32 *globalRejects)
{
    if (active)
        *active = gPspMeItemMotionScView.active ? 1u : 0u;
    if (candidates)
        *candidates = gPspMeItemMotionScView.candidates;
    if (adopted)
        *adopted = gPspMeItemMotionScView.adopted;
    if (slotRejects)
        *slotRejects = gPspMeItemMotionScView.slotRejects;
    if (globalRejects)
        *globalRejects = gPspMeItemMotionScView.globalRejects;
}
#endif

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
namespace
{
inline void PspMeItemRenderSinCos(float angle, float *outSin, float *outCos)
{
    // Deliberately identical to AnmManager::Draw's PSP trig gate. ME receives
    // these exact result bits and never evaluates trigonometry itself.
    if (std::isfinite(angle) && angle >= -16.0f * ZUN_PI &&
        angle <= 16.0f * ZUN_PI)
    {
        vfpu_sincos(angle, outSin, outCos);
        return;
    }
    sincosf(outSin, outCos, angle);
}

inline void PspApplyItemDrawPresentation(Item *item)
{
    item->sprite.pos.x =
        g_GameManager.arcadeRegionTopLeftPos.x + item->currentPosition.x;
    item->sprite.pos.y =
        g_GameManager.arcadeRegionTopLeftPos.y + item->currentPosition.y;
    item->sprite.pos.z = 0.01f;
    if (item->currentPosition.y < -8.0f)
    {
        item->sprite.pos.y = 8.0f + g_GameManager.arcadeRegionTopLeftPos.y;
        if (item->isOnscreen)
        {
            g_AnmManager->SetActiveSprite(&item->sprite,
                                          item->itemType + 694);
            item->isOnscreen = 0;
            item->sprite.zWriteDisable = 1;
        }
        i32 alpha = 255 - static_cast<i32>(
            (8.0f - item->currentPosition.y) * 255.0f / 128.0f);
        if (alpha < 64)
        {
            alpha = 64;
        }
        item->sprite.color.color =
            (item->sprite.color.color & 0x00ffffffu) |
            static_cast<u32>(alpha) << 24u;
    }
    else if (!item->isOnscreen)
    {
        g_AnmManager->SetActiveSprite(&item->sprite, item->itemType + 684);
        item->isOnscreen = 1;
        item->sprite.color.color = 0xffffffffu;
        item->sprite.zWriteDisable = 1;
    }
}
}
#endif

void AngleToVector(ZunVec3 *vec, f32 angle, f32 speed)
{
    vec->x = cosf(angle) * speed;
    vec->y = sinf(angle) * speed;
}

void GameManager::AddCurrentPower(i32 amount)
{
    if (CheckGameIntegrity())
    {
        NUKE_SUPERVISOR();
    }
    this->globals->currentPower += (f32)amount;
    RegenerateGameIntegrityCsum();
}

ItemManager::ItemManager()
{
#if defined(TH07_PSP_1000)
    this->items = nullptr;
#endif
    Reset();
}

void ItemManager::Reset()
{
#if defined(TH07_PSP_1000)
    Item *pool = this->items;
#endif
    memset(this, 0, sizeof(ItemManager));
#if defined(TH07_PSP_1000)
    this->items = pool;
    if (pool)
    {
        memset(pool, 0, sizeof(Item) * (kItemCapacity + 1));
    }
#endif
}

#if defined(TH07_PSP_1000)
bool ItemManager::PspEnsureItemPool()
{
    if (!this->items)
    {
        this->items = static_cast<Item *>(th07_psp_1000_alloc_pool(
            sizeof(Item) * static_cast<size_t>(kItemCapacity + 1)));
        if (this->items)
        {
            memset(this->items, 0,
                   sizeof(Item) * static_cast<size_t>(kItemCapacity + 1));
        }
    }
    if (!this->items)
    {
        th07_psp_boot_note("PSP1000 item pool allocation failed");
        return false;
    }
    memset(this->pspActiveItemBits, 0, sizeof(this->pspActiveItemBits));
    th07_psp_boot_notef("PSP1000 item pool %d slots %uK", kItemCapacity,
                        static_cast<unsigned int>(sizeof(Item) * kItemCapacity / 1024u));
    return true;
}

void ItemManager::PspReleaseItemPool()
{
    this->items = nullptr;
    memset(this->pspActiveItemBits, 0, sizeof(this->pspActiveItemBits));
}
#endif

Item::Item()
{
}

Item *ItemManager::SpawnItem(ZunVec3 *heading, i32 itemType, i32 state)
{
    Item *item = nullptr;
    i32 i;
    i32 itemIndex = this->nextIndex;
    bool spawned = false;

    if ((i32)g_GameManager.globals->currentPower >= 128)
    {
        if (itemType == ITEM_POWER_SMALL || itemType == ITEM_POWER_BIG)
        {
            itemType = ITEM_CHERRY;
        }
    }
    for (i = 0; i < kItemCapacity; i++)
    {
        this->nextIndex = itemIndex + 1;
        if (this->nextIndex >= kItemCapacity)
        {
            this->nextIndex = 0;
        }

#if defined(TH07_PSP)
        if (this->PspIsItemSlotTracked(itemIndex))
        {
            item = this->ItemAt(itemIndex);
            if (item && item->isInUse)
            {
                itemIndex = this->nextIndex;
                continue;
            }
            this->PspForgetItemSlot(itemIndex);
        }
        item = this->ItemAt(itemIndex);
#else
        item = this->ItemAt(itemIndex);
        if (item->isInUse)
        {
            itemIndex = this->nextIndex;
            continue;
        }
#endif
        item->isInUse = 1;
#if defined(TH07_PSP)
        this->PspTrackItemSlot(itemIndex);
#endif
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        if (++this->pspMeItemSlotGenerations[itemIndex] == 0u)
        {
            ++this->pspMeItemSlotGenerations[itemIndex];
        }
#endif
        item->currentPosition = *heading;
        item->startPosition.x = 0.0f;
        item->startPosition.y = -2.2f;
        item->startPosition.z = 0.0f;
        item->itemType = (u8)itemType;
        item->state = (u8)state;
        item->timer = 0;
        if (state == 2)
        {
            item->targetPosition.x = g_Rng.GetRandomFloatInRange(288.0f) + 48.0f;
            item->targetPosition.y = g_Rng.GetRandomFloatInRange(192.0f) - 64.0f;
            item->targetPosition.z = 0.0f;
            item->startPosition = item->currentPosition;
        }
        else if (state == 3)
        {
            item->state = 1;
        }
        else if (state == 4)
        {
            item->state = 0;
        }
        g_AnmManager->SetAnmIdxAndExecuteScript(&item->sprite, itemType + 708);
        item->sprite.color.color = 0xffffffff;
        item->sprite.zWriteDisable = 1;
        item->autoCollect = 0;
        item->isOnscreen = 1;
        spawned = true;
        break;
    }

    return spawned ? item : this->ItemFailureSentinel();
}

void ItemManager::OnUpdate()
{
    i32 prevPowerLevel2;
    i32 k;
    i32 prevPowerIdx;
    i32 j;
    Item *item;
    i32 itemAcquired;
    f32 playerAngle;
    i32 itemScore;
    f32 itemTimerSecs;
    i32 i;

    ZunVec3 local_20(g_Player.shooterData->itemCollectRadius,
                     g_Player.shooterData->itemCollectRadius, 16.0f);
    itemAcquired = 0;
    this->activeItemCount = 0;
    this->listTail = &this->listHead;
    this->listHead.next = NULL;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    this->pspMeItemListCount = 0u;
#if defined(TH07_PSP_ME_ADAPTIVE_AUX_RENDER)
    this->pspMeItemPreparedSerial = 0u;
    this->pspMeItemPreparedCount = 0u;
    this->pspMeItemPreparedPrefixCount = 0u;
    this->pspMeItemPreparedPrefixTail = nullptr;
    this->pspMeItemPreparedSuffixHead = nullptr;
    this->pspMeItemRequestedPrefixCount = 0u;
#endif
#endif

    for (i = 0; i < kItemCapacity; i++)
    {
#if defined(TH07_PSP)
        if (!this->PspIsItemSlotTracked(i))
        {
            continue;
        }
#endif
        item = this->ItemAt(i);
        if (!item->isInUse)
        {
#if defined(TH07_PSP)
            this->PspForgetItemSlot(i);
#endif
            continue;
        }

        this->activeItemCount++;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
        const PspMeItemMotionAdoptRoute pspMeMotionRoute =
            PspTryAdoptMeItemMotion(item, static_cast<u32>(i));
        if (pspMeMotionRoute == PSP_ME_ITEM_MOTION_TO_COLLISION)
        {
            goto check_collision;
        }
        if (pspMeMotionRoute == PSP_ME_ITEM_MOTION_TO_BOUNDS)
        {
            goto check_bounds;
        }
#endif
        if (item->state == 2)
        {
            if (item->timer < 60)
            {
                itemTimerSecs = item->timer.AsFloat() / 60.0f;
                item->currentPosition = itemTimerSecs * item->targetPosition +
                                        item->startPosition * (1.0f - itemTimerSecs);
                goto check_collision;
            }
            else if (item->timer == 60)
            {
                item->startPosition = ZunVec3(0.0f, 0.0f, 0.0f);
                item->state = 0;
            }
        }
        else
        {
            if (item->state == 1 ||
                ((128.0 <= (f64)(i32)g_GameManager.globals->currentPower ||
                  g_GameManager.difficulty >= 4) &&
                 g_Player.positionCenter.y < g_Player.shooterData->pocY) ||
                g_Player.hasBorder == 1)
            {
                if (g_Player.playerState != 1)
                {
                    playerAngle = g_Player.AngleToPlayer(&item->currentPosition);
                    AngleToVector(&item->startPosition, playerAngle,
                                  g_Player.shooterData->itemCollectSpeed);
                    item->state = 1;
                    if (g_Player.hasBorder == 1)
                    {
                        item->autoCollect = 1;
                    }
                }
                else
                {
                    item->startPosition.y = -0.5f;
                    item->state = 0;
                }
            }
            else
            {
                item->startPosition.x = 0.0f;
                item->startPosition.z = 0.0f;
                if (item->startPosition.y < -2.2f)
                {
                    item->startPosition.y = -2.2f;
                }
            }
        }
        item->currentPosition += item->startPosition * g_Supervisor.effectiveFramerateMultiplier;
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
    check_bounds:
#endif
        if (g_GameManager.arcadeRegionSize.y + 16.0f <= item->currentPosition.y)
        {
            item->isInUse = 0;
#if defined(TH07_PSP)
            this->PspForgetItemSlot(i);
#endif
            g_GameManager.DecreaseSubrank(3);
            continue;
        }
        if (item->startPosition.y < 3.0f)
        {
            item->startPosition.y += 0.03f * g_Supervisor.effectiveFramerateMultiplier;
        }
        else
        {
            item->startPosition.y = 3.0f;
        }
    check_collision:
        if (g_Player.CalcItemBoxCollision(&item->currentPosition, &local_20))
        {
#if defined(TH07_PSP_ME_ITEM_MOTION_UPDATE)
            // Collection may change power, spawn/despawn Items or clear
            // Bullets.  Close all remaining adoption for this canonical pass;
            // the next frame gets a fresh post-update seed.
            gPspMeItemMotionScView.authorityClosed = true;
#endif
            g_ReplayManager->replayEventFlags |= 0x40;
            switch (item->itemType)
            {
            case ITEM_POWER_SMALL:
                if ((i32)g_GameManager.globals->currentPower >= 128)
                {
                    g_GameManager.powerItemCountForScore++;
                    if ((u32)g_GameManager.powerItemCountForScore >= 31)
                    {
                        g_GameManager.powerItemCountForScore = 30;
                    }
                    itemScore = g_FullPowerScoreBonus[g_GameManager.powerItemCountForScore];
                    g_GameManager.AddScore(itemScore);
                    g_AsciiManager.CreatePopup1(&item->currentPosition, itemScore,
                                                itemScore >= 12800 ? 0xffffff00 : 0xffffffff);
                }
                else
                {
                    j = 0;
                    while ((i32)g_GameManager.globals->currentPower >= g_PowerLevels[j])
                    {
                        j++;
                    }
                    prevPowerIdx = j;
                    g_GameManager.powerItemCountForScore = 0;
                    g_GameManager.AddCurrentPower(1);
                    if ((i32)g_GameManager.globals->currentPower >= 128)
                    {
                        g_GameManager.globals->currentPower = 128.0f;
                        g_GameManager.RegenerateGameIntegrityCsum();
                        if (!g_EnemyManager.spellcardInfo.isActive)
                        {
#if defined(TH07_PSP_PERF_A1_SAME)
                            Th07PspPerfSetA1SameReason(
                                TH07_PSP_PERF_A1_REASON_FULL_POWER);
#endif
                            g_BulletManager.RemoveAllBullets(1);
                        }
                        g_Gui.ShowFullPowerMode(0, 1);
                        this->DespawnAllItems(i);
                    }
                    g_GameManager.AddScore(10);
                    g_Gui.showPower = 2;
                    while ((i32)g_GameManager.globals->currentPower >= g_PowerLevels[j])
                    {
                        j++;
                    }
                    if (j != prevPowerIdx)
                    {
                        g_AsciiManager.CreatePopup1(&item->currentPosition, -1, 0xffffc0a0);
                        g_SoundPlayer.PlaySoundByIdx(SOUND_POWERUP, 0);
                    }
                    else
                    {
                        g_AsciiManager.CreatePopup1(&item->currentPosition, 10, 0xffffffff);
                    }
                }
                g_GameManager.IncreaseSubrank(1);
                break;
            case ITEM_POINT:
                itemScore = item->IsBelowPoc() ? 50000 : 50000 - item->OffsetFromPoc() * 100;
                if (item->autoCollect == 1)
                {
                    itemScore = 50000;
                }
                if (itemScore >= 50000)
                {
                    if (g_GameManager.cherry - g_GameManager.globals->cherryStart > 50000)
                    {
                        itemScore = g_GameManager.cherry - g_GameManager.globals->cherryStart;
                    }
                }
                else if (g_GameManager.cherry - g_GameManager.globals->cherryStart > 50000)
                {
                    itemScore +=
                        (g_GameManager.cherry - g_GameManager.globals->cherryStart - 50000) / 5;
                }
                itemScore -= itemScore % 10;
                g_AsciiManager.CreatePopup1(&item->currentPosition, itemScore,
                                            item->currentPosition.y < g_Player.shooterData->pocY ||
                                                    item->autoCollect == 1
                                                ? 0xffffff00
                                                : 0xffffffff);
                g_GameManager.AddScore(itemScore);
                g_GameManager.globals->pointItemsCollectedThisStage++;
                g_GameManager.globals->pointItemsCollectedForExtend++;
                g_Gui.showPoint = 2;
                if (item->currentPosition.y < 128.0f)
                {
                    g_GameManager.IncreaseSubrank(10);
                }
                else
                {
                    g_GameManager.IncreaseSubrank(3);
                }
                if (g_GameManager.globals->extendsFromPointItems >= 0)
                {
                    for (;;)
                    {
                        if (g_GameManager.difficulty < 4)
                        {
                            if (g_GameManager.globals->extendsFromPointItems < 3)
                            {
                                g_GameManager.globals->nextNeededPointItemsForExtend =
                                    g_GameManager.globals->extendsFromPointItems * 75 + 50;
                            }
                            else if (g_GameManager.globals->extendsFromPointItems < 5)
                            {
                                g_GameManager.globals->nextNeededPointItemsForExtend =
                                    (g_GameManager.globals->extendsFromPointItems - 3) * 150 + 300;
                            }
                            else
                            {
                                g_GameManager.globals->nextNeededPointItemsForExtend =
                                    (g_GameManager.globals->extendsFromPointItems - 5) * 200 + 800;
                            }
                        }
                        else if (g_GameManager.globals->extendsFromPointItems == 0)
                        {
                            g_GameManager.globals->nextNeededPointItemsForExtend = 200;
                        }
                        else if (g_GameManager.globals->extendsFromPointItems == 1)
                        {
                            g_GameManager.globals->nextNeededPointItemsForExtend = 500;
                        }
                        else
                        {
                            g_GameManager.globals->nextNeededPointItemsForExtend =
                                (g_GameManager.globals->extendsFromPointItems - 2) * 500 + 800;
                        }

                        if (g_GameManager.globals->pointItemsCollectedForExtend >=
                            g_GameManager.globals->nextNeededPointItemsForExtend)
                        {
                            g_GameManager.ExtendFromPoints();
                            g_GameManager.globals->extendsFromPointItems++;
                            continue;
                        }
                        break;
                    }
                }
                break;
            case ITEM_POWER_BIG:
                if ((i32)g_GameManager.globals->currentPower >= 128)
                {
                    g_AsciiManager.CreatePopup1(&item->currentPosition, itemScore,
                                                itemScore >= 1000 ? 0xffffff00 : 0xffffffff);
                }
                else
                {
                    k = 0;
                    while ((i32)g_GameManager.globals->currentPower >= g_PowerLevels[k])
                    {
                        k++;
                    }
                    prevPowerLevel2 = k;
                    g_GameManager.AddCurrentPower(8);
                    if ((i32)g_GameManager.globals->currentPower >= 128)
                    {
                        g_GameManager.globals->currentPower = 128.0f;
                        g_GameManager.RegenerateGameIntegrityCsum();
                        if (!g_EnemyManager.spellcardInfo.isActive)
                        {
#if defined(TH07_PSP_PERF_A1_SAME)
                            Th07PspPerfSetA1SameReason(
                                TH07_PSP_PERF_A1_REASON_FULL_POWER);
#endif
                            g_BulletManager.RemoveAllBullets(1);
                        }
                        g_Gui.ShowFullPowerMode(0, 1);
                        this->DespawnAllItems(i);
                    }
                    g_Gui.showPower = 2;
                    g_GameManager.AddScore(10);
                    while ((i32)g_GameManager.globals->currentPower >= g_PowerLevels[k])
                    {
                        k++;
                    }
                    if (k != prevPowerLevel2)
                    {
                        g_AsciiManager.CreatePopup1(&item->currentPosition, -1, 0xffffc0a0);
                        g_SoundPlayer.PlaySoundByIdx(SOUND_POWERUP, 0);
                    }
                    else
                    {
                        g_AsciiManager.CreatePopup1(&item->currentPosition, 10, 0xffffffff);
                    }
                }
                break;
            case ITEM_BOMB:
                if ((i32)g_GameManager.globals->bombsRemaining < 8)
                {
                    g_GameManager.AddBombsRemaining(1);
                    g_Gui.showBombs = 2;
                }
                g_GameManager.IncreaseSubrank(5);
                break;
            case ITEM_LIFE:
                g_GameManager.ExtendFromPoints();
                break;
            case ITEM_FULL_POWER:
                if ((i32)g_GameManager.globals->currentPower < 128)
                {
#if defined(TH07_PSP_PERF_A1_SAME)
                    Th07PspPerfSetA1SameReason(
                        TH07_PSP_PERF_A1_REASON_FULL_POWER);
#endif
                    g_BulletManager.RemoveAllBullets(1);
                    g_Gui.ShowFullPowerMode(0, 1);
                    g_SoundPlayer.PlaySoundByIdx(SOUND_POWERUP, 0);
                    g_AsciiManager.CreatePopup1(&item->currentPosition, -1, 0xffffc0a0);
                    this->DespawnAllItems(i);
                }
                g_GameManager.globals->currentPower = 128.0f;
                g_GameManager.RegenerateGameIntegrityCsum();
                g_GameManager.AddScore(1000);
                g_AsciiManager.CreatePopup1(&item->currentPosition, 1000, 0xffffffff);
                g_Gui.showPower = 2;
                break;
            case ITEM_POINT_BULLET:
                if (!g_Player.isBombing)
                {
                    itemScore = g_GameManager.globals->grazeInTotal / 40 * 10 + 300;
                    if (itemScore <= 0)
                    {
                        itemScore = 10;
                    }
                }
                else
                {
                    itemScore = 100;
                }
                g_AsciiManager.CreatePopup2(&item->currentPosition, itemScore, -1);
                g_GameManager.AddScore(itemScore);
                if (!g_Player.bombInfo.isInUse)
                {
                    g_GameManager.AddCherryPlus(20);
                }
                else if ((i & 1) == 0)
                {
                    g_GameManager.AddCherryPlus(10);
                }
                else
                {
                    g_GameManager.AddCherry(10);
                }
                break;
            case ITEM_CHERRY_SMALL:
                g_GameManager.AddCherryPlus(30);
                g_GameManager.AddCherry(70);
                break;
            case ITEM_CHERRY:
                if (g_GameManager.IsCherryAtMax())
                {
                    itemScore =
                        item->ShouldAwardMaxScore() ? 50000 : 50000 - item->OffsetFromPoc() * 100;
                    itemScore -= itemScore % 10;
                    g_AsciiManager.CreatePopup1(
                        &item->currentPosition, itemScore,
                        item->currentPosition.y < g_Player.shooterData->pocY || item->autoCollect
                            ? 0xffffff00
                            : 0xffffffff);
                    g_GameManager.AddScore(itemScore);
                }
                itemScore = 1000;
                itemScore += g_GameManager.globals->spellCardsCaptured * 100;
                if (!g_GameManager.IsCherryAtMax())
                {
                    g_AsciiManager.CreatePopup1(&item->currentPosition, itemScore, 0xffff4040);
                }
                g_GameManager.AddCherryPlus(itemScore);
                break;
            case ITEM_STAR:
                itemScore = g_GameManager.globals->grazeInTotal / 40 * 10 + 300;
                if (itemScore <= 0)
                {
                    itemScore = 10;
                }
                if (g_GameManager.IsCherryAtMax())
                {
                    g_AsciiManager.CreatePopup1(&item->currentPosition, itemScore, 0xffffffff);
                }
                g_GameManager.AddScore(itemScore);
                itemScore = 100;
                if (!g_GameManager.IsCherryAtMax())
                {
                    g_AsciiManager.CreatePopup1(&item->currentPosition, itemScore, 0xffff4040);
                }
                g_GameManager.AddCherryPlus(itemScore);
                break;
            }
            item->isInUse = 0;
#if defined(TH07_PSP)
            this->PspForgetItemSlot(i);
#endif
            itemAcquired = 1;
            continue;
        }
        else
        {
            item->timer++;
            if (item->sprite.currentInstruction)
            {
                g_AnmManager->ExecuteScript(&item->sprite);
            }
            this->listTail->next = item;
            item->next = NULL;
            this->listTail = item;
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            ++this->pspMeItemListCount;
#endif
        }
    }
    if (itemAcquired)
    {
        g_SoundPlayer.PlaySoundByIdx(SOUND_21, 0);
    }
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM) && \
    !defined(TH07_PSP_ME_ADAPTIVE_AUX_RENDER)
    // All acquisition side effects, including DespawnAllItems mutations of an
    // earlier list node, must settle before presentation is prepared. This
    // canonical-order walk replaces the old draw-time presentation walk and
    // never publishes a partially prepared list.
    PspPrepareMeItemRenderStream();
#endif
}

#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
bool ItemManager::PspPrepareMeItemRenderStream()
{
    u32 prefixCount = this->pspMeItemRequestedPrefixCount;
    if (prefixCount == 0u)
    {
        prefixCount = 0xffffffffu;
    }
    if (++this->pspMeItemPrepareSerial == 0u)
    {
        ++this->pspMeItemPrepareSerial;
    }
    this->pspMeItemPreparedSerial = 0u;
    this->pspMeItemPreparedCount = 0u;
    this->pspMeItemPreparedPrefixCount = 0u;
    this->pspMeItemPreparedPrefixTail = nullptr;
    this->pspMeItemPreparedSuffixHead = nullptr;
    if (!g_AnmManager)
    {
        return false;
    }

    u32 seen[(kItemCapacity + 31u) / 32u] = {};
    const uintptr_t poolBegin =
        reinterpret_cast<uintptr_t>(this->ItemAt(0));
    const uintptr_t poolEnd = reinterpret_cast<uintptr_t>(
        this->ItemAt(kItemCapacity - 1)) + sizeof(Item);
    Item *item = this->listHead.next;
    Item *last = &this->listHead;
    u32 count = 0u;
    while (item)
    {
        const uintptr_t address = reinterpret_cast<uintptr_t>(item);
        if (count >= static_cast<u32>(kItemCapacity) ||
            address < poolBegin || address >= poolEnd ||
            ((address - poolBegin) % sizeof(Item)) != 0u)
        {
            return false;
        }
        const u32 slot = static_cast<u32>(
            (address - poolBegin) / sizeof(Item));
        const u32 bit = 1u << (slot & 31u);
        if ((seen[slot >> 5u] & bit) != 0u ||
            !this->PspIsItemSlotTracked(static_cast<i32>(slot)) ||
            !item->isInUse || this->pspMeItemSlotGenerations[slot] == 0u)
        {
            return false;
        }
        seen[slot >> 5u] |= bit;

        PspApplyItemDrawPresentation(item);
        const float rotation = item->sprite.rotation.z;
        float sine = 0.0f;
        float cosine = 1.0f;
        if (rotation != 0.0f)
        {
            PspMeItemRenderSinCos(rotation, &sine, &cosine);
        }
        this->pspMeItemRenderSin[slot] = sine;
        this->pspMeItemRenderCos[slot] = cosine;

        ++count;
        last = item;
        item = item->next;
        if (count == prefixCount)
        {
            this->pspMeItemPreparedPrefixTail = last;
            this->pspMeItemPreparedSuffixHead = item;
        }
    }
    if (this->listTail != last || (last != &this->listHead && last->next))
    {
        return false;
    }
    if (prefixCount == 0u)
    {
        return false;
    }
    if (prefixCount >= count)
    {
        prefixCount = count;
        this->pspMeItemPreparedPrefixTail = count ? last : nullptr;
        this->pspMeItemPreparedSuffixHead = nullptr;
    }
    if (prefixCount == 0u || !this->pspMeItemPreparedPrefixTail)
    {
        return false;
    }
    this->pspMeItemPreparedCount = count;
    this->pspMeItemPreparedPrefixCount = prefixCount;
    this->pspMeItemPreparedSerial = this->pspMeItemPrepareSerial;
    return true;
}

void ItemManager::PspDrawCanonicalItemSuffix(Item *item)
{
    while (item)
    {
        g_AnmManager->Draw(&item->sprite);
        item = item->next;
    }
}
#endif

void ItemManager::RemoveAllItems()
{
    Item *item;
    i32 i;

    for (i = 0; i < kItemCapacity; i++)
    {
#if defined(TH07_PSP)
        if (!this->PspIsItemSlotTracked(i))
        {
            continue;
        }
#endif
        item = this->ItemAt(i);
        if (!item->isInUse)
        {
            continue;
        }

        item->state = 1;
        item->startPosition = ZunVec3(0.0f, -0.5f, 0.0f);
    }
}

void ItemManager::DespawnAllItems(i32 param_1)
{
    Item *item;
    i32 i;

    for (i = 0; i < kItemCapacity; i++)
    {
#if defined(TH07_PSP)
        if (!this->PspIsItemSlotTracked(i))
        {
            continue;
        }
#endif
        item = this->ItemAt(i);
        if (item->isInUse == 0 || i == param_1)
        {
            continue;
        }

        if (item->itemType == 0 || item->itemType == 2)
        {
            if (item->startPosition.y > -0.5f)
            {
                item->startPosition.x = 0.0f;
                item->startPosition.y = -0.5f;
                item->startPosition.z = 0.0f;
            }
            g_EffectManager.SpawnParticles(0, &item->currentPosition, 1, 0xffffffff);
            item->itemType = 7;
            g_AnmManager->SetAnmIdxAndExecuteScript(&item->sprite, 715);
        }
    }
}

void ItemManager::ActivateAllItems()
{
    Item *item;
    i32 i;

    for (i = 0; i < kItemCapacity; i++)
    {
#if defined(TH07_PSP)
        if (!this->PspIsItemSlotTracked(i))
        {
            continue;
        }
#endif
        item = this->ItemAt(i);
        if (item->isInUse != 1)
        {
            continue;
        }

        if (item->state == 1)
        {
            item->state = 0;
            item->startPosition.x = 0.0f;
            item->startPosition.y = -0.9f;
            item->startPosition.z = 0.0f;
        }
    }
}

void ItemManager::OnDraw()
{
    Item *item;
#if !defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
    i32 local_8;
#endif

    item = this->listHead.next;
    while (item)
    {
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
        // A rejected or missing Item segment still performs the exact
        // canonical draw. Normal frames were prepared at the end of OnUpdate;
        // unusual lifecycle draws prepare lazily before reading the VM.
        if (!this->PspMeItemRenderStreamPrepared())
        {
            PspApplyItemDrawPresentation(item);
        }
#else
        item->sprite.pos.x = g_GameManager.arcadeRegionTopLeftPos.x + item->currentPosition.x;
        item->sprite.pos.y = g_GameManager.arcadeRegionTopLeftPos.y + item->currentPosition.y;
        item->sprite.pos.z = 0.01f;
        if (item->currentPosition.y < -8.0f)
        {
            item->sprite.pos.y = 8.0f + g_GameManager.arcadeRegionTopLeftPos.y;
            if (item->isOnscreen)
            {
                g_AnmManager->SetActiveSprite(&item->sprite, item->itemType + 694);
                item->isOnscreen = 0;
                item->sprite.zWriteDisable = 1;
            }
            local_8 = 255 - (i32)((8.0f - item->currentPosition.y) * 255.0f / 128.0f);
            if (local_8 < 64)
            {
                local_8 = 64;
            }
            item->sprite.color.color = (item->sprite.color.color & 0xffffff) | local_8 << 24;
        }
        else
        {
            if (!item->isOnscreen)
            {
                g_AnmManager->SetActiveSprite(&item->sprite, item->itemType + 684);
                item->isOnscreen = 1;
                item->sprite.color.color = 0xffffffff;
                item->sprite.zWriteDisable = 1;
            }
        }
#endif
        g_AnmManager->Draw(&item->sprite);
        item = item->next;
    }
}

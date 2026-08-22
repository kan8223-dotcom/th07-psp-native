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
        }
    }
    if (itemAcquired)
    {
        g_SoundPlayer.PlaySoundByIdx(SOUND_21, 0);
    }
}

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
    i32 local_8;

    item = this->listHead.next;
    while (item)
    {
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
        g_AnmManager->Draw(&item->sprite);
        item = item->next;
    }
}

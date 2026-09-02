#include "EnemyManager.hpp"

#include "AnmIdx.hpp"
#include "AsciiManager.hpp"
#include "Chain.hpp"
#include "EclManager.hpp"
#include "GameManager.hpp"
#include "Gui.hpp"
#include "Player.hpp"
#include "Rng.hpp"
#include "SoundPlayer.hpp"
#include "ZunResult.hpp"
#include "utils.hpp"

#if defined(TH07_PSP)
#include <cmath>
#include <pspmath.h>
#if defined(TH07_PSP_PERF_A1_SAME)
#include <pspkernel.h>
#endif

#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
#include "../psp/fileio.hpp"
#endif
#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE) || \
    defined(TH07_PSP_PERF_A1_SAME)
#include "../psp/graphics/PspGuGraphics.hpp"
#endif

#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
#include <cstdlib>
#include <malloc.h>
#endif

#if defined(TH07_PSP_1000)
#include "../psp/fileio.hpp"
#include "../psp/psp1000_arena.hpp"

#include <cstdlib>
#endif

namespace
{
inline void PspEnemyRenderSinCos(f32 angle, f32 *outSin, f32 *outCos)
{
    if (std::isfinite(angle) && angle >= -16.0f * ZUN_PI && angle <= 16.0f * ZUN_PI)
    {
        vfpu_sincos(angle, outSin, outCos);
        return;
    }
    sincosf(outSin, outCos, angle);
}

#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
constexpr u16 kPspEnemyP5WarmQueueEnd = 0xffffu;
constexpr u32 kPspEnemyP5WarmCanonical = 1u << 0;
constexpr u32 kPspEnemyP5WarmPrimaryAutoRotate = 1u << 1;
constexpr u32 kPspEnemyP5WarmInvisibleOnBomb = 1u << 2;
static_assert(EnemyManager::kEnemyCapacity < kPspEnemyP5WarmQueueEnd,
              "u16 queue sentinel must not alias an Enemy slot");

// One cache line per stable Enemy slot.  Draw reads this compact topology
// instead of following next pointers through the roughly 22 KiB Enemy
// objects.  AnmVm fields and every renderer/global input remain live.
struct alignas(64) PspEnemyP5WarmRecord
{
    Enemy *enemy;
    AnmVm *child0;
    AnmVm *primary;
    AnmVm *child1;
    f32 positionX;
    f32 positionY;
    f32 angle;
    u32 flags;
    u16 nextIndex;
    u16 slotIndex;
    u8 headIndex;
    u8 reserved[27];
};
static_assert(sizeof(PspEnemyP5WarmRecord) == 64,
              "enemy P5 warm record must remain one PSP cache line");

struct alignas(64) PspEnemyP5WarmQueue
{
    u16 heads[4];
    u16 headCounts[4];
    u16 recordCount;
    u16 p5RecordCount;
    u32 mutationEpoch;
    u32 published;
    u32 writtenBits[(EnemyManager::kEnemyCapacity + 31) / 32];
    PspEnemyP5WarmRecord records[EnemyManager::kEnemyCapacity];
};
static_assert(sizeof(PspEnemyP5WarmQueue) <= 96u * 1024u,
              "enemy P5 warm queue exceeds its optional-RAM budget");

inline PspEnemyP5WarmQueue *PspGetEnemyP5WarmQueue(EnemyManager *manager)
{
    return static_cast<PspEnemyP5WarmQueue *>(manager->pspEnemyP5WarmQueue);
}

inline const PspEnemyP5WarmQueue *PspGetEnemyP5WarmQueue(const EnemyManager *manager)
{
    return static_cast<const PspEnemyP5WarmQueue *>(manager->pspEnemyP5WarmQueue);
}

#if defined(TH07_PSP_PERF_DENSE_SLICE)
Th07PspEnemyP5WarmWindow gPspEnemyP5WarmWindow{};
#endif
#endif
} // namespace
#endif

u32 g_SpellcardScore[141] = {
    0x1E8480, 0x1E8480, 0x2191C0, 0x2191C0, 0x249F00, 0x249F00, 0x249F00, 0x249F00, 0x249F00,
    0x249F00, 0x27AC40, 0x27AC40, 0x27AC40, 0x27AC40, 0x27AC40, 0x27AC40, 0x27AC40, 0x27AC40,
    0x27AC40, 0x27AC40, 0x27AC40, 0x27AC40, 0x27AC40, 0x27AC40, 0x27AC40, 0x27AC40, 0x2DC6C0,
    0x2DC6C0, 0x2DC6C0, 0x2DC6C0, 0x2DC6C0, 0x2DC6C0, 0x2DC6C0, 0x2DC6C0, 0x2DC6C0, 0x2DC6C0,
    0x2DC6C0, 0x2DC6C0, 0x2DC6C0, 0x2DC6C0, 0x2DC6C0, 0x2DC6C0, 0x2DC6C0, 0x2DC6C0, 0x3567E0,
    0x3567E0, 0x3567E0, 0x3567E0, 0x3567E0, 0x3567E0, 0x3567E0, 0x3567E0, 0x3567E0, 0x3567E0,
    0x3567E0, 0x3567E0, 0x3567E0, 0x3567E0, 0x3567E0, 0x3567E0, 0x3567E0, 0x3567E0, 0x3567E0,
    0x3567E0, 0x3567E0, 0x3567E0, 0x3567E0, 0x3567E0, 0x3D0900, 0x3D0900, 0x3D0900, 0x3D0900,
    0x3D0900, 0x3D0900, 0x3D0900, 0x3D0900, 0x3D0900, 0x3D0900, 0x3D0900, 0x3D0900, 0x3D0900,
    0x3D0900, 0x3D0900, 0x3D0900, 0x3D0900, 0x3D0900, 0x3D0900, 0x3D0900, 0x4C4B40, 0x4C4B40,
    0x4C4B40, 0x4C4B40, 0x4C4B40, 0x4C4B40, 0x4C4B40, 0x4C4B40, 0x4C4B40, 0x4C4B40, 0x4C4B40,
    0x4C4B40, 0x4C4B40, 0x4C4B40, 0x4C4B40, 0x4C4B40, 0x4C4B40, 0x4C4B40, 0x4C4B40, 0x4C4B40,
    0x4C4B40, 0x4C4B40, 0x4C4B40, 0x4C4B40, 0x2DC6C0, 0x2DC6C0, 0x2DC6C0, 0x2DC6C0, 0x5B8D80,
    0x5B8D80, 0x6ACFC0, 0x6ACFC0, 0x6ACFC0, 0x6ACFC0, 0x6ACFC0, 0x6ACFC0, 0x6ACFC0, 0x6ACFC0,
    0x3D0900, 0x6ACFC0, 0x6ACFC0, 0x6ACFC0, 0x7A1200, 0x7A1200, 0x7A1200, 0x7A1200, 0x7A1200,
    0x7A1200, 0x7A1200, 0x7A1200, 0x3D0900, 0x7A1200, 0x3D0900};

ChainElem g_EnemyManagerDrawChain1;

EnemyManager g_EnemyManager;

ChainElem g_EnemyManagerCalcChain;

ChainElem g_EnemyManagerDrawChain2;

void Enemy::Move()
{
    this->deltaPos = this->position - this->prevPos;
    this->prevPos = this->position;
    if (!this->mirror)
    {
        this->position.x += g_Supervisor.effectiveFramerateMultiplier * this->axisSpeed.x;
    }
    else
    {
        this->position.x -= g_Supervisor.effectiveFramerateMultiplier * this->axisSpeed.x;
    }
    this->position.y += g_Supervisor.effectiveFramerateMultiplier * this->axisSpeed.y;
    this->position.z += g_Supervisor.effectiveFramerateMultiplier * this->axisSpeed.z;
}

void EnemyManager::Initialize()
{
    Enemy *enemy;
    i32 i;

#if defined(TH07_PSP_1000)
    Enemy *chunks[kEnemyChunkCount];
    memcpy(chunks, this->enemyChunks, sizeof(chunks));
#endif
#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
    // A direct reinitialize must not orphan the stage allocation.  Invalidate
    // it and advance the epoch so no previous calc publication can be drawn.
    void *enemyP5WarmQueue = this->pspEnemyP5WarmQueue;
    const u32 enemyMutationEpoch = this->pspEnemyMutationEpoch;
#endif
    memset(this, 0, sizeof(EnemyManager));
#if defined(TH07_PSP_1000)
    memcpy(this->enemyChunks, chunks, sizeof(chunks));
    for (i = 0; i < kEnemyChunkCount; i++)
    {
        if (this->enemyChunks[i])
        {
            memset(this->enemyChunks[i], 0,
                   sizeof(Enemy) * static_cast<size_t>(kEnemyChunkCapacity));
        }
    }
#endif
#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
    this->pspEnemyP5WarmQueue = enemyP5WarmQueue;
    this->pspEnemyMutationEpoch =
        enemyMutationEpoch + (enemyP5WarmQueue ? 1u : 0u);
    if (enemyP5WarmQueue)
    {
        PspGetEnemyP5WarmQueue(this)->published = 0u;
    }
#endif
    enemy = &this->enemyTemplate;
    memset(enemy, 0, sizeof(Enemy));
    for (i = 0; i < 2; i++)
    {
        enemy->vms[i].anmFileIdx = -1;
    }
    for (i = 0; i < 96; i++)
    {
        enemy->enemyHistory[i].position.x = -999.0f;
    }
    enemy->active = 1;
    enemy->timer = 0;
    enemy->isInBounds = 0;
    enemy->hitboxSize = ZunVec3(12.0f, 12.0f, 12.0f);
    enemy->axisSpeed = ZunVec3(0.0f, 0.0f, 0.0f);
    enemy->angularVelocity = 0.0f;
    enemy->angle = 0.0f;
    enemy->moveAcceleration = 0.0f;
    enemy->moveSpeed = 0.0f;
    enemy->moveMode = 0;
    enemy->disableBullets = 0;
    enemy->mirror = 0;
    enemy->isBoss = 0;
    enemy->stackDepth = 0;
    enemy->life = 1;
    enemy->score = 100;
    enemy->deathAnm1 = 0;
    enemy->deathAnm2 = 0;
    enemy->deathAnm3 = 0;
    enemy->shootInterval = 0;
    enemy->shootIntervalTimer = 0;
    enemy->shootOffset = ZunVec3(0.0f, 0.0f, 0.0f);
    enemy->anmExLeft = -1;
    enemy->anmExRight = -1;
    enemy->anmExDefaults = -1;
    enemy->canDie = 1;
    enemy->hasContactHitbox = 1;
    enemy->canBeDamaged = 1;
    enemy->hasNoCollision = 0;
    enemy->isHittable = 1;
    enemy->isProjectile = 0;
    enemy->deathType = 0;
    enemy->deathCallbackSub = -1;
    enemy->hasMovementBounds = 0;
    enemy->effectsNum = 0;
    enemy->runInterrupt = -1;
    for (i = 0; i < 4; i++)
    {
        enemy->lifeCallbackThreshold[i] = -1;
    }
    enemy->timerCallbackThreshold = -1;
    enemy->periodicCallbackSub = -1;
    enemy->laserIdx = 0;
    enemy->damageTintTimer = 0;
    enemy->primaryVmAutoRotate = 0;
    enemy->bulletRankSpeedLow = -0.15f;
    enemy->bulletRankSpeedHigh = 0.15f;
    enemy->bulletProps.soundIdx = SOUND_BOMB_MARISA_A_FOCUS;
    enemy->bulletProps.soundOverride = SOUND_25;
}

EnemyManager::EnemyManager()
{
#if defined(TH07_PSP_1000)
    memset(this->enemyChunks, 0, sizeof(this->enemyChunks));
#endif
#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
    this->pspEnemyP5WarmQueue = NULL;
    this->pspEnemyMutationEpoch = 0u;
#endif
    Initialize();
}

#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
bool EnemyManager::PspEnsureEnemyP5WarmQueue()
{
    if (this->pspEnemyP5WarmQueue)
    {
        return true;
    }

    const struct mallinfo heapBefore = mallinfo();
    void *allocation = memalign(64, sizeof(PspEnemyP5WarmQueue));
    if (!allocation)
    {
        return false;
    }
    memset(allocation, 0, sizeof(PspEnemyP5WarmQueue));
    this->pspEnemyP5WarmQueue = allocation;
    const struct mallinfo heapAfter = mallinfo();
    th07_psp_boot_notef("enemy p5 warm queue %u bytes heap %uK->%uK",
                        static_cast<unsigned int>(sizeof(PspEnemyP5WarmQueue)),
                        static_cast<unsigned int>(heapBefore.fordblks) / 1024u,
                        static_cast<unsigned int>(heapAfter.fordblks) / 1024u);
    return true;
}

void EnemyManager::PspReleaseEnemyP5WarmQueue()
{
    if (!this->pspEnemyP5WarmQueue)
    {
        return;
    }
    PspGetEnemyP5WarmQueue(this)->published = 0u;
    std::free(this->pspEnemyP5WarmQueue);
    this->pspEnemyP5WarmQueue = NULL;
    ++this->pspEnemyMutationEpoch;
}

bool EnemyManager::PspBeginEnemyP5WarmQueue()
{
    PspEnemyP5WarmQueue *queue = PspGetEnemyP5WarmQueue(this);
    if (!queue)
    {
        return false;
    }

    // Invalidate before calc touches any topology.  A later replay restart,
    // fixed-30 skipped draw, pause, or early exit can therefore expose only a
    // complete publication from the most recent finished calc pass.
    queue->published = 0u;
    queue->recordCount = 0u;
    queue->p5RecordCount = 0u;
    for (u32 head = 0; head < 4u; ++head)
    {
        queue->heads[head] = kPspEnemyP5WarmQueueEnd;
        queue->headCounts[head] = 0u;
    }
    memset(queue->writtenBits, 0, sizeof(queue->writtenBits));
    return true;
}

bool EnemyManager::PspCaptureEnemyP5WarmRecord(Enemy *enemy, u32 slotIndex,
                                                u32 headIndex)
{
    PspEnemyP5WarmQueue *queue = PspGetEnemyP5WarmQueue(this);
    if (!queue || !enemy || slotIndex >= static_cast<u32>(kEnemyCapacity) ||
        headIndex >= 4u || queue->recordCount >= kEnemyCapacity)
    {
        return false;
    }

    const u32 bit = 1u << (slotIndex & 31u);
    u32 &word = queue->writtenBits[slotIndex >> 5];
    if (word & bit)
    {
        return false;
    }
    word |= bit;

    PspEnemyP5WarmRecord &record = queue->records[slotIndex];
    record.enemy = enemy;
    record.child0 = &enemy->vms[0];
    record.primary = &enemy->primaryVm;
    record.child1 = &enemy->vms[1];
    record.positionX = enemy->position.x;
    record.positionY = enemy->position.y;
    record.angle = enemy->angle;
    record.flags = 0u;
    if (enemy->trailFlags != 0)
    {
        record.flags |= kPspEnemyP5WarmCanonical;
    }
    if (enemy->primaryVmAutoRotate)
    {
        record.flags |= kPspEnemyP5WarmPrimaryAutoRotate;
    }
    if (enemy->invisibleOnBomb)
    {
        record.flags |= kPspEnemyP5WarmInvisibleOnBomb;
    }
    record.nextIndex = queue->heads[headIndex];
    record.slotIndex = static_cast<u16>(slotIndex);
    record.headIndex = static_cast<u8>(headIndex);
    queue->heads[headIndex] = static_cast<u16>(slotIndex);
    ++queue->headCounts[headIndex];
    ++queue->recordCount;
    if (headIndex < 2u)
    {
        ++queue->p5RecordCount;
    }
    return true;
}

void EnemyManager::PspPublishEnemyP5WarmQueue(bool captureComplete)
{
    PspEnemyP5WarmQueue *queue = PspGetEnemyP5WarmQueue(this);
    if (!queue || !captureComplete)
    {
        return;
    }
    const u32 headTotal = static_cast<u32>(queue->headCounts[0]) +
                          static_cast<u32>(queue->headCounts[1]) +
                          static_cast<u32>(queue->headCounts[2]) +
                          static_cast<u32>(queue->headCounts[3]);
    if (headTotal != queue->recordCount ||
        static_cast<u32>(queue->headCounts[0]) + queue->headCounts[1] !=
            queue->p5RecordCount)
    {
        return;
    }
    queue->mutationEpoch = this->pspEnemyMutationEpoch;
    queue->published = 1u;
}

bool EnemyManager::PspEnemyP5WarmQueueReady() const
{
    const PspEnemyP5WarmQueue *queue = PspGetEnemyP5WarmQueue(this);
    return queue && queue->published != 0u &&
           queue->mutationEpoch == this->pspEnemyMutationEpoch &&
           static_cast<u32>(queue->headCounts[0]) + queue->headCounts[1] ==
               queue->p5RecordCount &&
           queue->recordCount <= kEnemyCapacity;
}
#endif

#if defined(TH07_PSP_1000)
bool EnemyManager::PspEnsureEnemyPool()
{
    for (i32 i = 0; i < kEnemyChunkCount; i++)
    {
        if (!this->enemyChunks[i])
        {
            this->enemyChunks[i] = static_cast<Enemy *>(th07_psp_1000_alloc_pool(
                sizeof(Enemy) * static_cast<size_t>(kEnemyChunkCapacity)));
            if (this->enemyChunks[i])
            {
                memset(this->enemyChunks[i], 0,
                       sizeof(Enemy) * static_cast<size_t>(kEnemyChunkCapacity));
            }
        }
        if (!this->enemyChunks[i])
        {
            th07_psp_boot_notef("PSP1000 enemy chunk %d/%d allocation failed", i + 1,
                                kEnemyChunkCount);
            PspReleaseEnemyPool();
            return false;
        }
    }
    th07_psp_boot_notef("PSP1000 enemy pool %d slots in %d chunks %uK", kEnemyCapacity,
                        kEnemyChunkCount,
                        static_cast<unsigned int>(sizeof(Enemy) * kEnemyCapacity / 1024u));
    return true;
}

void EnemyManager::PspReleaseEnemyPool()
{
    for (i32 i = 0; i < kEnemyChunkCount; i++)
    {
        this->enemyChunks[i] = nullptr;
    }
    memset(this->pspActiveEnemyBits, 0, sizeof(this->pspActiveEnemyBits));
}
#endif

Enemy::Enemy()
{
}

EnemyEclContext::EnemyEclContext()
{
}

Enemy *EnemyManager::SpawnEnemy(i32 eclSubId, ZunVec3 *pos, i32 life, i32 itemDrop, i32 score,
                                u8 mirror)
{
    Enemy *enemy;
    i32 i;

    for (i = 0; i < kEnemyCapacity; i++)
    {
        enemy = this->EnemyAt(i);
#if defined(TH07_PSP)
        if (this->PspIsEnemySlotTracked(i))
        {
            if (enemy->active)
            {
                continue;
            }
            this->PspForgetEnemySlot(i);
        }
#else
        if (enemy->active)
        {
            continue;
        }
#endif

        *enemy = this->enemyTemplate;
#if defined(TH07_PSP)
        // RunEcl may synchronously spawn another enemy. Reserve this slot
        // before entering ECL so a nested spawn cannot overwrite its parent.
        this->PspTrackEnemySlot(i);
#endif
        enemy->mirror = mirror;
        if (life >= 0)
        {
            enemy->life = life;
        }
        enemy->position = *pos;
        g_EclManager.CallEclSub(&enemy->currentContext, eclSubId);
        if (g_EclManager.RunEcl(enemy) == ZUN_ERROR)
        {
            enemy->active = 0;
#if defined(TH07_PSP)
            this->PspForgetEnemySlot(i);
#endif
        }
        else
        {
            enemy->color.color = enemy->primaryVm.color.color;
            enemy->itemDrop = (i8)itemDrop;
            if (score >= 0)
            {
                enemy->score = score;
            }
            enemy->maxLife = enemy->life;
        }
        break;
    }
    return enemy;
}

Enemy *EnemyManager::SpawnEnemyEx(i32 eclSubId, ZunVec3 *pos, i32 life, i32 itemDrop, i32 score,
                                  EclContextArgs *args)
{
    Enemy *enemy;
    i32 i;

    for (i = 0; i < kEnemyCapacity; i++)
    {
        enemy = this->EnemyAt(i);
#if defined(TH07_PSP)
        if (this->PspIsEnemySlotTracked(i))
        {
            if (enemy->active)
            {
                continue;
            }
            this->PspForgetEnemySlot(i);
        }
#else
        if (enemy->active)
        {
            continue;
        }
#endif

        *enemy = this->enemyTemplate;
#if defined(TH07_PSP)
        // Spawn opcodes are legal during the first ECL tick. Mark the parent
        // occupied first so recursive SpawnEnemyEx calls choose another slot.
        this->PspTrackEnemySlot(i);
#endif
        if (life >= 0)
        {
            enemy->life = life;
        }
        enemy->position = *pos;
        g_EclManager.CallEclSub(&enemy->currentContext, eclSubId);
        enemy->currentContext.eclContextArgs = *args;
        if (g_EclManager.RunEcl(enemy) == ZUN_ERROR)
        {
            enemy->active = 0;
#if defined(TH07_PSP)
            this->PspForgetEnemySlot(i);
#endif
        }
        else
        {
            enemy->color.color = enemy->primaryVm.color.color;
            enemy->itemDrop = (i8)itemDrop;
            if (life >= 0)
            {
                enemy->life = life;
            }
            if (score >= 0)
            {
                enemy->score = score;
            }
            enemy->maxLife = enemy->life;
        }
        break;
    }
    return enemy;
}

void Enemy::UpdateEffects()
{
    Effect *effect;

    for (i32 i = 0; i < this->effectsNum; i++)
    {
        effect = this->effects[i];
        if (!effect)
        {
            continue;
        }

        effect->vm.active = !this->hasNoCollision;
        effect->emitterPosition = this->position;
        if (effect->radius < this->effectDistance)
        {
            effect->radius = effect->radius + 0.3f;
        }
        effect->angularVelocity = utils::AddNormalizeAngle(effect->angularVelocity, 0.03141593f);
    }
}

void Enemy::ResetEffectArray()
{
    for (i32 i = 0; i < this->effectsNum; i++)
    {
        if (!this->effects[i])
        {
            continue;
        }

        this->effects[i]->isFadingOut = 1;
        this->effects[i] = NULL;
    }
    this->effectsNum = 0;
}

void EnemyManager::RunEclTimeline(EclTimeline *timeline)
{
    ZunVec3 pos4;
    ZunVec3 pos3;
    EclTimelineInstrArgs *args4;
    ZunVec3 pos2;
    ZunVec3 pos1;
    EclTimelineInstrArgs *args3;
    EclTimelineInstrArgs *args2;
    EclTimelineInstrArgs *args1;
    Enemy *enemy;

    while (0 <= timeline->timelineInstr->time)
    {
        if (timeline->timelineTime == timeline->timelineInstr->time)
        {
            switch (timeline->timelineInstr->opcode)
            {
            case 0:
                if (!g_Gui.BossPresent())
                {
                    args1 = &timeline->timelineInstr->args;
                    g_EnemyManager.SpawnEnemy(timeline->timelineInstr->arg0, args1->AsVec(),
                                              args1->args[3].i, args1->args[4].i, args1->args[5].i,
                                              0);
                }
                break;
            case 1:
                if (!g_Gui.BossPresent())
                {
                    g_EnemyManager.SpawnEnemy(timeline->timelineInstr->arg0,
                                              timeline->timelineInstr->args.AsVec(), -1, -1, -1, 0);
                }
                break;
            case 2:
                if (!g_Gui.BossPresent())
                {
                    args2 = &timeline->timelineInstr->args;
                    enemy = g_EnemyManager.SpawnEnemy(timeline->timelineInstr->arg0, args2->AsVec(),
                                                      args2->args[3].i, args2->args[4].i,
                                                      args2->args[5].i, 1);
                }
                break;
            case 3:
                if (!g_Gui.BossPresent())
                {
                    enemy = g_EnemyManager.SpawnEnemy(timeline->timelineInstr->arg0,
                                                      timeline->timelineInstr->args.AsVec(), -1, -1,
                                                      -1, 1);
                }
                break;
            case 4:
                if (!g_Gui.BossPresent())
                {
                    args3 = &timeline->timelineInstr->args;
                    pos1 = *args3->AsVec();
                    if (args3->AsVec()->x <= -990.0f)
                    {
                        pos1.x =
                            g_Rng.GetRandomFloatInRange(g_GameManager.playerMovementAreaSize.x);
                    }
                    if (args3->AsVec()->y <= -990.0f)
                    {
                        pos1.y =
                            g_Rng.GetRandomFloatInRange(g_GameManager.playerMovementAreaSize.y);
                    }
                    if (args3->AsVec()->z <= -990.0f)
                    {
                        pos1.z = g_Rng.GetRandomFloatInRange(800.0f);
                    }
                    g_EnemyManager.SpawnEnemy(timeline->timelineInstr->arg0, &pos1,
                                              args3->args[3].i, args3->args[4].i, args3->args[5].i,
                                              0);
                }
                break;
            case 5:
                if (!g_Gui.BossPresent())
                {
                    pos2 = *timeline->timelineInstr->args.AsVec();
                    if (pos2.x <= -990.0f)
                    {
                        pos2.x =
                            g_Rng.GetRandomFloatInRange(g_GameManager.playerMovementAreaSize.x);
                    }
                    if (pos2.y <= -990.0f)
                    {
                        pos2.y =
                            g_Rng.GetRandomFloatInRange(g_GameManager.playerMovementAreaSize.y);
                    }
                    if (pos2.z <= -990.0f)
                    {
                        pos2.z = g_Rng.GetRandomFloatInRange(800.0f);
                    }
                    g_EnemyManager.SpawnEnemy(timeline->timelineInstr->arg0, &pos2, -1, -1, -1, 0);
                }
                break;
            case 6:
                if (!g_Gui.BossPresent())
                {
                    args4 = &timeline->timelineInstr->args;
                    pos3 = *args4->AsVec();
                    if (args4->AsVec()->x <= -990.0f)
                    {
                        pos3.x =
                            g_Rng.GetRandomFloatInRange(g_GameManager.playerMovementAreaSize.x);
                    }
                    if (args4->AsVec()->y <= -990.0f)
                    {
                        pos3.y =
                            g_Rng.GetRandomFloatInRange(g_GameManager.playerMovementAreaSize.y);
                    }
                    if (args4->AsVec()->z <= -990.0f)
                    {
                        pos3.z = g_Rng.GetRandomFloatInRange(800.0f);
                    }
                    enemy = g_EnemyManager.SpawnEnemy(timeline->timelineInstr->arg0, &pos3,
                                                      args4->args[3].i, args4->args[4].i,
                                                      args4->args[5].i, 0);
                    enemy->mirror = 1;
                }
                break;
            case 7:
                if (!g_Gui.BossPresent())
                {
                    pos4 = *timeline->timelineInstr->args.AsVec();
                    if (pos4.x <= -990.0f)
                    {
                        pos4.x =
                            g_Rng.GetRandomFloatInRange(g_GameManager.playerMovementAreaSize.x);
                    }
                    if (pos4.y <= -990.0f)
                    {
                        pos4.y =
                            g_Rng.GetRandomFloatInRange(g_GameManager.playerMovementAreaSize.y);
                    }
                    if (pos4.z <= -990.0f)
                    {
                        pos4.z = g_Rng.GetRandomFloatInRange(800.0f);
                    }
                    enemy = g_EnemyManager.SpawnEnemy(timeline->timelineInstr->arg0, &pos4, -1, -1,
                                                      -1, 0);
                    enemy->mirror = 1;
                }
                break;
            case 8:
                g_Gui.MsgRead(timeline->timelineInstr->arg0 + g_GameManager.character * 10);
                break;
            case 9:
                if (g_Gui.MsgWait())
                {
                    timeline->timelineTime--;
                    goto stop;
                }
                break;
            case 10:
                g_EnemyManager.bosses[timeline->timelineInstr->args.args[0].i]->runInterrupt =
                    timeline->timelineInstr->args.args[1].i;
                break;
            case 11:
                g_GameManager.SetCurrentPower(timeline->timelineInstr->arg0);
                g_GameManager.RegenerateGameIntegrityCsum();
                break;
            case 12:
                if (g_EnemyManager.bosses[timeline->timelineInstr->arg0] &&
                    g_EnemyManager.bosses[timeline->timelineInstr->arg0]->active)
                {
                    timeline->timelineTime--;
                    goto stop;
                }
            }
        }
        else if (timeline->timelineTime < timeline->timelineInstr->time)
        {
            break;
        }
        timeline->timelineInstr =
            (EclTimelineInstr *)((u8 *)timeline->timelineInstr + timeline->timelineInstr->size);
    }
stop:
    timeline->timelineTime++;
}

i32 Enemy::HandleLifeCallback()
{
    i32 j;
    i32 i;
    Enemy *enemy;

    for (i = 0; i < 4; i++)
    {
        if (this->lifeCallbackThreshold[i] < 0)
        {
            continue;
        }

        if (this->life < this->lifeCallbackThreshold[i])
        {
            this->life = this->lifeCallbackThreshold[i];
            g_EclManager.CallEclSub(&this->currentContext, (i16)this->lifeCallbackSub[i]);
            this->lifeCallbackThreshold[i] = -1;
            this->timerCallbackThreshold = -1;
            this->periodicCallbackSub = -1;
            this->bulletRankSpeedLow = -0.5f;
            this->bulletRankSpeedHigh = 0.5f;
            this->bulletRankAmount1Low = 0;
            this->bulletRankAmount1High = 0;
            this->bulletRankAmount2Low = 0;
            this->bulletRankAmount2High = 0;
            this->stackDepth = 0;
            this->bulletProps = g_EnemyManager.enemyTemplate.bulletProps;
            this->shootInterval = 0;
            for (j = 0; j < EnemyManager::kEnemyCapacity; j++)
            {
                enemy = g_EnemyManager.EnemyAt(j);
#if defined(TH07_PSP)
                if (!g_EnemyManager.PspIsEnemySlotTracked(j))
                {
                    continue;
                }
#endif
                if (!enemy->active)
                {
                    continue;
                }

                if (enemy->isBoss)
                {
                    continue;
                }

                enemy->life = 0;
                if (!enemy->canDie && enemy->deathCallbackSub >= 0)
                {
                    g_EclManager.CallEclSub(&enemy->currentContext, (i16)enemy->deathCallbackSub);
                    enemy->deathCallbackSub = -1;
                }
            }
            return 1;
        }
    }
    return 0;
}

i32 Enemy::HandleTimerCallback()
{
    i32 j;
    Enemy *enemy;
    u32 cherryPenalty;
    i32 maxIdx;
    i32 max;
    i32 i;

    if (this->isBoss && this->bossId == 0)
    {
        g_Gui.SetSpellcardSecondsRemaining(
            (this->timerCallbackThreshold - this->timer.GetCurrent()) / 60);
    }
    if (this->timer >= this->timerCallbackThreshold)
    {
        max = 0;
        for (i = 0; i < 4; i++)
        {
            if (this->lifeCallbackThreshold[i] < 0)
            {
                continue;
            }
            if (max < this->lifeCallbackThreshold[i])
            {
                max = this->lifeCallbackThreshold[i];
                maxIdx = i;
            }
        }
        if (max > 0)
        {
            this->life = this->lifeCallbackThreshold[maxIdx];
            this->lifeCallbackThreshold[maxIdx] = -1;
        }
        g_EclManager.CallEclSub(&this->currentContext, (i16)this->timerCallbackSub);
        this->timerCallbackThreshold = -1;
        this->timerCallbackSub = this->deathCallbackSub;
        this->timer = 0;
        if (!this->isSurvivalSpellcard)
        {
            g_EnemyManager.spellcardInfo.captureScore = 0;
            g_EnemyManager.spellcardInfo.isCapturing = 0;
            if (g_EnemyManager.spellcardInfo.isActive)
            {
                g_EnemyManager.spellcardInfo.isActive++;
            }
#if defined(TH07_PSP_PERF_A1_SAME)
            Th07PspPerfSetA1SameReason(
                TH07_PSP_PERF_A1_REASON_SPELL_TIMEOUT);
#endif
            g_BulletManager.RemoveAllBullets(10);
            cherryPenalty =
                (f32)(g_GameManager.cherry - g_GameManager.globals->cherryStart) * 0.25f;
            cherryPenalty -= (i32)cherryPenalty % 10;
            g_GameManager.cherry -= cherryPenalty;
        }
        for (j = 0; j < EnemyManager::kEnemyCapacity; j++)
        {
            enemy = g_EnemyManager.EnemyAt(j);
#if defined(TH07_PSP)
            if (!g_EnemyManager.PspIsEnemySlotTracked(j))
            {
                continue;
            }
#endif
            if (!enemy->active)
            {
                continue;
            }

            if (enemy->isBoss)
            {
                continue;
            }

            enemy->life = 0;
            if (!enemy->canDie && enemy->deathCallbackSub >= 0)
            {
                g_EclManager.CallEclSub(&enemy->currentContext, (i16)enemy->deathCallbackSub);
                enemy->deathCallbackSub = -1;
            }
        }
        this->periodicCallbackSub = -1;
        this->bulletProps = g_EnemyManager.enemyTemplate.bulletProps;
        this->shootInterval = 0;
        this->bulletRankSpeedLow = -0.5f;
        this->bulletRankSpeedHigh = 0.5f;
        this->bulletRankAmount1Low = 0;
        this->bulletRankAmount1High = 0;
        this->bulletRankAmount2Low = 0;
        this->bulletRankAmount2High = 0;
        this->stackDepth = 0;
        return 1;
    }
    else
    {
        return 0;
    }
}

void Enemy::Despawn()
{
    if (this->deathType == 0)
    {
        this->active = 0;
    }
    else
    {
        this->canDie = 0;
    }
    if (this->isBoss && this->bossId < 4)
    {
        g_Gui.bossPresent = 0;
    }
    if (this->effectsNum != 0)
    {
        ResetEffectArray();
    }
    if (this->isBoss)
    {
        g_EnemyManager.bosses[this->bossId] = NULL;
    }
    g_ReplayManager->replayEventFlags |= 0x20;
}

void Enemy::ClampPos()
{
    if (this->hasMovementBounds)
    {
        if (this->position.x < this->lowerMoveLimit.x)
        {
            this->position.x = this->lowerMoveLimit.x;
        }
        else if (this->position.x > this->upperMoveLimit.x)
        {
            this->position.x = this->upperMoveLimit.x;
        }

        if (this->position.y < this->lowerMoveLimit.y)
        {
            this->position.y = this->lowerMoveLimit.y;
        }
        else if (this->position.y > this->upperMoveLimit.y)
        {
            this->position.y = this->upperMoveLimit.y;
        }
    }
}

void Enemy::CheckBulletPlayerCollision(ZunVec3 *bulletCenter, ZunVec3 *bulletSize)
{
    ZunVec3 grazeSize;

    grazeSize = *bulletSize / 0.7f;
    if (this->isProjectile && this->timer.HasTicked() && this->timer.current % 6 == 0)
    {
        g_Player.CheckGraze(bulletCenter, &grazeSize);
    }
    grazeSize = *bulletSize / 1.5f;
    if (g_Player.CalcKillboxCollision(bulletCenter, &grazeSize) == 1 && this->canDie &&
        (!this->isBoss && !this->isProjectile))
    {
        this->life = this->life - 10;
    }
}

u32 EnemyManager::OnUpdate(EnemyManager *arg)
{
    ZunVec3 bossMarkerPos;
    i32 l;
    i32 removedScore;
    i32 k;
    i32 timerLimit;
    ZunVec3 diffToPlayer;
    i32 cherryGain;
    Enemy *enemy;
    i32 playedDamageSound;
    i32 j;
    i32 grazeDamage;
    ZunVec3 currentHitbox;
    f32 angle;
    i32 i;
    i32 damage;
    i32 collisionOut;
    i32 stageFactor;
    ZunVec3 enemyDiff;
#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
    bool pspEnemyP5CaptureComplete = arg->PspBeginEnemyP5WarmQueue();
#endif

    collisionOut = 0;
    stageFactor = g_GameManager.currentStage >= 5 ? 10 : g_GameManager.currentStage * 2;
    if (!g_Gui.HasCurrentMsgIdx())
    {
        timerLimit = 2400;
        timerLimit -= (i32)g_GameManager.globals->livesRemaining * 4 * 60;
        if (arg->timelineTime.HasTicked() && arg->timelineTime.GetCurrent() % timerLimit == 0)
        {
            g_GameManager.IncreaseSubrank(100);
        }
        g_GameManager.playTimeAll++;
    }
    for (i = 0; i < 4; i++)
    {
        arg->enemyHead[i] = NULL;
    }
    for (i = 0; i < g_EclManager.eclFile->timelineCount; i++)
    {
        if (!arg->timelines[i].timelineInstr)
        {
            arg->timelines[i].timelineInstr = g_EclManager.GetTimeline(i);
        }
        RunEclTimeline(&arg->timelines[i]);
    }

    arg->enemyCountReal = 0;
    for (i = 0; i < kEnemyCapacity; i++)
    {
        enemy = arg->EnemyAt(i);
#if defined(TH07_PSP)
        if (!arg->PspIsEnemySlotTracked(i))
        {
            continue;
        }
#endif
        if (!enemy->active)
        {
#if defined(TH07_PSP)
            arg->PspForgetEnemySlot(i);
#endif
            continue;
        }
        arg->enemyCountReal++;
        if (enemy->freezeEclDuringBombs &&
            (g_Player.bombInfo.isInUse || g_Player.playerState != PLAYER_STATE_ALIVE))
        {
            enemy->timer--;
            goto LAB_00421da7;
        }
    HUH:
        if (g_EclManager.RunEcl(enemy) == ZUN_ERROR)
        {
            enemy->active = 0;
            enemy->Despawn();
            continue;
        }
        if (!enemy->disableMovement)
        {
            enemy->ClampPos();
            enemy->Move();
            enemy->ClampPos();
            if (enemy->specialEffect && !enemy->customSpecialEffectPos)
            {
                enemy->specialEffect->pos1 = enemy->specialEffect->pos1 +
                                             (enemy->position - enemy->specialEffect->pos1) / 16.0f;
            }
        }
        if (enemy->trailFlags != 0)
        {
            for (j = enemy->trailCount - 1; j > 0; j--)
            {
                enemy->enemyHistory[j].position = enemy->enemyHistory[j - 1].position;
                enemy->enemyHistory[j].axisSpeed = enemy->enemyHistory[j - 1].axisSpeed;
                enemy->enemyHistory[j].angle = enemy->enemyHistory[j - 1].angle;
            }
            enemy->enemyHistory[0].position = enemy->position;
            enemy->enemyHistory[0].axisSpeed = enemy->axisSpeed;
            enemy->enemyHistory[0].angle = enemy->angle;
        }
        if (!enemy->primaryVm.sprite)
        {
            enemy->hasNoCollision = 1;
        }
        if (!enemy->hasNoCollision && !enemy->isInBounds &&
            g_GameManager.IsInBounds(enemy->position.x, enemy->position.y,
                                     enemy->primaryVm.sprite->widthPx,
                                     enemy->primaryVm.sprite->heightPx) != 0)
        {
            enemy->isInBounds = 1;
        }
        if (enemy->isInBounds == 1 &&
            (((enemy->trailFlags == 0 &&
               g_GameManager.IsInBounds(enemy->position.x, enemy->position.y,
                                        enemy->primaryVm.sprite->widthPx,
                                        enemy->primaryVm.sprite->heightPx) == 0) ||
              (enemy->trailFlags != 0 &&
               (g_GameManager.IsInBounds(enemy->position.x, enemy->position.y,
                                         enemy->primaryVm.sprite->widthPx,
                                         enemy->primaryVm.sprite->heightPx) == 0 &&
                g_GameManager.IsInBounds(enemy->enemyHistory[enemy->trailCount - 1].position.x,
                                         enemy->enemyHistory[enemy->trailCount - 1].position.y,
                                         enemy->primaryVm.sprite->widthPx,
                                         enemy->primaryVm.sprite->heightPx) == 0))) &&
             !enemy->disableOOBDespawn))
        {
            enemy->active = 0;
            enemy->Despawn();
            continue;
        }
        if (enemy->HandleLifeCallback())
        {
            goto HUH;
        }
        if (enemy->timerCallbackThreshold >= 0 && enemy->HandleTimerCallback())
        {
            goto HUH;
        }
        enemy->primaryVm.color.color = enemy->color.color;
        g_AnmManager->ExecuteScript(&enemy->primaryVm);
        enemy->color.color = enemy->primaryVm.color.color;
        for (j = 0; j < 2; j++)
        {
            if (enemy->vms[j].anmFileIdx >= 0 && g_AnmManager->ExecuteScript(enemy->vms + j))
            {
                enemy->vms[j].anmFileIdx = -1;
            }
        }
        collisionOut = 0;
        playedDamageSound = 0;
        if (!enemy->hasNoCollision && !enemy->invisibleOnBomb)
        {
            if (enemy->canDie && enemy->hasContactHitbox)
            {
                enemy->CheckBulletPlayerCollision(&enemy->position, &enemy->hitboxSize);
                if (enemy->trailFlags != 0)
                {
                    currentHitbox = enemy->hitboxSize;
                    for (j = 1; j < enemy->trailInterval; j += 6)
                    {
                        if ((enemy->trailFlags & 2) != 0)
                        {
                            currentHitbox = enemy->hitboxSize - enemy->hitboxSize * (f32)j /
                                                                    (f32)(i32)enemy->trailInterval;
                        }
                        enemy->CheckBulletPlayerCollision(&enemy->enemyHistory[j].position,
                                                          &currentHitbox);
                    }
                }
            }
            enemy->lastDamage = 0;
            if (enemy->canDie && enemy->isHittable)
            {
                damage =
                    g_Player.CalcDamageToEnemy(&enemy->position, &enemy->hitboxSize, &collisionOut);
                if (enemy->grazeSize.x > 0.0f)
                {
                    grazeDamage = g_Player.CalcDamageToEnemy(&enemy->position, &enemy->grazeSize,
                                                             &collisionOut);
                    if (collisionOut == 0)
                    {
                        damage = (i32)((f32)damage + (f32)grazeDamage / 2.5f);
                    }
                }
                if (damage > 0)
                {
                    if ((enemy->isBoss || !g_Player.isFocus) && g_Player.bombInfo.isInUse == 0)
                    {
                        if (enemy->isBoss && !g_Player.isFocus)
                        {
                            cherryGain = damage / (10 - stageFactor / 3) * 10;
                        }
                        else
                        {
                            cherryGain = damage / (30 - stageFactor) * 10;
                        }
                        if (cherryGain > 70)
                        {
                            cherryGain = 70;
                        }
                        if (cherryGain == 0 &&
                            (g_Player.isFocus == 0 || (enemy->timer.GetCurrent() & 1) != 0))
                        {
                            cherryGain = 10;
                        }

                        // ABSOLUTELY no reason for this to be a switch statement
                        switch (g_GameManager.shotTypeAndCharacter)
                        {
                        default:
                            break;
                        case SHOT_REIMU_A:
                            if ((cherryGain == 20 || cherryGain == 30) &&
                                (enemy->timer.GetCurrent() & 1) != 0)
                            {
                                cherryGain -= 10;
                            }
                            if (g_GameManager.currentStage >= 5 &&
                                g_GameManager.currentStage <= 6 && !enemy->isBoss)
                            {
                                damage = damage / 2;
                            }
                            if (g_GameManager.currentStage == 4 && !enemy->isBoss)
                            {
                                damage -= damage / 4 + damage / 16;
                            }
                        }
                        if (cherryGain != 0)
                        {
                            g_GameManager.AddCherryPlus(cherryGain);
                        }
                    }
                    if (damage >= 70)
                    {
                        damage = 70;
                    }
                    g_GameManager.AddScore(damage / 5 * 10);
                    if (enemy->canBeDamaged)
                    {
                        if (arg->spellcardInfo.isActive)
                        {
                            if (collisionOut == 0)
                            {
                                if (damage > 7)
                                {
                                    damage = damage / 7;
                                }
                                else if (damage != 0)
                                {
                                    damage = 1;
                                }
                            }
                            else if (arg->spellcardInfo.usedBomb)
                            {
                                if (damage > 2)
                                {
                                    damage = (i32)((f32)damage / 2.5f);
                                }
                                else if (damage != 0)
                                {
                                    damage = 1;
                                }
                            }
                            else
                            {
                                damage = 0;
                            }
                        }
                        if (enemy->invincibilityTimer > 0)
                        {
                            if (enemy->isBoss)
                            {
                                damage /= 9;
                            }
                            else
                            {
                                damage = 0;
                            }
                        }
                        enemy->life -= damage;
                        enemy->lastDamage = damage;
                    }
                    playedDamageSound = 1;
                }
                if (enemy->isBoss)
                {
                    diffToPlayer = g_Player.positionOfLastEnemyHit - g_Player.positionCenter;
                    enemyDiff = enemy->position - g_Player.positionCenter;

                    if (!g_Player.targetingEnemy || fabsf(diffToPlayer.x) > fabsf(enemyDiff.x))
                    {
                        g_Player.positionOfLastEnemyHit = enemy->position;
                    }

                    if (g_GameManager.character == CHAR_SAKUYA)
                    {
                        diffToPlayer = g_Player.sakuyaTargetPosition - g_Player.positionCenter;
                        angle = atan2f(enemy->position.y - g_Player.positionCenter.y,
                                       enemy->position.x - g_Player.positionCenter.x);

                        if (angle >= -2.0943952f && angle <= -1.0471976f &&
                            (!g_Player.targetingEnemy ||
                             fabsf(diffToPlayer.x) > fabsf(enemyDiff.x)))
                        {
                            g_Player.sakuyaTargetPosition = enemy->position;
                            g_Player.targetingEnemy = 1;
                        }
                    }
                    else
                    {
                        g_Player.targetingEnemy = 1;
                    }
                }
                if (!g_Player.targetingEnemy)
                {
                    if (g_Player.positionOfLastEnemyHit.y < enemy->position.y)
                    {
                        g_Player.positionOfLastEnemyHit = enemy->position;
                    }
                    if (g_GameManager.character == CHAR_SAKUYA &&
                        g_Player.sakuyaTargetPosition.y < -900.0f)
                    {
                        angle = atan2f(enemy->position.y - g_Player.positionCenter.y,
                                       enemy->position.x - g_Player.positionCenter.x);
                        if (angle >= -2.0943952f && angle <= -1.0471976f)
                        {
                            g_Player.sakuyaTargetPosition = enemy->position;
                        }
                    }
                }
            }
        }
        if (enemy->life <= 0 && enemy->canDie)
        {
            for (k = 0; k < 4; k++)
            {
                enemy->lifeCallbackThreshold[k] = -1;
            }
            enemy->timerCallbackThreshold = -1;
            enemy->periodicCallbackSub = -1;

            switch (enemy->deathType)
            {
            case 3:
                enemy->life = 1;
                enemy->canBeDamaged = 0;
                enemy->deathType = 0;
                g_Gui.bossPresent = 0;
                g_ReplayManager->replayEventFlags |= 0x20;
                if (enemy->deathAnm1 >= 0)
                {
                    g_EffectManager.SpawnParticles(enemy->deathAnm1, &enemy->position, 1,
                                                   0xffffffff);
                    g_EffectManager.SpawnParticles(enemy->deathAnm1, &enemy->position, 1,
                                                   0xffffffff);
                    g_EffectManager.SpawnParticles(enemy->deathAnm1, &enemy->position, 1,
                                                   0xffffffff);
                }
                break;
            case 1:
                g_GameManager.AddScore(enemy->score);
                enemy->canDie = 0;
                goto END_BOSS;
            case 0:
                g_GameManager.AddScore(enemy->score);
                enemy->active = 0;
                goto END_BOSS;
            END_BOSS:
                if (enemy->isBoss)
                {
                    g_Gui.bossPresent = 0;
                    enemy->ResetEffectArray();
                }
            case 2:
                if (enemy->itemDrop >= 0)
                {
                    g_EffectManager.SpawnParticles(enemy->deathAnm2 + 4, &enemy->position, 3,
                                                   0xffffffff);
                    g_ItemManager.SpawnItem(&enemy->position, enemy->itemDrop, collisionOut);
                }
                else if (enemy->itemDrop == -1)
                {
                    if ((i32)arg->randomItemSpawnIdx % 3 == 0)
                    {
                        g_EffectManager.SpawnParticles(enemy->deathAnm2 + 4, &enemy->position, 6,
                                                       0xffffffff);
                        g_ItemManager.SpawnItem(&enemy->position,
                                                g_ItemDropTable[arg->randomItemTableIdx],
                                                collisionOut);
                        arg->randomItemTableIdx++;
                        if (arg->randomItemTableIdx >= 32)
                        {
                            arg->randomItemTableIdx = 0;
                        }
                    }
                    arg->randomItemSpawnIdx++;
                }
                if (enemy->isBoss && !g_EnemyManager.spellcardInfo.isActive)
                {
#if defined(TH07_PSP_PERF_A1_SAME)
                    Th07PspPerfSetA1SameReason(
                        TH07_PSP_PERF_A1_REASON_BOSS_DEFEAT);
#endif
                    removedScore = g_BulletManager.DespawnBullets(8000, 1);
#if defined(TH07_PSP_PERF_A1_SAME)
                    Th07PspPerfSetA1SameReason(
                        TH07_PSP_PERF_A1_REASON_BOSS_DEFEAT);
#endif
                    removedScore = g_EnemyManager.RemoveAllEnemies(8000, removedScore);
                    if (removedScore != 0)
                    {
                        g_GameManager.AddScore(removedScore);
                        g_Gui.ShowBonusScore(removedScore);
                    }
                }
                enemy->life = 0;
                g_ReplayManager->replayEventFlags |= 0x20;
                break;
            }

            g_SoundPlayer.PlaySoundByIdx(i % 2 + 2, 0);
            if (enemy->deathAnm1 >= 0)
            {
                g_EffectManager.SpawnParticles(enemy->deathAnm1, &enemy->position, 1, 0xffffffff);
                g_EffectManager.SpawnParticles(enemy->deathAnm2 + 4, &enemy->position, 4,
                                               0xffffffff);
            }
            if (enemy->deathCallbackSub >= 0)
            {
                enemy->bulletRankSpeedLow = -0.5f;
                enemy->bulletRankSpeedHigh = 0.5f;
                enemy->bulletRankAmount1Low = 0;
                enemy->bulletRankAmount1High = 0;
                enemy->bulletRankAmount2Low = 0;
                enemy->bulletRankAmount2High = 0;
                enemy->stackDepth = 0;
                for (l = 0; l < 4; l++)
                {
                    enemy->lifeCallbackThreshold[l] = -1;
                }
                enemy->timerCallbackThreshold = -1;
                enemy->periodicCallbackSub = -1;
                enemy->bulletProps = g_EnemyManager.enemyTemplate.bulletProps;
                enemy->shootInterval = 0;
                g_EclManager.CallEclSub(&enemy->currentContext, (i16)enemy->deathCallbackSub);
                enemy->deathCallbackSub = -1;
            }
        }

    LAB_00421da7:
        if (enemy->damageTintTimer != 0)
        {
            enemy->damageTintTimer--;
            enemy->primaryVm.useColor2 = 0;
        }
        else if (playedDamageSound != 0)
        {
            g_SoundPlayer.PlaySoundByIdx(SOUND_20, 0);
            enemy->primaryVm.color2.bytes.r = 255;
            enemy->primaryVm.color2.bytes.g = 128;
            enemy->primaryVm.color2.bytes.b = 192;
            enemy->primaryVm.color2.bytes.a = enemy->primaryVm.color.bytes.a;
            enemy->primaryVm.useColor2 = 1;
            enemy->damageTintTimer = 1;
        }
        else
        {
            enemy->primaryVm.useColor2 = 0;
        }
        if (enemy->isBoss)
        {
            if (!g_Gui.HasCurrentMsgIdx() && enemy->bossId == 0)
            {
                g_Gui.SetBossHealthBar((f32)enemy->life / (f32)enemy->maxLife);
            }

            if (enemy->bossId < 4)
            {
                if (!enemy->hasNoCollision)
                {
                    bossMarkerPos.x = enemy->position.x + 32.0f;
                }
                else
                {
                    bossMarkerPos.x = -999.0f;
                }
                bossMarkerPos.y = 472.0f;
                bossMarkerPos.z = 0.0f;

                g_AsciiManager.SetBossMarkerPos(enemy->bossId, &bossMarkerPos);
                g_AsciiManager.SetBossDamageTint(enemy->bossId, enemy->primaryVm.useColor2);
            }
        }
        enemy->UpdateEffects();
        if (!g_GameManager.isTimeStopped)
        {
            enemy->timer++;
        }
        if (enemy->invincibilityTimer > 0)
        {
            enemy->invincibilityTimer--;
        }
        if (!enemy->hasNoCollision && enemy->active)
        {
            enemy->next = arg->enemyHead[enemy->zLayer];
            arg->enemyHead[enemy->zLayer] = enemy;
#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
            if (pspEnemyP5CaptureComplete &&
                !arg->PspCaptureEnemyP5WarmRecord(enemy, static_cast<u32>(i),
                                                   enemy->zLayer))
            {
                pspEnemyP5CaptureComplete = false;
            }
#endif
        }
    }

    if (arg->timelineTime.current % 200 == 0 && g_GameManager.CheckGameIntegrity())
    {
        return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
    }
    arg->timelineTime++;
#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
    arg->PspPublishEnemyP5WarmQueue(pspEnemyP5CaptureComplete);
#endif
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

f32 AngleLerp(f32 start, f32 target, f32 t)
{
    f32 direct;
    f32 wrapped;

    if (start < target)
    {
        direct = target - start;
        wrapped = start + ZUN_2PI - target;
    }
    else
    {
        direct = start - target;
        wrapped = target + ZUN_2PI - start;
        start = target;
    }
    if (direct < wrapped)
    {
        return direct * t + start;
    }
    return wrapped * t + start;
}

#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
namespace
{
inline void PspDrawEnemyP5WarmVm(AnmVm *vm, f32 enemyX, f32 enemyY,
                                 f32 enemyAngle, f32 z, bool negateAngle)
{
    if (vm->anmFileIdx < 0)
    {
        return;
    }
    if (vm->autoRotate)
    {
        vm->SetRotationZ(negateAngle ? -enemyAngle : enemyAngle);
        vm->updateRotation = 1;
    }
    vm->pos.x = enemyX + vm->offset.x;
    vm->pos.y = enemyY + vm->offset.y;
    vm->pos.z = z;
    vm->pos.x += g_GameManager.arcadeRegionTopLeftPos.x;
    vm->pos.y += g_GameManager.arcadeRegionTopLeftPos.y;
    g_AnmManager->DrawPspFastSprite(vm);
}

inline void PspDrawEnemyP5WarmFast(const PspEnemyP5WarmRecord &record)
{
    // Keep the source order observable by alpha blending and by AnmVm state:
    // child0 -> primary -> child1.  Only topology and immutable calc outputs
    // are warm; arcade origin, VM offset/color, manager color multiplication,
    // viewport, textures and render state are all read here at draw time.
    PspDrawEnemyP5WarmVm(record.child0, record.positionX, record.positionY,
                         record.angle, 0.3f, false);

    AnmVm *primary = record.primary;
    if (record.flags & kPspEnemyP5WarmPrimaryAutoRotate)
    {
        primary->SetRotationZ(record.angle);
        primary->updateRotation = 1;
    }
    primary->pos.x = record.positionX + primary->offset.x;
    primary->pos.y = record.positionY + primary->offset.y;
    primary->pos.z = 0.29f;
    if ((record.flags & kPspEnemyP5WarmInvisibleOnBomb) == 0u)
    {
        primary->pos.x += g_GameManager.arcadeRegionTopLeftPos.x;
        primary->pos.y += g_GameManager.arcadeRegionTopLeftPos.y;
        g_AnmManager->DrawPspFastSprite(primary);
    }

    PspDrawEnemyP5WarmVm(record.child1, record.positionX, record.positionY,
                         record.angle, 0.3f, true);
}

inline void PspDrawEnemyP5CanonicalOne(EnemyManager *manager,
                                       const PspEnemyP5WarmRecord &record)
{
    // Trail geometry has a large, stateful canonical path.  Isolate exactly
    // one Enemy while retaining that proven implementation, then restore the
    // public four-head topology before continuing the compact queue.
    Enemy *enemy = record.enemy;
    const u32 headIndex = record.headIndex;
    Enemy *savedHead = manager->enemyHead[headIndex];
    Enemy *savedNext = enemy->next;
    manager->enemyHead[headIndex] = enemy;
    enemy->next = NULL;
    EnemyManager::ActualOnDraw(manager, headIndex, headIndex + 1u);
    enemy->next = savedNext;
    manager->enemyHead[headIndex] = savedHead;
}

bool PspValidateEnemyP5WarmQueue(const PspEnemyP5WarmQueue *queue)
{
    if (!queue)
    {
        return false;
    }
    u32 totalVisits = 0u;
    for (u32 head = 0; head < 2u; ++head)
    {
        u32 headVisits = 0u;
        u16 recordIndex = queue->heads[head];
        while (recordIndex != kPspEnemyP5WarmQueueEnd)
        {
            if (recordIndex >= EnemyManager::kEnemyCapacity ||
                headVisits >= queue->headCounts[head] ||
                totalVisits >= queue->p5RecordCount)
            {
                return false;
            }
            const PspEnemyP5WarmRecord &record = queue->records[recordIndex];
            if (record.slotIndex != recordIndex || record.headIndex != head ||
                !record.enemy || !record.child0 || !record.primary || !record.child1)
            {
                return false;
            }
            recordIndex = record.nextIndex;
            ++headVisits;
            ++totalVisits;
        }
        if (headVisits != queue->headCounts[head])
        {
            return false;
        }
    }
    return totalVisits == queue->p5RecordCount;
}

u32 PspDrawEnemyP5WarmQueue(EnemyManager *manager)
{
    const PspEnemyP5WarmQueue *queue = PspGetEnemyP5WarmQueue(manager);
    u32 visits = 0u;
    for (u32 head = 0; head < 2u; ++head)
    {
        u16 recordIndex = queue->heads[head];
        while (recordIndex != kPspEnemyP5WarmQueueEnd)
        {
            const PspEnemyP5WarmRecord &record = queue->records[recordIndex];
            if (record.flags & kPspEnemyP5WarmCanonical)
            {
                PspDrawEnemyP5CanonicalOne(manager, record);
#if defined(TH07_PSP_PERF_DENSE_SLICE)
                if (gTh07PspPerfDenseSliceActive)
                {
                    ++gPspEnemyP5WarmWindow.canonicalEnemyDraws;
                }
#endif
            }
            else
            {
                PspDrawEnemyP5WarmFast(record);
#if defined(TH07_PSP_PERF_DENSE_SLICE)
                if (gTh07PspPerfDenseSliceActive)
                {
                    ++gPspEnemyP5WarmWindow.fastEnemyDraws;
                }
#endif
            }
            recordIndex = record.nextIndex;
            ++visits;
        }
    }
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    if (gTh07PspPerfDenseSliceActive)
    {
        gPspEnemyP5WarmWindow.recordVisits += visits;
    }
#endif
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}
} // namespace
#endif

u32 EnemyManager::ActualOnDraw(EnemyManager *arg, i32 first, i32 last)
{
    f32 uvDiff;
    f32 cosAngle;
    f32 angle1;
    f32 uvStep;
    VertexTex1DiffuseXyzrhw *trailVert;
    f32 prevAngle;
    f32 currentUvX;
    f32 sinAngle;
    i32 vertexCount;
    f32 xOffset;
    f32 yOffset;
    Enemy *enemy;
    i32 j;
    AnmVm *vm;
    ZunColor baseColor;
    i32 i;
    Float2 scale;

    for (i = first; i < last; i++)
    {
        enemy = arg->enemyHead[i];
        while (enemy)
        {
            vm = &enemy->vms[0];
            for (j = 0; j < 1; j++, vm++)
            {
                if (vm->anmFileIdx >= 0)
                {
                    if (vm->autoRotate)
                    {
                        vm->SetRotationZ(enemy->angle);
                        vm->updateRotation = 1;
                    }

                    vm->pos = enemy->position + vm->offset;
                    vm->pos.z = 0.3f;
                    vm->pos.x += g_GameManager.arcadeRegionTopLeftPos.x;
                    vm->pos.y += g_GameManager.arcadeRegionTopLeftPos.y;

                    g_AnmManager->Draw(vm);
                }
            }

            if (enemy->primaryVmAutoRotate)
            {
                enemy->primaryVm.SetRotationZ(enemy->angle);
                enemy->primaryVm.updateRotation = 1;
            }
            enemy->primaryVm.pos = enemy->position + enemy->primaryVm.offset;
            enemy->primaryVm.pos.z = 0.29f;
            if ((enemy->trailFlags & 16) == 0 && !enemy->invisibleOnBomb)
            {
                enemy->primaryVm.pos.x += g_GameManager.arcadeRegionTopLeftPos.x;
                enemy->primaryVm.pos.y += g_GameManager.arcadeRegionTopLeftPos.y;
                g_AnmManager->Draw(&enemy->primaryVm);
            }

            for (j = 1; j < 2; j++, vm++)
            {
                if (vm->anmFileIdx >= 0)
                {
                    if (vm->autoRotate)
                    {
                        vm->SetRotationZ(-enemy->angle);
                        vm->updateRotation = 1;
                    }
                    vm->pos = enemy->position + vm->offset;
                    vm->pos.z = 0.3f;
                    vm->pos.x += g_GameManager.arcadeRegionTopLeftPos.x;
                    vm->pos.y += g_GameManager.arcadeRegionTopLeftPos.y;
                    g_AnmManager->Draw(vm);
                }
            }

            if (enemy->trailFlags != 0)
            {
                scale = enemy->primaryVm.scale;
                baseColor.color = enemy->primaryVm.color.color;

                if ((enemy->trailFlags & 8) == 0)
                {
                    for (j = enemy->trailNodeStep; j < enemy->trailCount; j += enemy->trailNodeStep)
                    {
                        if (enemy->enemyHistory[j].position.x < -990.0f)
                        {
                            continue;
                        }

                        if (enemy->primaryVmAutoRotate)
                        {
                            enemy->primaryVm.SetRotationZ(enemy->enemyHistory[j].angle);
                            enemy->primaryVm.updateRotation = 1;
                        }
                        if ((enemy->trailFlags & 2) != 0)
                        {
                            enemy->primaryVm.scale.x =
                                scale.x - (f32)j * scale.x / (f32)enemy->trailCount;
                        }
                        if ((enemy->trailFlags & 4) != 0)
                        {
                            enemy->primaryVm.color.bytes.a =
                                baseColor.bytes.a - baseColor.bytes.a * j / enemy->trailCount;
                        }
                        enemy->primaryVm.pos =
                            enemy->enemyHistory[j].position + enemy->primaryVm.offset;
                        enemy->primaryVm.pos.z = 0.3f;
                        enemy->primaryVm.pos.x += g_GameManager.arcadeRegionTopLeftPos.x;
                        enemy->primaryVm.pos.y += g_GameManager.arcadeRegionTopLeftPos.y;
                        g_AnmManager->Draw(&enemy->primaryVm);
                    }
                }
                else
                {
                    vertexCount = 0;

                    for (j = 0; j < enemy->trailCount; j += enemy->trailNodeStep)
                    {
                        if (enemy->enemyHistory[j].position.x < -990.0f)
                        {
                            break;
                        }
                        vertexCount += 2;
                    }
                    if (vertexCount > 2)
                    {
                        uvDiff =
                            enemy->primaryVm.sprite->uvEnd.x - enemy->primaryVm.sprite->uvStart.x;
                        uvStep = uvDiff / (f32)(i32)((vertexCount + 1) / 2 - 1);
                        currentUvX =
                            enemy->primaryVm.sprite->uvEnd.x + enemy->primaryVm.uvScrollPos.x;
                        trailVert = enemy->trailVertices;

                        for (j = 0; j < enemy->trailCount;
                             j += enemy->trailNodeStep, currentUvX -= uvStep)
                        {
                            if (enemy->enemyHistory[j].position.x < -990.0f)
                            {
                                break;
                            }

                            if (j == 0)
                            {
                                angle1 = enemy->enemyHistory[0].angle;
                            }
                            else
                            {
                                angle1 = AngleLerp(enemy->enemyHistory[j - 1].angle,
                                                   enemy->enemyHistory[j].angle, 0.5f);
                            }

                            if ((enemy->trailFlags & 2) != 0 && j > 0 &&
                                j + enemy->trailNodeStep < enemy->trailCount)
                            {
                                sinAngle = AngleLerp(
                                    enemy->enemyHistory[j + enemy->trailNodeStep - 1].angle,
                                    enemy->enemyHistory[enemy->trailNodeStep].angle, 0.5f);
                                if (fabsf(prevAngle - angle1) < 0.00001f &&
                                    fabsf(angle1 - sinAngle) < 0.00001f)
                                {
                                    vertexCount -= 2;
                                    continue;
                                }
                            }

                            prevAngle = angle1;
#if defined(TH07_PSP)
                            PspEnemyRenderSinCos(angle1, &sinAngle, &cosAngle);
#else
                            sinAngle = sinf(angle1);
                            cosAngle = cosf(angle1);
#endif
                            xOffset = 0.0f;
                            yOffset = scale.y * enemy->primaryVm.sprite->heightPx / 2.0f;

                            if ((enemy->trailFlags & 2) != 0)
                            {
                                angle1 = 1.0f - (f32)j / (f32)enemy->trailCount;
                                xOffset *= angle1;
                                yOffset *= angle1;
                            }

                            trailVert[1].color.color = enemy->primaryVm.color.color;
                            trailVert[0].color.color = trailVert[1].color.color;

                            if ((enemy->trailFlags & 4) != 0)
                            {
                                trailVert[1].color.bytes.a =
                                    baseColor.bytes.a - baseColor.bytes.a * j / enemy->trailCount;
                                trailVert[0].color.bytes.a = trailVert[1].color.bytes.a;
                            }

                            trailVert[0].pos = enemy->enemyHistory[j].position;
                            trailVert[0].pos.x += cosAngle * xOffset - sinAngle * yOffset + 32.0f;
                            trailVert[0].pos.y += sinAngle * xOffset + cosAngle * yOffset + 16.0f;
                            trailVert[0].textureUV.x = currentUvX;
                            trailVert[0].textureUV.y =
                                enemy->primaryVm.sprite->uvStart.y + enemy->primaryVm.uvScrollPos.y;
                            trailVert++;

                            trailVert[0].pos = enemy->enemyHistory[j].position;
                            trailVert[0].pos.x += cosAngle * xOffset + sinAngle * yOffset + 32.0f;
                            trailVert[0].pos.y += sinAngle * xOffset - cosAngle * yOffset + 16.0f;
                            trailVert[0].textureUV.x = currentUvX;
                            trailVert[0].textureUV.y =
                                enemy->primaryVm.sprite->uvEnd.y + enemy->primaryVm.uvScrollPos.y;
                            trailVert++;
                        }
                        if (vertexCount > 2)
                        {
                            g_AnmManager->DrawTriangleStrip(&enemy->primaryVm, enemy->trailVertices,
                                                            vertexCount);
                        }
                    }
                }
                enemy->primaryVm.scale = scale;
                enemy->primaryVm.color.color = baseColor.color;
            }
            enemy = enemy->next;
        }
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

u32 EnemyManager::OnDraw1(EnemyManager *arg)
{
#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
    if (arg->PspEnemyP5WarmQueueReady() &&
        PspValidateEnemyP5WarmQueue(PspGetEnemyP5WarmQueue(arg)))
    {
#if defined(TH07_PSP_PERF_DENSE_SLICE)
        if (gTh07PspPerfDenseSliceActive)
        {
            ++gPspEnemyP5WarmWindow.readyFrames;
        }
#endif
        return PspDrawEnemyP5WarmQueue(arg);
    }
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    if (gTh07PspPerfDenseSliceActive)
    {
        ++gPspEnemyP5WarmWindow.fallbackFrames;
    }
#endif
#endif
    return ActualOnDraw(arg, 0, 2);
}

u32 EnemyManager::OnDraw2(EnemyManager *arg)
{
    return ActualOnDraw(arg, 2, 4);
}

ZunResult EnemyManager::AddedCallback(EnemyManager *arg)
{
    if (arg->stgEnmAnmFilename && g_AnmManager->LoadAnms(ANM_FILE_ENEMY, arg->stgEnmAnmFilename,
                                                         ANM_OFFSET_ENEMY) != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }
    if (arg->stgEnm2AnmFilename && g_AnmManager->LoadAnms(ANM_FILE_ENEMY2, arg->stgEnm2AnmFilename,
                                                          ANM_OFFSET_ENEMY) != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }

    arg->randomItemSpawnIdx = g_Rng.GetRandomU16InRange(3);
    arg->randomItemTableIdx = g_Rng.GetRandomU16InRange(8);
    arg->spellcardInfo.isActive = 0;

    ZunVec3 vec = ZunVec3(-999.0f, -999.0f, -999.0f);
    g_AsciiManager.GetBossMarker(0)->pos = vec;
    g_AsciiManager.GetBossMarker(1)->pos = vec;
    g_AsciiManager.GetBossMarker(2)->pos = vec;
    g_AsciiManager.GetBossMarker(3)->pos = vec;
#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
    if (!arg->PspEnsureEnemyP5WarmQueue())
    {
        th07_psp_boot_note("enemy p5 warm queue unavailable fallback");
    }
#endif
    return ZUN_SUCCESS;
}

ZunResult EnemyManager::DeletedCallback(EnemyManager *arg)
{
    g_AnmManager->ReleaseAnm(16);
    g_AnmManager->ReleaseAnm(15);
#if defined(TH07_PSP_1000)
    arg->PspReleaseEnemyPool();
#endif
#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
    arg->PspReleaseEnemyP5WarmQueue();
#endif
#if !defined(TH07_PSP_1000) && !defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
    (void)arg;
#endif
    ZunVec3 vec = ZunVec3(-999.0f, -999.0f, -999.0f);
    g_AsciiManager.GetBossMarker(0)->pos = vec;
    g_AsciiManager.GetBossMarker(1)->pos = vec;
    g_AsciiManager.GetBossMarker(2)->pos = vec;
    g_AsciiManager.GetBossMarker(3)->pos = vec;
    return ZUN_SUCCESS;
}

#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE) && defined(TH07_PSP_PERF_DENSE_SLICE)
void Th07PspTakeEnemyP5WarmWindow(Th07PspEnemyP5WarmWindow *window)
{
    if (!window)
    {
        return;
    }
    *window = gPspEnemyP5WarmWindow;
    gPspEnemyP5WarmWindow = Th07PspEnemyP5WarmWindow{};
}
#endif

ZunResult EnemyManager::RegisterChain(const char *stgEnm1, const char *stgEnm2)
{
    EnemyManager *mgr = &g_EnemyManager;
    mgr->Initialize();
    mgr->stgEnmAnmFilename = stgEnm1;
    mgr->stgEnm2AnmFilename = stgEnm2;
    g_EnemyManagerCalcChain.callback = (ChainCallback)OnUpdate;
    g_EnemyManagerCalcChain.addedCallback = NULL;
    g_EnemyManagerCalcChain.deletedCallback = NULL;
    g_EnemyManagerCalcChain.addedCallback = (ChainLifecycleCallback)AddedCallback;
    g_EnemyManagerCalcChain.deletedCallback = (ChainLifecycleCallback)DeletedCallback;
    g_EnemyManagerCalcChain.arg = mgr;
    if (g_Chain.AddToCalcChain(&g_EnemyManagerCalcChain, 10))
    {
        return ZUN_ERROR;
    }

    g_EnemyManagerDrawChain1.callback = (ChainCallback)OnDraw1;
    g_EnemyManagerDrawChain1.addedCallback = NULL;
    g_EnemyManagerDrawChain1.deletedCallback = NULL;
    g_EnemyManagerDrawChain1.arg = mgr;
    if (g_Chain.AddToDrawChain(&g_EnemyManagerDrawChain1, 5) != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }

    g_EnemyManagerDrawChain2.callback = (ChainCallback)OnDraw2;
    g_EnemyManagerDrawChain2.addedCallback = NULL;
    g_EnemyManagerDrawChain2.deletedCallback = NULL;
    g_EnemyManagerDrawChain2.arg = mgr;
    if (g_Chain.AddToDrawChain(&g_EnemyManagerDrawChain2, 7) != ZUN_SUCCESS)
    {
        return ZUN_ERROR;
    }

    return ZUN_SUCCESS;
}

void EnemyManager::CutChain()
{
    g_Chain.Cut(&g_EnemyManagerCalcChain);
    g_Chain.Cut(&g_EnemyManagerDrawChain1);
    g_Chain.Cut(&g_EnemyManagerDrawChain2);
}

i32 EnemyManager::RemoveAllEnemies(i32 scoreMax, i32 scoreMin)
{
    i32 j;
    i32 i;
    Enemy *enemy;
    i32 totalScore;
    i32 popupScore;
#if defined(TH07_PSP_PERF_A1_SAME)
    Th07PspPerfA1SameSample perfA1Sample{};
    perfA1Sample.reason = Th07PspPerfTakeA1SameReason();
    perfA1Sample.mode = scoreMax == 8000 ? 1u << 0
                        : scoreMax == 0  ? 1u << 1
                                         : 1u << 2;
    if (scoreMin != 0)
    {
        perfA1Sample.mode |= 1u << 3;
    }
    const unsigned long long perfA1StartUs = sceKernelGetSystemTimeWide();
#endif

#if defined(TH07_PSP_ENEMY_P5_WARM_QUEUE)
    // Gui runs at calc 13, after this queue's calc-10 capture.  Even though
    // most removals become visible on the following Enemy update, invalidate
    // conservatively so the complete existing P5 list handles that frame.
    PspMarkEnemyMutation();
#endif

    totalScore = scoreMin;
    popupScore = 2000;
    for (i = 0; i < kEnemyCapacity; i++)
    {
        enemy = this->EnemyAt(i);
#if defined(TH07_PSP)
        if (!this->PspIsEnemySlotTracked(i))
        {
            continue;
        }
#endif
        if (!enemy->active)
        {
            continue;
        }
#if defined(TH07_PSP_PERF_A1_SAME)
        ++perfA1Sample.eligible;
#endif

        if (enemy->isBoss)
        {
            continue;
        }

#if defined(TH07_PSP_PERF_A1_SAME)
        ++perfA1Sample.affected;
#endif
        enemy->life = 0;
        if (enemy->isProjectile)
        {
#if defined(TH07_PSP_PERF_A1_SAME)
            ++perfA1Sample.auxiliary;
            ++perfA1Sample.itemAttempts;
            ++perfA1Sample.popups;
#endif
            g_ItemManager.SpawnItem(&enemy->position, ITEM_POINT_BULLET, 1);
            g_AsciiManager.CreatePopup1(&enemy->position, popupScore,
                                        popupScore >= scoreMax ? 0xffffff00 : 0xffffffff);
            totalScore += popupScore;
            popupScore += 30;
            if (popupScore > scoreMax)
            {
                popupScore = scoreMax;
            }
            if (enemy->trailFlags != 0)
            {
                for (j = 0; j < enemy->trailCount; j += 6)
                {
#if defined(TH07_PSP_PERF_A1_SAME)
                    ++perfA1Sample.itemAttempts;
                    ++perfA1Sample.popups;
#endif
                    g_ItemManager.SpawnItem(&enemy->enemyHistory[j].position, ITEM_POINT_BULLET, 1);
                    g_AsciiManager.CreatePopup1(&enemy->enemyHistory[j].position, popupScore,
                                                popupScore >= scoreMax ? 0xffffff00 : 0xffffffff);
                    totalScore += popupScore;
                    popupScore += 30;
                    if (popupScore > scoreMax)
                    {
                        popupScore = scoreMax;
                    }
                }
            }
        }
        if (!enemy->canDie && enemy->deathCallbackSub >= 0)
        {
            g_EclManager.CallEclSub(&enemy->currentContext, (i16)enemy->deathCallbackSub);
            enemy->deathCallbackSub = -1;
        }
    }
#if defined(TH07_PSP_PERF_A1_SAME)
    const unsigned long long perfA1EndUs = sceKernelGetSystemTimeWide();
    Th07PspPerfAddA1SameSample(
        TH07_PSP_PERF_A1_REMOVE_ALL_ENEMIES,
        perfA1EndUs - perfA1StartUs, perfA1Sample);
#endif
    return totalScore;
}

i32 EnemyManager::HasActiveBoss()
{
    for (i32 i = 0; i < 8; i++)
    {
        if (this->bosses[i])
        {
            return 1;
        }
    }
    return 0;
}

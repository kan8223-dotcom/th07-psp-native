#pragma once

#include "ItemManager.hpp"
#include "ZunMath.hpp"
#include "ZunResult.hpp"
#include "utils.hpp"

extern u32 *g_BulletColor;

typedef enum BulletState
{
    BULLET_INACTIVE = 0,
    BULLET_NORMAL = 1,
    BULLET_SPAWNING_FAST = 2,
    BULLET_SPAWNING_NORMAL = 3,
    BULLET_SPAWNING_SLOW = 4,
    BULLET_DESPAWN = 5,
    BULLET_END_ARRAY = 6
} BulletState;

typedef enum LaserState
{
    LASER_SPAWNING = 0,
    LASER_ACTIVE = 1,
    LASER_DESPAWNING = 2
} LaserState;

struct BulletTypeInfo
{
    i32 anmFileIdx;
    i32 spawnFastIdx;
    i32 spawnNormalIdx;
    i32 spawnSlowIdx;
    i32 spawnDonutIdx;
};

struct BulletTypeSprites
{
    AnmVm spriteBullet;
    AnmVm spriteSpawnEffectFast;
    AnmVm spriteSpawnEffectNormal;
    AnmVm spriteSpawnEffectSlow;
    AnmVm spriteSpawnEffectDonut;
    ZunVec3 grazeSize;
    u8 unused_b88;
    u8 bulletHeight;
    u8 collisionType;
    // pad 1
};

#if defined(TH07_PSP_1000)
// Fast, normal, and slow spawn animations are mutually exclusive states of a
// bullet. Keeping three complete AnmVm instances in every PSP-1000 payload
// wastes over 1 MiB at the original 1,024-slot capacity, so the low-memory
// runtime stores only the selected spawn animation. The 16 type templates
// above remain unchanged and provide the appropriate source VM at spawn time.
struct Psp1000BulletSprites
{
    AnmVm spriteBullet;
    AnmVm spriteSpawnEffect;
    AnmVm spriteSpawnEffectDonut;
    ZunVec3 grazeSize;
    u8 unused_b88;
    u8 bulletHeight;
    u8 collisionType;
    // pad 1
};
#endif

struct BulletCommand
{
    f32 speed;
    f32 angle;
    i32 duration;
    i32 loopCount;
    u32 type;
    i32 flag;
};

struct BulletCommandState
{
    ZunTimer timer;
    f32 speed;
    f32 angle;
    ZunVec3 vec3;
    i32 duration;
    i32 maxTimes;
    i32 minTimes;
};

struct EnemyBulletShooter
{
    EnemyBulletShooter()
    {
        memset(this, 0, sizeof(EnemyBulletShooter));
        this->soundOverride = -1;
    }

    BulletCommand *AddCommand(i32 command, i32 flag, u32 type);
    void AddAngleAccelCommand(i32 command, i32 flag, i32 duration, f32 angle, f32 speed);
    void AddDirChangeCommand(i32 command, i32 flag, i32 duration, i32 loopCount, f32 speed,
                             f32 angle);
    void AddSpawnDelayCommand(i32 command, i32 flag, i32 duration);
    void AddTargetVelocityCommand(i32 command, i32 flag, i32 duration, f32 speed, f32 angle);

    i16 sprite;
    i16 spriteOffset;
    ZunVec3 position;
    f32 angle1;
    f32 angle2;
    f32 speed1;
    f32 speed2;
    BulletCommand commands[6];
    i32 unused_b0[3];
    i16 count1;
    i16 count2;
    u16 aimMode;
    i16 unused_c2;
    u32 flags;
    i32 soundIdx;
    i32 soundOverride;
    BulletTypeSprites *sprites;
};

struct EnemyLaserShooter
{
    EnemyLaserShooter()
    {
        memset(this, 0, sizeof(EnemyLaserShooter));
        this->soundOverride = -1;
    }

    i16 sprite;
    i16 spriteOffset;
    ZunVec3 position;
    f32 angle1;
    f32 angle2;
    f32 speed1;
    f32 speed2;
    BulletCommand commands[5];
    f32 startOffset;
    f32 endOffset;
    f32 startLength;
    f32 width;
    i32 startTime;
    i32 duration;
    i32 endTime;
    i32 hitboxStartTime;
    i32 hitboxEndTime;
    i32 unused_bc;
    u16 type;
    i16 unused_c2;
    u32 flags;
    i32 unused_c8;
    i32 soundOverride;
    i32 unused_d0;
};

struct Laser
{
    void UpdateRotationZFromAngle()
    {
        f32 angle = utils::AddNormalizeAngle(ZUN_PI / 2.0f + this->angle, 0.0f);
        this->vm0.rotation.z = angle;
    }

    struct AnmVm vm0;
    struct AnmVm vm1;
    ZunVec3 pos;
    f32 angle;
    f32 startOffset;
    f32 endOffset;
    f32 startLength;
    f32 width;
    f32 targetWidth;
    f32 speed;
    i32 startTime;
    i32 hitboxStartTime;
    i32 duration;
    i32 endTime;
    i32 hitboxEndTime;
    i32 inUse;
    ZunTimer timer;
    u16 flags;
    i16 color;
    u8 state;
    u8 hideWarning;
    // pad 2
};

struct Bullet
{
    BulletCommand *AddCommand(i32 command, i32 flag, u32 type);
    void AddAngleAccelCommand(i32 command, i32 flag, i32 duration, f32 angle, f32 speed);
    void AddTargetVelocityCommand(i32 command, i32 flag, i32 duration, f32 speed, f32 angle);
    void RunCommands();

    void UpdateBulletBurstSpeed();
    void UpdateBulletTargetVelocity();
    void UpdateBulletTargetAngle();
    void UpdateBulletDirChangeAndResume();
    void UpdateBulletDirChangeAbsoluteAndResume();
    void UpdateBulletDirChangeAimAtPlayer();
    void UpdateBulletBounce();

    void Draw();
    AnmVm *SpawnEffectVm(u16 spawnState);
    void AssignTypeSprites(const BulletTypeSprites &source);

    void Initialize()
    {
        this->state = BULLET_INACTIVE;
        this->timer1 = 0;
        this->timer2 = 0;
    }

    void ClearCommand(i32 idx)
    {
        this->commands[idx].type = 0;
    }

#if defined(TH07_PSP_1000)
    Psp1000BulletSprites sprites;
#else
    BulletTypeSprites sprites;
#endif
    ZunVec3 pos;
    ZunVec3 velocity;
    ZunVec3 unused_ba4;
    f32 speed;
    f32 acceleration;
    f32 angularVelocity;
    f32 angle;
    f32 unused_bc0;
    f32 unused_bc4;
    ZunTimer timer1;
    ZunTimer timer2;
    i32 unused_be0[4];
    i32 spawnDelay;
    u16 exFlags;
    u16 moreFlags;
    i16 spriteOffset;
    i16 unused_bfa;
    u16 state;
    u16 outOfBoundsTime;
    u8 spawned;
    u8 grazed;
    // pad 2
    Bullet *next;
    i32 state2;
    i32 soundIdx;
    i32 curCmdIdx;
    BulletCommand commands[5];
    BulletCommandState commandStates[5];
#if defined(TH07_PSP)
    // Auto-rotating bullets normally keep the same heading for many frames.
    // Cache the render sin/cos so the PSP does not run one VFPU trig pair per
    // visible bullet on every draw.  Gameplay continues to use angle exactly
    // as before; these values are render-only and are refreshed on a change.
    f32 pspRenderSourceAngle;
    f32 pspRenderAngle;
    f32 pspRenderSin;
    f32 pspRenderCos;
    u32 pspRenderRotationValid;
#endif
};

#if defined(TH07_PSP_1000)
static_assert(sizeof(Bullet) == 2276,
              "PSP-1000 Bullet growth requires re-auditing the stage pool arena");
#endif

struct BulletManager
{
    // Replay state depends on the original 1,024 slot IDs, including the
    // next-slot cursor and reverse update order. PSP-1000 compacts each
    // payload but preserves the complete logical and physical slot space.
    static constexpr i32 kBulletCapacity = 1024;

    BulletManager();

    static ZunResult RegisterChain(const char *etamaAnmPath);
    static void CutChain();

    static ZunResult AddedCallback(BulletManager *arg);
    static ZunResult DeletedCallback(BulletManager *arg);
    static u32 OnUpdate(BulletManager *arg);
    static u32 OnDraw(BulletManager *arg);

    void Initialize();

    i32 DespawnBullets(i32 param_1, i32 turnIntoItem);
    void RemoveAllBullets(i32 param_1);
    void RemoveBulletsInRadius(ZunVec3 *centerPos, f32 radius);
    static void SetActiveSpriteByResolution(AnmVm *sprite, AnmVm *bulletTypeTemplate,
                                            Bullet *bullet, i32 spriteOffset);
    i32 SpawnBulletPattern(struct EnemyBulletShooter *bulletProps);
    Laser *SpawnLaserPattern(struct EnemyLaserShooter *laserProps);
    i32 SpawnSingleBullet(EnemyBulletShooter *bulletProps, i32 x, i32 y, f32 angle);
    void StopBulletMovement();

    BulletTypeSprites bulletTypeTemplates[16];
#if defined(TH07_PSP_1000)
    // The compact PSP-1000 runtime payload keeps only the one active spawn VM,
    // allowing all 1,024 original logical slots to have stable physical
    // storage. Allocate it in small chunks because a multi-megabyte contiguous
    // block is unavailable after several ANM archives have loaded.
    static constexpr i32 kBulletChunkCapacity = 64;
    static constexpr i32 kBulletChunkCount =
        (kBulletCapacity + kBulletChunkCapacity - 1) / kBulletChunkCapacity;
    Bullet *bulletChunks[kBulletChunkCount];
    bool PspEnsureBulletPool();
    void PspReleaseBulletPool();
#else
    Bullet bullets[kBulletCapacity + 1];
#endif

    Bullet *BulletAt(i32 index)
    {
#if defined(TH07_PSP_1000)
        return bulletChunks[index / kBulletChunkCapacity] + index % kBulletChunkCapacity;
#else
        return &bullets[index];
#endif
    }
    Laser lasers[64];
    i32 bulletCount;
    i32 screenClearTime;
    ZunTimer time;
    i32 updateCount;
    const char *etamaAnmPath;
    Bullet *bulletsPtrs[6];
#if defined(TH07_PSP_1000)
    i32 pspNextBulletIndex;
#else
    Bullet *bulletsStart;
#endif
    ItemType itemType;
#if defined(TH07_PSP)
    // Even the compact PSP-1000 Bullet payload is large. Avoid reading state
    // from every empty slot and evicting active bullets from Allegrex's small
    // cache; keep the original update order but consult this occupancy map
    // first.
    u32 pspActiveBulletBits[(kBulletCapacity + 31) / 32];

    bool PspIsBulletSlotTracked(i32 index) const
    {
        return (pspActiveBulletBits[index >> 5] & (1u << (index & 31))) != 0;
    }

    void PspTrackBulletSlot(i32 index)
    {
        pspActiveBulletBits[index >> 5] |= 1u << (index & 31);
    }

    void PspForgetBulletSlot(i32 index)
    {
        pspActiveBulletBits[index >> 5] &= ~(1u << (index & 31));
    }
#endif
};

extern BulletManager g_BulletManager;

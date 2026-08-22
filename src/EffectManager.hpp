#pragma once

#include "AnmVm.hpp"
#include "ZunMath.hpp"
#include "ZunResult.hpp"

typedef i32 (*EffectCallback)(struct Effect *);

struct Effect
{
    AnmVm vm;
    ZunVec3 pos1;
    ZunVec3 custom;
    ZunVec3 velocity;
    ZunVec3 acceleration;
    ZunVec3 basePosition;
    ZunVec3 emitterPosition;
    ZunVec3 direction;
    ZunQuaternion rotationQuat;
    f32 radius;
    f32 angularVelocity;
    ZunTimer timer;
    i32 unused_2c4;
    EffectCallback callback;
    i8 inUseFlag;
    i8 effectId;
    u8 isFadingOut;
    i8 fadeOutTime;
    i8 is2D;
    // pad 3
    Effect *next;
};

struct EffectTypeInfo
{
    i32 anmId;
    EffectCallback updateCallback;
    EffectCallback initCallback;
};

struct EffectManager
{
    // Effect slot reuse and init-callback RNG consumption are replay-visible.
    // Keep the original 400 normal slots on PSP-1000 as well.
    static constexpr i32 kNormalEffectCapacity = 400;
    static constexpr i32 kSpecialEffectCapacity = 8;
    static constexpr i32 kEffectCapacity = kNormalEffectCapacity + kSpecialEffectCapacity;

    EffectManager();
    void Reset();
#if defined(TH07_PSP_1000)
    bool PspEnsureEffectPool();
    void PspReleaseEffectPool();
#endif

    static ZunResult RegisterChain();
    static void CutChain();

    static ZunResult AddedCallback(EffectManager *arg);
    static ZunResult DeletedCallback(EffectManager *arg);
    static u32 OnUpdate(EffectManager *arg);
    static u32 OnDraw(EffectManager *arg);

    static i32 UpdatePhysics(Effect *effect);
    static i32 UpdateOrbitEffect(Effect *effect);
    static i32 UpdateGather60Frames(Effect *effect);
    static i32 UpdateGather240Frames(Effect *effect);
    static i32 UpdateBurstEaseOut30Frames(Effect *effect);
    static i32 UpdateAttachToCamera(Effect *effect);
    static i32 UpdateAttachToPlayer(Effect *effect);
    static i32 UpdateWeatherPhysics(Effect *effect);
    static i32 UpdateBurst30Frames(Effect *effect);

    static i32 InitDeceleratingBurst(Effect *effect);
    static i32 InitDeceleratingBurstFast(Effect *effect);
    static i32 Init2dEffect(Effect *effect);
    static i32 InitRandomDir(Effect *effect);
    static i32 InitRandomDirWithSpeed(Effect *effect);
    static i32 InitWeatherForward(Effect *effect);
    static i32 InitWeatherVortex(Effect *effect);
    static i32 InitWeatherBackward(Effect *effect);
    static i32 InitWeatherSlow(Effect *effect);
    static i32 InitWeatherFalling(Effect *effect);

    static void DoSomethingWithEffects(ZunVec3 *param_1);
    static void ModifyEffect1eAcceleration();
    static i32 UpdateNoOp(Effect *effect);

    Effect *SpawnParticles(i32 effectId, ZunVec3 *pos, i32 numParticles, u32 color);
    Effect *SpawnEffect(i32 effectId, ZunVec3 *pos, i32 param_3, i32 param_4, u32 color);
    Effect *SpawnMovingParticles(i32 effectId, ZunVec3 *pos, ZunVec3 *velocity, i32 numParticles,
                                 u32 color);
    i32 UpdateSpecialEffect();

    i32 nextIndex;
    i32 activeEffects;
    i32 activeEffectsCount;
    f32 globalColorMultiplierR;
    f32 globalColorMultiplierG;
    f32 globalColorMultiplierB;
    f32 globalColorMultiplierA;
#if defined(TH07_PSP_1000)
    Effect *effects;
#else
    Effect effects[kEffectCapacity + 1];
#endif
    Effect layer0;
    Effect layer1;
    Effect layer2;
    Effect layer3;
    Effect *layerPtrs[4];
    i32 frameCounter;
#if defined(TH07_PSP)
    // Avoid touching every AnmVm-heavy effect slot just to read its
    // in-use byte. External owners may retire an effect directly, so spawn
    // and update paths also repair stale bits when encountered.
    u32 pspActiveEffectBits[(kEffectCapacity + 31) / 32];

    bool PspIsEffectSlotTracked(i32 index) const
    {
        return (pspActiveEffectBits[index >> 5] & (1u << (index & 31))) != 0;
    }

    void PspTrackEffectSlot(i32 index)
    {
        pspActiveEffectBits[index >> 5] |= 1u << (index & 31);
    }

    void PspForgetEffectSlot(i32 index)
    {
        pspActiveEffectBits[index >> 5] &= ~(1u << (index & 31));
    }
#endif
};

#if defined(TH07_PSP_1000)
static_assert(sizeof(Effect) == 728,
              "PSP-1000 Effect growth requires re-auditing the stage pool arena");
#endif

extern EffectManager g_EffectManager;

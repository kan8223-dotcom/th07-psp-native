#include "EffectManager.hpp"

#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
#include "../psp/audio_me.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <pspmath.h>
#endif

#include "AnmIdx.hpp"
#include "AnmManager.hpp"
#include "GameManager.hpp"
#include "Player.hpp"
#include "Rng.hpp"
#include "Stage.hpp"
#include "ZunMath.hpp"
#include "ZunResult.hpp"
#include "utils.hpp"

#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
static_assert(sizeof(Effect) == 728u,
              "I-ME8 Effect traversal ABI changed");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(__builtin_offsetof(Effect, vm) == 0u &&
                  __builtin_offsetof(Effect, inUseFlag) == 716u &&
                  __builtin_offsetof(Effect, is2D) == 720u &&
                  __builtin_offsetof(Effect, next) == 724u,
              "I-ME8 Effect field offsets changed");
static_assert(__builtin_offsetof(AnmVm, pos.x) == 456u &&
                  __builtin_offsetof(AnmVm, pos.y) == 460u &&
                  __builtin_offsetof(AnmVm, pos.z) == 464u,
              "I-ME8 Effect VM position offsets changed");
#pragma GCC diagnostic pop
#endif

#if defined(TH07_PSP_1000)
#include "../psp/fileio.hpp"
#include "../psp/psp1000_arena.hpp"

#include <cstdlib>
#endif

EffectTypeInfo g_EffectMapping[34] = {
    {0x2ab, NULL, NULL},
    {0x2ac, NULL, NULL},
    {0x2ad, NULL, NULL},
    {0x2ae, EffectManager::UpdatePhysics, EffectManager::InitDeceleratingBurst},
    {0x2b3, EffectManager::UpdatePhysics, EffectManager::InitDeceleratingBurstFast},
    {0x2b4, EffectManager::UpdatePhysics, EffectManager::InitDeceleratingBurstFast},
    {0x2b5, EffectManager::UpdatePhysics, EffectManager::InitDeceleratingBurstFast},
    {0x2b6, EffectManager::UpdatePhysics, EffectManager::InitDeceleratingBurstFast},
    {0x2b7, EffectManager::UpdatePhysics, EffectManager::InitDeceleratingBurstFast},
    {0x2b8, EffectManager::UpdatePhysics, EffectManager::InitDeceleratingBurstFast},
    {0x2b9, EffectManager::UpdatePhysics, EffectManager::InitDeceleratingBurstFast},
    {0x2ba, EffectManager::UpdatePhysics, EffectManager::InitDeceleratingBurstFast},
    {0x2bb, NULL, NULL},
    {0x2bc, EffectManager::UpdateOrbitEffect, EffectManager::Init2dEffect},
    {0x2bc, EffectManager::UpdateOrbitEffect, EffectManager::Init2dEffect},
    {0x2bc, EffectManager::UpdateOrbitEffect, EffectManager::Init2dEffect},
    {0x2dc, NULL, NULL},
    {0x2af, EffectManager::UpdateGather60Frames, EffectManager::InitRandomDir},
    {0x2b0, EffectManager::UpdateGather240Frames, EffectManager::InitRandomDir},
    {0x2bd, EffectManager::UpdateNoOp, NULL},
    {0x2bf, EffectManager::UpdateWeatherPhysics, EffectManager::InitWeatherForward},
    {0x2c3, NULL, NULL},
    {0x2c0, EffectManager::UpdateBurstEaseOut30Frames, EffectManager::InitRandomDirWithSpeed},
    {0x304, EffectManager::UpdateAttachToCamera, NULL},
    {0x2c2, EffectManager::UpdateAttachToPlayer, NULL},
    {0x2da, EffectManager::UpdateNoOp, NULL},
    {0x2bf, EffectManager::UpdateWeatherPhysics, EffectManager::InitWeatherVortex},
    {0x2bf, EffectManager::UpdateWeatherPhysics, EffectManager::InitWeatherBackward},
    {0x2db, EffectManager::UpdateNoOp, NULL},
    {0x2b2, EffectManager::UpdateBurst30Frames, EffectManager::InitRandomDir},
    {0x2bf, EffectManager::UpdateWeatherPhysics, EffectManager::InitWeatherSlow},
    {0x2bf, EffectManager::UpdateWeatherPhysics, EffectManager::InitWeatherFalling},
    {0x2c1, EffectManager::UpdateBurstEaseOut30Frames, EffectManager::InitRandomDirWithSpeed},
    {0x2b1, EffectManager::UpdateGather60Frames, EffectManager::InitRandomDir},
};

EffectManager g_EffectManager;

ChainElem g_EffectManagerCalcChain;

ChainElem g_EffectManagerDrawChain;

EffectManager::EffectManager()
{
#if defined(TH07_PSP_1000)
    this->effects = nullptr;
#endif
    Reset();
    this->globalColorMultiplierR = 1.0f;
    this->globalColorMultiplierG = 1.0f;
    this->globalColorMultiplierB = 1.0f;
    this->globalColorMultiplierA = 1.0f;
}

void EffectManager::Reset()
{
#if defined(TH07_PSP_1000)
    Effect *pool = this->effects;
#endif
    memset(this, 0, sizeof(EffectManager));
#if defined(TH07_PSP_1000)
    this->effects = pool;
    if (pool)
    {
        memset(pool, 0, sizeof(Effect) * (kEffectCapacity + 1));
    }
#endif
}

#if defined(TH07_PSP_1000)
bool EffectManager::PspEnsureEffectPool()
{
    if (!this->effects)
    {
        this->effects = static_cast<Effect *>(th07_psp_1000_alloc_pool(
            sizeof(Effect) * static_cast<size_t>(kEffectCapacity + 1)));
        if (this->effects)
        {
            memset(this->effects, 0, sizeof(Effect) * static_cast<size_t>(kEffectCapacity + 1));
        }
    }
    if (!this->effects)
    {
        th07_psp_boot_note("PSP1000 effect pool allocation failed");
        return false;
    }
    th07_psp_boot_notef("PSP1000 effect pool %d+%d slots %uK", kNormalEffectCapacity,
                        kSpecialEffectCapacity,
                        static_cast<unsigned int>(sizeof(Effect) * kEffectCapacity / 1024u));
    return true;
}

void EffectManager::PspReleaseEffectPool()
{
    this->effects = nullptr;
    memset(this->pspActiveEffectBits, 0, sizeof(this->pspActiveEffectBits));
}
#endif

i32 EffectManager::InitDeceleratingBurstFast(Effect *effect)
{
    effect->velocity.x = (g_Rng.GetRandomFloatInRange(256.0f) - 128.0f) / 12.0f;
    effect->velocity.y = (g_Rng.GetRandomFloatInRange(256.0f) - 128.0f) / 12.0f;
    effect->velocity.z = 0.0f;
    effect->acceleration = -effect->velocity / 19.0f;
    effect->velocity *= g_Supervisor.effectiveFramerateMultiplier;
    effect->acceleration *= g_Supervisor.effectiveFramerateMultiplier;
    return 0;
}

i32 EffectManager::UpdatePhysics(Effect *effect)
{
    effect->pos1 += effect->velocity;
    effect->velocity += effect->acceleration;
    return 1;
}

i32 EffectManager::InitDeceleratingBurst(Effect *effect)
{
    effect->velocity.x = (g_Rng.GetRandomFloatInRange(256.0f) - 128.0f) * 4.0f / 33.0f;
    effect->velocity.y = (g_Rng.GetRandomFloatInRange(256.0f) - 128.0f) * 4.0f / 33.0f;
    effect->velocity.z = 0.0f;
    effect->acceleration = -effect->velocity / 20.0f;
    effect->velocity *= g_Supervisor.effectiveFramerateMultiplier;
    effect->acceleration *= g_Supervisor.effectiveFramerateMultiplier;
    return 0;
}

i32 EffectManager::Init2dEffect(Effect *effect)
{
    effect->is2D = 2;
    return 0;
}

i32 EffectManager::UpdateOrbitEffect(Effect *effect)
{
    f32 fadeOutRatio;
    ZunVec3 local_64;
    f32 cosAngle;
    ZunMatrix local_50;
    f32 sinAngle;
    ZunVec3 local_10;

    local_64.Normalize(&effect->direction);
    sinAngle = sinf(effect->angularVelocity);
    cosAngle = cosf(effect->angularVelocity);

    effect->rotationQuat.x = local_64.x * sinAngle;
    effect->rotationQuat.y = local_64.y * sinAngle;
    effect->rotationQuat.z = local_64.z * sinAngle;
    effect->rotationQuat.w = cosAngle;

    local_50.RotationQuaternion(&effect->rotationQuat);

    local_10.x = local_64.y * 1.0f - local_64.z * 0.0f;
    local_10.y = local_64.z * 0.0f - local_64.x * 1.0f;
    local_10.z = local_64.x * 0.0f - local_64.y * 0.0f;

    if (local_10.LengthSq() < 0.00001f)
    {
        local_64 = ZunVec3(1.0f, 0.0f, 0.0f);
    }
    else
    {
        local_10.Normalize(&local_10);
    }

    local_10 *= effect->radius;
    local_10.TransformCoord(&local_10, &local_50);
    local_10.z *= 6.0f;

    effect->pos1 = local_10 + effect->emitterPosition;

    if ((char)effect->isFadingOut)
    {
        effect->fadeOutTime++;
        if (effect->fadeOutTime >= 16)
        {
            return 0;
        }
        fadeOutRatio = 1.0f - (f32)effect->fadeOutTime / 16.0f;
        effect->vm.color.color = (effect->vm.color.color & 0xffffff) | (u32)(fadeOutRatio * 255.0f)
                                                                           << 24;
        effect->vm.scale.y = 2.0f - fadeOutRatio;
        effect->vm.scale.x = effect->vm.scale.y;
    }
    return 1;
}

i32 EffectManager::InitRandomDir(Effect *effect)
{
    f32 fVar1;

    effect->emitterPosition = effect->pos1;
    effect->emitterPosition.z = 0.0f;
    fVar1 = g_Rng.GetRandomFloatInRange(ZUN_2PI) - ZUN_PI;
    effect->direction.x = cosf(fVar1);
    effect->direction.y = sinf(fVar1);
    effect->direction.z = 0.0f;
    return 0;
}

i32 EffectManager::UpdateGather60Frames(Effect *effect)
{
    f32 distance = 256.0f - effect->timer.AsFloat() * 256.0f / 60.0f;
    effect->pos1 = effect->direction * distance + effect->emitterPosition;
    effect->pos1.z = 0.0f;
    return 1;
}

i32 EffectManager::UpdateAttachToPlayer(Effect *effect)
{
    if ((i32)!effect->vm.currentInstruction)
    {
        return false;
    }

    effect->pos1 = g_Player.positionCenter;
    return true;
}

i32 EffectManager::UpdateGather240Frames(Effect *effect)
{
    f32 distance = 256.0f - effect->timer.AsFloat() * 256.0f / 240.0f;
    effect->pos1 = effect->direction * distance + effect->emitterPosition;
    return 1;
}

i32 EffectManager::UpdateBurst30Frames(Effect *effect)
{
    f32 distance = effect->timer.AsFloat() * 256.0f / 30.0f;
    effect->pos1 = effect->direction * distance + effect->emitterPosition;
    return 1;
}

void EffectManager::DoSomethingWithEffects(ZunVec3 *param_1)
{
    i32 i;
    Effect *effect;

    effect = g_EffectManager.effects;
    for (i = 0; i < kNormalEffectCapacity; i++, effect++)
    {
        if (effect->effectId == 20 || effect->effectId == 31)
        {
            effect->basePosition += *param_1;
        }
    }
}

void EffectManager::ModifyEffect1eAcceleration()
{
    i32 i;
    Effect *effect;

    effect = g_EffectManager.effects;
    for (i = 0; i < kNormalEffectCapacity; i++, effect++)
    {
        if (effect->effectId == 30)
        {
            effect->acceleration.z = -0.01f;
        }
    }
}

i32 EffectManager::UpdateWeatherPhysics(Effect *effect)
{
    ZunVec3 local_10;

    effect->velocity += effect->acceleration;
    effect->basePosition += effect->velocity;
    effect->pos1 = effect->basePosition;

    local_10 = effect->pos1 - g_Stage.cam.pos;
    local_10.Normalize(&local_10);
    f32 dot = g_Stage.cam.lookAtDir.Dot(&local_10);
    if (dot < 0.94f)
    {
        return 0;
    }

    effect->vm.SetRotationZ(utils::AddNormalizeAngle(effect->vm.rotation.z, effect->vm.rotation.x));
    effect->vm.updateRotation = 1;
    if (effect->pos1.z >= 0.0f)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

i32 EffectManager::InitWeatherForward(Effect *effect)
{
    i32 chance;
    ZunVec3 camLookAtInv;

    camLookAtInv = -g_Stage.cam.lookAt;

    effect->basePosition = g_Stage.cam.lookAt + g_Stage.cam.pos;
    effect->basePosition.x += g_Rng.GetRandomFloatInRange(120.0f) - 60.0f + camLookAtInv.x / 2.0f;
    effect->basePosition.y += g_Rng.GetRandomFloatInRange(200.0f) - 100.0f + camLookAtInv.y / 2.0f;
    effect->basePosition.z += g_Rng.GetRandomFloatInRange(100.0f) - 100.0f + camLookAtInv.z / 2.0f;
    effect->velocity.x = g_Rng.GetRandomFloatInRange(0.06f) - 0.03f + effect->custom.x;
    effect->velocity.y = g_Rng.GetRandomFloatInRange(0.06f) - 0.03f + effect->custom.y;
    effect->velocity.z = g_Rng.GetRandomFloatInRange(0.1f) + 0.03f + effect->custom.z;
    effect->acceleration.x = g_Rng.GetRandomFloatInRange(0.0002f) - 0.0001f;
    effect->acceleration.y = g_Rng.GetRandomFloatInRange(0.0002f) - 0.0001f;
    effect->velocity = effect->velocity * g_Supervisor.effectiveFramerateMultiplier;
    effect->acceleration = effect->acceleration * g_Supervisor.effectiveFramerateMultiplier;
    effect->is2D = 1;
    effect->vm.rotation.z = g_Rng.GetRandomFloatInRange(ZUN_2PI) - ZUN_PI;
    effect->vm.rotation.x = g_Rng.GetRandomFloatInRange(0.03141593f) - 0.015707964f;

    chance = g_GameManager.cherry - g_GameManager.globals->cherryStart;
    chance = chance * 100 / g_GameManager.cherryMax;

    if ((u32)chance >= g_Rng.GetRandomU32InRange(100))
    {
        g_AnmManager->SetActiveSprite(&effect->vm, 728);
        effect->vm.color.bytes.r = 255;
        effect->vm.color.bytes.g = 255;
        effect->vm.color.bytes.b = 255;
    }
    return 0;
}

i32 EffectManager::InitWeatherVortex(Effect *effect)
{
    i32 chance;

    effect->basePosition.x = g_Rng.GetRandomFloatInRange(160.0f) - 80.0f;
    effect->basePosition.y = g_Rng.GetRandomFloatInRange(160.0f) - 80.0f;
    effect->basePosition.z = g_Rng.GetRandomFloatInRange(100.0f) - 50.0f;
    effect->velocity.x = -effect->basePosition.y / effect->custom.x;
    effect->velocity.y = effect->basePosition.x / effect->custom.x;
    effect->velocity.z = g_Rng.GetRandomFloatInRange(0.1f) + 0.09f;
    effect->basePosition += g_Stage.cam.lookAt / 2.0f + g_Stage.cam.pos;
    effect->velocity = effect->velocity * g_Supervisor.effectiveFramerateMultiplier;
    effect->is2D = 1;
    effect->vm.rotation.z = g_Rng.GetRandomFloatInRange(ZUN_2PI) - ZUN_PI;
    effect->vm.rotation.x = g_Rng.GetRandomFloatInRange(0.06283186f) - 0.03141593f;

    chance = g_GameManager.cherry - g_GameManager.globals->cherryStart;
    chance = chance * 100 / g_GameManager.cherryMax;

    if ((u32)chance >= g_Rng.GetRandomU32InRange(100))
    {
        g_AnmManager->SetActiveSprite(&effect->vm, 728);
        effect->vm.color.bytes.r = 255;
        effect->vm.color.bytes.g = 255;
        effect->vm.color.bytes.b = 255;
    }
    effect->acceleration.x = 0.0f;
    effect->acceleration.y = 0.0f;
    effect->acceleration.z = 0.0f;
    return 0;
}

i32 EffectManager::InitWeatherBackward(Effect *effect)
{
    effect->basePosition.x = g_Rng.GetRandomFloatInRange(160.0f) - 80.0f;
    effect->basePosition.y = g_Rng.GetRandomFloatInRange(160.0f) - 80.0f;
    effect->basePosition.z = g_Rng.GetRandomFloatInRange(100.0f) - 50.0f;
    effect->velocity.x = -effect->basePosition.y / effect->custom.x;
    effect->velocity.y = effect->basePosition.x / effect->custom.x;
    effect->velocity.z = -g_Rng.GetRandomFloatInRange(0.2f) - 0.06f;
    effect->basePosition += g_Stage.cam.lookAt / 2.0f + g_Stage.cam.pos;
    effect->velocity = effect->velocity * g_Supervisor.effectiveFramerateMultiplier;
    effect->is2D = 1;
    effect->vm.rotation.z = g_Rng.GetRandomFloatInRange(ZUN_2PI) - ZUN_PI;
    effect->vm.rotation.x = g_Rng.GetRandomFloatInRange(0.06283186f) - 0.03141593f;
    g_AnmManager->SetActiveSprite(&effect->vm, 728);
    effect->vm.color.bytes.r = 255;
    effect->vm.color.bytes.g = 255;
    effect->vm.color.bytes.b = 255;
    effect->acceleration.x = 0.0f;
    effect->acceleration.y = 0.0f;
    effect->acceleration.z = 0.0f;
    return 0;
}

i32 EffectManager::InitWeatherSlow(Effect *effect)
{
    effect->basePosition.x = g_Rng.GetRandomFloatInRange(160.0f) - 80.0f;
    effect->basePosition.y = g_Rng.GetRandomFloatInRange(160.0f) - 80.0f;
    effect->basePosition.z = g_Rng.GetRandomFloatInRange(100.0f) - 100.0f;
    effect->velocity.x = g_Rng.GetRandomFloatInRange(0.06f) - 0.03f + effect->custom.x;
    effect->velocity.y = g_Rng.GetRandomFloatInRange(0.06f) - 0.03f + effect->custom.y;
    effect->velocity.z = g_Rng.GetRandomFloatInRange(0.02f) + 0.01f + effect->custom.z;
    effect->basePosition += g_Stage.cam.lookAt / 2.0f + g_Stage.cam.pos;
    effect->is2D = 1;
    effect->vm.rotation.z = g_Rng.GetRandomFloatInRange(ZUN_2PI) - ZUN_PI;
    effect->vm.rotation.x = g_Rng.GetRandomFloatInRange(0.06283186f) - 0.03141593f;
    g_AnmManager->SetActiveSprite(&effect->vm, 728);
    effect->vm.color.bytes.r = 255;
    effect->vm.color.bytes.g = 255;
    effect->vm.color.bytes.b = 255;
    effect->acceleration.x = 0.0f;
    effect->acceleration.y = 0.0f;
    effect->acceleration.z = 0.0f;
    return 0;
}

i32 EffectManager::InitWeatherFalling(Effect *effect)
{
    effect->basePosition.x = g_Rng.GetRandomFloatInRange(160.0f) - 80.0f;
    effect->basePosition.y = g_Rng.GetRandomFloatInRange(160.0f) - 80.0f;
    effect->basePosition.z = g_Rng.GetRandomFloatInRange(200.0f) - 0.0f;
    effect->velocity.x = g_Rng.GetRandomFloatInRange(0.06f) - 0.03f + effect->custom.x;
    effect->velocity.y = g_Rng.GetRandomFloatInRange(0.06f) - 0.03f + effect->custom.y;
    effect->velocity.z = -g_Rng.GetRandomFloatInRange(0.1f) + effect->custom.z;
    effect->basePosition += g_Stage.cam.lookAt / 2.0f + g_Stage.cam.pos;
    effect->velocity = effect->velocity * g_Supervisor.effectiveFramerateMultiplier;
    effect->is2D = 1;
    effect->vm.rotation.z = g_Rng.GetRandomFloatInRange(ZUN_2PI) - ZUN_PI;
    effect->vm.rotation.x = g_Rng.GetRandomFloatInRange(0.06283186f) - 0.03141593f;
    g_AnmManager->SetActiveSprite(&effect->vm, 728);
    effect->vm.angleVel.z *= 2;
    effect->vm.color.bytes.r = 255;
    effect->vm.color.bytes.g = 255;
    effect->vm.color.bytes.b = 255;
    effect->acceleration.x = 0.0f;
    effect->acceleration.y = 0.0f;
    effect->acceleration.z = -0.015f;
    return 0;
}

i32 EffectManager::InitRandomDirWithSpeed(Effect *effect)
{
    f32 local_8;

    // double intentionally used here, strangely
    if (effect->custom.x > -990.0)
    {
        local_8 = utils::AddNormalizeAngle(effect->custom.x, 0.0f);
    }
    else
    {
        local_8 = g_Rng.GetRandomFloatInRange(ZUN_2PI) - ZUN_PI;
    }
    effect->emitterPosition = effect->pos1;
    effect->emitterPosition.z = 0.0f;
    effect->direction.x = cosf(local_8);
    effect->direction.y = sinf(local_8);
    effect->direction.z = 0.0f;
    effect->direction *= g_Rng.GetRandomFloatInRange(1.5f) + 1.0f;
    return 0;
}

i32 EffectManager::UpdateBurstEaseOut30Frames(Effect *effect)
{
    f32 fVar1;

    fVar1 = effect->timer.AsFloat() / 90.0f;
    fVar1 = 1.0f - (1.0f - fVar1) * (1.0f - fVar1);
    effect->pos1 = fVar1 * effect->direction * 128.0f + effect->emitterPosition;
    effect->pos1.z = 0.0f;
    return 1;
}

i32 EffectManager::UpdateAttachToCamera(Effect *effect)
{
    effect->is2D = 1;
    effect->basePosition = g_Stage.cam.lookAt + g_Stage.cam.pos;
    effect->pos1 = effect->basePosition;
    effect->pos1.z = 0.0f;
    effect->is2D = 3;
    return 1;
}

i32 EffectManager::UpdateNoOp(Effect *effect)
{
    (void)effect;
    return 1;
}

Effect *EffectManager::SpawnParticles(i32 effectId, ZunVec3 *pos, i32 numParticles, u32 color)
{
    i32 i;
    Effect *effect;

    effect = &this->effects[this->nextIndex];
    for (i = 0; i < kNormalEffectCapacity; i++)
    {
        this->nextIndex++;
        if (this->nextIndex >= kNormalEffectCapacity)
        {
            this->nextIndex = 0;
        }
#if defined(TH07_PSP)
        const i32 effectIndex = static_cast<i32>(effect - this->effects);
        if (this->PspIsEffectSlotTracked(effectIndex) && effect->inUseFlag)
#else
        if (effect->inUseFlag)
#endif
        {
            if (this->nextIndex == 0)
            {
                effect = this->effects;
            }
            else
            {
                effect++;
            }
            continue;
        }
#if defined(TH07_PSP)
        this->PspForgetEffectSlot(effectIndex);
#endif

        effect->is2D = 0;
        effect->inUseFlag = 1;
#if defined(TH07_PSP)
        this->PspTrackEffectSlot(effectIndex);
#endif
        effect->effectId = (u8)effectId;
        effect->pos1 = *pos;
        g_AnmManager->SetAnmIdxAndExecuteScript(&effect->vm, g_EffectMapping[effectId].anmId);
        effect->vm.zWriteDisable = 1;
        effect->vm.color.color = color;
        effect->callback = g_EffectMapping[effectId].updateCallback;
        effect->timer = 0;
        effect->isFadingOut = 0;
        effect->fadeOutTime = 0;
        effect->custom = ZunVec3(0.0f, 0.0f, 0.0f);
        if (g_EffectMapping[effectId].initCallback &&
            g_EffectMapping[effectId].initCallback(effect))
        {
            effect->inUseFlag = 0;
#if defined(TH07_PSP)
            this->PspForgetEffectSlot(effectIndex);
#endif
        }
        numParticles--;
        if (numParticles == 0)
        {
            break;
        }
        if (this->nextIndex == 0)
        {
            effect = this->effects;
        }
        else
        {
            effect++;
        }
    }

    return i >= kNormalEffectCapacity ? &this->effects[kEffectCapacity] : effect;
}

Effect *EffectManager::SpawnMovingParticles(i32 effectId, ZunVec3 *pos, ZunVec3 *velocity,
                                            i32 numParticles, u32 color)
{
    i32 i;
    Effect *effect;

    effect = &this->effects[this->nextIndex];

    for (i = 0; i < kNormalEffectCapacity; i++)
    {
        this->nextIndex++;
        if (this->nextIndex >= kNormalEffectCapacity)
        {
            this->nextIndex = 0;
        }
#if defined(TH07_PSP)
        const i32 effectIndex = static_cast<i32>(effect - this->effects);
        if (this->PspIsEffectSlotTracked(effectIndex) && effect->inUseFlag)
#else
        if (effect->inUseFlag)
#endif
        {
            if (this->nextIndex == 0)
            {
                effect = this->effects;
            }
            else
            {
                effect++;
            }
            continue;
        }
#if defined(TH07_PSP)
        this->PspForgetEffectSlot(effectIndex);
#endif

        effect->is2D = 0;
        effect->inUseFlag = 1;
#if defined(TH07_PSP)
        this->PspTrackEffectSlot(effectIndex);
#endif
        effect->effectId = effectId;
        effect->pos1 = *pos;
        g_AnmManager->SetAnmIdxAndExecuteScript(&effect->vm, g_EffectMapping[effectId].anmId);
        effect->vm.color.color = color;
        effect->callback = g_EffectMapping[effectId].updateCallback;
        effect->timer = 0;
        effect->isFadingOut = 0;
        effect->fadeOutTime = 0;
        effect->custom = *velocity;
        if (g_EffectMapping[effectId].initCallback &&
            g_EffectMapping[effectId].initCallback(effect))
        {
            effect->inUseFlag = 0;
#if defined(TH07_PSP)
            this->PspForgetEffectSlot(effectIndex);
#endif
        }
        numParticles--;
        if (numParticles == 0)
        {
            break;
        }
        if (this->nextIndex == 0)
        {
            effect = this->effects;
        }
        else
        {
            effect++;
        }
    }

    return i >= kNormalEffectCapacity ? &this->effects[kEffectCapacity] : effect;
}

Effect *EffectManager::SpawnEffect(i32 effectId, ZunVec3 *pos, i32 param_3, i32 param_4,
                                   u32 color)
{
    (void)param_4;

    Effect *effect;

    effect = &this->effects[param_3 + kNormalEffectCapacity];
#if defined(TH07_PSP)
    const i32 effectIndex = param_3 + kNormalEffectCapacity;
#endif
    effect->is2D = 0;
    effect->inUseFlag = 1;
#if defined(TH07_PSP)
    this->PspTrackEffectSlot(effectIndex);
#endif
    effect->effectId = effectId;
    effect->pos1 = *pos;
    g_AnmManager->SetAnmIdxAndExecuteScript(&effect->vm, g_EffectMapping[effectId].anmId);
    effect->vm.zWriteDisable = 1;
    effect->vm.color.color = color;
    effect->callback = g_EffectMapping[effectId].updateCallback;
    effect->timer = 0;
    effect->isFadingOut = 0;
    effect->fadeOutTime = 0;
    effect->custom = ZunVec3(0.0f, 0.0f, 0.0f);
    if (g_EffectMapping[effectId].initCallback)
    {
        if (g_EffectMapping[effectId].initCallback(effect))
        {
            effect->inUseFlag = 0;
#if defined(TH07_PSP)
            this->PspForgetEffectSlot(effectIndex);
#endif
        }
    }
    return effect;
}

u32 EffectManager::OnUpdate(EffectManager *arg)
{
    i32 i;
    Effect *effect;

    effect = arg->effects;
    arg->activeEffectsCount = 0;
    arg->layerPtrs[0] = &arg->layer0;
    arg->layerPtrs[1] = &arg->layer1;
    arg->layerPtrs[2] = &arg->layer2;
    arg->layerPtrs[3] = &arg->layer3;
    arg->layer0.next = NULL;
    arg->layer1.next = NULL;
    arg->layer2.next = NULL;
    arg->layer3.next = NULL;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    arg->pspMeEffectListCounts[0] = 0u;
    arg->pspMeEffectListCounts[1] = 0u;
#if defined(TH07_PSP_ME_ADAPTIVE_AUX_RENDER)
    arg->pspMeEffectPreparedSerial = 0u;
    arg->pspMeEffectPreparedCounts[0] = 0u;
    arg->pspMeEffectPreparedCounts[1] = 0u;
#endif
#endif
    for (i = 0; i < kEffectCapacity; i++, effect++)
    {
#if defined(TH07_PSP)
        if (!arg->PspIsEffectSlotTracked(i))
        {
            continue;
        }
#endif
        if (!effect->inUseFlag)
        {
#if defined(TH07_PSP)
            arg->PspForgetEffectSlot(i);
#endif
            continue;
        }

        arg->activeEffectsCount++;
        if (effect->callback && effect->callback(effect) != 1)
        {
            effect->inUseFlag = 0;
#if defined(TH07_PSP)
            arg->PspForgetEffectSlot(i);
#endif
            continue;
        }

        if (g_AnmManager->ExecuteScript(&effect->vm))
        {
            effect->inUseFlag = 0;
#if defined(TH07_PSP)
            arg->PspForgetEffectSlot(i);
#endif
            continue;
        }

        effect->timer++;
        effect->next = NULL;
        if (effect->is2D == 1 || effect->is2D == 3)
        {
            arg->layerPtrs[1]->next = effect;
            arg->layerPtrs[1] = effect;
        }
        else if (!effect->is2D)
        {
            if (effect->vm.blendMode != 0)
            {
                arg->layerPtrs[3]->next = effect;
                arg->layerPtrs[3] = effect;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
                ++arg->pspMeEffectListCounts[1];
#endif
            }
            else
            {
                arg->layerPtrs[0]->next = effect;
                arg->layerPtrs[0] = effect;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
                ++arg->pspMeEffectListCounts[0];
#endif
            }
        }
        else
        {
            arg->layerPtrs[2]->next = effect;
            arg->layerPtrs[2] = effect;
        }
    }
    arg->frameCounter++;
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM) && \
    !defined(TH07_PSP_ME_ADAPTIVE_AUX_RENDER)
    // Legacy I-ME8 prepares every frame.  The adaptive profile defers this
    // full list walk until the deterministic ME budget admits Effect.
    (void)arg->PspPrepareMeEffectRenderStream();
#endif
    if (arg->frameCounter % 300 == 100 && g_GameManager.CheckGameIntegrity())
    {
        return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
    }
    else
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }
}

#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
namespace
{
inline u32 PspMeEffectPhysicalAddress(const void *pointer)
{
    return static_cast<u32>(reinterpret_cast<uintptr_t>(pointer) &
                            0x1fffffffu);
}

inline u32 PspMeEffectMemberOffset(const void *base, const void *member)
{
    return static_cast<u32>(reinterpret_cast<uintptr_t>(member) -
                            reinterpret_cast<uintptr_t>(base));
}

inline void PspMeEffectSinCos(f32 angle, f32 *outSin, f32 *outCos)
{
    // Deliberately identical to AnmManager::Draw's PSP trig gate. ME consumes
    // the result bits and never evaluates sin/cos independently.
    if (std::isfinite(angle) && angle >= -16.0f * ZUN_PI &&
        angle <= 16.0f * ZUN_PI)
    {
        vfpu_sincos(angle, outSin, outCos);
        return;
    }
    sincosf(outSin, outCos, angle);
}
}

bool EffectManager::PspPrepareMeEffectRenderStream()
{
    if (++this->pspMeEffectPrepareSerial == 0u)
    {
        ++this->pspMeEffectPrepareSerial;
    }
    this->pspMeEffectPreparedSerial = 0u;
    this->pspMeEffectPreparedCounts[0] = 0u;
    this->pspMeEffectPreparedCounts[1] = 0u;
    if (!g_AnmManager)
    {
        return false;
    }

    u32 seen[(kEffectCapacity + 31u) / 32u] = {};
    const uintptr_t poolBegin =
        reinterpret_cast<uintptr_t>(&this->effects[0]);
    const uintptr_t poolEnd = reinterpret_cast<uintptr_t>(
        &this->effects[kEffectCapacity]);
    const i32 layers[2] = {0, 3};
    for (u32 segment = 0u; segment < 2u; ++segment)
    {
        Effect *effect = layers[segment] == 0 ? this->layer0.next
                                              : this->layer3.next;
        Effect *last = nullptr;
        u32 count = 0u;
        while (effect)
        {
            const uintptr_t address = reinterpret_cast<uintptr_t>(effect);
            if (count >= static_cast<u32>(kEffectCapacity) ||
                address < poolBegin || address >= poolEnd ||
                ((address - poolBegin) % sizeof(Effect)) != 0u)
            {
                return false;
            }
            const u32 slot = static_cast<u32>(
                (address - poolBegin) / sizeof(Effect));
            const u32 bit = 1u << (slot & 31u);
            if ((seen[slot >> 5u] & bit) != 0u ||
                !this->PspIsEffectSlotTracked(static_cast<i32>(slot)) ||
                !effect->inUseFlag || effect->is2D != 0 ||
                this->pspMeEffectSlotGenerations[slot] == 0u ||
                (layers[segment] == 0
                     ? effect->vm.blendMode != 0
                     : effect->vm.blendMode == 0))
            {
                return false;
            }
            seen[slot >> 5u] |= bit;

            // These writes are observable after Draw in the original path.
            // Commit them before publication even when the VM is invisible.
            effect->vm.pos = effect->pos1;
            effect->vm.pos.x += g_GameManager.arcadeRegionTopLeftPos.x;
            effect->vm.pos.y += g_GameManager.arcadeRegionTopLeftPos.y;
            f32 sine = 0.0f;
            f32 cosine = 1.0f;
            if (effect->vm.rotation.z != 0.0f)
            {
                PspMeEffectSinCos(effect->vm.rotation.z, &sine, &cosine);
            }
            this->pspMeEffectRenderSin[slot] = sine;
            this->pspMeEffectRenderCos[slot] = cosine;

            ++count;
            last = effect;
            effect = effect->next;
        }
        Effect *expectedTail = this->layerPtrs[layers[segment]];
        if (count == 0u)
        {
            expectedTail = layers[segment] == 0 ? &this->layer0
                                                : &this->layer3;
        }
        if ((count != 0u && expectedTail != last) ||
            (last && last->next))
        {
            return false;
        }
        this->pspMeEffectPreparedCounts[segment] = count;
    }
    if (this->pspMeEffectPreparedCounts[0] +
            this->pspMeEffectPreparedCounts[1] >
        static_cast<u32>(kEffectCapacity))
    {
        return false;
    }
    this->pspMeEffectPreparedSerial = this->pspMeEffectPrepareSerial;
    return true;
}

bool EffectManager::PspBuildMeEffectRenderLayout(
    Th07PspMeRenderEffectLayout *layout, u32 itemRecordCount) const
{
    if (!layout || this->pspMeEffectPreparedSerial == 0u ||
        this->pspMeEffectPreparedSerial != this->pspMeEffectPrepareSerial ||
        itemRecordCount > TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS ||
        this->pspMeEffectPreparedCounts[0] >
            TH07_PSP_ME_RENDER_STREAM_EFFECT_MAX_RECORDS ||
        this->pspMeEffectPreparedCounts[1] >
            TH07_PSP_ME_RENDER_STREAM_EFFECT_MAX_RECORDS ||
        itemRecordCount + this->pspMeEffectPreparedCounts[0] +
                this->pspMeEffectPreparedCounts[1] >
            TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS)
    {
        return false;
    }

    const Effect *layoutEffect = &this->effects[0];
    const AnmVm *layoutVm = &layoutEffect->vm;
    std::memset(layout, 0, sizeof(*layout));
    layout->effectLayoutVersion = TH07_PSP_ME_RENDER_EFFECT_LAYOUT_VERSION;
    layout->effectLayoutBytes = sizeof(*layout);
    layout->effectBasePhys = PspMeEffectPhysicalAddress(&this->effects[0]);
    layout->effectStride = sizeof(Effect);
    layout->effectCount = kEffectCapacity;
    layout->generationBasePhys = PspMeEffectPhysicalAddress(
        &this->pspMeEffectSlotGenerations[0]);
    layout->generationStride = sizeof(this->pspMeEffectSlotGenerations[0]);
    layout->generationCount = kEffectCapacity;
    layout->activeBitsPhys = PspMeEffectPhysicalAddress(
        &this->pspActiveEffectBits[0]);
    layout->activeBitsWordCount =
        sizeof(this->pspActiveEffectBits) / sizeof(this->pspActiveEffectBits[0]);
    layout->sinBasePhys = PspMeEffectPhysicalAddress(
        &this->pspMeEffectRenderSin[0]);
    layout->sinStride = sizeof(this->pspMeEffectRenderSin[0]);
    layout->cosBasePhys = PspMeEffectPhysicalAddress(
        &this->pspMeEffectRenderCos[0]);
    layout->cosStride = sizeof(this->pspMeEffectRenderCos[0]);
    layout->layer0HeadPhys = PspMeEffectPhysicalAddress(this->layer0.next);
    layout->layer0TailPhys = this->pspMeEffectPreparedCounts[0]
        ? PspMeEffectPhysicalAddress(this->layerPtrs[0]) : 0u;
    layout->layer3HeadPhys = PspMeEffectPhysicalAddress(this->layer3.next);
    layout->layer3TailPhys = this->pspMeEffectPreparedCounts[1]
        ? PspMeEffectPhysicalAddress(this->layerPtrs[3]) : 0u;
    layout->effectNextOffset = PspMeEffectMemberOffset(
        layoutEffect, &layoutEffect->next);
    layout->effectInUseOffset = PspMeEffectMemberOffset(
        layoutEffect, &layoutEffect->inUseFlag);
    layout->effectIs2DOffset = PspMeEffectMemberOffset(
        layoutEffect, &layoutEffect->is2D);
    layout->effectVmOffset = PspMeEffectMemberOffset(
        layoutEffect, &layoutEffect->vm);
    layout->vmPosXOffset = PspMeEffectMemberOffset(layoutVm,
                                                   &layoutVm->pos.x);
    layout->vmPosYOffset = PspMeEffectMemberOffset(layoutVm,
                                                   &layoutVm->pos.y);
    layout->vmPosZOffset = PspMeEffectMemberOffset(layoutVm,
                                                   &layoutVm->pos.z);
    layout->prepareSerialPhys = PspMeEffectPhysicalAddress(
        &this->pspMeEffectPrepareSerial);
    layout->preparedSerialPhys = PspMeEffectPhysicalAddress(
        &this->pspMeEffectPreparedSerial);
    layout->preparedCountsPhys = PspMeEffectPhysicalAddress(
        &this->pspMeEffectPreparedCounts[0]);
    layout->expectedPrepareSerial = this->pspMeEffectPreparedSerial;
    layout->expectedLayer0Count = this->pspMeEffectPreparedCounts[0];
    layout->expectedLayer3Count = this->pspMeEffectPreparedCounts[1];
    return true;
}

bool EffectManager::PspMeEffectRenderAuthorityMatches(
    const Th07PspMeRenderEffectLayout *layout) const
{
    if (!layout)
    {
        return false;
    }
    // Rebuild all 32 words from the live manager, not only serial/count/list
    // endpoints. This binds READY to the exact pool, bitmap, generation,
    // trig and ABI-offset identity that ME traversed. BuildLayout is const and
    // has no renderer/VM side effect, so validation remains READY_SC-only.
    Th07PspMeRenderEffectLayout expected{};
    return this->PspBuildMeEffectRenderLayout(&expected, 0u) &&
           std::memcmp(layout, &expected, sizeof(expected)) == 0;
}

void EffectManager::PspDrawCanonicalEffectLayer(i32 layer)
{
    Effect *effect = nullptr;
    if (layer == 0)
    {
        effect = this->layer0.next;
    }
    else if (layer == 2)
    {
        effect = this->layer2.next;
    }
    else if (layer == 3)
    {
        effect = this->layer3.next;
    }
    else
    {
        return;
    }
    while (effect)
    {
        effect->vm.pos = effect->pos1;
        if (layer != 2)
        {
            effect->vm.pos.x += g_GameManager.arcadeRegionTopLeftPos.x;
            effect->vm.pos.y += g_GameManager.arcadeRegionTopLeftPos.y;
            g_AnmManager->DrawPspFastSprite(&effect->vm);
        }
        else
        {
            g_AnmManager->DrawBillboard(&effect->vm);
        }
        effect = effect->next;
    }
}

bool EffectManager::PspValidateMeEffectRenderStream(
    const Th07PspMeRenderStreamJob *job,
    const Th07PspMeRenderStreamReady *ready) const
{
    if (!job || !ready || !g_AnmManager ||
        ready->token.slot != job->token.slot ||
        ready->token.generation != job->token.generation ||
        ready->vertexBytes % sizeof(Th07PspMeRenderStreamVertex) != 0u ||
        (job->flags & TH07_PSP_ME_RENDER_STREAM_JOB_EFFECT_LIST) == 0u ||
        job->version != TH07_PSP_ME_RENDER_STREAM_EFFECT_VERSION ||
        !this->PspMeEffectRenderAuthorityMatches(&job->effectLayout) ||
        ready->effectResult != TH07_PSP_ME_RENDER_STREAM_RESULT_OK ||
        ready->effectLayer0RecordCount !=
            job->effectLayout.expectedLayer0Count ||
        ready->effectLayer3RecordCount !=
            job->effectLayout.expectedLayer3Count ||
        ready->itemRecordCount >
            TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS ||
        ready->effectLayer0RecordCount >
            TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS -
                ready->itemRecordCount ||
        ready->effectLayer3RecordCount >
            TH07_PSP_ME_RENDER_STREAM_ITEM_MAX_RECORDS -
                ready->itemRecordCount - ready->effectLayer0RecordCount ||
        ready->itemRunCount > ready->runCount ||
        ready->effectLayer0RunCount >
            ready->runCount - ready->itemRunCount ||
        ready->effectLayer3RunCount >
            ready->runCount - ready->itemRunCount -
                ready->effectLayer0RunCount ||
        ready->itemVertexCount >
            ready->vertexBytes / sizeof(Th07PspMeRenderStreamVertex) ||
        ready->effectLayer0VertexCount >
            ready->vertexBytes / sizeof(Th07PspMeRenderStreamVertex) -
                ready->itemVertexCount ||
        ready->effectLayer3VertexCount >
            ready->vertexBytes / sizeof(Th07PspMeRenderStreamVertex) -
                ready->itemVertexCount - ready->effectLayer0VertexCount)
    {
        return false;
    }

    const u32 layer0FirstRun = ready->itemRunCount;
    const u32 layer3FirstRun =
        layer0FirstRun + ready->effectLayer0RunCount;
    const u32 layer0FirstVertex = ready->itemVertexCount;
    const u32 layer3FirstVertex =
        layer0FirstVertex + ready->effectLayer0VertexCount;
    const u32 firstRuns[2] = {layer0FirstRun, layer3FirstRun};
    const u32 runCounts[2] = {ready->effectLayer0RunCount,
                              ready->effectLayer3RunCount};
    const u32 recordCounts[2] = {ready->effectLayer0RecordCount,
                                 ready->effectLayer3RecordCount};
    const u32 vertexCounts[2] = {ready->effectLayer0VertexCount,
                                 ready->effectLayer3VertexCount};
    const u32 firstVertices[2] = {layer0FirstVertex, layer3FirstVertex};
    const u32 logicalLayers[2] = {0u, 3u};
    const u32 allowedState = TH07_PSP_ME_RENDER_STREAM_RUN_BLEND_ADD |
                             TH07_PSP_ME_RENDER_STREAM_RUN_ZWRITE_DISABLE;
    for (u32 segment = 0u; segment < 2u; ++segment)
    {
        u32 recordsSeen = 0u;
        u32 verticesSeen = 0u;
        u32 previousRecordEnd = 0u;
        for (u32 index = 0u; index < runCounts[segment]; ++index)
        {
            const Th07PspMeRenderStreamRun &run =
                ready->runs[firstRuns[segment] + index];
            const u32 verticesPerRecord =
                run.primitive == TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_SPRITES
                    ? 2u
                    : run.primitive ==
                              TH07_PSP_ME_RENDER_STREAM_PRIMITIVE_QUADS
                          ? 4u : 0u;
            if (verticesPerRecord == 0u || run.recordCount == 0u ||
                run.firstRecord >= recordCounts[segment] ||
                run.firstRecord < previousRecordEnd ||
                run.recordCount >
                    recordCounts[segment] - run.firstRecord ||
                run.vertexCount != run.recordCount * verticesPerRecord ||
                run.firstVertex != firstVertices[segment] + verticesSeen ||
                run.sourceFileIndex >= 264u ||
                g_AnmManager->textures[run.sourceFileIndex].id == 0u ||
                run.logicalState != logicalLayers[segment] ||
                (run.renderStateFlags & ~allowedState) != 0u ||
                (segment == 0u &&
                 (run.renderStateFlags &
                  TH07_PSP_ME_RENDER_STREAM_RUN_BLEND_ADD) != 0u) ||
                (segment == 1u &&
                 (run.renderStateFlags &
                  TH07_PSP_ME_RENDER_STREAM_RUN_BLEND_ADD) == 0u))
            {
                return false;
            }
            recordsSeen += run.recordCount;
            verticesSeen += run.vertexCount;
            previousRecordEnd = run.firstRecord + run.recordCount;
        }
        if (recordsSeen > recordCounts[segment] ||
            verticesSeen != vertexCounts[segment])
        {
            return false;
        }
    }

    // READY_SC remains untouched. In particular, an Effect-only rejection
    // cannot release/quarantine the independently valid Item/Bullet stream.
    return true;
}

bool EffectManager::PspConsumeMeEffectRenderStream(
    const Th07PspMeRenderStreamJob *job,
    const Th07PspMeRenderStreamReady *ready,
    Th07PspMeEffectSubmitRuns submitRuns, void *context)
{
    if (!submitRuns || !this->PspValidateMeEffectRenderStream(job, ready))
    {
        return false;
    }
    const u32 layer0FirstRun = ready->itemRunCount;
    const u32 layer3FirstRun =
        layer0FirstRun + ready->effectLayer0RunCount;
    const u32 effectEndRun = layer3FirstRun + ready->effectLayer3RunCount;
    // The caller must acquire the GE token only after Validate succeeds. Once
    // the first callback becomes visible, fallback is no longer legal; keep
    // the same owner open through priority-10 Item/Bullet consumption.
    submitRuns(context, layer0FirstRun, layer3FirstRun);
    this->PspDrawCanonicalEffectLayer(2);
    // Layer 2 may leave native vertices queued in AnmManager. The following
    // external run submission bypasses that queue, so flush unconditionally:
    // without this fence a same-texture/state layer-2 batch could reach GE
    // after the layer-3 direct list and invert canonical 0 -> 2 -> 3 order.
    g_AnmManager->Flush();
    submitRuns(context, layer3FirstRun, effectEndRun);
    return true;
}
#endif

u32 EffectManager::OnDraw(EffectManager *arg)
{
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    if (Th07PspTryConsumeMeEffectStream())
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }
    arg->PspDrawCanonicalEffectLayer(0);
    arg->PspDrawCanonicalEffectLayer(2);
    arg->PspDrawCanonicalEffectLayer(3);
    return CHAIN_CALLBACK_RESULT_CONTINUE;
#else
    Effect *effect;

    effect = arg->layer0.next;
    while (effect)
    {
        effect->vm.pos = effect->pos1;
        effect->vm.pos.x += g_GameManager.arcadeRegionTopLeftPos.x;
        effect->vm.pos.y += g_GameManager.arcadeRegionTopLeftPos.y;
#if defined(TH07_PSP)
        g_AnmManager->DrawPspFastSprite(&effect->vm);
#else
        g_AnmManager->Draw(&effect->vm);
#endif
        effect = effect->next;
    }
    effect = arg->layer2.next;
    while (effect)
    {
        effect->vm.pos = effect->pos1;
        g_AnmManager->DrawBillboard(&effect->vm);
        effect = effect->next;
    }
    effect = arg->layer3.next;
    while (effect)
    {
        effect->vm.pos = effect->pos1;
        effect->vm.pos.x += g_GameManager.arcadeRegionTopLeftPos.x;
        effect->vm.pos.y += g_GameManager.arcadeRegionTopLeftPos.y;
#if defined(TH07_PSP)
        g_AnmManager->DrawPspFastSprite(&effect->vm);
#else
        g_AnmManager->Draw(&effect->vm);
#endif
        effect = effect->next;
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
#endif
}

i32 EffectManager::UpdateSpecialEffect()
{
    i32 temp;
    f32 r;
    f32 g;
    f32 b;
    i32 counter;
    f32 a;
    Effect *effect;

    effect = this->layer1.next;
    counter = 0;

    if (g_Supervisor.cfg.effectQuality == QUALITY_WORST)
    {
        return 1;
    }

    while (effect)
    {
        counter++;
        if (g_Supervisor.cfg.effectQuality == QUALITY_MEDIUM)
        {
            if (counter & 1)
            {
                return 1;
            }
        }

        if (effect->effectId == 20)
        {
            r = (f32)effect->vm.color.bytes.r;
            g = (f32)effect->vm.color.bytes.g;
            b = (f32)effect->vm.color.bytes.b;
            a = (f32)effect->vm.color.bytes.a;

            temp = (i32)(r * this->globalColorMultiplierR);
            effect->vm.color.bytes.r = temp > 255 ? 255 : temp;

            temp = (i32)(g * this->globalColorMultiplierG);
            effect->vm.color.bytes.g = temp > 255 ? 255 : temp;

            temp = (i32)(b * this->globalColorMultiplierB);
            effect->vm.color.bytes.b = temp > 255 ? 255 : temp;

            temp = (i32)(a * this->globalColorMultiplierA);
            effect->vm.color.bytes.a = temp > 255 ? 255 : temp;
        }

        effect->vm.pos = effect->pos1;
        if (effect->is2D == 1)
        {
            g_AnmManager->DrawBillboard(&effect->vm);
        }
        else
        {
            g_AnmManager->DrawProjected(&effect->vm);
        }

        if (effect->effectId == 20)
        {
            effect->vm.color.bytes.r = (u8)r;
            effect->vm.color.bytes.g = (u8)g;
            effect->vm.color.bytes.b = (u8)b;
            effect->vm.color.bytes.a = (u8)a;
        }

        effect = effect->next;
    }

    return 1;
}

ZunResult EffectManager::AddedCallback(EffectManager *arg)
{
    arg->Reset();
    g_Stage.spellcardVmsIdx = 0;
    switch (g_GameManager.currentStage)
    {
    case 0:
    case 1:
        g_Stage.numSpellcardVms = 1;
        if (g_AnmManager->LoadAnms(ANM_FILE_EFFECTS, "data/eff01.anm", ANM_OFFSET_EFFECTS) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 2:
        g_Stage.numSpellcardVms = 1;
        if (g_AnmManager->LoadAnms(ANM_FILE_EFFECTS, "data/eff02.anm", ANM_OFFSET_EFFECTS) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 3:
        g_Stage.numSpellcardVms = 1;
        if (g_AnmManager->LoadAnms(ANM_FILE_EFFECTS, "data/eff03.anm", ANM_OFFSET_EFFECTS) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 4:
        g_Stage.numSpellcardVms = 2;
        if (g_AnmManager->LoadAnms(ANM_FILE_EFFECTS, "data/eff04.anm", ANM_OFFSET_EFFECTS) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        if (g_AnmManager->LoadAnms(ANM_FILE_EFFECTS2, "data/eff04b.anm", ANM_OFFSET_EFFECTS2) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 5:
        g_Stage.numSpellcardVms = 2;
        if (g_AnmManager->LoadAnms(ANM_FILE_EFFECTS, "data/eff05.anm", ANM_OFFSET_EFFECTS) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 6:
        g_Stage.numSpellcardVms = 2;
        if (g_AnmManager->LoadAnms(ANM_FILE_EFFECTS, "data/eff05.anm", ANM_OFFSET_EFFECTS) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        if (g_AnmManager->LoadAnms(ANM_FILE_EFFECTS3, "data/eff06.anm", ANM_OFFSET_EFFECTS3) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 7:
        g_Stage.numSpellcardVms = 1;
        if (g_AnmManager->LoadAnms(ANM_FILE_EFFECTS, "data/eff02.anm", ANM_OFFSET_EFFECTS) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        if (g_AnmManager->LoadAnms(ANM_FILE_EFFECTS2, "data/eff07.anm", ANM_OFFSET_EFFECTS2) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        break;
    case 8:
        g_Stage.numSpellcardVms = 2;
        if (g_AnmManager->LoadAnms(ANM_FILE_EFFECTS, "data/eff07.anm", ANM_OFFSET_EFFECTS) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
        if (g_AnmManager->LoadAnms(ANM_FILE_EFFECTS3, "data/eff08.anm", ANM_OFFSET_EFFECTS3) !=
            ZUN_SUCCESS)
        {
            return ZUN_ERROR;
        }
    }
    return ZUN_SUCCESS;
}

ZunResult EffectManager::DeletedCallback(EffectManager *arg)
{
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
    // BulletManager normally drains command 10 before Effect teardown. Keep
    // the local authority fail-closed as a second fence: no stale READY view
    // may survive ANM texture release or a stage/manager reincarnation.
    arg->pspMeEffectPreparedSerial = 0u;
    arg->pspMeEffectPreparedCounts[0] = 0u;
    arg->pspMeEffectPreparedCounts[1] = 0u;
    if (++arg->pspMeEffectPrepareSerial == 0u)
    {
        ++arg->pspMeEffectPrepareSerial;
    }
#endif
    g_AnmManager->ReleaseAnm(17);
    g_AnmManager->ReleaseAnm(18);
    g_AnmManager->ReleaseAnm(19);
    g_AnmManager->ReleaseAnm(20);
#if defined(TH07_PSP_1000)
    arg->PspReleaseEffectPool();
#else
    (void)arg;
#endif
    return ZUN_SUCCESS;
}

ZunResult EffectManager::RegisterChain()
{
    EffectManager *mgr = &g_EffectManager;
    mgr->Reset();
    g_EffectManagerCalcChain.callback = (ChainCallback)OnUpdate;
    g_EffectManagerCalcChain.addedCallback = NULL;
    g_EffectManagerCalcChain.deletedCallback = NULL;
    g_EffectManagerCalcChain.addedCallback = (ChainLifecycleCallback)AddedCallback;
    g_EffectManagerCalcChain.deletedCallback = (ChainLifecycleCallback)DeletedCallback;
    g_EffectManagerCalcChain.arg = mgr;
    if (g_Chain.AddToCalcChain(&g_EffectManagerCalcChain, 11))
    {
        return ZUN_ERROR;
    }

    g_EffectManagerDrawChain.callback = (ChainCallback)OnDraw;
    g_EffectManagerDrawChain.addedCallback = NULL;
    g_EffectManagerDrawChain.deletedCallback = NULL;
    g_EffectManagerDrawChain.arg = mgr;
    g_Chain.AddToDrawChain(&g_EffectManagerDrawChain, 9);
    return ZUN_SUCCESS;
}

void EffectManager::CutChain()
{
    g_Chain.Cut(&g_EffectManagerCalcChain);
    g_Chain.Cut(&g_EffectManagerDrawChain);
}

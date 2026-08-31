#include "EnemyEclInstr.hpp"

#include "BulletManager.hpp"
#include "EnemyManager.hpp"
#include "GameManager.hpp"
#include "Player.hpp"
#include "Rng.hpp"
#include "SoundPlayer.hpp"
#include "Stage.hpp"
#include "Supervisor.hpp"
#include "ZunMath.hpp"
#include "utils.hpp"

#if defined(TH07_PSP)
#define PSP_ECL_SKIP_UNTRACKED_BULLET(index)                                                   \
    if (!g_BulletManager.PspIsBulletSlotTracked(index))                                       \
    {                                                                                          \
        continue;                                                                              \
    }
#else
#define PSP_ECL_SKIP_UNTRACKED_BULLET(index)
#endif

#if defined(TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY)
// These EX instructions are the only writers that bypass BulletManager's
// tracked-slot/bulk mutation APIs.  Advance the cross-frame seed authority
// before touching any live Bullet so the next calc-12 pass fails closed.
#define PSP_ECL_MARK_BULLET_MUTATION() \
    g_BulletManager.PspMarkMeRenderMutation()
#else
#define PSP_ECL_MARK_BULLET_MUTATION()
#endif

EclExInstr g_EclExInstr[24] = {
    EnemyEclInstr::ExInsSetPosToBoss,
    EnemyEclInstr::ExInsAliceCurveBullets,
    EnemyEclInstr::ExInsTurnBulletsIntoOtherBullets,
    EnemyEclInstr::ExInsNoOp,
    EnemyEclInstr::ExInsDespawnLargeBulletAndSavePos,
    EnemyEclInstr::ExInsCopyMainBossMovement,
    EnemyEclInstr::ExInsSplitBulletsOrShootBackwards,
    EnemyEclInstr::ExInsReflectBulletsFromLasers,
    EnemyEclInstr::ExInsShootBulletsAlongLaser,
    EnemyEclInstr::ExInsEffect1eAccel,
    EnemyEclInstr::ExInsYoumuSetGameSpeed,
    EnemyEclInstr::ExInsYoumuRestoreGameSpeed,
    EnemyEclInstr::ExInsBurstLargeBullets,
    EnemyEclInstr::ExInsYoumuCurveBulletsBelow,
    EnemyEclInstr::ExInsYoumuRedirectBulletsToPlayer,
    EnemyEclInstr::ExInsFlashScreen,
    EnemyEclInstr::ExInsYuyukoTransformButterflyBullets,
    EnemyEclInstr::ExInsYuyukoButterflySpawnEnemy,
    EnemyEclInstr::ExInsYuyukoCountButterflyBullets,
    EnemyEclInstr::ExInsYuyukoFadeOutMusic,
    EnemyEclInstr::ExInsYuyukoPlayResurrectionButterflyBgm,
    EnemyEclInstr::ExInsBurstLargeBullets2,
    EnemyEclInstr::ExInsSpawnBulletsWithDirChange,
    EnemyEclInstr::ExInsSpawnBulletsWithDirChange2,
};

EclInterpFn g_EclInterpFuncs[8] = {
    EclManager::MathLerp, EclManager::MathLerp, EclManager::MathLerp, EclManager::MathLerp,
    EclManager::MathLerp, EclManager::MathLerp, EclManager::MathLerp, EclManager::MathCubicInterp,
};

void EnemyEclInstr::ExInsSetPosToBoss(Enemy *enemy, EclRawInstr *instr)
{
    i32 bossIdx = instr->args[1].i;
    enemy->position = g_EnemyManager.bosses[bossIdx]->position;
    enemy->axisSpeed = g_EnemyManager.bosses[bossIdx]->axisSpeed;
    enemy->angle = g_EnemyManager.bosses[bossIdx]->angle;
    enemy->disableMovement = 1;
}

void EnemyEclInstr::ExInsAliceCurveBullets(Enemy *enemy, EclRawInstr *instr)
{
    (void)enemy;

    PSP_ECL_MARK_BULLET_MUTATION();

    f32 local_10;
    Bullet *bullet;
    i32 i;

    BombEffects::RegisterChain(1, 30, 12, 0, 0);
    BombEffects::RegisterChain(3, 4, 3, 0x80ffcfcf, 0);
    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE || bullet->state == BULLET_DESPAWN)
        {
            continue;
        }

        if (bullet->sprites.spriteBullet.sprite && bullet->state2 == 0)
        {
            if (instr->args[1].i == 1 && bullet->spriteOffset != 8)
            {
                continue;
            }

            if (instr->args[1].i == 2 && bullet->spriteOffset != 4)
            {
                continue;
            }

            if (bullet->spriteOffset == 2)
            {
                local_10 = -ZUN_PI / (g_Rng.GetRandomFloatInRange(60.0f) + 180.0f);
            }
            else if (bullet->spriteOffset == 6)
            {
                local_10 = ZUN_PI / (g_Rng.GetRandomFloatInRange(60.0f) + 180.0f);
            }
            else if (bullet->spriteOffset == 8)
            {
                local_10 = ZUN_PI / (g_Rng.GetRandomFloatInRange(60.0f) + 180.0f);
            }
            else if (bullet->spriteOffset == 4)
            {
                local_10 = -ZUN_PI / (g_Rng.GetRandomFloatInRange(60.0f) + 180.0f);
            }
            bullet->speed = 0.3f;
            memset(bullet->commands, 0, sizeof(bullet->commands));
            if (g_GameManager.difficulty < 3)
            {
                bullet->AddAngleAccelCommand(0, 0, 60, local_10, 0.016666668f);
            }
            else
            {
                bullet->AddAngleAccelCommand(0, 0, 240, local_10, 0.005263158f);
            }
            bullet->state2 = 1;
        }
    }
}

void EnemyEclInstr::ExInsTurnBulletsIntoOtherBullets(Enemy *enemy, EclRawInstr *instr)
{
    PSP_ECL_MARK_BULLET_MUTATION();

    Bullet *bullet;
    f32 distance;
    f32 local_e4;
    i32 i;
    EnemyBulletShooter bulletProps;

    switch (instr->GetSecondArg().i)
    {
    case 0:
        BombEffects::RegisterChain(1, 32, 12, 0, 0);
        BombEffects::RegisterChain(3, 4, 1, 0x80cfcfff, 0);
        local_e4 = 128.0f;
        break;
    case 1:
        local_e4 = 192.0f;
        break;
    case 2:
        local_e4 = 256.0f;
        break;
    case 3:
        local_e4 = 999.0;
    }
    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE || bullet->state == BULLET_DESPAWN)
        {
            continue;
        }

        if (bullet->sprites.spriteBullet.sprite && bullet->spriteOffset == 2)
        {
            distance =
                sqrtf((enemy->position.x - bullet->pos.x) * (enemy->position.x - bullet->pos.x) +
                      (enemy->position.y - bullet->pos.y) * (enemy->position.y - bullet->pos.y));
            if (distance < local_e4)
            {
                bulletProps.position = bullet->pos;
                bulletProps.sprite = 0;
                bulletProps.spriteOffset = 6;
                bulletProps.angle1 = 0.0f;
                bulletProps.angle2 = -ZUN_PI;
                bulletProps.speed1 = 0.7f;
                bulletProps.count1 = 2;
                bulletProps.count2 = 1;
                bulletProps.flags = 2;
                bulletProps.aimMode = 6;
                bulletProps.AddTargetVelocityCommand(
                    0, 0, 180, g_Rng.GetRandomFloatInRange(0.005f) + 0.013f, 1.5707964f);
                g_BulletManager.SpawnBulletPattern(&bulletProps);
                bullet->Initialize();
            }
        }
    }
}

void EnemyEclInstr::ExInsNoOp(Enemy *enemy, EclRawInstr *instr)
{
    (void)enemy;
    (void)instr;
}

void EnemyEclInstr::ExInsDespawnLargeBulletAndSavePos(Enemy *enemy, EclRawInstr *instr)
{
    (void)instr;

    PSP_ECL_MARK_BULLET_MUTATION();

    Bullet *bullet;
    i32 i;
    EnemyBulletShooter bulletProps;

    enemy->currentContext.eclContextArgs.floatVars1[0] = -999.0f;
    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE || bullet->state == BULLET_DESPAWN)
        {
            continue;
        }

        if (bullet->sprites.spriteBullet.sprite->heightPx >= 60.0f)
        {
            enemy->currentContext.eclContextArgs.floatVars1[0] = bullet->pos.x;
            enemy->currentContext.eclContextArgs.floatVars1[1] = bullet->pos.y;
            g_EffectManager.SpawnParticles(2, &bullet->pos, 1, 0xffffffff);
            bullet->Initialize();
            break;
        }
    }
}

void EnemyEclInstr::ExInsCopyMainBossMovement(Enemy *enemy, EclRawInstr *instr)
{
    (void)instr;

    Enemy *boss = g_EnemyManager.bosses[0];
    enemy->moveInterpStartPos = boss->position;
    enemy->moveRadius = boss->moveRadius;
    enemy->moveAngularVelocity = boss->moveAngularVelocity;
}

void EnemyEclInstr::ExInsSplitBulletsOrShootBackwards(Enemy *enemy, EclRawInstr *instr)
{
    (void)enemy;

    PSP_ECL_MARK_BULLET_MUTATION();

    Bullet *bullet;
    i32 i;
    EnemyBulletShooter bulletProps;

    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE || bullet->state == BULLET_DESPAWN)
        {
            continue;
        }

        if (bullet->sprites.spriteBullet.sprite &&
            ((instr->args[1].i == 0 && bullet->spriteOffset == 6) ||
             (instr->args[1].i == 1 && bullet->spriteOffset == 15) ||
             (instr->args[1].i == 2 && bullet->spriteOffset == 2)))
        {
            bulletProps.position = bullet->pos;
            bulletProps.sprite = 6;
            bulletProps.spriteOffset = 15;
            bulletProps.angle1 = utils::AddNormalizeAngle(bullet->angle, ZUN_PI);
            bulletProps.angle2 = 0.5235988f;
            bulletProps.speed1 = bullet->speed * 1.1f;
            if (g_GameManager.difficulty < 3)
            {
                bulletProps.count1 = 4;
            }
            else
            {
                bulletProps.count1 = 2;
                bulletProps.angle2 = 1.5707964f;
            }
            bulletProps.count2 = 1;
            bulletProps.flags = 2;
            bulletProps.AddSpawnDelayCommand(0, 0, 130);
            if (instr->args[1].i == 0)
            {
                bulletProps.flags = 0x2002;
            }
            else if (instr->args[1].i == 1)
            {
                if (g_GameManager.difficulty != DIFF_LUNATIC)
                {
                    bulletProps.flags = 2;
                }
                else
                {
                    bulletProps.flags = 0x2002;
                }
                bulletProps.spriteOffset = 2;
            }
            else if (instr->args[1].i == 2)
            {
                bulletProps.flags = 2;
                bulletProps.spriteOffset = 10;
            }
            bulletProps.aimMode = 1;
            g_BulletManager.SpawnBulletPattern(&bulletProps);
            bulletProps.angle2 = 1.0471976f;
            if (instr->args[1].i == 0)
            {
                bulletProps.flags = 0x2000;
            }
            else if (instr->args[1].i == 1)
            {
                if (g_GameManager.difficulty != DIFF_LUNATIC)
                {
                    bulletProps.flags = 0;
                }
                else
                {
                    bulletProps.flags = 0x2000;
                }
            }
            else
            {
                bulletProps.flags = 0;
            }
            bulletProps.speed1 = bullet->speed * 0.7f;
            bulletProps.count1 = 2;
            g_BulletManager.SpawnBulletPattern(&bulletProps);
            bulletProps.speed1 = bullet->speed * 0.85f;
            bulletProps.count1 = 1;
            g_BulletManager.SpawnBulletPattern(&bulletProps);
            bullet->Initialize();
        }
    }
}

i32 IsPointInRotatedRect(ZunVec3 *point, ZunVec3 *center, ZunVec3 *size, ZunVec3 *pivot, f32 sine,
                         f32 cosine)
{
    ZunVec3 d;
    ZunVec3 rot;
    ZunVec3 p;

    d = *point - *pivot;

    rot.x = d.x * cosine + d.y * sine;
    rot.y = d.y * cosine - d.x * sine;

    p = rot + *pivot;

    d = *center - *size / 2.0f;

    rot = *center + *size / 2.0f;

    if (p.x > rot.x || p.x < d.x || p.y > rot.y || p.y < d.y)
    {
        return 0;
    }

    return 1;
}

void EnemyEclInstr::ExInsReflectBulletsFromLasers(Enemy *enemy, EclRawInstr *instr)
{
    (void)instr;

    PSP_ECL_MARK_BULLET_MUTATION();

    ZunVec3 center;
    f32 cosine;
    i32 i;
    Laser *laser;
    Bullet *bullet;
    ZunVec3 size;
    f32 dot;
    f32 sine;
    i32 j;

    laser = g_BulletManager.lasers;
    for (i = 0; i < 64; i++, laser++)
    {
        if (!laser->inUse)
        {
            continue;
        }

        if (enemy->timer.current % 2 != i)
        {
            continue;
        }

        if (laser->state < 2)
        {
            size.y = laser->width;
            size.x = laser->endOffset - laser->startOffset;
            center.x =
                (laser->endOffset - laser->startOffset) / 2.0f + laser->startOffset + laser->pos.x;
            center.y = laser->pos.y;
            sincosf(&sine, &cosine, laser->angle);
            for (j = 0; j < BulletManager::kBulletCapacity; j++)
            {
                bullet = g_BulletManager.BulletAt(j);
                PSP_ECL_SKIP_UNTRACKED_BULLET(j);
                if (bullet->state == BULLET_INACTIVE || bullet->state == BULLET_DESPAWN)
                {
                    continue;
                }

                if (bullet->sprites.spriteBullet.sprite &&
                    IsPointInRotatedRect(&bullet->pos, &center, &size, &laser->pos, sine, cosine))
                {
                    if (bullet->state2 > 0)
                    {
                        bullet->state2--;
                    }

                    if (bullet->state2 == 0)
                    {
                        if (bullet->speed > 0.5f)
                        {
                            bullet->speed = bullet->speed - 0.1f;
                        }
                        dot = cosine * bullet->velocity.y + sine * bullet->velocity.x;
                        if (dot >= 0.0f)
                        {
                            bullet->angle = utils::AddNormalizeAngle(laser->angle, 1.5707964f);
                        }
                        else
                        {
                            bullet->angle = utils::AddNormalizeAngle(laser->angle, -1.5707964f);
                        }
                        AngleToVector(&bullet->velocity, bullet->angle,
                                      g_Supervisor.effectiveFramerateMultiplier * bullet->speed);
                        bullet->state2 = 10;
                        bullet->AssignTypeSprites(g_BulletManager.bulletTypeTemplates[5]);
                        BulletManager::SetActiveBulletSprite(
                            bullet,
                            (i32)bullet->sprites.spriteBullet.activeSpriteIdx +
                                (i32)bullet->spriteOffset);
                    }
                }
            }
        }
    }
}

void EnemyEclInstr::ExInsShootBulletsAlongLaser(Enemy *enemy, EclRawInstr *instr)
{
    (void)instr;

    PSP_ECL_MARK_BULLET_MUTATION();

    ZunVec3 center;
    f32 cosine;
    i32 i;
    Laser *laser;
    f32 dirX;
    Bullet *bullet;
    ZunVec3 size;
    f32 dot;
    f32 sine;
    i32 j;
    f32 dirY;

    laser = g_BulletManager.lasers;
    for (i = 0; i < 64; i++, laser++)
    {
        if (!laser->inUse)
        {
            continue;
        }

        if (i % 3 != enemy->timer.current % 3)
        {
            continue;
        }

        if (laser->state < 2)
        {
            size.y = laser->width * 1.5f;
            size.x = laser->endOffset - laser->startOffset;
            center.x =
                (laser->endOffset - laser->startOffset) / 2.0f + laser->startOffset + laser->pos.x;
            center.y = laser->pos.y;
            sincosf(&sine, &cosine, laser->angle);
            dirX = -sine;
            dirY = cosine;
            for (j = 0; j < BulletManager::kBulletCapacity; j++)
            {
                bullet = g_BulletManager.BulletAt(j);
                PSP_ECL_SKIP_UNTRACKED_BULLET(j);
                if (bullet->state == BULLET_INACTIVE || bullet->state == BULLET_DESPAWN)
                {
                    continue;
                }

                if (bullet->sprites.spriteBullet.sprite && bullet->state2 != i + 1 &&
                    bullet->state2 >= 0 &&
                    IsPointInRotatedRect(&bullet->pos, &center, &size, &laser->pos, sine, cosine))
                {
                    if (g_GameManager.difficulty < 2)
                    {
                        bullet->speed *= g_Rng.GetRandomFloatInRange(0.3f) + 0.7f;
                    }
                    else
                    {
                        bullet->speed *= g_Rng.GetRandomFloatInRange(0.4f) + 0.8f;
                    }
                    dot = dirX * bullet->velocity.x + dirY * bullet->velocity.y;
                    if (dot >= 0.0f)
                    {
                        bullet->velocity.x = dirX;
                        bullet->velocity.y = dirY;
                    }
                    else
                    {
                        bullet->velocity.x = -dirX;
                        bullet->velocity.y = -dirY;
                    }
                    bullet->AssignTypeSprites(g_BulletManager.bulletTypeTemplates[5]);
                    BulletManager::SetActiveBulletSprite(
                        bullet, bullet->sprites.spriteBullet.activeSpriteIdx +
                                    bullet->spriteOffset);
                    bullet->angle = atan2f(bullet->velocity.y, bullet->velocity.x);
                    AngleToVector(&bullet->velocity, bullet->angle, bullet->speed);
                    if (g_GameManager.difficulty < 2)
                    {
                        bullet->state2 = -1;
                    }
                    else
                    {
                        bullet->state2 = i + 1;
                    }
                }
            }
        }
    }
}

void EnemyEclInstr::ExInsEffect1eAccel(Enemy *enemy, EclRawInstr *instr)
{
    (void)enemy;
    (void)instr;

    BombEffects::RegisterChain(1, 80, 8, 0, 0);
    EffectManager::ModifyEffect1eAcceleration();
}

void EnemyEclInstr::ExInsYoumuSetGameSpeed(Enemy *enemy, EclRawInstr *instr)
{
    (void)enemy;

    PSP_ECL_MARK_BULLET_MUTATION();

    Bullet *bullet;
    i32 i;

    g_Supervisor.effectiveFramerateMultiplier = 1.0f / (f32)instr->args[1].i;
    g_Stage.spellcardVms[0].pendingInterrupt = 2;
    g_Stage.spellcardVms[1].pendingInterrupt = 2;
    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE)
        {
            continue;
        }

        bullet->velocity *= g_Supervisor.effectiveFramerateMultiplier;
        bullet->sprites.spriteBullet.baseSpriteIdx = bullet->sprites.spriteBullet.activeSpriteIdx;
        if (bullet->sprites.spriteBullet.activeSpriteIdx >= 608 &&
            bullet->sprites.spriteBullet.activeSpriteIdx <= 623)
        {
            BulletManager::SetActiveBulletSprite(bullet, 623);
        }
    }
}

void EnemyEclInstr::ExInsYoumuRestoreGameSpeed(Enemy *enemy, EclRawInstr *instr)
{
    (void)enemy;

    PSP_ECL_MARK_BULLET_MUTATION();

    Bullet *bullet;
    f32 fps;
    i32 i;

    fps = 1.0f / g_Supervisor.effectiveFramerateMultiplier;
    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE)
        {
            continue;
        }

        bullet->velocity *= fps;
        if (bullet->sprites.spriteBullet.activeSpriteIdx >= 608 &&
            bullet->sprites.spriteBullet.activeSpriteIdx <= 623)
        {
            BulletManager::SetActiveBulletSprite(
                bullet, bullet->sprites.spriteBullet.baseSpriteIdx);
        }
    }
    g_Supervisor.effectiveFramerateMultiplier = 1.0f / (f32)instr->args[1].i;
    if (g_Supervisor.effectiveFramerateMultiplier < 1.0f)
    {
        g_Supervisor.flags |= 0x20;
    }
    g_Supervisor.effectiveFramerateMultiplier = 1.0f;
    g_Stage.spellcardVms[0].pendingInterrupt = 1;
    g_Stage.spellcardVms[1].pendingInterrupt = 1;
}

void EnemyEclInstr::ExInsBurstLargeBullets(Enemy *enemy, EclRawInstr *instr)
{
    PSP_ECL_MARK_BULLET_MUTATION();

    i32 j;
    Bullet *bullet;
    i32 i;
    i32 numBullets;
    EnemyBulletShooter bulletProps;

    BombEffects::RegisterChain(3, 8, 1, 0x50cfcfff, 0);

    numBullets = g_GameManager.difficulty == DIFF_EASY     ? 10
                 : g_GameManager.difficulty == DIFF_NORMAL ? 18
                 : g_GameManager.difficulty == DIFF_HARD   ? 22
                                                           : 25;
    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE)
        {
            continue;
        }

        if ((g_GameManager.difficulty < DIFF_HARD &&
             bullet->sprites.spriteBullet.sprite->heightPx > 48.0f &&
             bullet->pos.y > enemy->position.y - 64.0f &&
             bullet->pos.y < enemy->position.y + 64.0f) ||
            (g_GameManager.difficulty >= DIFF_HARD &&
             bullet->sprites.spriteBullet.sprite->heightPx > 48.0f &&
             bullet->pos.y > enemy->position.y - 48.0f &&
             bullet->pos.y < enemy->position.y + 48.0f))
        {
            for (j = 0; j < numBullets; j++)
            {
                bulletProps.position = bullet->pos;
                bulletProps.position.x += g_Rng.GetRandomFloatInRange(32.0f) - 16.0f;
                bulletProps.position.y += g_Rng.GetRandomFloatInRange(32.0f) - 16.0f;

                switch (g_Rng.GetRandomU16InRange(3))
                {
                case 0:
                    bulletProps.sprite = 0;
                    bulletProps.spriteOffset = 2;
                    break;
                case 1:
                    bulletProps.sprite = 3;
                    bulletProps.spriteOffset = 2;
                    break;
                case 2:
                    bulletProps.sprite = 7;
                    bulletProps.spriteOffset = 1;
                    break;
                }
                if (instr->args[1].i == 0)
                {
                    bulletProps.angle1 = g_Rng.GetRandomFloatInRange(4.712389f) - 1.5707964f;
                }
                else
                {
                    bulletProps.angle1 = utils::AddNormalizeAngle(
                        g_Rng.GetRandomFloatInRange(4.712389f), 0.7853982f);
                }
                bulletProps.speed1 = 0.1f;
                bulletProps.count1 = 1;
                bulletProps.count2 = 1;
                bulletProps.flags = j & 1 ? 2 : 0;
                bulletProps.aimMode = 1;
                bulletProps.AddAngleAccelCommand(0, 0, 100, 0.0f,
                                                 g_Rng.GetRandomFloatInRange(0.008f) + 0.01f);
                g_BulletManager.SpawnBulletPattern(&bulletProps);
            }
            bullet->Initialize();
        }
    }
}

void EnemyEclInstr::ExInsYoumuCurveBulletsBelow(Enemy *enemy, EclRawInstr *instr)
{
    (void)instr;

    PSP_ECL_MARK_BULLET_MUTATION();

    Bullet *bullet;
    i32 i;

    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE)
        {
            continue;
        }

        if (bullet->state2 == 0 && bullet->pos.y > enemy->position.y && bullet->pos.y < 352.0f &&
            bullet->pos.x > enemy->position.x - 16.0f && bullet->pos.x < enemy->position.x + 16.0f)
        {
            bullet->AddAngleAccelCommand(0, 0, 160, i & 1 ? 0.05235988f : -0.05235988f,
                                         -bullet->speed / 180.0f);
            bullet->state2 = 1;
        }
    }
}

void EnemyEclInstr::ExInsYoumuRedirectBulletsToPlayer(Enemy *enemy, EclRawInstr *instr)
{
    (void)enemy;
    (void)instr;

    PSP_ECL_MARK_BULLET_MUTATION();

    Bullet *bullet;
    i32 i;

    BombEffects::RegisterChain(3, 16, 1, 0x50cfcfff, 0);
    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE)
        {
            continue;
        }

        if (bullet->state2 == 1)
        {
            bullet->AddTargetVelocityCommand(0, 0, 90, 0.026666667f,
                                             g_Player.AngleToPlayer(&bullet->pos));
            bullet->ClearCommand(1);
            bullet->state2 = 2;
        }
    }
}

void EnemyEclInstr::ExInsFlashScreen(Enemy *enemy, EclRawInstr *instr)
{
    (void)enemy;

    BombEffects::RegisterChain(3, instr->args[1].i, 1, 0xd0cfcfff, 0);
}

void EnemyEclInstr::ExInsYuyukoTransformButterflyBullets(Enemy *enemy, EclRawInstr *instr)
{
    (void)instr;

    Bullet *bullet;
    EnemyBulletShooter bulletProps;
    i32 i;

    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE)
        {
            continue;
        }
        if (bullet->state2 == 0 && bullet->sprites.spriteBullet.activeSpriteIdx >= 632 &&
            bullet->sprites.spriteBullet.activeSpriteIdx <= 639)
        {
            bulletProps.position = bullet->pos;
            bulletProps.sprite = 0;
            bulletProps.spriteOffset = 6;
            bulletProps.angle1 = utils::AddNormalizeAngle(bullet->angle, ZUN_PI);
            bulletProps.angle2 = 0.3926991f;
            bulletProps.speed1 = enemy->currentContext.eclContextArgs.floatVars1[1];
            bulletProps.count1 = 5;
            bulletProps.count2 = 1;
            bulletProps.flags = 2;
            bulletProps.aimMode = 1;
            g_BulletManager.SpawnBulletPattern(&bulletProps);
        }
    }
}

void EnemyEclInstr::ExInsYuyukoButterflySpawnEnemy(Enemy *enemy, EclRawInstr *instr)
{
    (void)instr;

    PSP_ECL_MARK_BULLET_MUTATION();

    f32 angleOffset;
    EclContextArgs args;
    Bullet *bullet;
    i32 i;

    args = enemy->currentContext.eclContextArgs;
    angleOffset = -ZUN_PI;
    BombEffects::RegisterChain(3, 12, 1, 0x80cfcfff, 0);
    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE)
        {
            continue;
        }

        if (bullet->state2 == 0 && bullet->sprites.spriteBullet.activeSpriteIdx == 636)
        {
            args.floatVars1[0] = bullet->angle;
            args.floatVars1[7] = angleOffset;
            angleOffset = angleOffset + 0.7853982f;
            g_EnemyManager.SpawnEnemyEx(enemy->currentContext.subId + 1, &bullet->pos, 1, -2, 10,
                                        &args);
            bullet->Initialize();
        }
        else if (bullet->sprites.spriteBullet.activeSpriteIdx >= 632 &&
                 bullet->sprites.spriteBullet.activeSpriteIdx <= 639)
        {
            bullet->Initialize();
        }
    }
}

void EnemyEclInstr::ExInsYuyukoCountButterflyBullets(Enemy *enemy, EclRawInstr *instr)
{
    (void)instr;

    Bullet *bullet;
    i32 i;

    enemy->currentContext.eclContextArgs.intVars1[0] = 0;
    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE)
        {
            continue;
        }
        if (bullet->state2 == 0 && bullet->sprites.spriteBullet.activeSpriteIdx == 636)
        {
            enemy->currentContext.eclContextArgs.intVars1[0]++;
        }
    }
}

void EnemyEclInstr::ExInsBurstLargeBullets2(Enemy *enemy, EclRawInstr *instr)
{
    PSP_ECL_MARK_BULLET_MUTATION();

    i32 j;
    Bullet *bullet;
    i32 i;
    EnemyBulletShooter bulletProps;
    f32 triggerHeight;

    triggerHeight = g_GameManager.difficulty == DIFF_HARD ? 128.0f : 180.0f;
    BombEffects::RegisterChain(3, 8, 1, 0x50cfcfff, 0);
    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE)
        {
            continue;
        }

        if (bullet->sprites.spriteBullet.sprite->heightPx > 48.0f &&
            enemy->position.y - triggerHeight < bullet->pos.y &&
            bullet->pos.y < triggerHeight + enemy->position.y)
        {
            for (j = 0; j < 15; j++)
            {
                bulletProps.position = bullet->pos;
                bulletProps.position.x += g_Rng.GetRandomFloatInRange(32.0f) - 16.0f;
                bulletProps.position.y += g_Rng.GetRandomFloatInRange(32.0f) - 16.0f;

                switch ((u32)g_Rng.GetRandomU16InRange(3))
                {
                case 0:
                    bulletProps.sprite = 0;
                    bulletProps.spriteOffset = 4;
                    break;
                case 1:
                    bulletProps.sprite = 3;
                    bulletProps.spriteOffset = 4;
                    break;
                case 2:
                    bulletProps.sprite = 7;
                    bulletProps.spriteOffset = 2;
                    break;
                }
                if (instr->args[1].i == 0)
                {
                    bulletProps.angle1 = g_Rng.GetRandomFloatInRange(4.712389f) - 1.5707964f;
                }
                else
                {
                    bulletProps.angle1 = utils::AddNormalizeAngle(
                        g_Rng.GetRandomFloatInRange(4.712389f), 0.7853982f);
                }
                bulletProps.speed1 = 0.1f;
                bulletProps.count1 = 1;
                bulletProps.count2 = 1;
                bulletProps.flags = 0;
                bulletProps.aimMode = 1;
                bulletProps.AddAngleAccelCommand(0, 0, 100, 0.0f,
                                                 g_Rng.GetRandomFloatInRange(0.008f) + 0.01f);
                g_BulletManager.SpawnBulletPattern(&bulletProps);
            }
            bullet->Initialize();
        }
    }
}

void EnemyEclInstr::ExInsYuyukoFadeOutMusic(Enemy *enemy, EclRawInstr *instr)
{
    (void)enemy;
    (void)instr;

    g_Supervisor.FadeOutMusic(3.0f);
}

void EnemyEclInstr::ExInsYuyukoPlayResurrectionButterflyBgm(Enemy *enemy, EclRawInstr *instr)
{
    (void)enemy;
    (void)instr;

    if (g_Supervisor.PlayLoadedAudio(2) != ZUN_SUCCESS)
    {
        g_Supervisor.PlayAudio("bgm/th07_13b.mid");
    }
}

void EnemyEclInstr::ExInsSpawnBulletsWithDirChange(Enemy *enemy, EclRawInstr *instr)
{
    (void)instr;

    Bullet *bullet;
    u32 timerMod2;
    EnemyBulletShooter bulletProps;
    i32 i;

    if (enemy->timer.current % 3 == 0)
    {
        return;
    }

    timerMod2 = enemy->timer.current % 2;
    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE)
        {
            continue;
        }
        if ((bullet->exFlags & 0x40U) == 0 && bullet->pos.y < 320.0f &&
            bullet->sprites.spriteBullet.sprite->heightPx > 60.0f)
        {
            bulletProps.position = bullet->pos;
            if (timerMod2 != 0)
            {
                bulletProps.sprite = 1;
            }
            else
            {
                bulletProps.sprite = 3;
            }
            bulletProps.spriteOffset = bullet->spriteOffset == 1 ? 6 : 2;
            bulletProps.angle1 = g_Rng.GetRandomFloatInRange(ZUN_2PI) - ZUN_PI;
            bulletProps.angle2 = -ZUN_PI;
            if (timerMod2 != 0)
            {
                bulletProps.speed1 = 1.2f;
            }
            else
            {
                bulletProps.speed1 = 0.8f;
            }

            if (timerMod2 != 0)
            {
                bulletProps.count1 = 1;
            }
            else
            {
                bulletProps.count1 = 2;
            }

            bulletProps.count2 = 1;
            bulletProps.flags = 0x208;
            bulletProps.aimMode = 3;
            bulletProps.soundOverride = SOUND_25;
            if (timerMod2 != 0)
            {
                bulletProps.AddDirChangeCommand(0, 0, 60, 1, 0.0f, 3.1f);
            }
            g_BulletManager.SpawnBulletPattern(&bulletProps);
        }
    }
}

void EnemyEclInstr::ExInsSpawnBulletsWithDirChange2(Enemy *enemy, EclRawInstr *instr)
{
    (void)instr;

    Bullet *bullet;
    i32 timerMod3;
    EnemyBulletShooter bulletProps;
    i32 i;

    if (enemy->timer.current % 3 == 2)
    {
        return;
    }

    timerMod3 = enemy->timer.current % 3;
    for (i = 0; i < BulletManager::kBulletCapacity; i++)
    {
        bullet = g_BulletManager.BulletAt(i);
        PSP_ECL_SKIP_UNTRACKED_BULLET(i);
        if (bullet->state == BULLET_INACTIVE)
        {
            continue;
        }
        if ((bullet->exFlags & 0x40U) == 0 && bullet->pos.y < 320.0f &&
            bullet->sprites.spriteBullet.sprite->heightPx > 60.0f)
        {
            bulletProps.position = bullet->pos;
            if (timerMod3 != 0)
            {
                bulletProps.sprite = 1;
            }
            else
            {
                bulletProps.sprite = 3;
            }
            bulletProps.spriteOffset = bullet->spriteOffset == 2 ? 10 : 13;
            bulletProps.angle1 = g_Rng.GetRandomFloatInRange(ZUN_2PI) - ZUN_PI;
            bulletProps.angle2 = -ZUN_PI;
            if (timerMod3 != 0)
            {
                bulletProps.speed1 = 1.2f;
            }
            else
            {
                bulletProps.speed1 = 0.8f;
            }

            bulletProps.count1 = 1;
            bulletProps.count2 = 1;
            bulletProps.flags = 0x208;
            bulletProps.aimMode = 3;
            bulletProps.soundOverride = SOUND_25;
            if (timerMod3 != 0)
            {
                bulletProps.AddDirChangeCommand(0, 0, 40, 1, 0.0f, 2.9f);
            }
            g_BulletManager.SpawnBulletPattern(&bulletProps);
        }
    }
}

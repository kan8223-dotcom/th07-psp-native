#pragma once

#if defined(TH07_PSP_BULLET_COLLISION_BROADPHASE)

#include <cmath>

// A conservative front gate for BulletManager's two canonical Player AABB
// calls.  True means both calls are guaranteed to return zero; false retains
// the original functions and all of their observable side effects.  On true,
// the caller must still reproduce the canonical ITEM_POINT_BULLET assignment.
// grazeCallCanObserve may be false only when CheckGraze cannot reach ScoreGraze
// (for example, the timer/grazed gate or DEAD/SPAWNING player state).  Active
// bomb volumes and PLAYER_STATE_BORDER are deliberately left to the canonical
// path.
inline bool Th07PspBulletCollisionDefinitelyClear(
    float posX, float posY, float sizeX, float sizeY,
    bool grazeCallCanObserve, bool borderActive, int bombClearHighWater,
    float hitLeft, float hitTop, float hitRight, float hitBottom,
    float grazeLeft, float grazeTop, float grazeRight, float grazeBottom)
{
    if (borderActive || bombClearHighWater != 0 || sizeX < 0.0f ||
        sizeY < 0.0f || !std::isfinite(posX) || !std::isfinite(posY) ||
        !std::isfinite(sizeX) || !std::isfinite(sizeY))
    {
        return false;
    }

    const float halfX = sizeX / 2.0f;
    const float halfY = sizeY / 2.0f;
    const float bulletLeft = posX - halfX;
    const float bulletTop = posY - halfY;
    const float bulletRight = posX + halfX;
    const float bulletBottom = posY + halfY;
    if (!std::isfinite(bulletLeft) || !std::isfinite(bulletTop) ||
        !std::isfinite(bulletRight) || !std::isfinite(bulletBottom))
    {
        return false;
    }

    const bool hitboxSeparate =
        hitLeft > bulletRight || hitRight < bulletLeft ||
        hitTop > bulletBottom || hitBottom < bulletTop;
    if (!hitboxSeparate)
    {
        return false;
    }

    if (!grazeCallCanObserve)
    {
        return true;
    }

    const float expandedLeft = bulletLeft - 20.0f;
    const float expandedTop = bulletTop - 20.0f;
    const float expandedRight = bulletRight + 20.0f;
    const float expandedBottom = bulletBottom + 20.0f;
    if (!std::isfinite(expandedLeft) || !std::isfinite(expandedTop) ||
        !std::isfinite(expandedRight) || !std::isfinite(expandedBottom))
    {
        return false;
    }
    return grazeLeft > expandedRight || grazeRight < expandedLeft ||
           grazeTop > expandedBottom || grazeBottom < expandedTop;
}

#endif

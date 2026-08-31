#pragma once

#include <SDL2/SDL_video.h>

#include "ZunResult.hpp"
#include "inttypes.hpp"

typedef enum RenderResult
{
    RENDER_RESULT_EXIT_SUCCESS_2 = -1,
    RENDER_RESULT_KEEP_RUNNING = 0,
    RENDER_RESULT_EXIT_SUCCESS = 1,
    RENDER_RESULT_EXIT_ERROR = 2
} RenderResult;

struct GameWindow
{
    static i32 ChecksumExecutable();
    static ZunResult CreateGameWindow();
    static ZunResult InitInterface();
    static ZunResult InitRendering();
    static void Present();
    RenderResult Render();
    static void ResetRenderState();

    SDL_Window *window;
    i32 isAppClosing;
    i32 isAppActive;
    i32 isAppInactive;
    i8 curFrame;
    // pad 3
    i64 frequency;
    bool usesRelativePath;
    // pad 3
    u32 screen_save_active;
    u32 low_power_active;
    u32 power_off_active;
};

extern GameWindow g_GameWindow;

#if defined(TH07_PSP_BULLET_WARM_QUEUE) || \
    defined(TH07_PSP_ME_RENDER_PERFORMANCE)
// The warm queue moves the next draw's idempotent Bullet/VM render preparation
// into calc priority 12.  It may do so only when the next loop is guaranteed to
// draw at 60 Hz; fixed-30 skipped draws and a SELECT transition both require
// the untouched draw-time path.
bool Th07PspCanCommitBulletWarmQueue();
#endif

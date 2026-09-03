#include "GameWindow.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "AnmManager.hpp"
#if defined(TH07_PSP_ME_RENDER_WORKER)
#include "BulletManager.hpp"
#endif
#include "Chain.hpp"
#include "Controller.hpp"
#include "FileSystem.hpp"
#include "GameErrorContext.hpp"
#include "ScreenEffect.hpp"
#include "SoundPlayer.hpp"
#include "Stage.hpp"
#include "Supervisor.hpp"
#if defined(TH07_PSP_BULLET_WARM_QUEUE) || \
    defined(TH07_PSP_ME_RENDER_PERFORMANCE)
#include "ReplayManager.hpp"
#endif
#if !defined(TH07_PSP)
#include "graphics/Gles.hpp"
#else
#include <pspdisplay.h>
#if defined(TH07_PSP_PERF_DENSE_SLICE)
#include <pspkernel.h>
#endif

#include "graphics/PspGuGraphics.hpp"
#include "fileio.hpp"
#endif
#if !defined(TH07_PSP)
#include "graphics/Software.hpp"
#endif
#include "graphics/ZunGraphics.hpp"

GameWindow g_GameWindow;
i32 g_FrameCount;
f64 g_LastFrameTime;
u64 g_LastPerfCounter;

#if defined(TH07_PSP)
namespace
{
bool g_PspFixed30Fps;
bool g_PspDrawNextFrame = true;
unsigned int g_PspLastPresentVcount;
}
#endif

#if defined(TH07_PSP_BULLET_WARM_QUEUE) || \
    defined(TH07_PSP_ME_RENDER_PERFORMANCE)
bool Th07PspCanCommitBulletWarmQueue()
{
    return g_GameWindow.curFrame >= 0 && !g_PspFixed30Fps &&
           !WAS_PRESSED_RAW(TH_BUTTON_FPS_TOGGLE) &&
           !ReplayManager::MayRestartCalcChainAfterBulletUpdate();
}
#endif

static GfxInit g_RenderingBackends[] = {
#if !defined(TH07_PSP)
    GlesGraphics::Init,
    SoftwareGraphics::Init,
#else
    Th07CreatePspGuBackend,
#endif
};

void GameWindow::Present()
{
    char snapshotPath[252];
    i32 i;

#if defined(TH07_PSP)
    // PSP pause capture reads the complete frame which has just been drawn.
    // Doing this before the swap avoids depending on display-buffer latch
    // timing. The old 64 MiB path captured after the swap and sampled the new
    // draw target, producing a black pause background.
    g_AnmManager->TakeScreenshotIfRequested();
    g_Supervisor.gfxDevice->SwapBuffers();
#else
    g_Supervisor.gfxDevice->SwapBuffers();

    g_AnmManager->TakeScreenshotIfRequested();
#endif
    if (WAS_PRESSED_RAW(TH_BUTTON_HOME))
    {
        std::filesystem::create_directory("snapshot");
        for (i = 0; i < 1000; i++)
        {
            sprintf(snapshotPath, "snapshot/th%.3d.bmp", i);
            if (FileSystem::CheckFileExists(snapshotPath) == 0)
            {
                break;
            }
        }
        if (i < 1000)
        {
            g_Supervisor.SnapshotScreen(snapshotPath);
        }
    }
    if (g_Supervisor.renderSkipFrames != 0)
    {
        g_Supervisor.renderSkipFrames--;
    }
}

RenderResult GameWindow::Render()
{
#if defined(TH07_PSP)
    if (!this->isAppActive)
    {
        return RENDER_RESULT_KEEP_RUNNING;
    }

    // GU presentation already blocks on vblank.  SDL's PSP performance
    // counter does not share the desktop backend's timing characteristics,
    // so the original accumulator can throttle updates to roughly 1 fps.
    // Keep the game's 30 update-only warm-up frames, then run exactly one
    // draw/update/present cycle per vblank.
    if (this->curFrame < 0)
    {
        const i32 warmupResult = g_Chain.RunCalcChain();
        g_SoundPlayer.ProcessQueues();
        if (!warmupResult)
        {
            return RENDER_RESULT_EXIT_SUCCESS;
        }
        if (warmupResult == -1)
        {
            return RENDER_RESULT_EXIT_ERROR;
        }
        this->curFrame++;
        return RENDER_RESULT_KEEP_RUNNING;
    }

    const bool drawThisFrame = !g_PspFixed30Fps || g_PspDrawNextFrame;
    if (drawThisFrame)
    {
        g_AnmManager->ResetVertexBuffer();
        g_Supervisor.fogEnabled = 255;
        g_Supervisor.DisableFog();

        // The original renderer preserves the playfield between frames for
        // several stage/screen effects.  Preserve that behavior, but initialize
        // the four HUD-frame bands in both PSP backbuffers before their
        // translucent tiles are redrawn.  Clearing the whole screen fixes the
        // frame flicker too, but destroys those playfield effects.
        const ZunViewport frameBands[] = {
            {0, 0, 640, 16, 0.0f, 1.0f},
            {0, 464, 640, 16, 0.0f, 1.0f},
            {0, 16, 32, 448, 0.0f, 1.0f},
            {416, 16, 224, 448, 0.0f, 1.0f},
        };
        const ZunViewport savedViewport = g_Supervisor.viewport;
        g_Supervisor.gfxDevice->SetClearColor({0xff000000});
        for (const ZunViewport &band : frameBands)
        {
            g_Supervisor.gfxDevice->SetViewport(band);
            g_Supervisor.gfxDevice->Clear(CLEAR_COLOR_BUFFER | CLEAR_DEPTH_BUFFER);
        }
        g_Supervisor.gfxDevice->SetViewport(savedViewport);
        g_Chain.RunDrawChain();
#if defined(TH07_PSP_PERF_DENSE_SLICE)
        const bool denseSliceActive = gTh07PspPerfDenseSliceActive != 0;
        const unsigned long long densePostFlushStartUs =
            denseSliceActive ? sceKernelGetSystemTimeWide() : 0ull;
#endif
        g_AnmManager->Flush();
#if defined(TH07_PSP_PERF_DENSE_SLICE)
        if (denseSliceActive)
        {
            Th07PspPerfAddDensePostFlushTime(sceKernelGetSystemTimeWide() -
                                             densePostFlushStartUs);
        }
#endif
        g_Supervisor.gfxDevice->BindTexture({0});

        g_Supervisor.viewport.x = 0;
        g_Supervisor.viewport.y = 0;
        g_Supervisor.viewport.width = 640;
        g_Supervisor.viewport.height = 480;
        g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);
    }

#if defined(TH07_PSP_ME_RENDER_WORKER)
    // Capture immediately before this exact pass. A priority<18 BREAK leaves
    // the serial unchanged, so an earlier warm-up completion cannot publish.
    const unsigned int meRenderCalcSerialBefore =
        Th07PspMeRenderCaptureCalcSerial();
#endif
    const i32 chainResult = g_Chain.RunCalcChain();
    g_SoundPlayer.ProcessQueues();
    if (!chainResult)
    {
        th07_psp_boot_note("calc chain exit success");
        return RENDER_RESULT_EXIT_SUCCESS;
    }
    if (chainResult == -1)
    {
        th07_psp_boot_note("calc chain exit error");
        return RENDER_RESULT_EXIT_ERROR;
    }

    const bool toggledFixed30 = WAS_PRESSED_RAW(TH_BUTTON_FPS_TOGGLE);
    if (toggledFixed30)
    {
        g_PspFixed30Fps = !g_PspFixed30Fps;
        g_PspDrawNextFrame = !g_PspFixed30Fps;
        th07_psp_boot_note(g_PspFixed30Fps ? "fixed 30fps on" : "fixed 30fps off");
    }
    else if (g_PspFixed30Fps)
    {
        g_PspDrawNextFrame = !g_PspDrawNextFrame;
    }
    else
    {
        g_PspDrawNextFrame = true;
    }

#if defined(TH07_PSP_ME_RENDER_WORKER)
    // g_PspDrawNextFrame now describes the next Render() call.  Fixed-30
    // update-only passes deliberately publish no ME work.
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    const unsigned long long meRenderPostCalcStartUs =
        gTh07PspPerfDenseSliceActive ? sceKernelGetSystemTimeWide() : 0ull;
#endif
    Th07PspMeRenderAfterCalc(meRenderCalcSerialBefore,
                            !g_PspFixed30Fps || g_PspDrawNextFrame);
#if defined(TH07_PSP_PERF_DENSE_SLICE)
    if (gTh07PspPerfDenseSliceActive)
    {
        Th07PspPerfAddMerwPostCalcTime(
            sceKernelGetSystemTimeWide() - meRenderPostCalcStartUs);
    }
#endif
#endif

    if (drawThisFrame)
    {
        if (g_PspFixed30Fps && sceDisplayGetVcount() == g_PspLastPresentVcount)
        {
            // Fixed-30 runs the skipped update and the rendered update as a
            // pair, then presents no earlier than the second vblank.  This
            // gives the pair the full 33 ms budget while retaining two 60 Hz
            // simulation ticks per displayed frame.
            sceDisplayWaitVblankStart();
        }
        Present();
        g_PspLastPresentVcount = sceDisplayGetVcount();
    }
    g_FrameCount++;
    return RENDER_RESULT_KEEP_RUNNING;
#else
    f64 perfDiff;
    u64 perfCounter;
    i32 chainRes;

    if (!this->isAppActive)
    {
        return RENDER_RESULT_KEEP_RUNNING;
    }

    if (this->curFrame == 0)
    {
    begin_loop:
        if ((i32)g_Supervisor.cfg.frameskipConfig <= (i32)this->curFrame)
        {
            g_AnmManager->ResetVertexBuffer();
            g_Supervisor.fogEnabled = 255;
            g_Supervisor.DisableFog();
            g_Chain.RunDrawChain();
            g_AnmManager->Flush();
            g_Supervisor.gfxDevice->BindTexture({0});
        }

        g_AnmManager->Flush();
        g_Supervisor.viewport.x = 0;
        g_Supervisor.viewport.y = 0;
        g_Supervisor.viewport.width = 640;
        g_Supervisor.viewport.height = 480;
        g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);

        chainRes = g_Chain.RunCalcChain();
        g_SoundPlayer.ProcessQueues();

        if (!chainRes)
        {
            return RENDER_RESULT_EXIT_SUCCESS;
        }
        if (chainRes == -1)
        {
            return RENDER_RESULT_EXIT_ERROR;
        }

        this->curFrame++;
    }

    if (g_Supervisor.VsyncEnabled())
    {
        if (this->curFrame != 0)
        {
            perfCounter = SDL_GetPerformanceCounter();
            perfDiff = (f64)(perfCounter - g_LastPerfCounter) / (f64)g_GameWindow.frequency;

            if (perfDiff < 0.0)
            {
                g_LastPerfCounter = perfCounter;
            }

            if (perfDiff >= (1.0 / 60.0) || g_GameWindow.usesRelativePath)
            {
                u64 frameTicks = g_GameWindow.frequency / 60.0;

                while (perfDiff >= (1.0 / 60.0))
                {
                    g_LastPerfCounter += frameTicks;
                    perfDiff -= (1.0 / 60.0);
                }

                if ((i32)g_Supervisor.cfg.frameskipConfig < (i32)this->curFrame)
                {
                    goto LAB_00434a18;
                }

                goto begin_loop;
            }
        }
    }

    if (!g_Supervisor.VsyncEnabled())
    {
        if ((i32)g_Supervisor.cfg.frameskipConfig >= (i32)this->curFrame)
        {
            Present();
            goto begin_loop;
        }

    LAB_00434a18:
        Present();
        this->curFrame = 0;
        g_FrameCount++;
    }

    return RENDER_RESULT_KEEP_RUNNING;
#endif
}

ZunResult GameWindow::InitInterface()
{
    for (auto gfxInit : g_RenderingBackends)
    {
        g_Supervisor.gfxDevice = gfxInit();
        if (g_Supervisor.gfxDevice)
        {
            g_Supervisor.flags |= 2;
            g_Supervisor.lockableBackBuffer = 1;
            return ZUN_SUCCESS;
        }
    }

    g_GameErrorContext.Fatal("Direct3D オブジェクトは何故か作成出来なかった\n");
    return ZUN_ERROR;
}

ZunResult GameWindow::CreateGameWindow()
{
#if defined(TH07_PSP)
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) < 0)
    {
        g_GameErrorContext.Fatal("SDL の初期化が失敗しました\n");
        return ZUN_ERROR;
    }
    g_GameWindow.window = nullptr;
    g_GameWindow.isAppActive = 1;
    g_GameWindow.isAppInactive = 0;
    g_LastPerfCounter = SDL_GetPerformanceCounter();
    return ZUN_SUCCESS;
#else
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        g_GameErrorContext.Fatal("Direct3D オブジェクトは何故か作成出来なかった\n");
        return ZUN_ERROR;
    }

    u32 flags = SDL_WINDOW_SHOWN;
#if !defined(TH07_PSP)
    flags |= SDL_WINDOW_OPENGL;
#endif
    if (!g_Supervisor.cfg.windowed)
    {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    g_GameWindow.isAppActive = 1;
    g_GameWindow.isAppInactive = 0;
    g_LastPerfCounter = SDL_GetPerformanceCounter();

#if !defined(TH07_PSP)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    g_GameWindow.window =
        SDL_CreateWindow("東方妖々夢　〜 Perfect Cherry Blossom. ver 1.00b",
                         SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
#if defined(TH07_PSP)
                         480, 272, flags);
#else
                         640, 480, flags);
#endif
    if (!g_GameWindow.window)
    {
        Supervisor::DebugPrint("sdl window create failed: %s\n", SDL_GetError());
        return ZUN_ERROR;
    }

    SDL_RaiseWindow(g_GameWindow.window);
    return ZUN_SUCCESS;
#endif
}

ZunResult GameWindow::InitRendering()
{
    ZunVec3 pEye;
    ZunVec3 pAt;
    ZunVec3 pUp;
    f32 fov;
    f32 aspectRatio;
    f32 halfWidth;
    f32 halfHeight;
    f32 halfCameraDistance;

    halfWidth = 320.0f;
    halfHeight = 240.0f;
    aspectRatio = 1.3333334f;
    fov = 0.5235988f;
    halfCameraDistance = halfHeight / tanf(fov / 2.0f);
    pUp.x = 0.0f;
    pUp.y = 1.0f;
    pUp.z = 0.0f;
    pAt.x = halfWidth;
    pAt.y = -halfHeight;
    pAt.z = 0.0f;
    pEye.x = halfWidth;
    pEye.y = -halfHeight;
    pEye.z = -halfCameraDistance;
    g_Supervisor.viewMatrix.LookAtLH(&pEye, &pAt, &pUp);
    g_Supervisor.projectionMatrix.PerspectiveFovLH(fov, aspectRatio, 100.0f, 10000.0f);

    g_Supervisor.gfxDevice->SetTransformMatrix(MATRIX_VIEW, g_Supervisor.viewMatrix);
    g_Supervisor.gfxDevice->SetTransformMatrix(MATRIX_PROJECTION, g_Supervisor.projectionMatrix);

    g_Supervisor.viewport.x = 0;
    g_Supervisor.viewport.y = 0;
    g_Supervisor.viewport.width = 640;
    g_Supervisor.viewport.height = 480;
    g_Supervisor.viewport.minZ = 0.0f;
    g_Supervisor.viewport.maxZ = 1.0f;
    g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);

    ResetRenderState();
    ScreenEffect::SetViewport(0xff000000);
    g_GameWindow.isAppClosing = 0;
    g_Supervisor.lastFrameTime = 0;
    g_Supervisor.cfg.colorMode16bit = 0;

    return ZUN_SUCCESS;
}

void GameWindow::ResetRenderState()
{
    ZunColor fogColor;

    if (!g_Supervisor.cfg.disableZBuffer)
    {
        g_Supervisor.gfxDevice->Enable(CAPS_DEPTH_TEST);
    }
    else
    {
        g_Supervisor.gfxDevice->Disable(CAPS_DEPTH_TEST);
    }

    g_Supervisor.gfxDevice->Enable(CAPS_BLEND);
    g_Supervisor.gfxDevice->SetBlendMode(BLEND_ALPHA, BLEND_ALPHA);
    g_Supervisor.gfxDevice->SetDepthFunc(DEPTH_FUNC_ALWAYS);
    g_Supervisor.gfxDevice->Enable(CAPS_ALPHA_TEST);
    g_Supervisor.gfxDevice->SetAlphaTestRef(4);

    if (!g_Supervisor.cfg.disableFog)
    {
        g_Supervisor.gfxDevice->Enable(CAPS_FOG);
    }
    else
    {
        g_Supervisor.gfxDevice->Disable(CAPS_FOG);
    }

    fogColor.color = 0xffa0a0a0;
    g_Supervisor.gfxDevice->SetFogColor(fogColor);
    g_Supervisor.gfxDevice->SetFogRange(1000.0f, 5000.0f);

    g_Supervisor.gfxDevice->SetTextureFilter();
    if (g_AnmManager)
    {
        g_AnmManager->SetBlendMode(255);
        g_AnmManager->SetColorOp(255);
        g_AnmManager->SetVertexShader(255);
        g_AnmManager->SetTexture(0);
        g_AnmManager->SetCameraMode(255);
    }
    g_Stage.renderStateWasReset = 1;
}

i32 GameWindow::ChecksumExecutable()
{
    // the game uses exechecksum and exesize to write to replay and score files about the program
    // that produced that file, and in the original executable those are compared to values in the
    // verfile to check if they're "good" untampered files. obviously it's not gonna match, so we
    // just return these hardcoded values.
    g_Supervisor.exeSize = 650752;
    return g_Supervisor.exeChecksum = 0xaec5445c;
}

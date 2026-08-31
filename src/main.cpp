#include <SDL2/SDL.h>
#include <cstdio>

// pull in gameerrorcontext::flush before anmmanager::releasesurfaces
#include "AnmManager.hpp"
#include "Chain.hpp"
#include "Controller.hpp"
#include "BulletManager.hpp"
#include "EffectManager.hpp"
#include "EnemyManager.hpp"
#include "FileSystem.hpp"
#include "GameErrorContext.hpp"
#include "GameManager.hpp"
#include "GameWindow.hpp"
#include "Gui.hpp"
#include "ItemManager.hpp"
#include "ResultScreen.hpp"
#include "SoundPlayer.hpp"
#include "Supervisor.hpp"
#include "Stage.hpp"
#include "ZunResult.hpp"
#include "dxutil.hpp"

#if defined(TH07_PSP)
#include "fileio.hpp"
#if defined(TH07_PSP_ME_RENDER_WORKER)
#include "audio_me.h"
#endif
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
#include "graphics/PspGuGraphics.hpp"
#endif
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
#include "ge4_game_bridge.hpp"
#endif
#if defined(TH07_PSP_SHIKIGAMI)
#include "shikigami_th07.h"
#endif
#if defined(TH07_PSP_1000)
#include "psp1000_arena.hpp"
#endif
#include <pspkernel.h>
extern "C" void th07_psp_platform_init();
extern "C" int th07_psp_platform_running();
#endif

#if defined(TH07_PSP_SHIKIGAMI)
constexpr u32 kShikigamiPublishPeriodFrames = 3;

static void PublishShikigamiFrame(u32 frameNumber)
{
    Th07ShikigamiFrameSnapshot snapshot{};
    snapshot.frame_number = frameNumber;
    snapshot.fps_x10 = static_cast<u32>(g_Supervisor.curFps > 0
                                            ? g_Supervisor.curFps * 10
                                            : 0);
    snapshot.supervisor_state = g_Supervisor.curState;
    snapshot.stage = g_GameManager.currentStage;
    snapshot.spell_index = g_EnemyManager.spellcardInfo.isActive
                               ? g_EnemyManager.spellcardInfo.spellcardIdx
                               : -1;
    if (g_GameManager.demo)
        snapshot.game_flags |= TH07_SHIKIGAMI_GAME_DEMO;
    if (g_GameManager.replay)
        snapshot.game_flags |= TH07_SHIKIGAMI_GAME_REPLAY;
    if (g_GameManager.isPaused || g_GameManager.isInPauseMenu)
        snapshot.game_flags |= TH07_SHIKIGAMI_GAME_PAUSED;
    if (g_Gui.BossPresent())
        snapshot.game_flags |= TH07_SHIKIGAMI_GAME_BOSS;
    if (g_EnemyManager.spellcardInfo.isActive || g_Stage.spellCardState > 0)
        snapshot.game_flags |= TH07_SHIKIGAMI_GAME_SPELL;
    if (g_Gui.HasCurrentMsgIdx())
        snapshot.game_flags |= TH07_SHIKIGAMI_GAME_DIALOGUE;
    th07_psp_audio_shikigami_snapshot(&snapshot.audio);
    th07_shikigami_publish_frame(&snapshot);
}
#endif

bool AnmManager::TakeScreenshotIfRequested()
{
    if (this->screenshotTextureId < 0)
    {
        return false;
    }

    const bool captured =
        TakeScreenshot(this->screenshotTextureId, this->screenshotSrcLeft, this->screenshotSrcTop,
                       this->screenshotSrcWidth, this->screenshotSrcHeight, this->screenshotDstLeft,
                       this->screenshotDstTop, this->screenshotDstWidth, this->screenshotDstHeight);
    this->screenshotTextureId = -1;
    return captured;
}

int main(int argc, char *argv[])
{
#if defined(TH07_PSP)
    th07_psp_fileio_set_launch_path(argc > 0 ? argv[0] : nullptr);
    th07_psp_fileio_init();
#if defined(TH07_PSP_1000)
    if (!th07_psp_1000_arena_init())
        return 1;
#endif
    th07_psp_platform_init();
    th07_psp_boot_note("platform initialized");
#if defined(TH07_PSP_1000)
    th07_psp_boot_notef("BUILD PSP1000 pools E%d B%d I%d F%d FREE%uK",
                        EnemyManager::kEnemyCapacity, BulletManager::kBulletCapacity,
                        ItemManager::kItemCapacity, EffectManager::kNormalEffectCapacity,
                        static_cast<unsigned int>(sceKernelTotalFreeMemSize() / 1024u));
#endif
#endif
    (void)argc;
    (void)argv;

    i32 res;
#if defined(TH07_PSP_ME_RENDER_WORKER)
    // Keep this declaration ahead of every legacy goto target. The worker
    // profile initializes it only after audio/rendering are ready.
    int meRenderInit = 0;
#endif
#if defined(TH07_PSP)
    bool firstRender = true;
#if defined(TH07_PSP_SHIKIGAMI)
    u32 shikigamiFrame = 0;
#endif
#endif

    res = RENDER_RESULT_KEEP_RUNNING;

    if (g_Supervisor.LoadConfig("th07.cfg") != ZUN_SUCCESS)
    {
        goto stop;
    }
#if defined(TH07_PSP)
    th07_psp_boot_note("config loaded");
#endif

    GameWindow::ChecksumExecutable();
    g_GameWindow.frequency = SDL_GetPerformanceFrequency();

#if !defined(TH07_PSP_MECC_BGM_384K) && !defined(TH07_PSP_MECC_AUDIO_4M)
start:
#endif
    if (GameWindow::CreateGameWindow())
    {
        goto stop;
    }
#if defined(TH07_PSP)
    th07_psp_boot_note("window initialized");
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
    // InitInterface constructs the GU backend, whose first-GU-idle hook is the
    // only caller of th07_psp_ge4_enable_after_gu_idle().  The bridge must
    // therefore be prepared before InitInterface, or the hook no-ops and the
    // aperture never widens (the confirmed R6 hardware failure).  Prepare only
    // validates/locks at 2 MiB and has no GU dependency.
    th07_psp_ge4_prepare();
#endif
#endif

    if (GameWindow::InitInterface())
    {
        goto stop;
    }
#if defined(TH07_PSP)
    th07_psp_boot_note("interface initialized");
#endif

    if (GameWindow::InitRendering())
    {
        goto stop;
    }
#if defined(TH07_PSP)
    th07_psp_boot_note("rendering initialized");
#endif

#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
    if (g_SoundPlayer.InitializeSound() != ZUN_SUCCESS)
    {
        th07_psp_boot_note("MECC audio initialization failed");
        goto stop;
    }
#else
    g_SoundPlayer.InitializeSound();
#endif
#if defined(TH07_PSP_ME_RENDER_WORKER)
    // Main-RAM audio deliberately left ME idle.  M-ME0 is the only profile
    // which takes process-wide custom-core ownership for render research.
    meRenderInit = th07_psp_me_audio_init();
    if (meRenderInit < 0)
    {
        th07_psp_boot_note("MERW M0A takeover unsafe -> cold reboot");
        goto stop;
    }
    if (meRenderInit == 0)
    {
        Th07PspMeRenderSetAvailable(false);
        th07_psp_boot_note("MERW M0 OFF (not real PSP-3000)");
    }
    else
    {
        Th07PspMeRenderBenchSummary summary{};
        Th07PspMeRenderBenchCase
            cases[TH07_PSP_ME_RENDER_BENCH_CASES]{};
        th07_psp_me_render_bench_snapshot(
            &summary, cases, TH07_PSP_ME_RENDER_BENCH_CASES);
        const bool benchOk =
            summary.ready != 0u && summary.caseCount != 0u &&
            summary.caseCount <= TH07_PSP_ME_RENDER_BENCH_CASES &&
            summary.passedCases == summary.caseCount &&
            summary.failedCases == 0u && summary.mismatchWords == 0u &&
            summary.timeouts == 0u && summary.boundsFaults == 0u &&
            summary.guardFaults == 0u && summary.protocolFaults == 0u &&
            summary.meEdramBytes == 0u;
        th07_psp_boot_notef(
            "MERW M0A G%u C%u/%u MM%u TO%u BD%u GD%u PF%u INIT%uus "
            "PRX%u W%uus/R%d L%uus/R%d OUT%u EDRAM%u",
            benchOk ? 1u : 0u, summary.passedCases, summary.caseCount,
            summary.mismatchWords, summary.timeouts, summary.boundsFaults,
            summary.guardFaults, summary.protocolFaults, summary.takeoverUs,
            summary.prxBytes, summary.prxWriteUs, summary.prxWriteResult,
            summary.prxLoadUs, summary.prxLoadResult, summary.maxOutputBytes,
            summary.meEdramBytes);
        for (u32 index = 0u; index < summary.caseCount; ++index)
        {
            const Th07PspMeRenderBenchCase &entry = cases[index];
            th07_psp_boot_notef(
                "MERW M0C I%u N%u S%u K%u G%u MM%u WB%u SUB%u WAIT%u INV%u CP%u MIC%u MKC%u MWC%u",
                index, entry.recordCount, entry.inputStride,
                entry.cacheMode, entry.result, entry.mismatchWords,
                entry.scWritebackUs, entry.scSubmitUs,
                entry.dispatchWaitUs, entry.scInvalidateUs,
                entry.scCopyUs, entry.meInvalidateCycles,
                entry.meKernelCycles, entry.meWritebackCycles);
        }
        if (!benchOk)
        {
            Th07PspMeRenderSetAvailable(false);
            th07_psp_boot_note("MERW M0A FAILED -> STOP / COLD REBOOT");
            goto stop;
        }
        Th07PspMeRenderSetAvailable(true);
#if defined(TH07_PSP_ME_RENDER_CORRECTNESS)
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
#if defined(TH07_PSP_ME_RENDER_PERFORMANCE)
#if defined(TH07_PSP_ME_RENDER_RAW_LIVE)
#if defined(TH07_PSP_ME_RENDER_DIRECT_LIST)
#if defined(TH07_PSP_ME_BULLET_COMPACT_UPDATE)
        th07_psp_boot_note(
#if defined(TH07_PSP_ME_BULLET_TRUSTED_SEED_AUTHORITY)
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            "MERW I-ME8 ALL-IN; TRUSTED-SEED=1; "
#elif defined(TH07_PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP)
            "MERW I-ME8R NO-EFFECT; TRUSTED-SEED=1; "
#else
#if defined(TH07_SHIKIGAMI_BUILD_ID) && \
    TH07_SHIKIGAMI_BUILD_ID == 0x2608311bu
            "MERW I-ME8R3 CACHE-SAFE; TRUSTED-SEED=1; "
#else
            "MERW I-ME8R2 NO-EFFECT NO-LEAN; TRUSTED-SEED=1; "
#endif
#endif
#else
            "MERW I-ME7 COMPACT-UPDATE + LIST-LIVE ME ON; CONTIGUOUS "
            "MAIN-RAM SEED, ASYNC/NONBLOCKING, SLOT-JIT FALLBACK; "
#endif
#if defined(TH07_PSP_ME_ITEM_RENDER_STREAM)
            "ITEM-DRAW-ME=1; "
#else
            "ITEM-DRAW-ME=0; "
#endif
#if defined(TH07_PSP_ME_EFFECT_RENDER_STREAM)
            "EFFECT0/3-ME=1; "
#else
            "EFFECT0/3-ME=0; "
#endif
#if defined(TH07_PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP)
            "LEAN-CACHE=1; "
#else
            "LEAN-CACHE=0; "
#endif
#if defined(TH07_PSP_GUI_TILE_BATCH)
            "GUI-TILE=1; "
#else
            "GUI-TILE=0; "
#endif
            "ME_EDRAM=UNUSED 0/0");
#elif defined(TH07_PSP_ME_BULLET_FAST_UPDATE)
        th07_psp_boot_note(
            "MERW I-ME6 BULLET-UPDATE + LIST-LIVE ME ON; MOTION/BOUNDS/"
            "SAFE-COLLISION ME, SIDE-EFFECTS SC; ME_EDRAM=UNUSED 0/0");
#else
        th07_psp_boot_note(
            "MERW I-ME5 LIST-LIVE BULLET ME GE CONSUME ON; READY+RUN "
            "VALIDATE=1 HASH=0 SC FALLBACK=1 ME_AUDIO_JOBS=0 "
            "ME_EDRAM=UNUSED 0/0");
#endif
#else
        th07_psp_boot_note(
            "MERW I-ME4 RAW-LIVE BULLET ME GE CONSUME ON; READY+RUN "
            "VALIDATE=1 HASH=0 SC FALLBACK=1 ME_AUDIO_JOBS=0 "
            "ME_EDRAM=UNUSED 0/0");
#endif
#else
        th07_psp_boot_note(
            "MERW I-ME3 FUSED PERFORMANCE GE CONSUME ON; READY+RUN VALIDATE=1 "
            "HASH=0 SC FALLBACK=1 ME_AUDIO_JOBS=0 ME_EDRAM=UNUSED 0/0");
#endif
#else
        th07_psp_boot_note(
            "MERW I-ME2 GE CONSUME ON; WHOLE-FRAME VALIDATE=1 SC FALLBACK=1 "
            "ME_AUDIO_JOBS=0 ME_EDRAM=UNUSED 0/0");
#endif
#else
        th07_psp_boot_note(
            "MERW I-ME1 CORRECTNESS STREAM ON; SC DRAW=1 GE CONSUME=0 "
            "ME_AUDIO_JOBS=0 ME_EDRAM=UNUSED 0/0");
#endif
#else
        th07_psp_boot_note(
            "MERW M0B SHADOW SYNTH4 ON; GE CONSUME=0 "
            "ME_AUDIO_JOBS=0 ME_EDRAM=UNUSED 0/0");
#endif
    }
#endif
    Controller::ResetKeyboard();
    g_AnmManager = new AnmManager();
    if (!g_Supervisor.cfg.windowed)
    {
        SDL_ShowCursor(SDL_DISABLE);
    }
    res = g_Supervisor.RegisterChain();
    if (res != ZUN_SUCCESS)
    {
        if (res == ZUN_ERROR)
        {
            goto cleanup;
        }
        res = RENDER_RESULT_EXIT_ERROR;
        goto cleanup;
    }
#if defined(TH07_PSP)
    th07_psp_boot_note("game chain registered");
#endif
    res = RENDER_RESULT_KEEP_RUNNING;
    g_GameWindow.curFrame = -30;
#if defined(TH07_PSP)
    th07_psp_boot_note("main loop entered");
#endif
    while (!g_GameWindow.isAppClosing
#if defined(TH07_PSP)
           && th07_psp_platform_running()
#endif
    )
    {
        SDL_Event e;

        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
            case SDL_WINDOWEVENT:
                switch (e.window.event)
                {
                case SDL_WINDOWEVENT_FOCUS_GAINED:
                    g_GameWindow.isAppActive = 1;
                    g_GameWindow.isAppInactive = 0;
                    if (!g_Supervisor.cfg.windowed)
                    {
                        SDL_ShowCursor(SDL_DISABLE);
                    }
                    break;
                case SDL_WINDOWEVENT_FOCUS_LOST:
                    g_GameWindow.isAppActive = 0;
                    g_GameWindow.isAppInactive = 1;
                    SDL_ShowCursor(SDL_ENABLE);
                    break;
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!g_Supervisor.controller)
                {
                    g_Supervisor.controller = SDL_GameControllerOpen(e.cdevice.which);
                }
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (g_Supervisor.controller)
                {
                    SDL_Joystick *joy = SDL_GameControllerGetJoystick(g_Supervisor.controller);

                    if (SDL_JoystickInstanceID(joy) == e.cdevice.which)
                    {
                        SDL_GameControllerClose(g_Supervisor.controller);
                        g_Supervisor.controller = nullptr;
                    }
                }
                break;
            case SDL_QUIT:
                g_GameWindow.isAppClosing = true;
                break;
            }
        }

        res = g_GameWindow.Render();
#if defined(TH07_PSP)
#if defined(TH07_PSP_SHIKIGAMI)
        if (res == RENDER_RESULT_KEEP_RUNNING)
        {
            const u32 frameNumber = ++shikigamiFrame;
            if (frameNumber % kShikigamiPublishPeriodFrames == 0u)
            {
                PublishShikigamiFrame(frameNumber);
            }
        }
#endif
        if (firstRender)
        {
            th07_psp_boot_note("first render complete");
#if defined(TH07_PSP_SHIKIGAMI)
            if (res == RENDER_RESULT_KEEP_RUNNING)
            {
                th07_shikigami_start();
            }
#endif
            firstRender = false;
        }
#endif
        if (res != RENDER_RESULT_KEEP_RUNNING)
        {
            break;
        }
        g_Supervisor.flags = g_Supervisor.flags & 0xffffffef;
    }

cleanup:
    if (g_GameManager.plst.base.magic != 0)
    {
        ResultScreen::RegisterChain(2);
    }
    g_Chain.Release();
    while (g_SoundPlayer.ProcessQueues())
        ;

stop:
#if defined(TH07_PSP_PERF_DIAG) && !defined(TH07_PSP_SHIKIGAMI)
    // Also preserve title/menu windows and early-exit diagnostics when no
    // GameManager deletion boundary was reached.
    th07_psp_perf_log_flush();
#endif
#if defined(TH07_PSP) && defined(TH07_PSP_SHIKIGAMI) && \
    defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    // Stop the network observer while the last valid GU frame is still on
    // screen.  Waiting until after renderer/GE4 teardown exposed the restored
    // aperture as vertical stripes whenever PSP networking took its bounded
    // shutdown timeout.
    th07_shikigami_shutdown();
#if defined(TH07_PSP_PERF_DIAG)
    // The observer is now unable to read while the RAM log is compacted to
    // the recovery file.
    th07_psp_perf_log_flush();
#endif
#endif
#if defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    // The render worker cannot stop while GE still owns one of its output
    // slots.  Fence the final list while both the renderer and ME service are
    // alive, then let SoundPlayer's process-owner teardown drain the worker.
    Th07PspFenceMeRenderBeforeMeShutdown();
#endif
    g_SoundPlayer.Release();
    delete g_AnmManager;
    g_AnmManager = NULL;

    SAFE_DELETE(g_Supervisor.gfxDevice);
#if defined(TH07_PSP_GE_PORTRAIT_CACHE)
    // SoundPlayer::Release stopped ME first; the renderer destructor has now
    // synchronized GE, released every upper allocation and called sceGuTerm.
    th07_psp_ge4_shutdown();
#endif
    if (g_GameWindow.window)
    {
        SDL_DestroyWindow(g_GameWindow.window);
        g_GameWindow.window = NULL;
    }
    SDL_ShowCursor(SDL_ENABLE);
    if (res == RENDER_RESULT_EXIT_ERROR)
    {
        g_GameErrorContext.m_BufferEnd = g_GameErrorContext.m_Buffer;
        *g_GameErrorContext.m_BufferEnd = '\0';
#if defined(TH07_PSP_MECC_BGM_384K) || defined(TH07_PSP_MECC_AUDIO_4M)
        g_GameErrorContext.Log(
            "MECC診断版は同一プロセス内で再起動できません。完全電源OFF後に再起動してください\n");
        th07_psp_boot_note("MECC in-process restart denied; cold reboot required");
#else
        g_GameErrorContext.Log("再起動を要するオプションが変更されたので再起動します\n");
        goto start;
#endif
    }
    FileSystem::WriteDataToFile("th07.cfg", &g_Supervisor.cfg, sizeof(GameConfiguration));
    g_GameErrorContext.Flush();
#if defined(TH07_PSP)
#if defined(TH07_PSP_SHIKIGAMI) && \
    !defined(TH07_PSP_ME_RENDER_GE_CONSUME)
    th07_shikigami_shutdown();
#endif
    th07_psp_boot_note("main exited");
#if defined(TH07_PSP_PERF_DIAG)
    th07_psp_fileio_shutdown();
#endif
#endif
    return 0;
}

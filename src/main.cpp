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
#include "ItemManager.hpp"
#include "ResultScreen.hpp"
#include "SoundPlayer.hpp"
#include "Supervisor.hpp"
#include "ZunResult.hpp"
#include "dxutil.hpp"

#if defined(TH07_PSP)
#include "fileio.hpp"
#if defined(TH07_PSP_1000)
#include "psp1000_arena.hpp"
#endif
#include <pspkernel.h>
extern "C" void th07_psp_platform_init();
extern "C" int th07_psp_platform_running();
#endif

void AnmManager::TakeScreenshotIfRequested()
{
    if (this->screenshotTextureId >= 0)
    {
        TakeScreenshot(this->screenshotTextureId, this->screenshotSrcLeft, this->screenshotSrcTop,
                       this->screenshotSrcWidth, this->screenshotSrcHeight, this->screenshotDstLeft,
                       this->screenshotDstTop, this->screenshotDstWidth, this->screenshotDstHeight);
        this->screenshotTextureId = -1;
    }
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
#if defined(TH07_PSP)
    bool firstRender = true;
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

start:
    if (GameWindow::CreateGameWindow())
    {
        goto stop;
    }
#if defined(TH07_PSP)
    th07_psp_boot_note("window initialized");
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

    g_SoundPlayer.InitializeSound();
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
        if (firstRender)
        {
            th07_psp_boot_note("first render complete");
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
    g_SoundPlayer.Release();
    delete g_AnmManager;
    g_AnmManager = NULL;

    SAFE_DELETE(g_Supervisor.gfxDevice);
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
        g_GameErrorContext.Log("再起動を要するオプションが変更されたので再起動します\n");
        goto start;
    }
    FileSystem::WriteDataToFile("th07.cfg", &g_Supervisor.cfg, sizeof(GameConfiguration));
    g_GameErrorContext.Flush();
#if defined(TH07_PSP)
    th07_psp_boot_note("main exited");
#endif
    return 0;
}

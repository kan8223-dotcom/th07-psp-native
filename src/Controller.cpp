#include "Controller.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keyboard.h>
#include <cstdio>
#if defined(TH07_PSP)
#include <pspctrl.h>
#include "fileio.hpp"
#endif

#include "Supervisor.hpp"
#include "inttypes.hpp"
#include "utils.hpp"

static u16 g_AutoFocusTimer;

#define KEY_PRESSED(scancode, thButton) (keys[scancode] ? thButton : 0)
#define JOYSTICK_MIDPOINT(min, max) ((min + max) / 2)

static const SDL_GameControllerButton g_DIToSDLButton[] = {
    SDL_CONTROLLER_BUTTON_A,
    SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_X,
    SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_BACK,
    SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_LEFTSTICK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_GUIDE,
};

u32 Controller::SetButton(u16 *outButtons, i32 controllerButton, u32 thButton)
{
    if (controllerButton < 0 || (size_t)controllerButton >= ARRAY_SIZE(g_DIToSDLButton))
    {
        return 0;
    }

    if (SDL_GameControllerGetButton(g_Supervisor.controller, g_DIToSDLButton[controllerButton]))
    {
        *outButtons |= thButton;
        return thButton;
    }

    return 0;
}

u16 Controller::GetControllerInput(u16 buttons)
{
    if (!g_Supervisor.controller)
    {
        return buttons;
    }

    u32 isShooting =
        SetButton(&buttons, g_Supervisor.cfg.controllerMapping.shootButton, TH_BUTTON_SHOOT);

    if (g_Supervisor.cfg.shotSlow)
    {
        if (isShooting)
        {
            if (g_AutoFocusTimer < 20)
            {
                g_AutoFocusTimer++;
            }
            if (g_AutoFocusTimer >= 10)
            {
                buttons |= TH_BUTTON_FOCUS;
            }
        }
        else if (g_AutoFocusTimer > 10)
        {
            g_AutoFocusTimer -= 10;
            buttons |= TH_BUTTON_FOCUS;
        }
        else
        {
            g_AutoFocusTimer = 0;
        }
    }

    SetButton(&buttons, g_Supervisor.cfg.controllerMapping.bombButton, TH_BUTTON_BOMB);
    SetButton(&buttons, g_Supervisor.cfg.controllerMapping.focusButton, TH_BUTTON_FOCUS);
    SetButton(&buttons, g_Supervisor.cfg.controllerMapping.menuButton, TH_BUTTON_MENU);
    SetButton(&buttons, g_Supervisor.cfg.controllerMapping.upButton, TH_BUTTON_UP);
    SetButton(&buttons, g_Supervisor.cfg.controllerMapping.downButton, TH_BUTTON_DOWN);
    SetButton(&buttons, g_Supervisor.cfg.controllerMapping.leftButton, TH_BUTTON_LEFT);
    SetButton(&buttons, g_Supervisor.cfg.controllerMapping.rightButton, TH_BUTTON_RIGHT);
    SetButton(&buttons, g_Supervisor.cfg.controllerMapping.skipButton, TH_BUTTON_SKIP);

    SetButton(&buttons, 7, TH_BUTTON_D);

    Sint16 x =
        SDL_GameControllerGetAxis(g_Supervisor.controller, SDL_CONTROLLER_AXIS_LEFTX) / 32.767f;
    Sint16 y =
        SDL_GameControllerGetAxis(g_Supervisor.controller, SDL_CONTROLLER_AXIS_LEFTY) / 32.767f;

    if (x > g_Supervisor.cfg.padAxisX)
    {
        buttons |= TH_BUTTON_RIGHT;
    }

    if (x < -g_Supervisor.cfg.padAxisX)
    {
        buttons |= TH_BUTTON_LEFT;
    }

    if (y > g_Supervisor.cfg.padAxisY)
    {
        buttons |= TH_BUTTON_DOWN;
    }

    if (y < -g_Supervisor.cfg.padAxisY)
    {
        buttons |= TH_BUTTON_UP;
    }

    // technically the original game never had dpad support but ehhhh
    if (SDL_GameControllerGetButton(g_Supervisor.controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
    {
        buttons |= TH_BUTTON_RIGHT;
    }
    if (SDL_GameControllerGetButton(g_Supervisor.controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
    {
        buttons |= TH_BUTTON_LEFT;
    }
    if (SDL_GameControllerGetButton(g_Supervisor.controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
    {
        buttons |= TH_BUTTON_DOWN;
    }
    if (SDL_GameControllerGetButton(g_Supervisor.controller, SDL_CONTROLLER_BUTTON_DPAD_UP))
    {
        buttons |= TH_BUTTON_UP;
    }

    return buttons;
}

static u8 g_ControllerData[32 * 4];

u8 *Controller::GetControllerState()
{
    memset(g_ControllerData, 0, sizeof(g_ControllerData));

    if (!g_Supervisor.controller)
    {
        return g_ControllerData;
    }

    for (size_t i = 0; i < ARRAY_SIZE(g_DIToSDLButton); ++i)
    {
        if (SDL_GameControllerGetButton(g_Supervisor.controller, g_DIToSDLButton[i]))
        {
            g_ControllerData[i] = 0x80;
        }
    }

    return g_ControllerData;
}

u16 Controller::GetInput()
{
#if defined(TH07_PSP)
    SceCtrlData pad{};
    const int sampleCount = sceCtrlPeekBufferPositive(&pad, 1);
    static bool loggedFirstPoll;
    if (!loggedFirstPoll)
    {
        char message[72];
        std::snprintf(message, sizeof(message), "input poll %d %08x %u,%u", sampleCount,
                      static_cast<unsigned int>(pad.Buttons), pad.Lx, pad.Ly);
        th07_psp_boot_note(message);
        loggedFirstPoll = true;
    }
    if (sampleCount <= 0)
    {
        return 0;
    }

    static bool loggedFirstInput;
    if (!loggedFirstInput && (pad.Buttons != 0 || pad.Lx < 64 || pad.Lx > 192 || pad.Ly < 64 ||
                              pad.Ly > 192))
    {
        char message[64];
        std::snprintf(message, sizeof(message), "input first %08x %u,%u",
                      static_cast<unsigned int>(pad.Buttons), pad.Lx, pad.Ly);
        th07_psp_boot_note(message);
        loggedFirstInput = true;
    }

    u16 buttons = 0;
    if (pad.Buttons & PSP_CTRL_UP) buttons |= TH_BUTTON_UP;
    if (pad.Buttons & PSP_CTRL_DOWN) buttons |= TH_BUTTON_DOWN;
    if (pad.Buttons & PSP_CTRL_LEFT) buttons |= TH_BUTTON_LEFT;
    if (pad.Buttons & PSP_CTRL_RIGHT) buttons |= TH_BUTTON_RIGHT;
    if (pad.Lx < 64) buttons |= TH_BUTTON_LEFT;
    if (pad.Lx > 192) buttons |= TH_BUTTON_RIGHT;
    if (pad.Ly < 64) buttons |= TH_BUTTON_UP;
    if (pad.Ly > 192) buttons |= TH_BUTTON_DOWN;

    // Match the proven TH6 PSP layout: Cross=shot/confirm,
    // Circle=bomb/cancel, Square/L/R=focus, Triangle=skip, Start=pause.
    if (pad.Buttons & PSP_CTRL_CROSS) buttons |= TH_BUTTON_SHOOT | TH_BUTTON_ENTER;
    if (pad.Buttons & PSP_CTRL_CIRCLE) buttons |= TH_BUTTON_BOMB;
    if (pad.Buttons & (PSP_CTRL_SQUARE | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER))
        buttons |= TH_BUTTON_FOCUS;
    if (pad.Buttons & PSP_CTRL_TRIANGLE) buttons |= TH_BUTTON_SKIP;
    if (pad.Buttons & PSP_CTRL_START) buttons |= TH_BUTTON_MENU;
    if (pad.Buttons & PSP_CTRL_SELECT) buttons |= TH_BUTTON_FPS_TOGGLE;
#if defined(TH07_PSP_DIRECT_GAME)
    // Gameplay bring-up helper: keep firing while still allowing every
    // physical PSP control to be tested normally.
    buttons |= TH_BUTTON_SHOOT;
#endif
    return buttons;
#else
    u16 buttons = 0;

    const u8 *keys = SDL_GetKeyboardState(NULL);

    buttons |= KEY_PRESSED(SDL_SCANCODE_UP, TH_BUTTON_UP);
    buttons |= KEY_PRESSED(SDL_SCANCODE_DOWN, TH_BUTTON_DOWN);
    buttons |= KEY_PRESSED(SDL_SCANCODE_LEFT, TH_BUTTON_LEFT);
    buttons |= KEY_PRESSED(SDL_SCANCODE_RIGHT, TH_BUTTON_RIGHT);
    buttons |= KEY_PRESSED(SDL_SCANCODE_KP_8, TH_BUTTON_UP);
    buttons |= KEY_PRESSED(SDL_SCANCODE_KP_2, TH_BUTTON_DOWN);
    buttons |= KEY_PRESSED(SDL_SCANCODE_KP_4, TH_BUTTON_LEFT);
    buttons |= KEY_PRESSED(SDL_SCANCODE_KP_6, TH_BUTTON_RIGHT);
    buttons |= KEY_PRESSED(SDL_SCANCODE_KP_7, TH_BUTTON_UP_LEFT);
    buttons |= KEY_PRESSED(SDL_SCANCODE_KP_9, TH_BUTTON_UP_RIGHT);
    buttons |= KEY_PRESSED(SDL_SCANCODE_KP_1, TH_BUTTON_DOWN_LEFT);
    buttons |= KEY_PRESSED(SDL_SCANCODE_KP_3, TH_BUTTON_DOWN_RIGHT);
    buttons |= KEY_PRESSED(SDL_SCANCODE_HOME, TH_BUTTON_HOME);
    buttons |= KEY_PRESSED(SDL_SCANCODE_D, TH_BUTTON_D);
    buttons |= KEY_PRESSED(SDL_SCANCODE_Z, TH_BUTTON_SHOOT);
    buttons |= KEY_PRESSED(SDL_SCANCODE_X, TH_BUTTON_BOMB);
    buttons |= KEY_PRESSED(SDL_SCANCODE_LSHIFT, TH_BUTTON_FOCUS);
    buttons |= KEY_PRESSED(SDL_SCANCODE_RSHIFT, TH_BUTTON_FOCUS);
    buttons |= KEY_PRESSED(SDL_SCANCODE_ESCAPE, TH_BUTTON_MENU);
    buttons |= KEY_PRESSED(SDL_SCANCODE_LCTRL, TH_BUTTON_SKIP);
    buttons |= KEY_PRESSED(SDL_SCANCODE_RCTRL, TH_BUTTON_SKIP);
    buttons |= KEY_PRESSED(SDL_SCANCODE_Q, TH_BUTTON_Q);
    buttons |= KEY_PRESSED(SDL_SCANCODE_S, TH_BUTTON_S);
    buttons |= KEY_PRESSED(SDL_SCANCODE_R, TH_BUTTON_RESET);
    buttons |= KEY_PRESSED(SDL_SCANCODE_RETURN, TH_BUTTON_ENTER);

    return GetControllerInput(buttons);
#endif
}

void Controller::ResetKeyboard()
{
#if !defined(TH07_PSP)
    SDL_ResetKeyboard();
#endif
}

#include <pspctrl.h>
#include <pspkernel.h>
#include <psppower.h>
#include <pspsdk.h>

#include "fileio.hpp"

PSP_MODULE_INFO("TH07 PSP Native", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-2048);

extern "C" void th07_psp_audio_set_system_suspended(int suspended);

namespace
{
volatile int gRunning = 1;

int ExitCallback(int, int, void *)
{
    gRunning = 0;
    return 0;
}

int PowerCallback(int, int powerInfo, void *)
{
    if (powerInfo & PSP_POWER_CB_SUSPENDING)
    {
        th07_psp_audio_set_system_suspended(1);
    }
    if (powerInfo & PSP_POWER_CB_RESUME_COMPLETE)
    {
        th07_psp_audio_set_system_suspended(0);
    }
    return 0;
}

int CallbackThread(SceSize, void *)
{
    const int callback = sceKernelCreateCallback("th07_exit", ExitCallback, nullptr);
    if (callback >= 0)
    {
        sceKernelRegisterExitCallback(callback);
    }
    const int powerCallback = sceKernelCreateCallback("th07_power", PowerCallback, nullptr);
    if (powerCallback >= 0)
    {
        scePowerRegisterCallback(-1, powerCallback);
    }
    // Power callbacks arrive once for suspend and again for resume.  Stay in
    // callback-aware sleep after the first wake; HOME exit flips gRunning and
    // lets this helper terminate after the engine has begun normal cleanup.
    while (gRunning)
    {
        sceKernelSleepThreadCB();
    }
    return 0;
}

int PowerKeepAliveThread(SceSize, void *)
{
    // Title demos, replays and staff rolls can run for minutes without any
    // controller input.  Match the final TH06 PSP port: keep both the LCD and
    // automatic-suspend timers alive independently of game-loop progress.
    while (gRunning)
    {
        scePowerTick(PSP_POWER_TICK_ALL);
        sceKernelDelayThread(1000 * 1000);
    }
    return 0;
}
} // namespace

extern "C" void th07_psp_platform_init()
{
    pspSdkDisableFPUExceptions();
    scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    const int thread = sceKernelCreateThread("th07_callbacks", CallbackThread, 0x11, 0x1000,
                                             PSP_THREAD_ATTR_USER, nullptr);
    if (thread >= 0)
    {
        sceKernelStartThread(thread, 0, nullptr);
    }

    const int powerThread = sceKernelCreateThread("th07_power_keepalive", PowerKeepAliveThread,
                                                   0x20, 0x1000,
                                                   PSP_THREAD_ATTR_USER, nullptr);
    if (powerThread >= 0 && sceKernelStartThread(powerThread, 0, nullptr) >= 0)
    {
        th07_psp_boot_note("power keepalive ready");
    }
}

extern "C" int th07_psp_platform_running()
{
    return gRunning;
}

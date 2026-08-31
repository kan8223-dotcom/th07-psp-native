#include "ge4_game_bridge.hpp"

#include <kubridge.h>
#include <pspge.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <pspmodulemgr.h>
#include <psppower.h>
#include <pspsdk.h>
#include <systemctrl.h>

#include <cstddef>
#include <cstdint>

extern "C" void th07_psp_boot_note(const char *message);
extern "C" void th07_psp_boot_notef(const char *format, ...);

namespace
{
constexpr char kWrapperPath[] = "./ge4wrap_texv1.prx";
constexpr char kWrapperModule[] = "th07_ge4_texbw_v1_wrap";
constexpr char kWrapperLibrary[] = "ge4wrap_texv1";
constexpr unsigned int kGetHwSizeNid = 0x2ddac688u;
constexpr unsigned int kGetModelNid = 0xbb75238fu;
constexpr unsigned int kSetSizeNid = 0x703b997bu;
constexpr unsigned int kTwoMiB = 0x00200000u;
constexpr unsigned int kFourMiB = 0x00400000u;
constexpr unsigned int kExpectedEdramBase = 0x04000000u;
constexpr int kRequiredModel = 3;
constexpr unsigned int kMaxGePolls = 50000000u;
constexpr unsigned long long kGeTimeoutUs = 5000000ull;
constexpr unsigned int kGePollDelayUs = 50u;

static_assert(sizeof(KernelCallArg) == 56u,
              "KUBridge KernelCallArg ABI size changed");
static_assert(offsetof(KernelCallArg, ret1) == 48u,
              "KUBridge ret1 ABI offset changed");

struct ExportCall
{
    int outer;
    unsigned int value;
};

SceUID gWrapper = -1;
bool gPrepareAttempted = false;
bool gEnableAttempted = false;
volatile int gPrepared = 0;
volatile int gPowerLocked = 0;
volatile int gActive = 0;
unsigned int gGetHwSizeAddress = 0;
unsigned int gGetModelAddress = 0;
unsigned int gSetSizeAddress = 0;

[[noreturn]] void ColdOffLoop(const char *reason, int result, unsigned int size);

ExportCall CallKernelExport(unsigned int address, unsigned int arg1 = 0)
{
    KernelCallArg args{};
    args.arg1 = arg1;
    // The resolved kernel address is never invoked as a user-mode function
    // pointer. KUBridge is the sole call boundary for every wrapper export.
    const int outer = kuKernelCall(
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(address)), &args);
    return ExportCall{outer, args.ret1};
}

void ClearExportAddresses()
{
    gGetHwSizeAddress = 0;
    gGetModelAddress = 0;
    gSetSizeAddress = 0;
}

bool LoadWrapper()
{
    gWrapper = pspSdkLoadStartModule(kWrapperPath, PSP_MEMORY_PARTITION_KERNEL);
    if (gWrapper < 0)
    {
        th07_psp_boot_notef("GE4 inactive: proven wrapper load rc=%08x",
                            static_cast<unsigned int>(gWrapper));
        return false;
    }
    return true;
}

bool ResolveWrapperExports()
{
    gGetHwSizeAddress =
        sctrlHENFindFunction(kWrapperModule, kWrapperLibrary, kGetHwSizeNid);
    gGetModelAddress =
        sctrlHENFindFunction(kWrapperModule, kWrapperLibrary, kGetModelNid);
    gSetSizeAddress =
        sctrlHENFindFunction(kWrapperModule, kWrapperLibrary, kSetSizeNid);
    if (gGetHwSizeAddress == 0 || gGetModelAddress == 0 ||
        gSetSizeAddress == 0)
    {
        th07_psp_boot_notef("GE4 inactive: export resolve HW=%08x M=%08x S=%08x",
                            gGetHwSizeAddress, gGetModelAddress,
                            gSetSizeAddress);
        return false;
    }
    return true;
}

bool StopUnloadWrapper(int *stopResultOut = nullptr,
                       int *unloadResultOut = nullptr)
{
    if (gWrapper < 0)
    {
        if (stopResultOut)
            *stopResultOut = 0;
        if (unloadResultOut)
            *unloadResultOut = 0;
        ClearExportAddresses();
        return true;
    }
    int status = 0;
    const int stopResult =
        sceKernelStopModule(gWrapper, 0, nullptr, &status, nullptr);
    const int unloadResult =
        stopResult < 0 ? stopResult : sceKernelUnloadModule(gWrapper);
    if (stopResultOut)
        *stopResultOut = stopResult;
    if (unloadResultOut)
        *unloadResultOut = unloadResult;
    if (stopResult < 0 || unloadResult < 0)
    {
        th07_psp_boot_notef("GE4 wrapper unload failed stop=%08x unload=%08x",
                            static_cast<unsigned int>(stopResult),
                            static_cast<unsigned int>(unloadResult));
        return false;
    }
    gWrapper = -1;
    ClearExportAddresses();
    return true;
}

void UnlockPower()
{
    if (!__atomic_load_n(&gPowerLocked, __ATOMIC_ACQUIRE))
        return;
    const int result = scePowerUnlock(0);
    if (result != 0)
        ColdOffLoop("cleanup-power-unlock", result, sceGeEdramGetSize());
    __atomic_store_n(&gPowerLocked, 0, __ATOMIC_RELEASE);
}

[[noreturn]] void ColdOffLoop(const char *reason, int result,
                              unsigned int size)
{
    th07_psp_boot_notef("GE4 COLD OFF REQUIRED %s rc=%08x size=%08x",
                        reason, static_cast<unsigned int>(result), size);
    sceIoSync("ms0:", 0);
    sceIoSync("ef0:", 0);
    // An uncertain aperture or power-lock state is not recoverable in-process.
    for (;;)
        sceKernelDelayThread(1000 * 1000);
}

void FailClosedCleanup()
{
    int stopResult = 0;
    int unloadResult = 0;
    if (!StopUnloadWrapper(&stopResult, &unloadResult))
        ColdOffLoop("cleanup-wrapper-unload",
                    stopResult < 0 ? stopResult : unloadResult,
                    sceGeEdramGetSize());
    UnlockPower();
}

int WaitForGeIdle()
{
    const unsigned long long start =
        static_cast<unsigned long long>(sceKernelGetSystemTimeWide());
    for (unsigned int polls = 0; polls < kMaxGePolls; ++polls)
    {
        const int state = sceGeDrawSync(1);
        if (state == PSP_GE_LIST_DONE || state < 0)
            return state;
        const int delay = sceKernelDelayThread(kGePollDelayUs);
        if (delay < 0)
            return delay;
        const unsigned long long now =
            static_cast<unsigned long long>(sceKernelGetSystemTimeWide());
        if (now - start >= kGeTimeoutUs)
            return -0x3e30;
    }
    return -0x3e31;
}

void RestoreTwoMiBOrCold(const char *reason)
{
    const int syncResult = WaitForGeIdle();
    if (syncResult != PSP_GE_LIST_DONE)
        ColdOffLoop("restore2-sync", syncResult, sceGeEdramGetSize());

    sceKernelDcacheWritebackInvalidateAll();
    const ExportCall restore = CallKernelExport(gSetSizeAddress, kTwoMiB);
    const unsigned int sizeFinal = sceGeEdramGetSize();
    if (restore.outer < 0 || static_cast<int>(restore.value) < 0 ||
        sizeFinal != kTwoMiB)
    {
        th07_psp_boot_notef(
            "GE4 restore2 failed outer=%08x inner=%08x size=%08x",
            static_cast<unsigned int>(restore.outer), restore.value, sizeFinal);
        ColdOffLoop(reason, restore.outer < 0 ? restore.outer
                                             : static_cast<int>(restore.value),
                    sizeFinal);
    }
}
} // namespace

extern "C" int th07_psp_ge4_prepare()
{
    if (gPrepareAttempted)
        return __atomic_load_n(&gPrepared, __ATOMIC_ACQUIRE);
    gPrepareAttempted = true;

    if (!LoadWrapper())
        return 0;
    if (!ResolveWrapperExports())
    {
        FailClosedCleanup();
        return 0;
    }

    const ExportCall modelCall = CallKernelExport(gGetModelAddress);
    const ExportCall hwSizeCall = CallKernelExport(gGetHwSizeAddress);
    const unsigned int base = static_cast<unsigned int>(
        reinterpret_cast<std::uintptr_t>(sceGeEdramGetAddr()));
    const unsigned int sizeBefore = sceGeEdramGetSize();
    if (modelCall.outer < 0 || hwSizeCall.outer < 0)
    {
        th07_psp_boot_notef("GE4 inactive: export call M=%08x H=%08x",
                            static_cast<unsigned int>(modelCall.outer),
                            static_cast<unsigned int>(hwSizeCall.outer));
        FailClosedCleanup();
        return 0;
    }

    const int model = static_cast<int>(modelCall.value);
    const unsigned int hwSize = hwSizeCall.value;
    if (model != kRequiredModel || base != kExpectedEdramBase ||
        hwSize != kFourMiB || sizeBefore != kTwoMiB)
    {
        th07_psp_boot_notef("GE4 inactive: runtime gate M%d B%08x H%08x S%08x",
                            model, base, hwSize, sizeBefore);
        FailClosedCleanup();
        return 0;
    }

    const int lockResult = scePowerLock(0);
    if (lockResult < 0)
    {
        th07_psp_boot_notef("GE4 inactive: power lock rc=%08x",
                            static_cast<unsigned int>(lockResult));
        FailClosedCleanup();
        return 0;
    }
    if (lockResult > 0)
    {
        // Only exact zero proves that this process owns the sole shared lock.
        // A positive result leaves ownership uncertain, so retain all state.
        ColdOffLoop("power-lock-uncertain", lockResult, sceGeEdramGetSize());
    }
    __atomic_store_n(&gPowerLocked, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&gPrepared, 1, __ATOMIC_RELEASE);
    th07_psp_boot_note("GE4 prepared: frozen wrapper resolved at 2MiB");
    return 1;
}

extern "C" int th07_psp_ge4_enable_after_gu_idle()
{
    if (!__atomic_load_n(&gPrepared, __ATOMIC_ACQUIRE))
    {
        // R6 reached this exact silent path: the GU backend initialized before
        // main() had prepared the bridge, so the aperture never widened.  Keep
        // the no-op fail-closed behaviour, but never again silently.
        th07_psp_boot_note("GE4 enable skipped: bridge not prepared");
        return th07_psp_ge4_active();
    }
    if (gEnableAttempted)
        return th07_psp_ge4_active();
    gEnableAttempted = true;

    if (!__atomic_load_n(&gPowerLocked, __ATOMIC_ACQUIRE) || gWrapper < 0 ||
        gSetSizeAddress == 0)
        ColdOffLoop("enable-state-invalid", -1, sceGeEdramGetSize());

    const unsigned int sizeBefore = sceGeEdramGetSize();
    if (sizeBefore != kTwoMiB)
        ColdOffLoop("enable-baseline-size", -1, sizeBefore);

    // The renderer calls here only after its initial sceGuFinish/Sync. Keep an
    // independent GE-idle gate before changing the hardware aperture.
    const int syncResult = WaitForGeIdle();
    if (syncResult != PSP_GE_LIST_DONE)
        ColdOffLoop("pre-enable-sync", syncResult, sizeBefore);

    sceKernelDcacheWritebackInvalidateAll();
    const ExportCall set4 = CallKernelExport(gSetSizeAddress, kFourMiB);
    const unsigned int sizeAfter = sceGeEdramGetSize();
    if (set4.outer < 0 || static_cast<int>(set4.value) < 0 ||
        sizeAfter != kFourMiB)
    {
        if (sizeAfter == kFourMiB)
            RestoreTwoMiBOrCold("enable-fail-restore2");
        else if (sizeAfter != kTwoMiB)
            ColdOffLoop("enable-fail-unknown-size",
                        set4.outer < 0 ? set4.outer
                                     : static_cast<int>(set4.value),
                        sizeAfter);

        th07_psp_boot_notef(
            "GE4 inactive: Set4 outer=%08x inner=%08x size=%08x",
            static_cast<unsigned int>(set4.outer), set4.value, sizeAfter);
        __atomic_store_n(&gPrepared, 0, __ATOMIC_RELEASE);
        FailClosedCleanup();
        return 0;
    }

    const int postSet4SyncResult = WaitForGeIdle();
    if (postSet4SyncResult != PSP_GE_LIST_DONE)
    {
        // Set4 and its readback succeeded, but the proven V6 acceptance gate
        // also requires GE idle after the transition. Restore before retiring
        // the callable wrapper and shared lock.
        RestoreTwoMiBOrCold("post-enable-restore2");
        th07_psp_boot_notef("GE4 inactive: post-Set4 sync rc=%08x",
                            static_cast<unsigned int>(postSet4SyncResult));
        __atomic_store_n(&gPrepared, 0, __ATOMIC_RELEASE);
        FailClosedCleanup();
        return 0;
    }

    __atomic_store_n(&gActive, 1, __ATOMIC_RELEASE);
    th07_psp_boot_note("GE4 ACTIVE model3 upper 2MiB PORTRAIT lock-shared");
    return 1;
}

extern "C" int th07_psp_ge4_active()
{
    return __atomic_load_n(&gActive, __ATOMIC_ACQUIRE);
}

extern "C" int th07_psp_ge4_power_lock_held()
{
    return __atomic_load_n(&gActive, __ATOMIC_ACQUIRE) &&
           __atomic_load_n(&gPowerLocked, __ATOMIC_ACQUIRE);
}

extern "C" void th07_psp_ge4_fail_closed(const char *reason)
{
    ColdOffLoop(reason ? reason : "unspecified", -1, sceGeEdramGetSize());
}

extern "C" void th07_psp_ge4_shutdown()
{
    if (!gPrepareAttempted)
        return;
    if (gWrapper < 0)
    {
        if (__atomic_load_n(&gPrepared, __ATOMIC_ACQUIRE) ||
            __atomic_load_n(&gActive, __ATOMIC_ACQUIRE) ||
            __atomic_load_n(&gPowerLocked, __ATOMIC_ACQUIRE))
            ColdOffLoop("shutdown-state-invalid", -1, sceGeEdramGetSize());
        return;
    }

    if (!__atomic_load_n(&gPrepared, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&gPowerLocked, __ATOMIC_ACQUIRE) ||
        gSetSizeAddress == 0)
        ColdOffLoop("shutdown-state-invalid", -1, sceGeEdramGetSize());

    // The caller has stopped audio, released all upper allocations and called
    // sceGuTerm. Restore/read back 2 MiB while the wrapper and sole lock live.
    RestoreTwoMiBOrCold("restore2");
    __atomic_store_n(&gActive, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&gPrepared, 0, __ATOMIC_RELEASE);
    th07_psp_boot_note("GE4 restored 2MiB");

    int stopResult = 0;
    int unloadResult = 0;
    if (!StopUnloadWrapper(&stopResult, &unloadResult))
        ColdOffLoop("final-wrapper-unload",
                    stopResult < 0 ? stopResult : unloadResult,
                    sceGeEdramGetSize());
    UnlockPower();
    // R6 is deliberately one-shot. Never reload or widen after terminal
    // restore in the same process.
}

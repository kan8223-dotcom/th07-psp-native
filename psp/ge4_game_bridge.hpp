#pragma once

// The R6 bridge owns the GE aperture as one process-wide lifecycle. Prepare
// loads and validates the frozen wrapper while GE is still at 2 MiB. The
// renderer enables 4 MiB only after its initial GU list is idle. Shutdown runs
// after sceGuTerm and restores 2 MiB before retiring the wrapper and lock.
#ifdef __cplusplus
extern "C" {
#endif

int th07_psp_ge4_prepare(void);
int th07_psp_ge4_enable_after_gu_idle(void);
int th07_psp_ge4_active(void);
int th07_psp_ge4_power_lock_held(void);
void th07_psp_ge4_fail_closed(const char *reason);
void th07_psp_ge4_shutdown(void);

#ifdef __cplusplus
}
#endif

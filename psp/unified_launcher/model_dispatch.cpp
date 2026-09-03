#include "model_dispatch.hpp"

#include "container_format.h"

#include <kubridge.h>

Th07UnifiedModelSelection th07_unified_select_model()
{
    const int raw_model = kuKernelGetModel();
    Th07UnifiedModelSelection selection;

    selection.raw_model = raw_model;
    selection.safe_fallback = false;
    selection.reason = "hardware model accepted";

    /* PspModel contains the eight generations 01g through 11g (0..7). */
    if (raw_model < 0 || raw_model > 7) {
        selection.effective_model = 0;
        selection.profile_id = TH07_UNIFIED_PROFILE_PSP1000;
        selection.safe_fallback = true;
        selection.reason = raw_model < 0 ? "model query failed; safe PSP-1000 fallback"
                                         : "unknown model; safe PSP-1000 fallback";
    } else if (raw_model == 0) {
        selection.effective_model = 0;
        selection.profile_id = TH07_UNIFIED_PROFILE_PSP1000;
    } else {
        selection.effective_model = static_cast<uint32_t>(raw_model);
        selection.profile_id = TH07_UNIFIED_PROFILE_PSP2000_PLUS;
    }
    return selection;
}

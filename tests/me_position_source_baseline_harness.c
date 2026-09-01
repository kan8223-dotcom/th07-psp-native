#include <stddef.h>
#include <stdint.h>

#define TH07_PSP_ME_RENDER_WORKER 1
#define TH07_PSP_ME_RENDER_CORRECTNESS 1
#define TH07_PSP_ME_RENDER_RAW_LIVE 1
#define TH07_PSP_ME_RENDER_DIRECT_LIST 1
#include "psp/audio_me.h"

_Static_assert(TH07_PSP_ME_RENDER_LIST_LAYOUT_VERSION == 0x4c4c3031u,
               "feature-off direct-list ABI must remain LL01");
_Static_assert(sizeof(Th07PspMeRenderListLayout) == 128u,
               "feature-off direct-list ABI must remain 128 bytes");
_Static_assert(offsetof(Th07PspMeRenderListLayout,
                        bulletRenderAngleOffset) == 84u,
               "feature-off direct-list suffix moved");

int main(void)
{
    Th07PspMeRenderListLayout layout = {0};
    layout.listLayoutVersion = TH07_PSP_ME_RENDER_LIST_LAYOUT_VERSION;
    layout.listLayoutBytes = sizeof(layout);
    return layout.listLayoutVersion == 0x4c4c3031u &&
                   layout.listLayoutBytes == 128u
               ? 0 : 1;
}

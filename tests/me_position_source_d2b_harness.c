#include <stddef.h>
#include <stdint.h>

#define TH07_PSP_ME_RENDER_WORKER 1
#define TH07_PSP_ME_RENDER_CORRECTNESS 1
#define TH07_PSP_ME_RENDER_RAW_LIVE 1
#define TH07_PSP_ME_RENDER_DIRECT_LIST 1
#define TH07_PSP_BULLET_POSITION_SOA_READ 1
#include "psp/audio_me.h"

_Static_assert(TH07_PSP_ME_RENDER_LIST_LAYOUT_VERSION == 0x4c4c3032u,
               "D2B requires LL02");
_Static_assert(TH07_PSP_ME_RENDER_POSITION_SOURCE_VERSION == 0x50533031u,
               "D2B requires PS01");
_Static_assert(TH07_PSP_ME_RENDER_POSITION_SOURCE_AOS == 0u,
               "AoS source kind changed");
_Static_assert(TH07_PSP_ME_RENDER_POSITION_SOURCE_SOA == 1u,
               "SoA source kind changed");
_Static_assert(sizeof(Th07PspMeRenderPositionSource) == 72u,
               "PS01 descriptor layout changed");
_Static_assert(offsetof(Th07PspMeRenderPositionSource, ownerBasePhys) == 16u,
               "PS01 owner prefix changed");
_Static_assert(offsetof(Th07PspMeRenderPositionSource, posXBasePhys) == 32u,
               "PS01 position planes changed");
_Static_assert(offsetof(Th07PspMeRenderPositionSource,
                        expectedManagerSerial) == 64u,
               "PS01 identity suffix changed");
_Static_assert(offsetof(Th07PspMeRenderListLayout, positionSource) == 88u,
               "LL02 position source moved");
_Static_assert(sizeof(Th07PspMeRenderListLayout) == 204u,
               "LL02 descriptor layout changed");

int main(void)
{
    Th07PspMeRenderListLayout layout = {0};
    layout.listLayoutVersion = TH07_PSP_ME_RENDER_LIST_LAYOUT_VERSION;
    layout.listLayoutBytes = sizeof(layout);
    layout.positionSource.version =
        TH07_PSP_ME_RENDER_POSITION_SOURCE_VERSION;
    layout.positionSource.bytes = sizeof(layout.positionSource);
    layout.positionSource.kind = TH07_PSP_ME_RENDER_POSITION_SOURCE_SOA;
    return layout.listLayoutBytes == 204u &&
                   layout.positionSource.bytes == 72u
               ? 0 : 1;
}

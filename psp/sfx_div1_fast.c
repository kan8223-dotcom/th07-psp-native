#include "sfx_div1_fast.h"

unsigned int th07_psp_sfx_compose_div1(
    const int *wide, short *io, unsigned int samples)
{
    unsigned int limited = 0;
    for (unsigned int sample = 0; sample < samples; ++sample)
    {
        const int background = io[sample];
        int effect = wide[sample];
        if (effect > 0)
        {
            const int headroom = 32767 - background;
            if (effect > headroom)
            {
                effect = headroom;
                ++limited;
            }
        }
        else if (effect < 0)
        {
            const int headroom = -32768 - background;
            if (effect < headroom)
            {
                effect = headroom;
                ++limited;
            }
        }
        io[sample] = (short)(background + effect);
    }
    return limited;
}

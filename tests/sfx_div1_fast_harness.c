#include "psp/sfx_div1_fast.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum
{
    MAX_SAMPLES = 2048,
    GUARD_WORDS = 8
};

typedef struct GuardedPcm
{
    uint32_t before[GUARD_WORDS];
    short samples[MAX_SAMPLES];
    uint32_t after[GUARD_WORDS];
} GuardedPcm;

static uint32_t gRandom = 0x260902a5u;
static unsigned long long gCases;

static uint32_t next_random(void)
{
    gRandom ^= gRandom << 13;
    gRandom ^= gRandom >> 17;
    gRandom ^= gRandom << 5;
    return gRandom;
}

static unsigned int compose_reference_div1(
    const int *wide, short *io, unsigned int samples)
{
    const int divisor = 1;
    unsigned int limited = 0;
    for (unsigned int sample = 0; sample < samples; ++sample)
    {
        const int background = io[sample];
        int effect = wide[sample] / divisor;
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

static void initialize_guarded(GuardedPcm *pcm, const short *samples,
                               unsigned int count)
{
    for (unsigned int index = 0; index < GUARD_WORDS; ++index)
    {
        pcm->before[index] = 0xa5c30000u + index;
        pcm->after[index] = 0x5a3c0000u + index;
    }
    memset(pcm->samples, 0x6d, sizeof(pcm->samples));
    if (count != 0u)
        memcpy(pcm->samples, samples, count * sizeof(samples[0]));
}

static int compare_case(const int *wide, const short *background,
                        unsigned int samples, const char *label)
{
    GuardedPcm reference;
    GuardedPcm candidate;
    initialize_guarded(&reference, background, samples);
    initialize_guarded(&candidate, background, samples);

    const unsigned int referenceLimited =
        compose_reference_div1(wide, reference.samples, samples);
    const unsigned int candidateLimited =
        th07_psp_sfx_compose_div1(wide, candidate.samples, samples);
    ++gCases;
    if (referenceLimited != candidateLimited ||
        memcmp(&reference, &candidate, sizeof(reference)) != 0)
    {
        fprintf(stderr,
                "%s mismatch samples=%u ref-limited=%u candidate-limited=%u\n",
                label, samples, referenceLimited, candidateLimited);
        return 0;
    }
    return 1;
}

static int run_exhaustive_headroom(void)
{
    static const int fixedEffects[] = {
        INT_MIN, -2097152, -65536, -32769, -32768, -32767, -2, -1,
        0, 1, 2, 32766, 32767, 32768, 65535, 2097152, INT_MAX};
    int wide[32];
    short background[32];

    for (int bg = -32768; bg <= 32767; ++bg)
    {
        unsigned int count = 0;
        for (unsigned int index = 0;
             index < sizeof(fixedEffects) / sizeof(fixedEffects[0]); ++index)
        {
            background[count] = (short)bg;
            wide[count++] = fixedEffects[index];
        }
        const int positiveHeadroom = 32767 - bg;
        const int negativeHeadroom = -32768 - bg;
        const int boundaries[] = {
            positiveHeadroom - 1, positiveHeadroom, positiveHeadroom + 1,
            negativeHeadroom - 1, negativeHeadroom, negativeHeadroom + 1};
        for (unsigned int index = 0;
             index < sizeof(boundaries) / sizeof(boundaries[0]); ++index)
        {
            background[count] = (short)bg;
            wide[count++] = boundaries[index];
        }
        if (!compare_case(wide, background, count, "headroom"))
            return 0;
    }
    return 1;
}

static int run_random_multivoice(void)
{
    static const unsigned int sampleCounts[] = {
        1u, 2u, 3u, 511u, 512u, 1023u, 1024u, 2048u};
    int wide[MAX_SAMPLES];
    short background[MAX_SAMPLES];
    for (unsigned int test = 0; test < 4096u; ++test)
    {
        const unsigned int samples =
            sampleCounts[next_random() %
                         (sizeof(sampleCounts) / sizeof(sampleCounts[0]))];
        for (unsigned int sample = 0; sample < samples; ++sample)
        {
            const unsigned int voices = next_random() % 65u;
            int effect = 0;
            for (unsigned int voice = 0; voice < voices; ++voice)
                effect += (int)(short)next_random();
            wide[sample] = effect;
            background[sample] = (short)next_random();
        }
        if (!compare_case(wide, background, samples, "random-multivoice"))
            return 0;
    }
    return 1;
}

static int run_directed_cancellation(void)
{
    const int wide[] = {
        60000, -60000, 32768, -32769, 0, 1, -1, 2097088,
        -2097152, 30000, -30000, 65535, -65536, INT_MAX, INT_MIN};
    const short background[] = {
        -30000, 30000, -1, 1, 32767, -32768, 0, -32768,
        32767, -30000, 30000, -32768, 32767, -12345, 12345};
    return compare_case(
        wide, background, sizeof(wide) / sizeof(wide[0]), "directed");
}

int main(void)
{
    _Static_assert(sizeof(short) == 2, "PCM sample must be signed 16-bit");
    _Static_assert(sizeof(int) == 4, "wide sample must be signed 32-bit");

    const int unusedWide = 123;
    const short unusedPcm = -456;
    if (!compare_case(&unusedWide, &unusedPcm, 0u, "zero") ||
        !run_directed_cancellation() ||
        !run_exhaustive_headroom() ||
        !run_random_multivoice())
        return 1;

    printf("A5 DIV1 PCM exact: %llu guarded cases\n", gCases);
    return 0;
}

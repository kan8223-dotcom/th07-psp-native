/*
 * PSP/Allegrex raw-bit oracle for A1-MOVE startup vectors.
 *
 * This is intentionally independent from psp/audio_me.c.  Compile with
 * psp-gcc -O2 -G0 -march=allegrex -mtune=allegrex -fno-builtin-atan2f
 * -fno-builtin-cosf -fno-builtin-sinf, then run the ELF with psp-run or on a
 * PSP test shell.  The printed words are the fixed selftest table audited in
 * audio_me.c; host-libm output is never used as authority.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t bits(float value)
{
    union { float f; uint32_t u; } word;
    word.f = value;
    return word.u;
}

volatile uint32_t g_me_item_motion_oracle_words[10];

static void print_home(float px, float py, float ix, float iy,
                       float speed, float multiplier)
{
    volatile float dx = px - ix;
    volatile float dy = py - iy;
    float angle = (dy == 0.0f && dx == 0.0f)
        ? 1.5707964f : atan2f(dy, dx);
    volatile float vx = cosf(angle) * speed;
    volatile float vy = sinf(angle) * speed;
    volatile float ox = ix + vx * multiplier;
    volatile float oy = iy + vy * multiplier;
    printf("HOME %08x %08x %08x %08x\n",
           bits(ox), bits(oy), bits(vx), bits(vy));
}

static void print_interp(float sx, float sy, float tx, float ty,
                         int timer, float subframe)
{
    volatile float timerValue = (float)timer + subframe;
    volatile float t = timerValue / 60.0f;
    volatile float oneMinusT = 1.0f - t;
    volatile float startTermX = sx * oneMinusT;
    volatile float startTermY = sy * oneMinusT;
    volatile float targetTermX = tx * t;
    volatile float targetTermY = ty * t;
    volatile float x = targetTermX + startTermX;
    volatile float y = targetTermY + startTermY;
    printf("INTERP %08x %08x\n", bits(x), bits(y));
}

__attribute__((noinline, used)) void store_oracle_words(void)
{
    volatile float dx = 100.0f - 124.0f;
    volatile float dy = 80.0f - 112.0f;
    float angle = atan2f(dy, dx);
    volatile float vx = cosf(angle) * 4.0f;
    volatile float vy = sinf(angle) * 4.0f;
    volatile float ox = 124.0f + vx * 0.75f;
    volatile float oy = 112.0f + vy * 0.75f;
    g_me_item_motion_oracle_words[0] = bits(ox);
    g_me_item_motion_oracle_words[1] = bits(oy);
    g_me_item_motion_oracle_words[2] = bits(vx);
    g_me_item_motion_oracle_words[3] = bits(vy);

    volatile float timerValue = 30.0f + 0.25f;
    volatile float t = timerValue / 60.0f;
    volatile float oneMinusT = 1.0f - t;
    volatile float startTermX = 10.0f * oneMinusT;
    volatile float startTermY = 20.0f * oneMinusT;
    volatile float targetTermX = 70.0f * t;
    volatile float targetTermY = 80.0f * t;
    volatile float ix = targetTermX + startTermX;
    volatile float iy = targetTermY + startTermY;
    g_me_item_motion_oracle_words[4] = bits(ix);
    g_me_item_motion_oracle_words[5] = bits(iy);
}

#if defined(PSP_SIM_BARE)
__asm__(
    ".section .text.start,\"ax\",@progbits\n"
    ".globl _start\n"
    "_start:\n"
    "lui $sp, 0x0a00\n"
    "addiu $sp, $sp, -64\n"
    "jal store_oracle_words\n"
    "nop\n"
    "break 0\n");
#else
int main(void)
{
    print_home(100.0f, 80.0f, 10.0f, 80.0f, 4.0f, 1.0f);
    print_home(100.0f, 80.0f, 124.0f, 112.0f, 4.0f, 0.75f);
    print_home(100.0f, 80.0f, 100.0f, 80.0f, 4.0f, 0.75f);
    print_interp(10.0f, 20.0f, 70.0f, 80.0f, 30, 0.25f);
    return 0;
}
#endif

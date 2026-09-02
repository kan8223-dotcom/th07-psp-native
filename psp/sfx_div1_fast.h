#ifndef TH07_PSP_SFX_DIV1_FAST_H
#define TH07_PSP_SFX_DIV1_FAST_H

#ifdef __cplusplus
extern "C" {
#endif

// Add an already-accumulated signed-32-bit SFX bus to signed-16-bit BGM when
// mixDivisor is exactly one.  The generic mixer deliberately remains outside
// this translation unit; its named symbol is also the fail-closed PSP
// disassembly boundary for the A5-SCALAR DIV/MFLO gate.
unsigned int th07_psp_sfx_compose_div1(
    const int *wide, short *io, unsigned int samples);

#ifdef __cplusplus
}
#endif

#endif

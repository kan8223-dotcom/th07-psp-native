#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    TH07_PSP_ME_MAX_MIX_INPUTS = 64,
    TH07_PSP_ME_MAX_MIX_FRAMES = 1024,
    TH07_PSP_MIX_S16 = 0,
    TH07_PSP_MIX_MULAW8 = 1
};

typedef struct Th07PspMixInput
{
    const void *samples;
    // Total source frames.  Effects remain at their native sample rate, so ME
    // performs the same 16.16 nearest-neighbour stepping as the SC fallback.
    // PSP-1000 stores mono SFX as full-rate G.711 mu-law; BGM and the standard
    // PSP build use signed 16-bit PCM.
    unsigned int frames;
    unsigned int destinationFrame;
    unsigned int channels;
    unsigned int sourceFrame;
    unsigned int sourceFraction;
    unsigned int stepFixed;
    unsigned int gainQ16;
    // Set only for mutable input (the freshly assembled BGM block).  Loaded
    // SFX are immutable and are written back once by LoadSound().
    unsigned int needsWriteback;
    unsigned int sampleFormat;
} Th07PspMixInput;

typedef struct Th07PspMixJob
{
    unsigned int frames;
    unsigned int inputCount;
    unsigned int mixDivisor;
    Th07PspMixInput inputs[TH07_PSP_ME_MAX_MIX_INPUTS];
} Th07PspMixJob;

// Returns 1 when ME produced this block, 0 when the identical SC fallback did.
// Either return value leaves `output` ready for the existing software ring.
int th07_psp_me_audio_mix(const Th07PspMixJob *job, short *output);
// Run the identical integer mixer synchronously on the main CPU.  TH07's
// audio output thread has only one 512-frame block of deadline slack, so SFX
// jobs must not pay a blocking ME round trip before submitting that block.
// Accumulate SFX in the internal 32-bit bus and add them directly to `io`.
// Only the SFX contribution is limited against each untouched BGM sample's
// remaining signed-16-bit headroom; no intermediate 16-bit clip is possible.
// This entry point uses one internal wide bus and is intentionally
// non-reentrant; call it only from TH07's single audio-output thread.
int th07_psp_sc_audio_mix_into(const Th07PspMixJob *job, short *io,
                               unsigned int *limitedSamples);
int th07_psp_me_audio_init(void);
void th07_psp_me_audio_shutdown(void);
void th07_psp_me_audio_diag_window(unsigned int *jobs, unsigned int *fallbacks,
                                    unsigned int *timeouts, unsigned int *maxWaitUs);

// Pack one engine vertex stream into the native interleaved GE layout on ME.
// A successful output remains valid until th07_psp_me_vertex_frame_begin(),
// which the renderer calls only after the previous GE list has completed.
typedef struct Th07PspMeVertexPack
{
    const void *position;
    const void *texcoord;
    const void *diffuse;
    unsigned int positionStride;
    unsigned int texcoordStride;
    unsigned int diffuseStride;
    unsigned int count;
    unsigned int textured;
    unsigned int colored;
} Th07PspMeVertexPack;

void th07_psp_me_vertex_frame_begin(void);
int th07_psp_me_vertex_pack(const Th07PspMeVertexPack *job, const void **output);

#ifdef __cplusplus
}
#endif

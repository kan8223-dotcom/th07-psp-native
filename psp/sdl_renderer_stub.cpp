#if defined(TH07_PSP_1000)

#include <SDL2/SDL.h>

// SDL's PSP renderer object owns a private 1 MiB GE display list. TH07 uses
// its own libGU backend and never creates an SDL_Renderer, but SDL's static
// driver table would still pull that object into the EBOOT. Supply the exact
// internal driver ABI with a harmless unavailable renderer for the 32 MiB
// build so only TH07's command list is resident.
namespace
{
SDL_Renderer *CreateUnusedRenderer(SDL_Window *, Uint32)
{
    SDL_SetError("SDL renderer disabled: TH07 uses its native GU backend");
    return nullptr;
}
} // namespace

extern "C"
{
struct Th07UnusedSdlRenderDriver
{
    SDL_Renderer *(*createRenderer)(SDL_Window *, Uint32);
    SDL_RendererInfo info;
};

Th07UnusedSdlRenderDriver PSP_RenderDriver = {
    CreateUnusedRenderer,
    {"th07-native-gu", 0, 0, {}, 0, 0},
};

int SDL_PSP_RenderGetProp(SDL_Renderer *, int, void *)
{
    return -1;
}
}

#endif

#ifndef AVP3DS_SDL_SURFACE_SHIM_H
#define AVP3DS_SDL_SURFACE_SHIM_H

/*
 * Minimal SDL surface compatibility for the original AvP
 * software-menu rasterizer.
 *
 * This is not a full SDL implementation.
 */

typedef struct SDL_Surface
{
    void *pixels;
    int w;
    int h;
    int pitch;
} SDL_Surface;

#define SDL_MUSTLOCK(surface) 0

static inline int SDL_LockSurface(SDL_Surface *surface)
{
    (void)surface;
    return 0;
}

static inline void SDL_UnlockSurface(SDL_Surface *surface)
{
    (void)surface;
}

#endif

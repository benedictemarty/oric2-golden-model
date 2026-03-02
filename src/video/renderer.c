/**
 * @file renderer.c
 * @brief SDL2 renderer (headless mode if SDL2 not available)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 0.4.0-alpha
 */

#include "video/video.h"

#ifdef HAS_SDL2
#include <SDL2/SDL.h>

static SDL_Window* window;
static SDL_Renderer* sdl_renderer;
static SDL_Texture* texture;
static bool fullscreen;

bool renderer_init(int scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) return false;
    window = SDL_CreateWindow("Phosphoric",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        ORIC_SCREEN_W * scale, ORIC_SCREEN_H * scale,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) return false;
    sdl_renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_renderer) return false;
    texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
        ORIC_SCREEN_W, ORIC_SCREEN_H);
    if (!texture) return false;
    SDL_RenderSetLogicalSize(sdl_renderer, ORIC_SCREEN_W, ORIC_SCREEN_H);
    fullscreen = false;
    return true;
}

void renderer_cleanup(void) {
    if (texture) SDL_DestroyTexture(texture);
    if (sdl_renderer) SDL_DestroyRenderer(sdl_renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

void renderer_present(video_t* vid) {
    SDL_UpdateTexture(texture, NULL, vid->framebuffer, ORIC_SCREEN_W * 3);
    SDL_RenderClear(sdl_renderer);
    SDL_RenderCopy(sdl_renderer, texture, NULL, NULL);
    SDL_RenderPresent(sdl_renderer);
}

void renderer_toggle_fullscreen(void) {
    fullscreen = !fullscreen;
    SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

#else

bool renderer_init(int scale) { (void)scale; return true; }
void renderer_cleanup(void) {}
void renderer_present(video_t* vid) { (void)vid; }
void renderer_toggle_fullscreen(void) {}

#endif

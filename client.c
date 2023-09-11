/*
================================================================================

    tinyMMO - an attempt to write a simple MMO-RPG in my spare time
    written by Sebastian Steinhauer <s.steinhauer@yahoo.de>

    This is free and unencumbered software released into the public domain.

    Anyone is free to copy, modify, publish, use, compile, sell, or
    distribute this software, either in source code form or as a compiled
    binary, for any purpose, commercial or non-commercial, and by any
    means.

    In jurisdictions that recognize copyright laws, the author or authors
    of this software dedicate any and all copyright interest in the
    software to the public domain. We make this dedication for the benefit
    of the public at large and to the detriment of our heirs and
    successors. We intend this dedication to be an overt act of
    relinquishment in perpetuity of all present and future rights to this
    software under copyright law.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
    OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
    ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
    OTHER DEALINGS IN THE SOFTWARE.

    For more information, please refer to <https://unlicense.org>

================================================================================
*/
/*==[[ Includes ]]============================================================*/
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>

#include "SDL.h"


/*==[[ Defines / Enums ]]======================================================*/
enum {
    TICK_RATE                   = 20, // 20 ticks per second

    VIDEO_COLS                  = 16, // tile columns
    VIDEO_ROWS                  = 16, // tile rows
    TILE_SIZE                   = 8, // tile size (8x8 pixels)
};

#define VIDEO_TITLE             "tinyMMO - Client"
#define VIDEO_FACTOR            0.8f

#define TICK_TIME               (1000.0 / (double)TICK_RATE)


/*==[[ Global State ]]=========================================================*/

static struct state_t {
    bool                        running; // keep the client running
    int                         argc; // command line argument count
    char                        **argv; // command line argument vector
    uint32_t                    tick; // current game tick

    struct {
        SDL_Window              *window; // SDL2 window handle
        SDL_Renderer            *renderer; // SDL2 renderer handle
        SDL_Texture             *texture; // SDL2 texture handle
        uint8_t                 screen[VIDEO_ROWS][VIDEO_COLS]; // tilemap
    } video;
} state;


/*==[[ Helper Functions ]]====================================================*/

// show error message and quit the client
static void panic(const char *fmt, ...) {
    char message[1024];
    va_list va;
    va_start(va, fmt); SDL_vsnprintf(message, sizeof(message), fmt, va); va_end(va);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Panic!", message, state.video.window);
    exit(EXIT_FAILURE);
}

// format given string
static const char *format_string(const char *fmt, ...) {
    static char text[1024];
    va_list va;
    va_start(va, fmt); SDL_vsnprintf(text, sizeof(text), fmt, va); va_end(va);
    return text;
}

// clear the screen
static void clear_screen(void) {
    SDL_zero(state.video.screen);
}

// draw tile
static void draw_tile(const int x, const int y, const uint8_t tile) {
    if ((x >= 0) && (x < VIDEO_COLS) && (y >= 0) && (y < VIDEO_ROWS))
        state.video.screen[y][x] = tile;
}

// draw text
static void draw_text(const int x, const int y, const char *text) {
    for (int xx = x; *text; ++text, ++xx)
        draw_tile(xx, y, (uint8_t)*text);
}

// take a screenshot (this is for documentation only)
void debug_screenshot(void) {
    int w, h, bpp;
    SDL_GetRendererOutputSize(state.video.renderer, &w, &h);
    uint32_t pixel_format = SDL_GetWindowPixelFormat(state.video.window);
    uint32_t rmask, gmask, bmask, amask;
    SDL_PixelFormatEnumToMasks(pixel_format, &bpp, &rmask, &gmask, &bmask, &amask);
    SDL_Surface *surface = SDL_CreateRGBSurface(0, w, h, bpp, rmask, gmask, bmask, amask);
    SDL_RenderReadPixels(state.video.renderer, NULL, pixel_format, surface->pixels, surface->pitch);
    SDL_SaveBMP(surface, "screenshot.bmp");
    SDL_FreeSurface(surface);
}


/*==[[ Asset Handling ]]======================================================*/

// load tileset
static SDL_Texture *load_tileset(const char *filename) {
    SDL_Surface *surface = SDL_LoadBMP(filename);
    if (surface == NULL)
        panic("SDL_LoadBMP() failed: %s", SDL_GetError());
    if ((surface->w != 16 * TILE_SIZE) || (surface->h != 16 * TILE_SIZE)) {
        SDL_FreeSurface(surface);
        panic("Tileset image has wrong size");
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(state.video.renderer, surface);
    SDL_FreeSurface(surface);
    if (texture == NULL)
        panic("SDL_CreateTextureFromSurface() failed: %s", SDL_GetError());
    return texture;
}

// free all assets
static void free_assets(void) {
    if (state.video.texture != NULL) {
        SDL_DestroyTexture(state.video.texture);
        state.video.texture = NULL;
    }
}

// load all assets
static void load_assets(void) {
    free_assets();
    state.video.texture = load_tileset("assets/tiles.bmp");
}


/*==[[ Video Rendering ]]=====================================================*/

// render single tile to screen
static void render_tile(const int x, const int y, const uint8_t tile) {
    const SDL_Rect src = { .x = (tile % 16) * TILE_SIZE, .y = (tile / 16) * TILE_SIZE, .w = TILE_SIZE, .h = TILE_SIZE };
    const SDL_Rect dst = { .x = x * TILE_SIZE, .y = y * TILE_SIZE, .w = TILE_SIZE, .h = TILE_SIZE };
    SDL_RenderCopy(state.video.renderer, state.video.texture, &src, &dst);
}

// render the video
static void render_video(void) {
    SDL_RenderClear(state.video.renderer);
    for (int y = 0; y < VIDEO_ROWS; ++y) {
        for (int x = 0; x < VIDEO_COLS; ++x) {
            render_tile(x, y, state.video.screen[y][x]);
        }
    }
    SDL_RenderPresent(state.video.renderer);
}


/*==[[ Input Handling ]]======================================================*/

// handle SDL events
static void handle_SDL_events(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT: state.running = false; break;
        }
    }
}


/*==[[ Init / Shutdown / Main Loop ]]=========================================*/

// run single client tick
static void run_tick(void) {
    state.tick++;
    clear_screen();
    draw_text(0, 0, "Hello World!");
    draw_text(0, 1, format_string("tick: %d", state.tick));
}

// run the client main loop
static void run_client(void) {
    double delta_time = 0.0;
    uint32_t last_time = SDL_GetTicks();
    while (state.running) {
        // advance time
        uint32_t current_time = SDL_GetTicks();
        delta_time += current_time - last_time;
        last_time = current_time;
        // advance ticks
        for (; delta_time >= TICK_TIME; delta_time -= TICK_TIME)
            run_tick();
        // update client
        handle_SDL_events();
        render_video();
    }
}

// shutdown the client
static void quit_client(void) {
    free_assets();
    if (state.video.renderer != NULL)
        SDL_DestroyRenderer(state.video.renderer);
    if (state.video.window != NULL)
        SDL_DestroyWindow(state.video.window);
    SDL_Quit();
}

// initialize the client
static void init_client(int argc, char **argv) {
    // init state and SDL2 library
    state = (struct state_t){ .running = true, .argc = argc, .argv = argv };
    atexit(quit_client);
    if (SDL_Init(SDL_INIT_EVERYTHING))
        panic("SDL_Init() failed: %s", SDL_GetError());

    // init video system
    int w = VIDEO_COLS * TILE_SIZE, h = VIDEO_ROWS * TILE_SIZE;
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
        dm.w *= VIDEO_FACTOR; dm.h *= VIDEO_FACTOR;
        while ((w < dm.w) && (h < dm.h)) { w *= 2; h *= 2; }
        while ((w > dm.w) || (h > dm.h)) { w /= 2; h /= 2; }
    }
    if ((state.video.window = SDL_CreateWindow(VIDEO_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_RESIZABLE)) == NULL)
        panic("SDL_CreateWindow() failed: %s", SDL_GetError());
    if ((state.video.renderer = SDL_CreateRenderer(state.video.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)) == NULL)
        panic("SDL_CreateRenderer() failed: %s", SDL_GetError());
    if (SDL_RenderSetLogicalSize(state.video.renderer, VIDEO_COLS * TILE_SIZE, VIDEO_ROWS * TILE_SIZE))
        panic("SDL_RenderSetLogicalSize() failed: %s", SDL_GetError());
}

// main entry point
int main(int argc, char **argv) {
    init_client(argc, argv);
    load_assets();
    run_client();
    return 0;
}


/*==[[  ]]====================================================================*/

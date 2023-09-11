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

    AUDIO_RATE                  = 8000, // we mix audio at 8KHz
    AUDIO_VOICES                = 8, // we support 8 concurrent sounds
    AUDIO_SOUNDS                = 32, // we have 32 sound effects
    AUDIO_TRACKS                = 8, // we have 8 music tracks
};

#define VIDEO_TITLE             "tinyMMO - Client"
#define VIDEO_FACTOR            0.8f

#define TICK_TIME               (1000.0 / (double)TICK_RATE)

// button bit-masks
typedef enum {
    BUTTON_A                    = 1,
    BUTTON_B                    = 2,
    BUTTON_X                    = 4,
    BUTTON_Y                    = 8,
    BUTTON_UP                   = 16,
    BUTTON_DOWN                 = 32,
    BUTTON_LEFT                 = 64,
    BUTTON_RIGHT                = 128,
} button_t;


/*==[[ Types ]]===============================================================*/

// sound effect / music asset
typedef struct sound_t {
    int16_t                     *data; // 16-bit PCM data
    int                         length; // length in PCM samples
} sound_t;

// sound / music playback channel
typedef struct voice_t {
    sound_t                     *sound; // which sound is currently played
    int                         position; // current playback position
    bool                        loop; // shall we loop?
} voice_t;


/*==[[ Global State ]]========================================================*/

static struct state_t {
    bool                        running; // keep the client running
    int                         argc; // command line argument count
    char                        **argv; // command line argument vector
    uint32_t                    tick; // current game tick

    // input system
    struct {
        uint8_t                 down; // bit-mask of pressed buttons
    } input;

    // video system
    struct {
        SDL_Window              *window; // SDL2 window handle
        SDL_Renderer            *renderer; // SDL2 renderer handle
        SDL_Texture             *texture; // SDL2 texture handle
        uint8_t                 screen[VIDEO_ROWS][VIDEO_COLS]; // tilemap
    } video;

    // audio system
    struct {
        SDL_AudioDeviceID       device; // SDL2 audio device handle
        float                   gain; // global audio volume
        voice_t                 voices[AUDIO_VOICES]; // our audio voices
        sound_t                 sounds[AUDIO_SOUNDS]; // our sound effects
        sound_t                 music; // current music track
        int                     music_id;
    } audio;
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

// clamp integer value
static int clampi(const int x, const int min, const int max) {
    if (x < min) return min; else if (x > max) return max; else return x;
}

// clamp float value
static float clampf(const float x, const float min, const float max) {
    if (x < min) return min; else if (x > max) return max; else return x;
}


/*==[[ Video Functions ]]=====================================================*/

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

// toggle fullscreen
static void toggle_fullscreen(void) {
    uint32_t flags = SDL_GetWindowFlags(state.video.window);
    if (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
        SDL_SetWindowFullscreen(state.video.window, 0);
    } else {
        SDL_SetWindowFullscreen(state.video.window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
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

/*==[[ Audio Functions ]]=====================================================*/

// stop all audio output
static void stop_audio(void) {
    SDL_LockAudioDevice(state.audio.device);
    for (int i = 0; i < AUDIO_VOICES; ++i)
        state.audio.voices[i] = (voice_t){0};
    SDL_UnlockAudioDevice(state.audio.device);
}

// adjust global audio volume
static void adjust_gain(const float delta) {
    SDL_LockAudioDevice(state.audio.device);
    state.audio.gain = clampf(state.audio.gain + delta, 0.0f, 1.0f);
    SDL_UnlockAudioDevice(state.audio.device);
}

// forward declaration of sound loading
static void load_sound(sound_t *sound, const char *filename);

// play music
static void play_music(const int n) {
    // make sure we skip everything when we are currently playing the same music
    if (n == state.audio.music_id)
        return;
    state.audio.music_id = n;
    // stop music playback on voice 0
    SDL_LockAudioDevice(state.audio.device);
    state.audio.voices[0] = (voice_t){0};
    SDL_UnlockAudioDevice(state.audio.device);
    // free previous loaded audio track
    if (state.audio.music.data != NULL) {
        SDL_FreeWAV((Uint8*)state.audio.music.data);
        state.audio.music = (sound_t){0};
    }
    // load music track (if possible)
    load_sound(&state.audio.music, format_string("assets/music%02d.wav", n));
    if (state.audio.music.data != NULL) {
        SDL_LockAudioDevice(state.audio.device);
        state.audio.voices[0] = (voice_t){ .sound = &state.audio.music, .loop = true };
        SDL_UnlockAudioDevice(state.audio.device);
    }
}

// play sound effect
static void play_sound(const int n) {
    // make sure the sound effect exists
    if ((n < 0) || (n >= AUDIO_SOUNDS) || (state.audio.sounds[n].data == NULL))
        return;
    // find free audio voice to play this effect
    SDL_LockAudioDevice(state.audio.device);
    for (int i = 1; i < AUDIO_VOICES; ++i) {
        if (state.audio.voices[i].sound == NULL) {
            state.audio.voices[i] = (voice_t){ .sound = &state.audio.sounds[n] };
            break;
        }
    }
    SDL_UnlockAudioDevice(state.audio.device);
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

// load sound effect / music track
static void load_sound(sound_t *sound, const char *filename) {
    SDL_AudioSpec spec;
    Uint8 *sample_data;
    Uint32 sample_length;
    if (SDL_LoadWAV(filename, &spec, &sample_data, &sample_length) == NULL)
        return;
    if ((spec.format != AUDIO_S16SYS) || (spec.channels != 1) || (spec.freq != AUDIO_RATE)) {
        SDL_FreeWAV(sample_data);
        panic("Sound (%s) is not 16-bit PCM mono 8KHz");
    }
    *sound = (sound_t){ .data = (int16_t*)sample_data, .length = sample_length / 2 };
}

// free all assets
static void free_assets(void) {
    stop_audio();
    // free tileset
    if (state.video.texture != NULL) {
        SDL_DestroyTexture(state.video.texture);
        state.video.texture = NULL;
    }
    // free sounds
    for (int i = 0; i < AUDIO_SOUNDS; ++i) {
        if (state.audio.sounds[i].data != NULL) {
            SDL_FreeWAV((Uint8*)state.audio.sounds[i].data);
            state.audio.sounds[i] = (sound_t){0};
        }
    }
    // free music
    if (state.audio.music.data != NULL) {
        SDL_FreeWAV((Uint8*)state.audio.music.data);
        state.audio.music = (sound_t){0};
    }
}

// load all assets
static void load_assets(void) {
    free_assets();
    state.video.texture = load_tileset("assets/tiles.bmp");
    for (int i = 0; i < AUDIO_SOUNDS; ++i)
        load_sound(&state.audio.sounds[i], format_string("assets/sound%02d.wav", i));
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


/*==[[ Audio Rendering ]]=====================================================*/

// render a single audio voice
static int render_voice(voice_t *voice) {
    if (voice->sound == NULL)
        return 0;
    if (voice->position >= voice->sound->length) {
        if (voice->loop) {
            voice->position = 0;
        } else {
            voice->sound = NULL;
            return 0;
        }
    }
    return voice->sound->data[voice->position++];
}

// render all audio voices from SDL callback
static void render_audio(void *userdata, Uint8 *stream8, int len8) {
    (void)userdata;
    int16_t *stream = (int16_t*)stream8;
    const int len = len8 / 2;
    for (int i = 0; i < len; ++i) {
        int total = 0;
        for (int j = 0; j < AUDIO_VOICES; ++j)
            total += render_voice(&state.audio.voices[j]);
        *stream++ = (int16_t)clampi(total * state.audio.gain, -32768, 32767);
    }
}


/*==[[ Input Handling ]]======================================================*/

// apply a button state
static void apply_button(const button_t button, const bool down) {
    if (down) {
        state.input.down |=  button;
    } else {
        state.input.down &= ~button;
    }
}

// apply mouse buttons
static void apply_mouse(const int button, const bool down) {
    switch (button) {
        case SDL_BUTTON_LEFT: apply_button(BUTTON_A, down); break;
        case SDL_BUTTON_RIGHT: apply_button(BUTTON_B, down); break;
    }
}

// apply keyboard buttons
static void apply_keyboard(const SDL_KeyCode key, const bool down) {
    switch (key) {
        // DPAD
        case SDLK_w: case SDLK_UP: case SDLK_8: case SDLK_KP_8: apply_button(BUTTON_UP, down); break;
        case SDLK_s: case SDLK_DOWN: case SDLK_2: case SDLK_KP_2: apply_button(BUTTON_DOWN, down); break;
        case SDLK_a: case SDLK_LEFT: case SDLK_4: case SDLK_KP_4: apply_button(BUTTON_LEFT, down); break;
        case SDLK_d: case SDLK_RIGHT: case SDLK_6: case SDLK_KP_6: apply_button(BUTTON_RIGHT, down); break;
        // action keys
        case SDLK_i: case SDLK_RETURN: case SDLK_RETURN2: apply_button(BUTTON_A, down); break;
        case SDLK_o: case SDLK_SPACE: apply_button(BUTTON_B, down); break;
        case SDLK_k: apply_button(BUTTON_X, down); break;
        case SDLK_l: apply_button(BUTTON_Y, down); break;
        // special key handling
        case SDLK_ESCAPE: if (down) state.running = false; break;
        case SDLK_F1: if (down) adjust_gain(-0.1f); break;
        case SDLK_F2: if (down) adjust_gain( 0.1f); break;
        case SDLK_F9: if (down) load_assets(); break;
        case SDLK_F12: if (down) toggle_fullscreen(); break;
        default: break;
    }
}

// apply gamepad buttons
static void apply_gamepad(const int button, const bool down) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A: apply_button(BUTTON_A, down); break;
        case SDL_CONTROLLER_BUTTON_B: apply_button(BUTTON_B, down); break;
        case SDL_CONTROLLER_BUTTON_X: apply_button(BUTTON_X, down); break;
        case SDL_CONTROLLER_BUTTON_Y: apply_button(BUTTON_Y, down); break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: apply_button(BUTTON_UP, down); break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: apply_button(BUTTON_DOWN, down); break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: apply_button(BUTTON_LEFT, down); break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: apply_button(BUTTON_RIGHT, down); break;
    }
}

// handle SDL events
static void handle_SDL_events(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT: state.running = false; break;
            case SDL_MOUSEBUTTONDOWN: apply_mouse(ev.button.button, true); break;
            case SDL_MOUSEBUTTONUP: apply_mouse(ev.button.button, false); break;
            case SDL_KEYDOWN: apply_keyboard(ev.key.keysym.sym, true); break;
            case SDL_KEYUP: apply_keyboard(ev.key.keysym.sym, false); break;
            case SDL_CONTROLLERBUTTONDOWN: apply_gamepad(ev.cbutton.button, true); break;
            case SDL_CONTROLLERBUTTONUP: apply_gamepad(ev.cbutton.button, false); break;
            case SDL_CONTROLLERDEVICEADDED: SDL_GameControllerOpen(ev.cdevice.which); break;
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
    draw_text(0, 2, format_string("down: %d", state.input.down));
    if (state.input.down & BUTTON_A) play_sound(0);
    if (state.input.down & BUTTON_X) debug_screenshot();
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
    if (state.audio.device != 0)
        SDL_CloseAudioDevice(state.audio.device);
    if (state.video.renderer != NULL)
        SDL_DestroyRenderer(state.video.renderer);
    if (state.video.window != NULL)
        SDL_DestroyWindow(state.video.window);
    SDL_Quit();
}

// initialize the client
static void init_client(int argc, char **argv) {
    // init state and SDL2 library
    state = (struct state_t){ .running = true, .argc = argc, .argv = argv, .audio.gain = 1.0f, .audio.music_id = -1 };
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
    // init audio system
    const SDL_AudioSpec want = { .format = AUDIO_S16SYS, .freq = AUDIO_RATE, .channels = 1, .samples = 1024, .callback = render_audio };
    SDL_AudioSpec have;
    if ((state.audio.device = SDL_OpenAudioDevice(NULL, SDL_FALSE, &want, &have, 0)) == 0)
        panic("SDL_OpenAudioDevice() failed: %s", SDL_GetError());
    SDL_PauseAudioDevice(state.audio.device, SDL_FALSE);
}

// main entry point
int main(int argc, char **argv) {
    init_client(argc, argv);
    load_assets();
    play_music(0);
    run_client();
    return 0;
}


/*==[[  ]]====================================================================*/

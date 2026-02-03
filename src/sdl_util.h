#ifndef SDL_UTIL_H
#define SDL_UTIL_H

#include "chip8.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#define DISPLAY_SCALE 15 // start with 640x320 resolution

typedef struct {
    SDL_AudioSpec spec;
    SDL_AudioDeviceID device_id;
} sdl_audio_state;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    sdl_audio_state audio;
} sdl_state;

typedef struct {
    chip8 *emulator;
    sdl_audio_state *audio;
} callback_data;

bool sdl_create_context(sdl_state *state);
bool sdl_init_audio(sdl_state *state, callback_data *userdata);
void sdl_cleanup(sdl_state *state);
void render(sdl_state *state, chip8 *emulator);

#endif

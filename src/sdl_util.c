#include "sdl_util.h"
#define AMPLITUDE 2000
#define BEEP_FREQ 600
#define SAMPLE_RATE 44100

void audio_callback(void *userdata, uint8_t *stream, int len) {
    callback_data *data = (callback_data *)userdata;
    int16_t *buffer     = (int16_t *)stream;
    int num_samples     = len / sizeof(*buffer);
    double phase        = 0;
    if (data->emulator->flags.beep) {
        for (int i = 0; i < num_samples; ++i) {
            buffer[i]  = (phase < .5) ? AMPLITUDE : -AMPLITUDE;
            phase     += (double)(BEEP_FREQ) / (double)data->audio->spec.freq;
            if (phase >= 1.0) {
                phase -= 1.0;
            }
        }
    } else {
        memset(stream, 0, len);
    }
}

bool sdl_create_context(sdl_state *state) {
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_VIDEO) < 0) {
        return false;
    }

    state->window =
        SDL_CreateWindow("CHIP-8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         DISPLAY_WIDTH * DISPLAY_SCALE, DISPLAY_HEIGHT * DISPLAY_SCALE, 0);
    if (!state->window) {
        return false;
    }

    state->renderer = SDL_CreateRenderer(state->window, -1, 0);
    if (!state->renderer) {
        return false;
    }
    return true;
}

bool sdl_init_audio(sdl_state *state, callback_data *data) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        return false;
    }
    SDL_zero(state->audio.spec);
    state->audio.spec.freq     = SAMPLE_RATE;
    state->audio.spec.format   = AUDIO_S16SYS;
    state->audio.spec.channels = 1;
    state->audio.spec.samples  = 512;
    state->audio.spec.callback = audio_callback;
    state->audio.spec.userdata = data;

    SDL_AudioSpec device_obtained = {0};
    state->audio.device_id = SDL_OpenAudioDevice(NULL, 0, &state->audio.spec, &device_obtained, 0);
    if (state->audio.device_id == 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    SDL_PauseAudioDevice(state->audio.device_id, 0);
    return true;
}

void sdl_cleanup(sdl_state *state) {
    SDL_DestroyWindow(state->window);
    SDL_DestroyRenderer(state->renderer);
    SDL_Quit();
}

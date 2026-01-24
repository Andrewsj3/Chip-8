#include "font.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MEM_SIZE 4096 // 4kB
#define DISPLAY_WIDTH 64
#define DISPLAY_HEIGHT 32
#define DISPLAY_SCALE 15 // start with 640x320 resolution
#define STACK_SIZE 32
#define REGISTER_COUNT 16
#define FONT_HEIGHT_PX 5
#define CLOCK_FREQ 700
#define TIMER_FREQ 60

#define AMPLITUDE 2000
#define BEEP_FREQ 600
#define SAMPLE_RATE 44100

#define UNUSED(x) (void)x

#define VERSION "0.1.0"

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
    uint16_t delay;
    uint16_t sound;
} chip8_timers;

typedef struct {
    uint8_t kind;
    uint8_t X;
    uint8_t Y;
    uint8_t N;
    uint8_t NN;
    uint16_t NNN;
} chip8_instruction;

typedef struct {
    uint16_t index;               // I
    uint8_t vars[REGISTER_COUNT]; // V0 - VF
} chip8_regs;

typedef struct {
    bool beep;
    bool exit;
    bool draw;
} chip8_flags;

typedef struct {
    uint8_t display[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    uint8_t memory[MEM_SIZE];
    uint16_t pc;
    chip8_instruction cur_instr;
    chip8_timers timers;
    chip8_flags flags;
    chip8_regs regs;
} chip8;

typedef struct {
    chip8 emulator;
    sdl_audio_state audio;
} callback_data;

// Debug printf
void dprintf(const char *restrict format, ...) {
#ifdef NDEBUG
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
#else
    UNUSED(format); // silence compiler small-talk
#endif
}

void beep_set(bool state, chip8_flags *flags, const sdl_audio_state *audio) {
    SDL_LockAudioDevice(audio->device_id);
    flags->beep = state;
    SDL_UnlockAudioDevice(audio->device_id);
}

void sdl_cleanup(sdl_state *state) {
    SDL_DestroyWindow(state->window);
    SDL_DestroyRenderer(state->renderer);
    SDL_Quit();
}

uint32_t update_timers(uint32_t interval, void *param) {
    callback_data *data = (callback_data *)param;
    if (data->emulator.flags.exit) {
        return 0;
    }
    if (data->emulator.timers.delay > 0) {
        --data->emulator.timers.delay;
    }

    if (data->emulator.timers.sound > 0) {
        --data->emulator.timers.sound;
        if (!data->emulator.flags.beep && data->emulator.timers.sound > 0) {
            beep_set(true, &data->emulator.flags, &data->audio);
        }
    } else {
        if (data->emulator.flags.beep) {
            beep_set(false, &data->emulator.flags, &data->audio);
        }
    }
    return interval;
}

void audio_callback(void *userdata, uint8_t *stream, int len) {
    callback_data *data = (callback_data *)userdata;
    int16_t *buffer     = (int16_t *)stream;
    int num_samples     = len / sizeof(*buffer);
    double phase        = 0;
    if (data->emulator.flags.beep) {
        for (int i = 0; i < num_samples; ++i) {
            buffer[i]  = (phase < .5) ? AMPLITUDE : -AMPLITUDE;
            phase     += (double)(BEEP_FREQ) / (double)data->audio.spec.freq;
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

void set_pixel(chip8 *emulator, int x, int y) {
    uint8_t *pixel  = &emulator->display[y * DISPLAY_WIDTH + x];
    *pixel         ^= 1;
    if (*pixel == 0) {
        // pixel turned off, set collision flag
        emulator->regs.vars[0xF] = 1;
    }
}

void draw_pixels(sdl_state *state, const chip8 *emulator) {
    SDL_SetRenderDrawColor(state->renderer, 242, 130, 19, SDL_ALPHA_OPAQUE);
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; ++i) {
        if (!emulator->display[i]) {
            continue;
        }
        int x          = (i % DISPLAY_WIDTH) * DISPLAY_SCALE;
        int y          = (i / DISPLAY_WIDTH) * DISPLAY_SCALE;
        // dprintf("x = %d, y = %d\n", x, y);
        SDL_Rect pixel = {.x = x, .y = y, .w = DISPLAY_SCALE, .h = DISPLAY_SCALE};
        SDL_RenderFillRect(state->renderer, &pixel);
    }
    SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
}

void render(sdl_state *state, chip8 *emulator) {
    if (emulator->flags.draw) {
        SDL_RenderClear(state->renderer);
        draw_pixels(state, emulator);
        SDL_RenderPresent(state->renderer);
        emulator->flags.draw = false;
    }
}

void poll_input(chip8_flags *flags) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            flags->exit = true;
            return;
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
                flags->exit = true;
                return;
            }
            break;
        }
    }
}

chip8_instruction get_cur_instr(chip8 *emulator) {
    uint8_t byte1  = emulator->memory[emulator->pc];
    uint8_t byte2  = emulator->memory[emulator->pc + 1];
    uint16_t instr = (byte1 << 8) | byte2;
    uint8_t kind   = instr >> 12 & 0xF;
    uint8_t X      = instr >> 8 & 0xF;
    uint8_t Y      = instr >> 4 & 0xF;
    uint8_t N      = instr >> 0 & 0x000F;
    uint8_t NN     = instr >> 0 & 0x00FF;
    uint16_t NNN   = instr >> 0 & 0x0FFF;
    return (chip8_instruction){
        .kind = kind,
        .X    = X,
        .Y    = Y,
        .N    = N,
        .NN   = NN,
        .NNN  = NNN,
    };
}

uint32_t emulate_instruction(uint32_t interval, void *param) {
    chip8 *emulator = (chip8 *)param;
    if (emulator->pc > MEM_SIZE) {
        puts("OOB");
        return 0;
    }
    if (emulator->flags.exit) {
        return 0;
    }
    emulator->cur_instr     = get_cur_instr(emulator);
    chip8_instruction instr = emulator->cur_instr;
    dprintf("kind = %04X, X = %X, Y = %X, N = %04X, NN = %04X, NNN = %04X\n", instr.kind, instr.X,
            instr.Y, instr.N, instr.NN, instr.NNN);
    emulator->pc += 2;
    switch (instr.kind) {
    case 0x0:
        if (instr.NN == 0xE0) {
            memset(emulator->display, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT);
        }
        break;
    case 0x1:
        dprintf("jump to %04X\n", instr.NNN);
        emulator->pc = instr.NNN;
        break;
    case 0x6:
        dprintf("set register V%X to %d\n", instr.X, instr.NN);
        emulator->regs.vars[instr.X] = instr.NN;
        break;
    case 0x7:
        dprintf("add %d to register V%X\n", instr.NN, instr.X);
        emulator->regs.vars[instr.X] += instr.NN;
        break;
    case 0xA:
        dprintf("set index register I to %d\n", instr.NNN);
        emulator->regs.index = instr.NNN;
        break;
    case 0xD:
        emulator->flags.draw = true;
        dprintf("display x=%d, y=%d, N=%d\n", emulator->regs.vars[instr.X],
                emulator->regs.vars[instr.Y], instr.N);
        uint16_t x = emulator->regs.vars[instr.X];
        uint16_t y = emulator->regs.vars[instr.Y];
        uint8_t sprite;
        for (uint8_t row = 0; row < instr.N; ++row) {
            sprite = emulator->memory[emulator->regs.index + row];
            for (uint8_t col = 0; col < 8; ++col) {
                uint8_t cur_x = x + col;
                uint8_t cur_y = y + row;
                if ((sprite & (0x80 >> col))) {
                    set_pixel(emulator, cur_x, cur_y);
                }
            }
        }
        break;
    default:
        puts("unknown");
        emulator->flags.exit = true;
        return 0;
    }
    return interval;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Expected a Chip-8 ROM file as only argument.\n");
        exit(1);
    }
    const uint32_t ticks_per_cycle = 1;         // 1000 instructions per second
    const uint32_t ticks_per_timer = 1000 / 60; // timers update at 60Hz, also used for display
    const size_t entry_point       = 0x200;
    uint64_t cycle_start;
    uint64_t ticks_taken;
    sdl_state state = {
        .window   = NULL,
        .renderer = NULL,
    };
    chip8 emulator;
    FILE *rom = fopen(argv[1], "rb");
    if (rom == NULL) {
        perror("fopen");
        exit(1);
    }
    fseek(rom, 0, SEEK_END);
    long rom_size = ftell(rom);
    rewind(rom);
    dprintf("rom_size = %ld\n", rom_size);
    if (fread(&emulator.memory[entry_point], 1, rom_size, rom) == (size_t)rom_size) {
        dprintf("Loaded ROM successfully");
        emulator.pc = entry_point;
    }
    fclose(rom);
    memcpy(emulator.memory, font, FONT_MEM_SIZE);
    callback_data data = {.emulator = emulator, .audio = state.audio};
    if (!sdl_create_context(&state) || !sdl_init_audio(&state, &data)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        emulator.flags.exit = true;
    }
    if (SDL_AddTimer(ticks_per_timer, update_timers, &data) == 0) {
        fprintf(stderr, "SDL_AddTimer: %s\n", SDL_GetError());
        emulator.flags.exit = true;
    }
    if (SDL_AddTimer(ticks_per_cycle, emulate_instruction, &emulator) == 0) {
        fprintf(stderr, "SDL_AddTimer: %s\n", SDL_GetError());
        emulator.flags.exit = true;
    }
    printf("Chip-8 Emulator v" VERSION ", powered by SDL.\n");
    while (!emulator.flags.exit) {
        cycle_start = SDL_GetTicks();
        poll_input(&emulator.flags);
        render(&state, &emulator);
        ticks_taken = SDL_GetTicks() - cycle_start;
        if (ticks_taken < ticks_per_cycle) {
            SDL_Delay(ticks_per_cycle - ticks_taken);
        }
    }
    sdl_cleanup(&state);
    return 0;
}

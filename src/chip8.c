#include "sdl_util.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#define UNUSED(x) (void)x
#define COL_MAX 8

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

chip8_instruction get_cur_instr(chip8 *emulator) {
    uint8_t byte1  = emulator->memory[emulator->pc];
    uint8_t byte2  = emulator->memory[emulator->pc + 1];
    uint16_t instr = (byte1 << 8) | byte2;
    uint8_t kind   = instr >> 12 & 0xF;
    uint8_t X      = instr >> 8 & 0xF;
    uint8_t Y      = instr >> 4 & 0xF;
    uint8_t N      = instr >> 0 & 0x00F;
    uint8_t NN     = instr >> 0 & 0x0FF;
    uint16_t NNN   = instr >> 0 & 0xFFF;
    return (chip8_instruction){
        .kind = kind,
        .X    = X,
        .Y    = Y,
        .N    = N,
        .NN   = NN,
        .NNN  = NNN,
    };
}

void set_pixel(chip8 *emulator, int x, int y) {
    uint8_t *pixel  = &emulator->display[y * DISPLAY_WIDTH + x];
    *pixel         ^= 1;
    if (*pixel == 0) {
        // pixel turned off, set collision flag
        emulator->regs.vars[0xF] = 1;
    }
}

uint32_t emulate_instruction(uint32_t interval, void *param) {
    chip8 *emulator = (chip8 *)param;
    if (emulator->pc > MEM_SIZE) {
        puts("Critical: Program counter out of bounds, exiting.");
        emulator->flags.exit = true;
    }
    if (emulator->flags.exit) {
        return 0;
    }
    chip8_instruction instr = get_cur_instr(emulator);
    dprintf("kind = %04X, X = %X, Y = %X, N = %04X, NN = %04X, NNN = %04X\n", instr.kind, instr.X,
            instr.Y, instr.N, instr.NN, instr.NNN);
    emulator->pc += 2;
    switch (instr.kind) {
    case 0x0:
        if (instr.NN == 0xE0) {
            memset(emulator->display, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT);
            emulator->flags.draw = true;
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
            for (uint8_t col = 0; col < COL_MAX; ++col) {
                uint8_t cur_x = x + col;
                uint8_t cur_y = y + row;
                if (sprite & (1 << (COL_MAX - 1 - col))) {
                    // check each bit of the sprite
                    set_pixel(emulator, cur_x, cur_y);
                }
            }
        }
        break;
    default:
        printf("Unimplemented instruction %X%X\n", instr.kind, instr.NNN);
        emulator->flags.exit = true;
        return 0;
    }
    return interval;
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

void beep_set(bool state, chip8_flags *flags, const sdl_audio_state *audio) {
    SDL_LockAudioDevice(audio->device_id);
    flags->beep = state;
    SDL_UnlockAudioDevice(audio->device_id);
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

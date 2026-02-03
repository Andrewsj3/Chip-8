#include "chip8.h"
#include "font.h"
#include "sdl_util.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define CLOCK_FREQ 700
#define TIMER_FREQ 60

#define VERSION "0.2.0"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Expected a Chip-8 ROM file as only argument.\n");
        exit(1);
    }
    srand(time(NULL));
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
        dprintf("Loaded ROM successfully\n");
        emulator.pc = entry_point;
    }
    fclose(rom);
    memcpy(emulator.memory, font, FONT_MEM_SIZE);
    callback_data data = {.emulator = &emulator, .audio = &state.audio};
    if (!sdl_create_context(&state) || !sdl_init_audio(&state, &data)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        emulator.flags.exit = true;
    }
    if (SDL_AddTimer(ticks_per_timer, update_timers, &data) == 0) {
        fprintf(stderr, "SDL_AddTimer: %s\n", SDL_GetError());
        emulator.flags.exit = true;
    }
    emulator.flags.draw = true; // make sure we get the window up to capture inputs
    if (SDL_AddTimer(ticks_per_cycle, emulate_instruction, &emulator) == 0) {
        fprintf(stderr, "SDL_AddTimer: %s\n", SDL_GetError());
        emulator.flags.exit = true;
    }
    printf("Chip-8 Emulator v" VERSION ", powered by SDL.\n");
    memset(emulator.display, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT);
    memset(emulator.keypad, 0, KEYPAD_ENTRIES);
    while (!emulator.flags.exit) {
        cycle_start = SDL_GetTicks();
        render(&state, &emulator);
        poll_input(&emulator);
        ticks_taken = SDL_GetTicks() - cycle_start;
        if (ticks_taken < ticks_per_timer) {
            SDL_Delay(ticks_per_timer - ticks_taken);
        }
    }
    sdl_cleanup(&state);
    return 0;
}

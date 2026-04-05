#include "chip8.h"
#include "font.h"
#include "sdl_util.h"
#include <SDL2/SDL.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define CLOCK_FREQ 700
#define TIMER_FREQ 60

#define VERSION "1.0.0"
static int refresh_rate            = 60;
static uint16_t width              = DISPLAY_WIDTH * DISPLAY_SCALE;
static uint16_t height             = DISPLAY_HEIGHT * DISPLAY_SCALE;
static bool slightly_bigger_pixels = false;

static const struct option long_options[] = {
    {"fg-colour", required_argument, NULL, 'c'},
    {"bg-colour", required_argument, NULL, 'C'},
    {"refresh-rate", required_argument, NULL, 'r'},
    {"width", required_argument, NULL, 'w'},
    {"height", required_argument, NULL, 'W'},
    {"scale", required_argument, NULL, 's'},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'v'},
};

static void print_usage(void) {
    puts("Usage: chip-8 [OPTIONS] <romfile>\n"
         "An interpreter for the CHIP-8 programming language.\n"
         "\n"
         "  -c, --fg-colour=rrggbb\n"
         "\tSets the colour of lit pixels\n"
         "  -C, --bg-colour=rrggbb\n"
         "\tSets the colour of the background\n"
         "  -r, --refresh-rate=hz\n"
         "\tSets the refresh rate of the display\n"
         "  -w, --width=px\n"
         "\tSets the width of the display, default 960\n"
         "  -H, --height=px\n"
         "\tSets the height of the display, default 480\n"
         "  -s, --scale=factor\n"
         "\tScales up the display by the given factor, default 15. Cannot be used with -w or -h\n"
         "  -h, --help\n"
         "\tShow this help and exit\n"
         "  -v, --version\n"
         "\tShow version information and exit\n");
    exit(0);
}

static bool validate_hex_string(char *str) {
    if (strlen(str) != 6) {
        return false;
    }
    return strtok(str, "0123456789aAbBcCdDeEfF") == NULL;
}

static sdl_colour hex_to_rgb(char *str) {
    uint32_t hex_val = strtoul(str, NULL, 16);
    return (sdl_colour){
        .r = hex_val >> 16 & 0xFF,
        .g = hex_val >> 8 & 0xFF,
        .b = hex_val >> 0 & 0xFF,
    };
}

static void handle_args(int argc, char **const argv, sdl_state *state) {
    int val = 0, scale = 0;
    bool use_scale = false;
    while ((val = getopt_long(argc, argv, ":hvc:C:r:w:H:s:", long_options, NULL)) != -1) {
        switch (val) {
        case 'h': print_usage(); break;
        case 'v': puts("chip-8 version " VERSION "\n"); break;
        case 'c':
            if (validate_hex_string(optarg)) {
                state->fg_colour = hex_to_rgb(optarg);
                break;
            } else {
                puts("Invalid hex string, format should be `rrggbb`");
                exit(1);
            }
        case 'C':
            if (validate_hex_string(optarg)) {
                state->bg_colour = hex_to_rgb(optarg);
                break;
            } else {
                puts("Invalid hex string, format should be `rrggbb`");
                exit(1);
            }
        case 'r':
            if ((refresh_rate = atoi(optarg)) <= 0) {
                refresh_rate = 60;
            }
            break;
        case 'w':
            if (use_scale) {
                puts("Cannot set width if `scale` option is used.");
            } else if ((width = atoi(optarg)) <= 0 || width > 7680) {
                width = DISPLAY_WIDTH * DISPLAY_SCALE;
            }
            break;
        case 'H':
            if (use_scale) {
                puts("Cannot set height if `scale` option is used.");
            } else if ((height = atoi(optarg)) <= 0 || height > 4320) {
                height = DISPLAY_HEIGHT * DISPLAY_SCALE;
            }
            break;
        case 's':
            if (slightly_bigger_pixels) {
                puts("Cannot use `scale` option if width or height is manually set.");
            } else {
                if ((scale = strtoul(optarg, NULL, 10)) <= 5) {
                    scale = DISPLAY_SCALE;
                }
                width     = DISPLAY_WIDTH * scale;
                height    = DISPLAY_HEIGHT * scale;
                use_scale = true;
            }
            break;
        case '?':
            printf("Unknown option `%s`\n", argv[optind - 1]);
            puts("Try `chip-8 --help` for a list of available options.");
            exit(1);
        case ':':
            printf("`%s` requires an argument\n", argv[optind - 1]);
            puts("Try `chip-8 --help` for more information.");
            exit(1);
        default: puts("Unknown error"); exit(1);
        }
    }
}

int main(int argc, char **const argv) {
    srand(time(NULL));
    const size_t entry_point = 0x200;
    uint64_t cycle_start;
    uint64_t ticks_taken;
    sdl_state state = {
        .window    = NULL,
        .renderer  = NULL,
        .fg_colour = {.r = 242, .g = 130, .b = 19}, // orange by default
    };
    handle_args(argc, argv, &state);
    const uint32_t ticks_per_cycle = 1; // 1000 instructions per second
    const uint32_t ticks_per_timer = 1000 / refresh_rate;
    if (optind == argc) {
        puts("Expected a ROM file");
        exit(1);
    }
    chip8 emulator;
    emulator.scale_x                = width;
    emulator.scale_y                = height;
    emulator.slightly_bigger_pixels = 2 * height != width;
    // Don't enable this if aspect ratio is 2:1
    FILE *rom                       = fopen(argv[optind], "rb");
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
    if (!sdl_create_context(&state, width, height) || !sdl_init_audio(&state, &data)) {
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

#include "chip8.h"
#include "instructions.h"
#include "sdl_util.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#define UNUSED(x) (void)x
#define COL_MAX 8
#define FONT_HEIGHT_PX 5
#define FLAG_REGISTER 0xF

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
    uint8_t kind   = instr >> 0xC & 0x00F;
    uint8_t X      = instr >> 0x8 & 0x00F;
    uint8_t Y      = instr >> 0x4 & 0x00F;
    uint8_t N      = instr >> 0x0 & 0x00F;
    uint8_t NN     = instr >> 0x0 & 0x0FF;
    uint16_t NNN   = instr >> 0x0 & 0xFFF;
    return (chip8_instruction){
        .kind = kind,
        .X    = X,
        .Y    = Y,
        .N    = N,
        .NN   = NN,
        .NNN  = NNN,
    };
}

void handle_arithmetic(chip8 *emulator, const chip8_instruction *instr) {
    uint8_t *vars = emulator->regs.vars;
    switch (instr->N) {
    case SET_REG_TO_REG:
        vars[instr->X] = vars[instr->Y];
        dprintf("V%X = V%X: V%X = %X\n", instr->X, instr->Y, instr->X, vars[instr->X]);
        break;
    case OR_REG_WITH_REG:
        vars[instr->X] |= vars[instr->Y];
        dprintf("V%X = V%X | V%X: V%X = %X\n", instr->X, instr->X, instr->Y, instr->X,
                vars[instr->X]);
        break;
    case AND_REG_WITH_REG:
        vars[instr->X] &= vars[instr->Y];
        dprintf("V%X = V%X & V%X: V%X = %X\n", instr->X, instr->X, instr->Y, instr->X,
                vars[instr->X]);
        break;
    case XOR_REG_WITH_REG:
        vars[instr->X] ^= vars[instr->Y];
        dprintf("V%X = V%X ^ V%X: V%X = %X\n", instr->X, instr->X, instr->Y, instr->X,
                vars[instr->X]);
        break;
    case ADD_REG_TO_REG: {
        uint16_t result     = vars[instr->X] + vars[instr->Y];
        vars[FLAG_REGISTER] = result > UINT8_MAX;
        vars[instr->X]      = (uint8_t)result;
        dprintf("V%X = V%X + V%X: V%X = %X\n", instr->X, instr->X, instr->Y, instr->X,
                vars[instr->X]);
        break;
    }
    case SUB_VX_BY_VY:
        vars[FLAG_REGISTER]  = vars[instr->X] >= vars[instr->Y];
        vars[instr->X]      -= vars[instr->Y];
        dprintf("V%X = V%X - V%X: V%X = %X\n", instr->X, instr->X, instr->Y, instr->X,
                vars[instr->X]);
        break;
    case SHIFT_RIGHT:
        vars[FLAG_REGISTER]   = vars[instr->X] & 1;
        vars[instr->X]      >>= 1;
        dprintf("Shift V%X right, V%X = %X\n", instr->X, instr->X, vars[instr->X]);
        break;
    case SUB_VY_BY_VX:
        vars[FLAG_REGISTER] = vars[instr->Y] >= vars[instr->X];
        vars[instr->X]      = vars[instr->Y] - vars[instr->X];
        dprintf("V%X = V%X - V%X: V%X = %X\n", instr->X, instr->Y, instr->X, instr->X,
                vars[instr->X]);
        break;
    case SHIFT_LEFT:
        vars[FLAG_REGISTER]   = vars[instr->X] > INT8_MAX;
        vars[instr->X]      <<= 1;
        dprintf("Shift V%X left, V%X = %X\n", instr->X, instr->X, vars[instr->X]);
        break;
    default:
        printf("Illegal instruction %X%03X\n", instr->kind, instr->NNN);
        emulator->flags.exit = true;
        break;
    }
}

void handle_misc(chip8 *emulator, const chip8_instruction *instr) {
    uint8_t *vars = emulator->regs.vars;
    switch (instr->NN) {
    case SET_VX_TO_DELAY_TIMER:
        vars[instr->X] = emulator->timers.delay;
        dprintf("Set V%X to value of delay timer (%X)\n", instr->X, emulator->timers.delay);
        break;
    case GET_KEY: {
        bool pressed = false;
        for (int i = 0; i < KEYPAD_ENTRIES; ++i) {
            if (emulator->keypad[i]) {
                vars[instr->X] = i;
                pressed        = true;
                break;
            }
        }
        if (!pressed) {
            emulator->pc -= 2;
        }
        dprintf("Waiting for key (key%s pressed)\n", pressed ? "" : " not");
        break;
    }
    case SET_DELAY_TIMER_TO_VX:
        emulator->timers.delay = vars[instr->X];
        dprintf("Set delay timer to value of V%X (%X)\n", instr->X, emulator->timers.delay);
        break;
    case SET_SOUND_TIMER_TO_VX:
        emulator->timers.sound = vars[instr->X];
        dprintf("Set sound timer to value of V%X (%X)\n", instr->X, emulator->timers.sound);
        break;
    case ADD_TO_INDEX:
        emulator->regs.index += vars[instr->X];
        dprintf("Add V%X to index register (I = %X)\n", instr->X, emulator->regs.index);
        break;
    case SET_INDEX_TO_CHAR:
        emulator->regs.index = (vars[instr->X] & 0xF) * FONT_HEIGHT_PX;
        // Sometimes the value in VX is greater than 15, so we need the binary and.
        // Otherwise we'll just draw random stuff from ram
        dprintf("Set index register to font character %X (I = %04X)\n", vars[instr->X],
                emulator->regs.index);
        break;
    case BINARY_TO_DECIMAL:
        if (emulator->regs.index + 2 < MEM_SIZE) {
            emulator->memory[emulator->regs.index + 0] = vars[instr->X] / 100;
            emulator->memory[emulator->regs.index + 1] = (vars[instr->X] / 10) % 10;
            emulator->memory[emulator->regs.index + 2] = vars[instr->X] % 10;
        }
        dprintf("Do BCD on V%X: I = %d, I + 1 = %d, I + 2 = %d\n", instr->X,
                emulator->memory[emulator->regs.index], emulator->memory[emulator->regs.index + 1],
                emulator->memory[emulator->regs.index + 2]);
        break;
    case STORE_MEM:
        for (int i = 0; i <= instr->X; ++i) {
            emulator->memory[emulator->regs.index + i] = vars[i];
        }
        dprintf("Store registers V0 - V%X to memory\n", instr->X);
        break;
    case LOAD_MEM:
        for (int i = 0; i <= instr->X; ++i) {
            vars[i] = emulator->memory[emulator->regs.index + i];
        }
        dprintf("Load registers V0 - V%X from memory\n", instr->X);
        break;
    default:
        printf("Illegal instruction %X%03X\n", instr->kind, instr->NNN);
        emulator->flags.exit = true;
        break;
    }
}

void set_pixel(chip8 *emulator, int x, int y) {
    uint8_t *pixel  = &emulator->display[y * DISPLAY_WIDTH + x];
    *pixel         ^= 1;
    if (*pixel == 0) {
        emulator->regs.vars[FLAG_REGISTER] = true;
    }
}

uint32_t emulate_instruction(uint32_t interval, void *userdata) {
    chip8 *emulator = (chip8 *)userdata;
    if (emulator->pc > MEM_SIZE) {
        puts("Critical: Program counter out of bounds, exiting.");
        emulator->flags.exit = true;
    }
    if (emulator->flags.exit) {
        return 0;
    }
#ifdef NDEBUG
    if (!emulator->flags.run) {
        if (!emulator->flags.step) {
            return interval;
        } else {
            emulator->flags.step = false;
        }
    }
#endif
    chip8_instruction instr = get_cur_instr(emulator);
    uint8_t *vars           = emulator->regs.vars;
    dprintf("Address: %04X, Instruction: %X%03X, Description: ", emulator->pc, instr.kind,
            instr.NNN);
    emulator->pc += 2;
    switch (instr.kind) {
    case 0x0:
        if (instr.NN == CLEAR_SCREEN) {
            memset(emulator->display, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT);
            dprintf("Clear screen\n");
            emulator->flags.draw = true;
        } else if (instr.NN == RETURN_FROM_SUBROUTINE) {
            emulator->pc = emulator->stack[--emulator->sp];
            dprintf("Return from subroutine to address %04X\n", emulator->pc);
        } else {
            dprintf("Ignored instruction %X%03X\n", instr.kind, instr.NNN);
        }
        break;
    case JUMP:
        emulator->pc = instr.NNN;
        dprintf("Jump to address %04X\n", emulator->pc);
        break;
    case CALL_SUBROUTINE:
        emulator->stack[emulator->sp++] = emulator->pc;
        emulator->pc                    = instr.NNN;
        dprintf("Call subroutine %04X\n", emulator->pc);
        break;
    case JUMP_EQ_IMMEDIATE:
        dprintf("Skip if V%X == NN: %02X == %02X (%sskipped)\n", instr.X, vars[instr.X], instr.NN,
                vars[instr.X] == instr.NN ? "" : "not ");
        if (vars[instr.X] == instr.NN) {
            emulator->pc += 2;
        }
        break;
    case JUMP_NEQ_IMMEDIATE:
        dprintf("Skip if V%X != NN: %02X != %02X (%sskipped)\n", instr.X, vars[instr.X], instr.NN,
                vars[instr.X] != instr.NN ? "" : "not ");
        if (vars[instr.X] != instr.NN) {
            emulator->pc += 2;
        }
        break;
    case JUMP_EQ_REG:
        dprintf("Skip if V%X == VY: %02X == %02X (%sskipped)\n", instr.X, vars[instr.X],
                vars[instr.Y], vars[instr.X] == vars[instr.Y] ? "" : "not ");
        if (vars[instr.X] == vars[instr.Y]) {
            emulator->pc += 2;
        }
        break;
    case SET_REG_TO_IMMEDIATE:
        dprintf("Set V%X to %02X\n", instr.X, instr.NN);
        vars[instr.X] = instr.NN;
        break;
    case ADD_IMMEDIATE_TO_REG:
        dprintf("Add %02X to V%X\n", instr.NN, instr.X);
        vars[instr.X] += instr.NN;
        break;
    case MATH: handle_arithmetic(emulator, &instr); break;
    case JUMP_NEQ_REG:
        dprintf("Skip if V%X != V%X: %X != %X (%sskipped)\n", instr.X, instr.Y, vars[instr.X],
                vars[instr.Y], vars[instr.X] != vars[instr.Y] ? "" : "not ");
        if (vars[instr.X] != vars[instr.Y]) {
            emulator->pc += 2;
        }
        break;
    case SET_INDEX:
        dprintf("Set index register I = %X\n", instr.NNN);
        emulator->regs.index = instr.NNN;
        break;
    case JUMP_WITH_OFFSET:
        emulator->pc = instr.NNN + vars[0];
        dprintf("Set pc to %04X + %X (%X)\n", instr.NNN, vars[0], emulator->pc);
        break;
    case RAND:
        vars[instr.X] = (rand() % 256) & instr.NN;
        dprintf("Set V%X to random number %X\n", instr.X, vars[instr.X]);
        break;
    case DISPLAY:
        emulator->flags.draw = true;
        vars[FLAG_REGISTER]  = 0;
        uint16_t x           = vars[instr.X] % DISPLAY_WIDTH;
        uint16_t y           = vars[instr.Y] % DISPLAY_HEIGHT;
        uint8_t sprite;
        for (uint8_t row = 0; row < instr.N; ++row) {
            sprite = emulator->memory[emulator->regs.index + row];
            for (uint8_t col = 0; col < COL_MAX; ++col) {
                uint8_t cur_x = x + col;
                uint8_t cur_y = y + row;
                if (cur_x < DISPLAY_WIDTH && cur_y < DISPLAY_HEIGHT) {
                    // bounds check on the sprite
                    if (sprite & (1 << (COL_MAX - col - 1))) {
                        // check each bit of the sprite
                        set_pixel(emulator, cur_x, cur_y);
                    }
                }
            }
        }
        dprintf("Draw sprite of height %d at (%d, %d) from I (%X)\n", instr.N, x, y,
                emulator->regs.index);
        break;
    case KEY:
        switch (instr.NN) {
        case KEY_DOWN:
            if (emulator->keypad[vars[instr.X]]) {
                emulator->pc += 2;
            }
            dprintf("Skip if key %X in V%X is held (%sskipped)\n", vars[instr.X], instr.X,
                    emulator->keypad[vars[instr.X]] ? "" : "not ");
            break;
        case KEY_UP:
            if (!emulator->keypad[vars[instr.X]]) {
                emulator->pc += 2;
            }
            dprintf("Skip if key %X in V%X is not held (%sskipped)\n", vars[instr.X], instr.X,
                    emulator->keypad[vars[instr.X]] ? "not " : "");
            break;
        default:
            printf("Illegal instruction %X%03X\n", instr.kind, instr.NNN);
            emulator->flags.exit = true;
        }
        break;
    case MISC: handle_misc(emulator, &instr); break;
    default:
        printf("Illegal instruction %X%03X\n", instr.kind, instr.NNN);
        emulator->flags.exit = true;
    }
    if (emulator->flags.exit) {
        return 0;
    } else {
        return interval;
    }
}

void draw_pixels(sdl_state *state, const chip8 *emulator) {
    SDL_SetRenderDrawColor(state->renderer, state->fg_colour.r, state->fg_colour.g,
                           state->fg_colour.b, SDL_ALPHA_OPAQUE);
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; ++i) {
        if (!emulator->display[i]) {
            continue;
        }
        int x          = (i % DISPLAY_WIDTH) * emulator->scale_x / DISPLAY_WIDTH;
        int y          = (i / DISPLAY_WIDTH) * emulator->scale_y / DISPLAY_HEIGHT;
        SDL_Rect pixel = {
            .x = x,
            .y = y,
            .w = emulator->scale_x / DISPLAY_WIDTH + emulator->slightly_bigger_pixels,
            .h = emulator->scale_y / DISPLAY_HEIGHT + emulator->slightly_bigger_pixels,
        };
        SDL_RenderFillRect(state->renderer, &pixel);
    }
}

void render(sdl_state *state, chip8 *emulator) {
    if (emulator->flags.draw) {
        SDL_SetRenderDrawColor(state->renderer, state->bg_colour.r, state->bg_colour.g,
                               state->bg_colour.b, SDL_ALPHA_OPAQUE);
        SDL_RenderClear(state->renderer);
        draw_pixels(state, emulator);
        SDL_RenderPresent(state->renderer);
        emulator->flags.draw = false;
    }
}

void poll_input(chip8 *chip8) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT: chip8->flags.exit = true; return;
        case SDL_KEYDOWN:
            switch (event.key.keysym.scancode) {
            case SDL_SCANCODE_ESCAPE: chip8->flags.exit = true; return;
            case SDL_SCANCODE_1: chip8->keypad[0x1] = 1; break;
            case SDL_SCANCODE_2: chip8->keypad[0x2] = 1; break;
            case SDL_SCANCODE_3: chip8->keypad[0x3] = 1; break;
            case SDL_SCANCODE_4: chip8->keypad[0xC] = 1; break;
            case SDL_SCANCODE_Q: chip8->keypad[0x4] = 1; break;
            case SDL_SCANCODE_W: chip8->keypad[0x5] = 1; break;
            case SDL_SCANCODE_E: chip8->keypad[0x6] = 1; break;
            case SDL_SCANCODE_R: chip8->keypad[0xD] = 1; break;
            case SDL_SCANCODE_A: chip8->keypad[0x7] = 1; break;
            case SDL_SCANCODE_S: chip8->keypad[0x8] = 1; break;
            case SDL_SCANCODE_D: chip8->keypad[0x9] = 1; break;
            case SDL_SCANCODE_F: chip8->keypad[0xE] = 1; break;
            case SDL_SCANCODE_Z: chip8->keypad[0xA] = 1; break;
            case SDL_SCANCODE_X: chip8->keypad[0x0] = 1; break;
            case SDL_SCANCODE_C: chip8->keypad[0xB] = 1; break;
            case SDL_SCANCODE_V: chip8->keypad[0xF] = 1; break;
#ifdef NDEBUG
            case SDL_SCANCODE_RIGHT: chip8->flags.step = true; break;
            case SDL_SCANCODE_SPACE: chip8->flags.run ^= true;
#endif
            default: break;
            }
            break;
        case SDL_KEYUP:
            switch (event.key.keysym.scancode) {
            case SDL_SCANCODE_ESCAPE: chip8->flags.exit = true; return;
            case SDL_SCANCODE_1: chip8->keypad[0x1] = 0; break;
            case SDL_SCANCODE_2: chip8->keypad[0x2] = 0; break;
            case SDL_SCANCODE_3: chip8->keypad[0x3] = 0; break;
            case SDL_SCANCODE_4: chip8->keypad[0xC] = 0; break;
            case SDL_SCANCODE_Q: chip8->keypad[0x4] = 0; break;
            case SDL_SCANCODE_W: chip8->keypad[0x5] = 0; break;
            case SDL_SCANCODE_E: chip8->keypad[0x6] = 0; break;
            case SDL_SCANCODE_R: chip8->keypad[0xD] = 0; break;
            case SDL_SCANCODE_A: chip8->keypad[0x7] = 0; break;
            case SDL_SCANCODE_S: chip8->keypad[0x8] = 0; break;
            case SDL_SCANCODE_D: chip8->keypad[0x9] = 0; break;
            case SDL_SCANCODE_F: chip8->keypad[0xE] = 0; break;
            case SDL_SCANCODE_Z: chip8->keypad[0xA] = 0; break;
            case SDL_SCANCODE_X: chip8->keypad[0x0] = 0; break;
            case SDL_SCANCODE_C: chip8->keypad[0xB] = 0; break;
            case SDL_SCANCODE_V: chip8->keypad[0xF] = 0; break;
            default: break;
            }
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
    if (data->emulator->flags.exit) {
        return 0;
    }
    if (data->emulator->timers.delay > 0) {
        --data->emulator->timers.delay;
    }

    if (data->emulator->timers.sound > 0) {
        --data->emulator->timers.sound;
        if (!data->emulator->flags.beep && data->emulator->timers.sound > 0) {
            beep_set(true, &data->emulator->flags, data->audio);
        }
    } else {
        if (data->emulator->flags.beep) {
            beep_set(false, &data->emulator->flags, data->audio);
        }
    }
    return interval;
}

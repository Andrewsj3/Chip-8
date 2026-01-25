#ifndef CHIP8_H
#define CHIP8_H

#include <stdbool.h>
#include <stdint.h>
#define REGISTER_COUNT 16
#define DISPLAY_WIDTH 64
#define DISPLAY_HEIGHT 32
#define MEM_SIZE 4096 // 4kB

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

void dprintf(const char *restrict format, ...);

uint32_t emulate_instruction(uint32_t interval, void *userdata);
void poll_input(chip8_flags *flags);
uint32_t update_timers(uint32_t interval, void* userdata);

#endif

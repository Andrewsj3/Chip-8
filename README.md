# Chip-8

A [CHIP-8](https://en.wikipedia.org/wiki/CHIP-8) interpreter/emulator written in C using
[SDL2](https://www.libsdl.org/).

CHIP-8 ROMs are not provided here, but you can find some at <https://github.com/kripod/chip8-roms>

## Dependencies
* C compiler that supports C99
* SDL2

## Quick Start
```sh
make # DEBUG=1 for debug messages
./chip-8 /path/to/rom
```

> [!IMPORTANT]
> This is in early development, do not expect most roms to work.
> Only the IBM ROM works at the moment.

EXE = chip-8
SRC_DIR = ./src
OBJ_DIR = ./obj
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))
CFLAGS += -Wall -Wextra -Wpedantic -Wformat
CSTD = -std=c99
LDFLAGS = -I/usr/include/SDL2 -D_GNU_SOURCE=1 -D_REENTRANT -lSDL2
MAKEFLAGS += --no-builtin-rules
ifeq ($(DEBUG),1)
	CFLAGS += -DNDEBUG
endif

$(OBJ_DIR)/%.o:$(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(CSTD) -c -o $@ $<

all: $(OBJ_DIR) $(EXE)
	@echo All sources built successfully

$(OBJ_DIR):
	mkdir -p ./obj

$(EXE): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(CSTD)

clean:
	rm -f $(EXE) $(OBJS)

.PHONY: clean tidy run

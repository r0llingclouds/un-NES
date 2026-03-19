# Makefile - NES Game Port (SDL2/C)
#
# Build: make
# Run:   make run
# Clean: make clean
#
# Dependencies: SDL2 (brew install sdl2)

CC       = cc
CFLAGS   = -Wall -Wextra -Wpedantic -std=c11 -O2
LDFLAGS  =

# SDL2 flags
SDL_CFLAGS  := $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null)
SDL_LDFLAGS := $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2 2>/dev/null)

CFLAGS  += $(SDL_CFLAGS)
LDFLAGS += $(SDL_LDFLAGS) -lm

# Source files
SRCDIR  = src
SOURCES = $(SRCDIR)/main.c \
          $(SRCDIR)/game.c \
          $(SRCDIR)/ppu.c \
          $(SRCDIR)/input.c \
          $(SRCDIR)/audio.c \
          $(SRCDIR)/data.c \
          $(SRCDIR)/utils.c

OBJECTS = $(SOURCES:.c=.o)
TARGET  = nes_port

.PHONY: all clean run debug

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

debug: CFLAGS += -g -DDEBUG -O0
debug: clean all

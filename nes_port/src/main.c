/*
 * main.c - NES Game Port: SDL2 Main Loop
 *
 * Replaces the NES's NMI-driven architecture with a standard
 * 60fps game loop using SDL2.
 *
 * Original architecture:
 *   Reset ($8000): Init PPU, stack, RAM → infinite loop
 *   NMI ($8082): Per-frame: PPU DMA, game dispatch, input, render
 *
 * SDL equivalent:
 *   main(): SDL init → game_init() → loop { input → update → render }
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "game.h"
#include "ppu.h"
#include "input.h"
#include "audio.h"
#include "data.h"

#define WINDOW_SCALE  3
#define WINDOW_W      (NES_SCREEN_W * WINDOW_SCALE)
#define WINDOW_H      (NES_SCREEN_H * WINDOW_SCALE)
#define TARGET_FPS    60
#define FRAME_TIME_MS (1000 / TARGET_FPS)

static SDL_Window   *window   = NULL;
static SDL_Renderer *renderer = NULL;
static GameState     game;

static bool init_sdl(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    window = SDL_CreateWindow(
        "NES Port",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    /* Maintain NES aspect ratio when resizing */
    SDL_RenderSetLogicalSize(renderer, NES_SCREEN_W, NES_SCREEN_H);

    return true;
}

static void cleanup(void) {
    ppu_cleanup();
    audio_cleanup();
    input_cleanup();
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window)   SDL_DestroyWindow(window);
    SDL_Quit();
}

int main(int argc, char *argv[]) {
    const char *rom_path = "test.nes";
    const char *chr_path = "assets/chr.bin";

    if (argc > 1) rom_path = argv[1];
    if (argc > 2) chr_path = argv[2];

    /* Load ROM data */
    if (data_load_rom(rom_path) != 0) {
        fprintf(stderr, "Failed to load ROM: %s\n", rom_path);
        return 1;
    }

    /* Initialize SDL */
    if (!init_sdl()) {
        cleanup();
        return 1;
    }

    /* Initialize subsystems */
    if (!ppu_init(chr_path)) {
        fprintf(stderr, "Failed to initialize PPU\n");
        cleanup();
        return 1;
    }

    if (!ppu_create_display(renderer)) {
        fprintf(stderr, "Failed to create PPU display\n");
        cleanup();
        return 1;
    }

    if (!audio_init()) {
        fprintf(stderr, "Warning: Audio init failed, continuing without sound\n");
    }

    input_init();

    /* Initialize game state (replaces Reset at $8000) */
    game_init(&game);

    printf("NES Port running. Press ESC to quit.\n");

    /* Main game loop (replaces NMI-driven infinite loop at $8057) */
    while (game.running) {
        Uint32 frame_start = SDL_GetTicks();

        /* 1. Input (replaces L8E5C controller read) */
        if (!input_update(&game)) {
            game.running = false;
            break;
        }

        /* 2. Game logic update (replaces NMI handler game logic) */
        game_update(&game);

        /* 3. Render (replaces PPU hardware rendering) */
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        ppu_render_frame(renderer, &game);
        SDL_RenderPresent(renderer);

        /* 4. Frame timing (target 60fps like NES) */
        Uint32 frame_time = SDL_GetTicks() - frame_start;
        if (frame_time < FRAME_TIME_MS) {
            SDL_Delay(FRAME_TIME_MS - frame_time);
        }

        game.frame_count++;
    }

    cleanup();
    return 0;
}

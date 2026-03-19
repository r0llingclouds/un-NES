/*
 * ppu.h - PPU Emulation Layer
 *
 * Replaces NES PPU hardware with SDL2 software rendering.
 * Decodes CHR tiles, renders nametables and sprites to an SDL surface.
 */

#ifndef PPU_H
#define PPU_H

#include <SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include "game.h"

/* NES master palette: 64 RGB colors */
extern const uint32_t nes_palette_rgb[64];

/* Decoded tile pixel data (pre-decoded from CHR ROM) */
/* Each tile is 8x8 pixels, 2-bit color indices */
extern uint8_t chr_tiles[NES_CHR_TILES][8][8];

/* Initialize PPU: load CHR ROM, decode tiles */
bool ppu_init(const char *chr_path);

/* Create the SDL rendering surface/texture */
bool ppu_create_display(SDL_Renderer *renderer);

/* Render a full frame to the SDL texture */
void ppu_render_frame(SDL_Renderer *renderer, GameState *gs);

/* Cleanup PPU resources */
void ppu_cleanup(void);

/* --- PPU Buffer Operations (replacing NES PPU register writes) --- */

/* Write to nametable (replaces PPUADDR+PPUDATA writes to $2000-$2FFF) */
void ppu_write_nametable(GameState *gs, uint16_t addr, uint8_t value);

/* Write to palette (replaces writes to $3F00-$3F1F) */
void ppu_write_palette(GameState *gs, uint8_t index, uint8_t color);

/* Process PPU update buffer (replaces L8EDD/L8E92 buffer processing) */
void ppu_process_update_buffer(GameState *gs);

/* Process PPU buffer commands starting at any CPU address (RAM or ROM) */
void ppu_process_buffer_at(GameState *gs, uint16_t addr);

/* Fill nametables with blank tile $24 and clear attributes (replaces L8E19) */
void ppu_fill_nametables(GameState *gs);

/* Clear OAM (set all sprites off-screen, replaces L8220) */
void ppu_clear_oam(GameState *gs);

/* Set scroll position */
void ppu_set_scroll(GameState *gs, uint8_t x, uint8_t y);

#endif /* PPU_H */

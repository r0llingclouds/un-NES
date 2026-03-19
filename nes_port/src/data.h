/*
 * data.h - ROM Data Tables
 *
 * Contains all data extracted from the NES ROM:
 * - PPU update buffers
 * - Palette data
 * - Nametable/tilemap data
 * - Sprite frame tables
 * - Level/entity data
 * - Lookup tables
 */

#ifndef DATA_H
#define DATA_H

#include <stdint.h>

/* Full PRG ROM data (32KB) for direct access during porting.
 * Addresses in the original code ($8000-$FFFF) map to prg_rom[addr - 0x8000]. */
extern const uint8_t prg_rom[0x8000];

/* CHR ROM data (8KB raw) */
extern const uint8_t chr_rom[0x2000];

/* NES Master Palette (64 colors → RGB values) */
extern const uint32_t nes_master_palette[64];

/* Game mode dispatch table (decoded from L8E04 tables at $8218) */
extern const uint16_t game_mode_handlers[4];

/* PPU update buffer addresses (from $805A/$806D tables) */
extern const uint16_t ppu_buffer_addrs[6];

/* Load PRG ROM from .nes file */
int data_load_rom(const char *nes_path);

/* Read a byte from PRG ROM using original 6502 address space ($8000-$FFFF) */
static inline uint8_t rom_read(uint16_t addr) {
    return prg_rom[addr - 0x8000];
}

/* Read a 16-bit little-endian value from PRG ROM */
static inline uint16_t rom_read16(uint16_t addr) {
    return prg_rom[addr - 0x8000] | (prg_rom[addr - 0x8000 + 1] << 8);
}

#endif /* DATA_H */

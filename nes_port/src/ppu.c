/*
 * ppu.c - PPU Emulation Layer
 *
 * Software rendering replacement for the NES PPU.
 * Decodes CHR ROM tiles and renders nametable + sprites
 * to an SDL texture.
 *
 * Original PPU operations replaced:
 *   - PPUADDR/PPUDATA writes → ppu_write_nametable/palette
 *   - OAM DMA → direct OAM buffer access
 *   - PPUSCROLL → scroll offset in rendering
 *   - PPUCTRL/PPUMASK → control flags in PPUState
 */

#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "ppu.h"
#include "game.h"
#include "data.h"

/* Decoded CHR tile pixels: [tile_index][row][col] = 2-bit color */
uint8_t chr_tiles[NES_CHR_TILES][8][8];

/* SDL texture for rendering */
static SDL_Texture *screen_texture = NULL;

/* Frame buffer (ARGB8888) */
static uint32_t framebuffer[NES_SCREEN_W * NES_SCREEN_H];

/* ============================================================
 * NES Master Palette (2C02 NTSC)
 * 64 colors, each as 0xAARRGGBB
 * ============================================================ */
const uint32_t nes_palette_rgb[64] = {
    0xFF545454, 0xFF001E74, 0xFF081090, 0xFF300088,
    0xFF440064, 0xFF5C0030, 0xFF540400, 0xFF3C1800,
    0xFF202A00, 0xFF083A00, 0xFF004000, 0xFF003C00,
    0xFF00323C, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFF989698, 0xFF084CC4, 0xFF3032EC, 0xFF5C1EE4,
    0xFF8814B0, 0xFFA01464, 0xFF982220, 0xFF783C00,
    0xFF545A00, 0xFF287200, 0xFF087C00, 0xFF007628,
    0xFF006678, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFECEEEC, 0xFF4C9AEC, 0xFF7080EC, 0xFFA06CEC,
    0xFFD060EC, 0xFFEC60C0, 0xFFEC7274, 0xFFCC8C38,
    0xFFB09C18, 0xFF7CB010, 0xFF4CC034, 0xFF38C878,
    0xFF38B4CC, 0xFF3C3C3C, 0xFF000000, 0xFF000000,
    0xFFECEEEC, 0xFFA8CCEC, 0xFFBCBCEC, 0xFFD4B2EC,
    0xFFECAEEC, 0xFFECAED4, 0xFFECB4B0, 0xFFE4C490,
    0xFFCCD278, 0xFFB4DE78, 0xFFA8E290, 0xFF98E2B4,
    0xFFA0D6E4, 0xFFA0A2A0, 0xFF000000, 0xFF000000,
};

/* ============================================================
 * Decode CHR ROM tiles
 * ============================================================
 * NES CHR format: 16 bytes per tile
 *   Bytes 0-7:  bit plane 0 (low bit of each pixel)
 *   Bytes 8-15: bit plane 1 (high bit of each pixel)
 *   Each byte = 8 pixels in a row, MSB = leftmost pixel
 *   2-bit color index: (plane1_bit << 1) | plane0_bit
 */
static void decode_chr(const uint8_t *chr_data, int num_tiles) {
    for (int t = 0; t < num_tiles; t++) {
        const uint8_t *tile_data = &chr_data[t * 16];
        for (int row = 0; row < 8; row++) {
            uint8_t plane0 = tile_data[row];
            uint8_t plane1 = tile_data[row + 8];
            for (int col = 0; col < 8; col++) {
                int bit = 7 - col;
                uint8_t color = ((plane1 >> bit) & 1) << 1 |
                                ((plane0 >> bit) & 1);
                chr_tiles[t][row][col] = color;
            }
        }
    }
}

/* ============================================================
 * ppu_init - Load and decode CHR ROM
 * ============================================================ */
bool ppu_init(const char *chr_path) {
    FILE *f = fopen(chr_path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open CHR file: %s\n", chr_path);
        /* Try to use chr_rom from data.c if loaded */
        if (chr_rom[0] != 0 || chr_rom[1] != 0) {
            decode_chr(chr_rom, NES_CHR_TILES);
            printf("PPU: Decoded %d tiles from embedded CHR ROM\n", NES_CHR_TILES);
            return true;
        }
        return false;
    }

    uint8_t chr_data[0x2000];
    size_t read = fread(chr_data, 1, 0x2000, f);
    fclose(f);

    if (read < 0x2000) {
        fprintf(stderr, "CHR file too small: %zu bytes (expected 8192)\n", read);
        return false;
    }

    decode_chr(chr_data, NES_CHR_TILES);
    printf("PPU: Decoded %d tiles from %s\n", NES_CHR_TILES, chr_path);
    return true;
}

/* ============================================================
 * ppu_create_display
 * ============================================================ */
bool ppu_create_display(SDL_Renderer *renderer) {
    screen_texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        NES_SCREEN_W, NES_SCREEN_H);

    if (!screen_texture) {
        fprintf(stderr, "Failed to create screen texture: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

/* ============================================================
 * Render background (nametable)
 * ============================================================
 * Renders the 32x30 tile nametable with attribute-based palette
 * selection and scrolling.
 */
static void render_background(GameState *gs) {
    int base_nt = gs->ppu.ctrl & PPUCTRL_NAMETABLE;
    int bg_table = (gs->ppu.ctrl & PPUCTRL_BG_TABLE) ? 256 : 0;
    int sx = gs->ppu.scroll_x;
    int sy = gs->ppu.scroll_y;

    /* Only render if BG enabled */
    if (!(gs->ppu.mask & PPUMASK_BG_ENABLE)) return;

    for (int screen_y = 0; screen_y < NES_SCREEN_H; screen_y++) {
        int tile_y = ((screen_y + sy) / 8) % 30;
        int fine_y = (screen_y + sy) % 8;

        for (int screen_x = 0; screen_x < NES_SCREEN_W; screen_x++) {
            int tile_x = ((screen_x + sx) / 8) % 32;
            int fine_x = (screen_x + sx) % 8;

            /* Which nametable (handle mirroring) */
            int nt = base_nt;
            int abs_tile_x = (screen_x + sx) / 8;
            int abs_tile_y = (screen_y + sy) / 8;
            if (abs_tile_x >= 32) nt ^= 1;  /* Horizontal mirroring */
            if (abs_tile_y >= 30) nt ^= 2;
            nt &= 3;

            /* Get tile index from nametable */
            int nt_offset = tile_y * 32 + tile_x;
            uint8_t tile_idx = gs->ppu.nametable[nt][nt_offset];

            /* Get attribute (palette select) */
            int attr_x = tile_x / 4;
            int attr_y = tile_y / 4;
            int attr_offset = attr_y * 8 + attr_x;
            uint8_t attr_byte = gs->ppu.attrtable[nt][attr_offset];

            /* Extract 2-bit palette index from attribute byte */
            int shift = ((tile_y / 2) % 2) * 4 + ((tile_x / 2) % 2) * 2;
            int palette_idx = (attr_byte >> shift) & 0x03;

            /* Get pixel color from tile */
            uint8_t pixel = chr_tiles[bg_table + tile_idx][fine_y][fine_x];

            /* Color 0 = universal background */
            uint32_t color;
            if (pixel == 0) {
                color = nes_palette_rgb[gs->ppu.palette[0]];
            } else {
                uint8_t pal_entry = gs->ppu.palette[palette_idx * 4 + pixel];
                color = nes_palette_rgb[pal_entry & 0x3F];
            }

            framebuffer[screen_y * NES_SCREEN_W + screen_x] = color;
        }
    }
}

/* ============================================================
 * Render sprites from OAM buffer
 * ============================================================
 * NES OAM entry: Y, tile, attributes, X
 * Attributes: VHP000CC
 *   V = vertical flip, H = horizontal flip
 *   P = priority (0=front, 1=behind BG)
 *   CC = palette (4-7)
 */
static void render_sprites(GameState *gs) {
    if (!(gs->ppu.mask & PPUMASK_SPR_ENABLE)) return;

    int spr_table = (gs->ppu.ctrl & PPUCTRL_SPR_TABLE) ? 256 : 0;

    /* Render in reverse order (sprite 0 has highest priority) */
    for (int i = 63; i >= 0; i--) {
        OAMEntry *spr = &gs->oam[i];

        /* Off-screen check */
        if (spr->y >= 0xEF) continue;

        int spr_y = spr->y + 1;  /* OAM Y is 1 less than display Y */
        int spr_x = spr->x;
        int tile = spr->tile;
        int attr = spr->attr;
        bool flip_h = (attr & 0x40) != 0;
        bool flip_v = (attr & 0x80) != 0;
        bool behind_bg = (attr & 0x20) != 0;
        int palette = (attr & 0x03) + 4;  /* Sprite palettes are 4-7 */

        for (int row = 0; row < 8; row++) {
            int draw_y = spr_y + row;
            if (draw_y < 0 || draw_y >= NES_SCREEN_H) continue;

            int tile_row = flip_v ? (7 - row) : row;

            for (int col = 0; col < 8; col++) {
                int draw_x = spr_x + col;
                if (draw_x < 0 || draw_x >= NES_SCREEN_W) continue;

                int tile_col = flip_h ? (7 - col) : col;
                uint8_t pixel = chr_tiles[spr_table + tile][tile_row][tile_col];

                /* Transparent */
                if (pixel == 0) continue;

                /* Behind-BG priority: don't draw over non-zero BG pixels */
                if (behind_bg) {
                    /* Check if BG pixel is non-transparent */
                    uint32_t bg = framebuffer[draw_y * NES_SCREEN_W + draw_x];
                    uint32_t bg_color0 = nes_palette_rgb[gs->ppu.palette[0]];
                    if (bg != bg_color0) continue;
                }

                uint8_t pal_entry = gs->ppu.palette[palette * 4 + pixel];
                framebuffer[draw_y * NES_SCREEN_W + draw_x] =
                    nes_palette_rgb[pal_entry & 0x3F];
            }
        }
    }
}

/* ============================================================
 * ppu_render_frame
 * ============================================================ */
void ppu_render_frame(SDL_Renderer *renderer, GameState *gs) {
    /* Clear framebuffer to background color */
    uint32_t bg_color = nes_palette_rgb[gs->ppu.palette[0] & 0x3F];
    for (int i = 0; i < NES_SCREEN_W * NES_SCREEN_H; i++) {
        framebuffer[i] = bg_color;
    }

    /* Render background tiles */
    render_background(gs);

    /* Render sprites */
    render_sprites(gs);

    /* Upload framebuffer to texture */
    SDL_UpdateTexture(screen_texture, NULL, framebuffer,
                      NES_SCREEN_W * sizeof(uint32_t));
    SDL_RenderCopy(renderer, screen_texture, NULL, NULL);
}

/* ============================================================
 * PPU Helper Functions
 * ============================================================ */

void ppu_write_nametable(GameState *gs, uint16_t addr, uint8_t value) {
    /* Map PPU address $2000-$2FFF to nametable */
    addr &= 0x0FFF;

    /* Horizontal mirroring: $2000=$2400, $2800=$2C00 */
    int nt = (addr / 0x400);
    /* Horizontal mirroring maps: 0→0, 1→0, 2→1, 3→1 */
    int mirrored_nt = nt / 2;
    int offset = addr & 0x3FF;

    if (offset < 960) {
        gs->ppu.nametable[mirrored_nt][offset] = value;
    } else if (offset < 1024) {
        gs->ppu.attrtable[mirrored_nt][offset - 960] = value;
    }
}

void ppu_write_palette(GameState *gs, uint8_t index, uint8_t color) {
    index &= 0x1F;
    /* Mirror $3F10/$3F14/$3F18/$3F1C to $3F00/$3F04/$3F08/$3F0C */
    if (index >= 16 && (index & 0x03) == 0) {
        index -= 16;
    }
    gs->ppu.palette[index] = color & 0x3F;
}

/*
 * ppu_process_update_buffer - Replaces L8EDD/L8E92
 *
 * Original buffer format (pointed to by $00/$01):
 *   Byte 0: if 0, no update. Otherwise: PPU high address
 *   Byte 1: PPU low address
 *   Byte 2: control byte:
 *     bit 7: direction (0=horizontal, 1=vertical)
 *     bit 6: RLE mode (0=sequential, 1=repeat single value)
 *     bits 5-0: length
 *   Bytes 3+: data (length bytes, or 1 byte if RLE)
 *   Repeats until byte 0 is 0
 */
/* Read a byte from RAM or ROM based on address (like 6502 LDA ($00),Y) */
static uint8_t read_byte(GameState *gs, uint16_t addr) {
    if (addr >= 0x8000) return rom_read(addr);
    return gs->ram[addr & 0x7FF];
}

void ppu_process_buffer_at(GameState *gs, uint16_t buf_addr) {
    /* Read buffer from RAM or ROM */
    int pos = 0;
    while (pos < 512) {
        uint8_t first = read_byte(gs, buf_addr + pos);
        if (first == 0 || first == 0xFF) break;  /* $00 or $FF terminates */

        uint16_t ppu_addr = (first << 8) | read_byte(gs, buf_addr + pos + 1);
        uint8_t control = read_byte(gs, buf_addr + pos + 2);
        int length = (control & 0x3F);
        if (length == 0) length = 64;
        bool vertical = (control & 0x80) != 0;
        bool rle = (control & 0x40) != 0;
        pos += 3;

        for (int i = 0; i < length; i++) {
            uint8_t value;
            if (rle) {
                value = read_byte(gs, buf_addr + pos);
            } else {
                value = read_byte(gs, buf_addr + pos + i);
            }

            /* Write to appropriate PPU memory */
            if (ppu_addr >= 0x3F00 && ppu_addr < 0x3F20) {
                ppu_write_palette(gs, ppu_addr & 0x1F, value);
            } else if (ppu_addr >= 0x2000 && ppu_addr < 0x3000) {
                ppu_write_nametable(gs, ppu_addr, value);
            }

            /* Advance PPU address */
            ppu_addr += vertical ? 32 : 1;
        }

        pos += rle ? 1 : length;
    }
}

void ppu_process_update_buffer(GameState *gs) {
    /* Get buffer address from PPU update tables */
    if (gs->ppu_update_idx >= 6) return;

    uint16_t buf_addr = ppu_buffer_addrs[gs->ppu_update_idx];
    ppu_process_buffer_at(gs, buf_addr);
}

void ppu_fill_nametables(GameState *gs) {
    /* Replaces L8E19: fill nametables with tile $24, attributes with $00 */
    for (int nt = 0; nt < 4; nt++) {
        memset(gs->ppu.nametable[nt], 0x24, 960);
        memset(gs->ppu.attrtable[nt], 0x00, 64);
    }
}

void ppu_clear_oam(GameState *gs) {
    /* Set all sprites off-screen (Y = $F8), like L8220 */
    for (int i = 0; i < NES_OAM_SPRITES; i++) {
        gs->oam[i].y = 0xF8;
        gs->oam[i].tile = 0;
        gs->oam[i].attr = 0;
        gs->oam[i].x = 0;
    }
}

void ppu_set_scroll(GameState *gs, uint8_t x, uint8_t y) {
    gs->ppu.scroll_x = x;
    gs->ppu.scroll_y = y;
    gs->scroll_x = x;
    gs->scroll_y = y;
}

void ppu_cleanup(void) {
    if (screen_texture) {
        SDL_DestroyTexture(screen_texture);
        screen_texture = NULL;
    }
}

#!/usr/bin/env python3
"""Generate data.c with embedded PRG ROM and CHR ROM as C arrays."""

import os

with open('test.nes', 'rb') as f:
    rom = f.read()

prg = rom[0x10:0x8010]        # 32KB PRG
chr_data = rom[0x8010:0xA010] # 8KB CHR

lines = []
lines.append('/*')
lines.append(' * data.c - ROM Data Tables')
lines.append(' *')
lines.append(' * Auto-generated from test.nes')
lines.append(' * PRG ROM: 32KB ($8000-$FFFF)')
lines.append(' * CHR ROM: 8KB (512 tiles)')
lines.append(' */')
lines.append('')
lines.append('#include <stdio.h>')
lines.append('#include <string.h>')
lines.append('#include "data.h"')
lines.append('')

# Game mode handler addresses
lines.append('/* Game mode dispatch addresses (from L8E04 table at $8218) */')
lines.append('const uint16_t game_mode_handlers[4] = {')
lines.append('    0x8231,  /* Mode 0: Title/Init */')
lines.append('    0xAEDC,  /* Mode 1: Gameplay */')
lines.append('    0x838B,  /* Mode 2: Battle */')
lines.append('    0x9218,  /* Mode 3: Cutscene */')
lines.append('};')
lines.append('')

# PPU buffer addresses
lines.append('/* PPU update buffer addresses (from tables at $805A/$806D) */')
lines.append('const uint16_t ppu_buffer_addrs[6] = {')
addrs = []
for i in range(6):
    lo = prg[0x5A + i]
    hi = prg[0x6D + i]
    addr = lo | (hi << 8)
    addrs.append(f'    0x{addr:04X}')
lines.append(',\n'.join(addrs))
lines.append('};')
lines.append('')

# NES master palette
lines.append('/* NES 2C02 NTSC Master Palette (64 RGB colors) */')
lines.append('const uint32_t nes_master_palette[64] = {')
pal = [
    0x545454,0x001E74,0x081090,0x300088,0x440064,0x5C0030,0x540400,0x3C1800,
    0x202A00,0x083A00,0x004000,0x003C00,0x00323C,0x000000,0x000000,0x000000,
    0x989698,0x084CC4,0x3032EC,0x5C1EE4,0x8814B0,0xA01464,0x982220,0x783C00,
    0x545A00,0x287200,0x087C00,0x007628,0x006678,0x000000,0x000000,0x000000,
    0xECEEEC,0x4C9AEC,0x7080EC,0xA06CEC,0xD060EC,0xEC60C0,0xEC7274,0xCC8C38,
    0xB09C18,0x7CB010,0x4CC034,0x38C878,0x38B4CC,0x3C3C3C,0x000000,0x000000,
    0xECEEEC,0xA8CCEC,0xBCBCEC,0xD4B2EC,0xECAEEC,0xECAED4,0xECB4B0,0xE4C490,
    0xCCD278,0xB4DE78,0xA8E290,0x98E2B4,0xA0D6E4,0xA0A2A0,0x000000,0x000000,
]
for i in range(0, 64, 8):
    row = ', '.join(f'0xFF{c:06X}' for c in pal[i:i+8])
    lines.append(f'    {row},')
lines.append('};')
lines.append('')

# PRG ROM array
lines.append('/* PRG ROM (32KB, mapped $8000-$FFFF) */')
lines.append('const uint8_t prg_rom[0x8000] = {')
for i in range(0, len(prg), 16):
    chunk = prg[i:i+16]
    hex_vals = ', '.join(f'0x{b:02X}' for b in chunk)
    if i % 256 == 0:
        lines.append(f'    /* ${0x8000+i:04X} */ {hex_vals},')
    else:
        lines.append(f'    {hex_vals},')
lines.append('};')
lines.append('')

# CHR ROM array
lines.append('/* CHR ROM (8KB, 512 tiles) */')
lines.append('const uint8_t chr_rom[0x2000] = {')
for i in range(0, len(chr_data), 16):
    chunk = chr_data[i:i+16]
    hex_vals = ', '.join(f'0x{b:02X}' for b in chunk)
    if i % 256 == 0:
        lines.append(f'    /* tile {i//16} */ {hex_vals},')
    else:
        lines.append(f'    {hex_vals},')
lines.append('};')
lines.append('')

# data_load_rom function
lines.append('int data_load_rom(const char *nes_path) {')
lines.append('    /* ROM data is already embedded in prg_rom[] and chr_rom[] */')
lines.append('    FILE *f = fopen(nes_path, "rb");')
lines.append('    if (!f) {')
lines.append('        printf("Note: ROM file not found (%s), using embedded data\\n", nes_path);')
lines.append('        return 0;')
lines.append('    }')
lines.append('    fclose(f);')
lines.append('    printf("ROM data loaded (embedded, 32KB PRG + 8KB CHR)\\n");')
lines.append('    return 0;')
lines.append('}')

with open('nes_port/src/data.c', 'w') as f:
    f.write('\n'.join(lines) + '\n')

print(f'Generated data.c: {os.path.getsize("nes_port/src/data.c")} bytes')

#!/usr/bin/env python3
"""
Extract CHR ROM data from an iNES NES ROM and render two PNG sprite sheets.

Layout assumptions (from iNES header):
  - 16-byte header at offset 0x00
  - 32 KB PRG ROM at offset 0x10
  - 8 KB CHR ROM at offset 0x8010

CHR tile format (NES 2-bit planar):
  Each tile is 8x8 pixels, 16 bytes total (two 8-byte bit planes).
  For row r (0-7):
      plane0_byte = tile_data[r]       (bit 0 of each pixel)
      plane1_byte = tile_data[r + 8]   (bit 1 of each pixel)
  Pixel column c (0=leftmost, 7=rightmost):
      bit0 = (plane0_byte >> (7 - c)) & 1
      bit1 = (plane1_byte >> (7 - c)) & 1
      color_index = (bit1 << 1) | bit0   -> 0..3
"""

import os
import shutil
from PIL import Image

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
ROM_PATH = os.path.join(BASE_DIR, "test.nes")

# iNES offsets
HEADER_SIZE = 16
PRG_SIZE = 0x8000  # 32 KB
CHR_OFFSET = HEADER_SIZE + PRG_SIZE  # 0x8010
CHR_SIZE = 0x2000  # 8 KB

TILE_BYTES = 16
TILES_PER_TABLE = 256
GRID_SIDE = 16  # 16x16 tiles per sheet
TILE_PX = 8
SHEET_PX = GRID_SIDE * TILE_PX  # 128

PALETTE = [
    (0, 0, 0),        # 0 - black
    (85, 85, 85),     # 1 - dark
    (170, 170, 170),  # 2 - medium
    (255, 255, 255),  # 3 - light
]


def decode_tile(tile_data):
    """Return an 8x8 list-of-lists of 2-bit color indices."""
    pixels = []
    for row in range(8):
        plane0 = tile_data[row]
        plane1 = tile_data[row + 8]
        row_pixels = []
        for col in range(8):
            bit0 = (plane0 >> (7 - col)) & 1
            bit1 = (plane1 >> (7 - col)) & 1
            row_pixels.append((bit1 << 1) | bit0)
        pixels.append(row_pixels)
    return pixels


def render_sheet(chr_data, table_index):
    """Render 256 tiles from the given pattern table into a 128x128 RGBA image."""
    img = Image.new("RGBA", (SHEET_PX, SHEET_PX), (0, 0, 0, 255))
    offset = table_index * TILES_PER_TABLE * TILE_BYTES

    for tile_idx in range(TILES_PER_TABLE):
        tile_start = offset + tile_idx * TILE_BYTES
        tile_data = chr_data[tile_start:tile_start + TILE_BYTES]
        pixels = decode_tile(tile_data)

        grid_x = (tile_idx % GRID_SIDE) * TILE_PX
        grid_y = (tile_idx // GRID_SIDE) * TILE_PX

        for row in range(8):
            for col in range(8):
                ci = pixels[row][col]
                r, g, b = PALETTE[ci]
                a = 0 if ci == 0 else 255
                img.putpixel((grid_x + col, grid_y + row), (r, g, b, a))

    return img


def main():
    with open(ROM_PATH, "rb") as f:
        rom = f.read()

    print(f"ROM size: {len(rom)} bytes")

    # Extract and save raw CHR data
    chr_data = rom[CHR_OFFSET:CHR_OFFSET + CHR_SIZE]
    chr_bin_path = os.path.join(BASE_DIR, "chr.bin")
    with open(chr_bin_path, "wb") as f:
        f.write(chr_data)
    print(f"Wrote {len(chr_data)} bytes to {chr_bin_path}")

    # Render pattern table 0 -> bg_tiles.png
    bg_img = render_sheet(chr_data, 0)
    bg_path = os.path.join(BASE_DIR, "bg_tiles.png")
    bg_img.save(bg_path)
    print(f"Saved {bg_path}")

    # Render pattern table 1 -> spr_tiles.png
    spr_img = render_sheet(chr_data, 1)
    spr_path = os.path.join(BASE_DIR, "spr_tiles.png")
    spr_img.save(spr_path)
    print(f"Saved {spr_path}")

    # Copy outputs to nes_port/assets/
    assets_dir = os.path.join(BASE_DIR, "nes_port", "assets")
    for src in [chr_bin_path, bg_path, spr_path]:
        dst = os.path.join(assets_dir, os.path.basename(src))
        shutil.copy2(src, dst)
        print(f"Copied -> {dst}")

    print("Done.")


if __name__ == "__main__":
    main()

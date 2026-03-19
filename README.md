# NES Reverse-Engineering & SDL2 Port

Reverse-engineering of an NES ROM (`test.nes`) with a full port to C/SDL2. The project includes the raw 6502 disassembly, an annotated version with labeled subroutines, Python tooling for asset extraction and code generation, and a playable C port that replaces NES hardware with SDL2.

## Project Structure

```
assembly/
├── test.nes                 # Original NES ROM (32KB PRG + 8KB CHR, Mapper 0)
├── test.asm                 # Raw da65 disassembly of test.nes
├── test_annotated.asm       # Annotated disassembly with labels and comments
├── nes.info                 # da65 info file (labels, address ranges)
├── chr.bin                  # Extracted 8KB CHR ROM (512 tiles)
├── bg_tiles.png             # Background tile sheet rendered from CHR
├── spr_tiles.png            # Sprite tile sheet rendered from CHR
├── chr_extract.py           # Extracts CHR data and renders tile PNGs
├── gen_data_c.py            # Generates data.c from ROM (PRG + CHR arrays)
└── nes_port/
    ├── Makefile
    ├── assets/
    │   ├── chr.bin          # CHR ROM for runtime tile decoding
    │   ├── bg_tiles.png
    │   └── spr_tiles.png
    └── src/
        ├── main.c           # SDL2 init, 60fps game loop
        ├── game.c / game.h  # Game state, mode dispatch, entities, score
        ├── ppu.c / ppu.h    # PPU emulation: nametables, sprites, palettes
        ├── input.c / input.h# Controller input (keyboard + gamepad)
        ├── audio.c / audio.h# Audio stubs
        ├── data.c / data.h  # Auto-generated ROM data tables
        └── utils.c / utils.h# BCD math, address translation helpers
```

## The NES Game

The original ROM is a scrolling action game with four game modes:

| Mode | ID | Description |
|------|----|-------------|
| Init/Title | 0 | Title screen and initialization |
| Gameplay | 1 | Main scrolling action |
| Battle | 2 | Battle sequences |
| Cutscene | 3 | Cutscene playback |

The game features an entity system (16 entities), BCD score tracking with high score, pause functionality (Start button), and a sprite-0-hit-based scroll split.

## Building & Running the C Port

### Prerequisites

- C11 compiler (cc/gcc/clang)
- SDL2 (`brew install sdl2` on macOS, `apt install libsdl2-dev` on Debian/Ubuntu)

### Build

```bash
cd nes_port
make          # Build the port
make debug    # Build with debug symbols and -O0
make clean    # Remove build artifacts
```

### Run

```bash
make run
# or directly:
./nes_port [rom_path] [chr_path]
```

Default paths: `test.nes` for the ROM, `assets/chr.bin` for CHR data. The window opens at 3x scale (768x720) and is resizable with aspect ratio preserved.

## Python Tools

**`chr_extract.py`** — Extracts the 8KB CHR ROM from `test.nes`, saves `chr.bin`, and renders `bg_tiles.png` / `spr_tiles.png` using the NES 2-bit planar tile format. Requires Pillow.

```bash
python3 chr_extract.py
```

**`gen_data_c.py`** — Reads `test.nes` and generates `nes_port/src/data.c` with the PRG ROM, CHR ROM, game mode handler addresses, and other data tables embedded as C arrays.

```bash
python3 gen_data_c.py
```

## Assembly Files

- **`test.asm`** — Raw disassembly produced by [da65](https://cc65.github.io/doc/da65.html) using the info file `nes.info`
- **`test_annotated.asm`** — Hand-annotated version with named subroutines, comments explaining game logic, RAM map, and hardware register usage
- **`nes.info`** — da65 configuration: label definitions, code/data segment ranges, address mappings

## Controls

### Player 1

| NES Button | Keyboard | Gamepad |
|------------|----------|---------|
| A | Z | A |
| B | X | B / X |
| Select | Backspace | Back |
| Start | Enter | Start |
| D-pad | Arrow keys | D-pad / Left stick |

### Player 2

| NES Button | Keyboard | Gamepad |
|------------|----------|---------|
| A | J | A |
| B | K | B / X |
| Select | Right Shift | Back |
| Start | L | Start |
| D-pad | W/A/S/D | D-pad / Left stick |

## Architecture Notes

- **Direct RAM mapping**: `GameState` contains a `uint8_t ram[0x800]` array mirroring NES work RAM, allowing ported code to use the same addresses as the original assembly
- **PPU emulation**: Software rendering replaces NES PPU hardware — nametables, attribute tables, palettes, and OAM are maintained in structs and rendered to an SDL texture each frame
- **NMI → game loop**: The NES NMI-driven architecture (reset at `$8000`, NMI at `$8082`) is replaced by a standard 60fps `poll → update → render` loop with vsync
- **Entity system**: 16 entities with parallel arrays for state and type, matching the original `$0730+` / `$06A1+` RAM layout
- **BCD scores**: Score and high-score use 6-byte BCD, matching the original `$07DD-$07E2` layout and comparison routine at `$8F97`

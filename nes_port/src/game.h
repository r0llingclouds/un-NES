/*
 * game.h - NES Game Port: Core game state and type definitions
 *
 * Ported from test.asm (da65 disassembly of test.nes)
 * Original ROM: 32KB PRG + 8KB CHR, Mapper 0, Horizontal Mirroring
 */

#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * NES Hardware Constants
 * ============================================================ */

#define NES_SCREEN_W     256
#define NES_SCREEN_H     240
#define NES_TILE_SIZE    8
#define NES_NAMETABLE_W  32   /* tiles */
#define NES_NAMETABLE_H  30   /* tiles */
#define NES_OAM_SPRITES  64
#define NES_PALETTE_SIZE 64   /* master palette entries */
#define NES_CHR_TILES    512  /* total tiles in CHR ROM */

/* PPU Control bits (PPUCTRL $2000) */
#define PPUCTRL_NMI_ENABLE   0x80
#define PPUCTRL_SPRITE_SIZE  0x20  /* 0=8x8, 1=8x16 */
#define PPUCTRL_BG_TABLE     0x10  /* BG pattern table select */
#define PPUCTRL_SPR_TABLE    0x08  /* Sprite pattern table select */
#define PPUCTRL_VRAM_INC     0x04  /* 0=+1, 1=+32 */
#define PPUCTRL_NAMETABLE    0x03  /* Base nametable (0-3) */

/* PPU Mask bits (PPUMASK $2001) */
#define PPUMASK_EMPHASIS     0xE0
#define PPUMASK_SPR_ENABLE   0x10
#define PPUMASK_BG_ENABLE    0x08
#define PPUMASK_SPR_LEFT8    0x04
#define PPUMASK_BG_LEFT8     0x02
#define PPUMASK_GRAYSCALE    0x01

/* Controller button bits */
#define BTN_A       0x80
#define BTN_B       0x40
#define BTN_SELECT  0x20
#define BTN_START   0x10
#define BTN_UP      0x08
#define BTN_DOWN    0x04
#define BTN_LEFT    0x02
#define BTN_RIGHT   0x01

/* Game modes (stored in $0770) */
#define MODE_INIT       0
#define MODE_GAMEPLAY   1
#define MODE_BATTLE     2
#define MODE_CUTSCENE   3

/* ============================================================
 * OAM Sprite Entry
 * ============================================================ */
typedef struct {
    uint8_t y;          /* Y position (0-239, $F8=off-screen) */
    uint8_t tile;       /* Tile index in pattern table */
    uint8_t attr;       /* Attributes: vhp000cc */
    uint8_t x;          /* X position (0-255) */
} OAMEntry;

/* ============================================================
 * PPU State (replaces NES PPU hardware)
 * ============================================================ */
typedef struct {
    uint8_t nametable[4][960];    /* 4 nametables, 32x30 tiles */
    uint8_t attrtable[4][64];     /* 4 attribute tables */
    uint8_t palette[32];          /* BG palettes (0-15) + SPR palettes (16-31) */
    uint8_t ctrl;                 /* Shadow of PPUCTRL ($0778) */
    uint8_t mask;                 /* Shadow of PPUMASK ($0779) */
    uint8_t scroll_x;             /* Horizontal scroll ($073F) */
    uint8_t scroll_y;             /* Vertical scroll ($0740) */
    uint16_t vram_addr;           /* Current VRAM address for writes */
    bool addr_latch;              /* High/low byte toggle for PPUADDR */
} PPUState;

/* ============================================================
 * Game State (replaces NES RAM $0000-$07FF)
 * ============================================================ */
typedef struct {
    /* Full NES work RAM - direct mapping for easy porting */
    uint8_t ram[0x800];

    /* Named fields (aliases into ram[] for clarity) */
    /* Zero page temporaries */
    /* $00-$01: indirect pointer (temp) */
    /* $04-$05: dispatch return address */
    /* $06-$07: dispatch target address */
    /* $09: animation frame counter */

    /* OAM shadow buffer at $0200-$02FF */
    OAMEntry oam[NES_OAM_SPRITES];

    /* PPU state */
    PPUState ppu;

    /* Controller state */
    uint8_t joy1;       /* Player 1 buttons ($06FC) */
    uint8_t joy2;       /* Player 2 buttons ($06FD) */
    uint8_t joy1_prev;  /* Previous frame ($074A) */
    uint8_t joy2_prev;  /* Previous frame ($074B) */

    /* Game state */
    uint8_t game_mode;       /* $0770: current game mode (0-3) */
    uint8_t game_submode;    /* $0772: submode within current mode */
    uint8_t ppu_update_idx;  /* $0773: PPU update buffer index */
    uint8_t rendering_on;    /* $0774: rendering enabled flag */
    uint8_t nmi_control;     /* $0776: NMI control flags */
    uint8_t frame_delay;     /* $0777: frame delay counter */

    /* Scroll */
    uint8_t scroll_x;  /* $073F */
    uint8_t scroll_y;  /* $0740 */

    /* Timers */
    uint8_t anim_timer;       /* $0747: animation timer */
    uint8_t timer_base;       /* $077F: base timer */
    uint8_t timers[0x28];     /* $0780-$07A7: general-purpose timers */

    /* Score (6-byte BCD) */
    uint8_t score[6];         /* $07DD-$07E2 */
    uint8_t high_score[6];    /* $07D7-$07DC */

    /* World/level tracking */
    uint8_t world_number;       /* $075F: current world (0-indexed, display as +1) */
    uint8_t level_number;       /* $075C: current level within world (0-indexed) */
    uint8_t remaining_lives;    /* $075A: lives remaining */
    uint8_t area_type;          /* $0744: area palette type (0=water,1=ground,2=underground,3=castle) */

    /* Entity data */
    uint8_t entity_state[16]; /* $0730+: entity states */
    uint8_t entity_type[16];  /* $06A1+: entity types */

    /* Misc */
    uint8_t warm_boot;        /* $07FF: magic value $A5 */
    uint8_t sprite_hit_flag;  /* $0722: sprite 0 hit flag */

    /* Frame counter (increments every frame) */
    uint32_t frame_count;
    bool running;
} GameState;

/* ============================================================
 * Function Declarations
 * ============================================================ */

/* Initialization */
void game_init(GameState *gs);

/* Per-frame update (replaces NMI handler logic) */
void game_update(GameState *gs);

/* Game mode handlers */
void game_mode_init(GameState *gs);       /* Mode 0 - Title/Init */
void game_mode_gameplay(GameState *gs);   /* Mode 1 - Main gameplay */
void game_mode_battle(GameState *gs);     /* Mode 2 - Battle */
void game_mode_cutscene(GameState *gs);   /* Mode 3 - Cutscene */

/* Pause/state check (L8182) */
void game_pause_check(GameState *gs);

/* Score management (L8F97) */
void score_compare_update(GameState *gs);

/* Timer management */
void timers_update(GameState *gs);

/* Entity management */
void entities_update(GameState *gs);

#endif /* GAME_H */

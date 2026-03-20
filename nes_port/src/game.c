/*
 * game.c - Game State Machine and Core Logic
 *
 * Ported from test.asm NMI handler ($8082) and game mode dispatch.
 *
 * Original flow per frame (NMI):
 *   1. PPU register updates (ctrl, mask)
 *   2. OAM DMA ($0200 → PPU OAM)
 *   3. Process PPU update buffer ($0773 index)
 *   4. Sound driver (LF2D0)
 *   5. Input read (L8E5C)
 *   6. Pause check (L8182)
 *   7. Score update (L8F97)
 *   8. Timer decrements
 *   9. Sprite 0 hit wait + scroll set
 *  10. Game mode dispatch (L8212 via L8E04)
 */

#include <string.h>
#include <stdio.h>
#include "game.h"
#include "ppu.h"
#include "audio.h"
#include "data.h"
#include "utils.h"

/* ============================================================
 * game_init - Replaces Reset handler at $8000
 * ============================================================
 * Original:
 *   SEI, CLD, PPUCTRL=$10, stack=$FF
 *   Wait vblank x2
 *   Validate RAM magic ($07FF == $A5)
 *   JSR L90CC (init)
 *   SND_CHN=$0F, PPUMASK=$06
 *   JSR L8220 (clear OAM)
 *   JSR L8E19 (PPU setup)
 *   Enable NMI, infinite loop
 */
void game_init(GameState *gs) {
    /* Clear all RAM (like the reset routine's RAM init) */
    memset(gs->ram, 0, sizeof(gs->ram));
    memset(gs->oam, 0, sizeof(gs->oam));
    memset(&gs->ppu, 0, sizeof(gs->ppu));

    /* Set initial state */
    gs->game_mode = MODE_INIT;
    gs->game_submode = 0;
    gs->ppu_update_idx = 0;
    gs->rendering_on = 1;       /* $0774 = 1 (inc $0774 at $804C) */
    gs->nmi_control = 0;
    gs->frame_delay = 0;

    /* PPU control shadows */
    gs->ppu.ctrl = PPUCTRL_NMI_ENABLE | PPUCTRL_BG_TABLE;  /* $80 | $10 */
    gs->ppu.mask = 0x1E;   /* BG+SPR enabled + left-8-pixel show (NMI ORs $1E at $809C) */

    /* Clear OAM (set all sprites off-screen, like L8220) */
    ppu_clear_oam(gs);

    /* Fill nametables with blank tile $24 (like L8E19) */
    ppu_fill_nametables(gs);

    /* Init scrolling */
    gs->scroll_x = 0;
    gs->scroll_y = 0;

    /* Set warm boot magic ($8034: LDA #$A5, STA $07FF) */
    gs->warm_boot = 0xA5;
    gs->ram[0x7FF] = 0xA5;
    gs->ram[0x7A7] = 0xA5;   /* $07A7 = $A5 at $8039 */

    /* Clear score */
    score_clear(gs->score);
    score_clear(gs->high_score);

    gs->frame_count = 0;
    gs->running = true;

    printf("Game initialized (mode=%d)\n", gs->game_mode);
}

/* ============================================================
 * game_update - Replaces NMI handler logic at $8082
 * ============================================================
 * Called once per frame (60fps). Runs all game subsystems.
 */
void game_update(GameState *gs) {
    /* Process PPU update buffer (NMI always processes, even when idx=0) */
    ppu_process_update_buffer(gs);
    gs->ppu_update_idx = 0;  /* Clear after processing ($80DB) */

    /* Sound driver update (replaces JSR LF2D0) */
    audio_update(gs);

    /* Pause/state check (replaces JSR L8182) */
    game_pause_check(gs);

    /* Score comparison/update (replaces JSR L8F97) */
    score_compare_update(gs);

    /* Timer decrements (replaces $80F6-$8119 timer logic) */
    if (!(gs->nmi_control & 0x01)) {  /* bit 0 of $0776 not set */
        timers_update(gs);
    }

    /* Increment frame counter ($8119: INC $09) */
    gs->ram[0x09]++;

    /* Game mode dispatch (replaces L8212 via L8E04 jump table) */
    switch (gs->game_mode) {
    case MODE_INIT:
        game_mode_init(gs);
        break;
    case MODE_GAMEPLAY:
        game_mode_gameplay(gs);
        break;
    case MODE_BATTLE:
        game_mode_battle(gs);
        break;
    case MODE_CUTSCENE:
        game_mode_cutscene(gs);
        break;
    default:
        /* Unknown mode - reset to init */
        gs->game_mode = MODE_INIT;
        break;
    }

    /* Update scroll position ($815C-$8165) */
    gs->ppu.scroll_x = gs->scroll_x;
    gs->ppu.scroll_y = gs->scroll_y;
}

/* ============================================================
 * game_pause_check - Replaces L8182
 * ============================================================
 * Original checks:
 *   if game_mode == 2: handle pause
 *   if game_mode == 1 && submode == 3: handle pause
 *   If Start pressed and not already paused:
 *     toggle pause, set delay timer
 */
void game_pause_check(GameState *gs) {
    bool can_pause = false;

    if (gs->game_mode == MODE_BATTLE) {
        can_pause = true;
    } else if (gs->game_mode == MODE_GAMEPLAY && gs->game_submode == 5) {
        can_pause = true;
    }

    if (!can_pause) return;

    /* Frame delay active? ($0777 != 0) */
    if (gs->frame_delay > 0) {
        gs->frame_delay--;
        return;
    }

    /* Check Start button ($06FC & $10) */
    if (gs->joy1 & BTN_START) {
        /* Already paused? ($0776 & $80) */
        if (gs->nmi_control & 0x80) return;

        /* Set pause delay ($0777 = $2B) */
        gs->frame_delay = 0x2B;

        /* Toggle pause bit in nmi_control */
        uint8_t ctrl = gs->nmi_control;
        gs->ram[0xFA] = ctrl + 1;  /* STY $FA at $81B5 */
        ctrl ^= 0x01;              /* Toggle bit 0 */
        ctrl |= 0x80;              /* Set "pause pressed" flag */
        gs->nmi_control = ctrl;
    } else {
        /* Clear pause-pressed flag */
        gs->nmi_control &= 0x7F;
    }
}

/* ============================================================
 * score_compare_update - Replaces L8F97
 * ============================================================
 * Compares current score ($07DD, 6 bytes) against high score ($07D7).
 * If current > high, copies current to high score.
 * Called twice: once with offset 5, once with offset 11.
 */
void score_compare_update(GameState *gs) {
    if (score_compare(gs->score, gs->high_score) > 0) {
        score_copy(gs->high_score, gs->score);
    }
}

/* ============================================================
 * timers_update - Replaces timer decrement logic at $80F6-$8119
 * ============================================================
 * Original:
 *   if $0747 != 0: dec $0747, skip rest
 *   dec $077F (base timer)
 *   if < 0: reset to $14, process extended timers ($0780-$07A3)
 *   else: process timers $0780-$0794 (21 timers)
 *   decrement non-zero timers
 */
void timers_update(GameState *gs) {
    /* Animation timer ($0747) */
    if (gs->anim_timer > 0) {
        gs->anim_timer--;
        if (gs->anim_timer > 0) return;
    }

    /* Determine timer range */
    int count = 0x14;  /* Default: 21 timers */

    if (gs->timer_base > 0) {
        gs->timer_base--;
    }

    if (gs->timer_base == 0) {
        gs->timer_base = 0x14;    /* Reset to 20 */
        count = 0x23;             /* Extended: 36 timers */
    }

    /* Decrement active timers */
    for (int i = count; i >= 0; i--) {
        if (gs->timers[i] > 0) {
            gs->timers[i]--;
        }
    }
}

/* ============================================================
 * HUD Drawing Helper
 * ============================================================ */
static void draw_hud(GameState *gs) {
    /* "MARIO" at row 2, col 3 ($2043) */
    static const uint8_t mario_text[] = {0x16,0x0A,0x1B,0x12,0x18};
    for (int i = 0; i < 5; i++)
        ppu_write_nametable(gs, 0x2043 + i, mario_text[i]);
    /* Score "000000" at row 3, col 3 ($2063) */
    for (int i = 0; i < 6; i++)
        ppu_write_nametable(gs, 0x2063 + i, gs->score[i]);
    /* Coin icon area: "x00" at row 3, col 11 */
    ppu_write_nametable(gs, 0x206B, 0x2E); /* "x" */
    ppu_write_nametable(gs, 0x206D, 0x00); /* "0" */
    ppu_write_nametable(gs, 0x206E, 0x00); /* "0" */
    /* "WORLD" at row 2, col 19 ($2053) */
    static const uint8_t world_text[] = {0x20,0x18,0x1B,0x15,0x0D};
    for (int i = 0; i < 5; i++)
        ppu_write_nametable(gs, 0x2053 + i, world_text[i]);
    /* "1-1" at row 3, col 20 ($207A) */
    ppu_write_nametable(gs, 0x207A, gs->world_number + 1);
    ppu_write_nametable(gs, 0x207B, 0x28);  /* dash */
    ppu_write_nametable(gs, 0x207C, gs->level_number + 1);
}

/* ============================================================
 * Game Mode Handlers
 * ============================================================ */

/*
 * Mode 0: Init/Title Screen
 * Dispatched from $8231 via L8E04
 * Loads $0772 (submode) and dispatches further
 */
void game_mode_init(GameState *gs) {
    /* Sub-dispatch based on $0772 (game_submode) */
    switch (gs->game_submode) {
    case 0:
        /* Initial setup - clear screen, load title graphics */
        ppu_clear_oam(gs);

        /* Set up initial palette from ROM at $85CF */
        for (int i = 0; i < 32; i++) {
            ppu_write_palette(gs, i, rom_read(0x85CF + i));
        }

        /* Load title screen nametable data from ROM at $8752 */
        ppu_process_buffer_at(gs, 0x8752);

        /* State 12 equivalent: copy title screen PPU commands from CHR ROM to RAM.
           Original code reads PPU $1EC0-$1FF9 via PPUDATA into RAM $0300-$0439. */
        extern const uint8_t chr_rom[];
        memcpy(&gs->ram[0x300], &chr_rom[0x1EC0], 314);

        /* Process the title screen PPU buffer (logo, menu, copyright) */
        ppu_process_buffer_at(gs, 0x0300);

        /* Fix 10a: Write initial HUD digit values */
        for (int i = 0; i < 6; i++)
            ppu_write_nametable(gs, 0x2063 + i, 0x00);  /* P1 score "000000" */
        ppu_write_nametable(gs, 0x206D, 0x00);           /* Coin count "0" */
        ppu_write_nametable(gs, 0x206E, 0x00);           /* Coin count "0" */
        ppu_write_nametable(gs, 0x207A, 0x01);           /* World "1" */
        ppu_write_nametable(gs, 0x207B, 0x28);           /* Dash "-" */
        ppu_write_nametable(gs, 0x207C, 0x01);           /* Level "1" */
        for (int i = 0; i < 6; i++)
            ppu_write_nametable(gs, 0x22F1 + i, 0x00);  /* TOP score "000000" at cols 17-22 */

        /* Fix 10b: Make copyright text visible — palette 1 color 1 → white */
        ppu_write_palette(gs, 5, 0x30);

        /* Fix 10c: Menu cursor sprite next to "1 PLAYER GAME" */
        gs->oam[0].y    = 143;   /* Row 18 = pixel 144, OAM Y = display_y - 1 */
        gs->oam[0].tile = 0x75;  /* Mushroom sprite tile */
        gs->oam[0].attr = 0x00;  /* SPR palette 0, no flip, in front of BG */
        gs->oam[0].x    = 80;    /* Left of "1 PLAYER GAME" */

        /* Select BG pattern table (CHR bank) */
        gs->ppu.ctrl |= PPUCTRL_BG_TABLE;

        /* Transition to title screen display */
        gs->game_submode = 1;
        break;

    case 1:
        /* Title screen - wait for Start */
        if (gs->joy1 & BTN_START) {
            /* Initialize game state (matches title_start_game at $82D8) */
            gs->world_number = 0;     /* World 1 (0-indexed) */
            gs->level_number = 0;     /* Level 1 (0-indexed) */
            gs->remaining_lives = 2;  /* 3 lives (display as lives+1) */
            gs->area_type = 1;        /* Ground/overworld */
            memset(gs->score, 0, 6);
            memset(gs->high_score, 0, 6);

            gs->game_mode = MODE_GAMEPLAY;
            gs->game_submode = 0;
        }
        break;

    default:
        gs->game_submode = 0;
        break;
    }
}

/*
 * Mode 1: Main Gameplay
 * Dispatched from $AEDC via L8E04
 * This is the primary game loop where the action happens.
 */
void game_mode_gameplay(GameState *gs) {
    switch (gs->game_submode) {
    case 0:
        /* PPU clear + OAM clear (submode 0) */
        ppu_clear_oam(gs);
        ppu_fill_nametables(gs);
        /* Clear title screen PPU command buffer in RAM so it stops
           being re-applied by ppu_process_update_buffer each frame */
        memset(&gs->ram[0x300], 0, 314);
        gs->scroll_x = 0;
        gs->scroll_y = 0;
        gs->game_submode = 1;
        break;

    case 1:
        /* Load gameplay palette + draw HUD */
        ppu_process_buffer_at(gs, 0x8CA4);
        draw_hud(gs);
        gs->game_submode = 2;
        break;

    case 2: {
        /* Draw "WORLD  1-1" center text + lives */
        /* "WORLD" at row 12, col 11 ($218B) */
        uint16_t base = 0x218B;
        static const uint8_t world_label[] = {0x20,0x18,0x1B,0x15,0x0D};
        for (int i = 0; i < 5; i++)
            ppu_write_nametable(gs, base + i, world_label[i]);
        /* Space then "1-1" */
        ppu_write_nametable(gs, base + 6, 0x24); /* space */
        ppu_write_nametable(gs, base + 7, gs->world_number + 1);
        ppu_write_nametable(gs, base + 8, 0x28); /* dash */
        ppu_write_nametable(gs, base + 9, gs->level_number + 1);

        /* Lives display at row 15: Mario sprite + "x" + count */
        gs->oam[0].y    = 119;  /* Row 15 pixel = 15*8=120, OAM Y = 119 */
        gs->oam[0].tile = 0x75; /* Small Mario sprite */
        gs->oam[0].attr = 0x00;
        gs->oam[0].x    = 100;  /* Col ~12.5 */
        ppu_write_nametable(gs, 0x21EE, 0x2E);  /* "x" at col 14 */
        ppu_write_nametable(gs, 0x21F0, gs->remaining_lives + 1); /* Lives count */

        /* Start delay timer: ~2.5 seconds = 150 frames at 60fps */
        gs->frame_delay = 150;
        gs->game_submode = 3;
        break;
    }

    case 3:
        /* Wait on "WORLD 1-1" screen */
        if (gs->frame_delay > 0) {
            gs->frame_delay--;
        } else {
            ppu_clear_oam(gs);
            ppu_fill_nametables(gs);
            gs->game_submode = 4;
        }
        break;

    case 4:
        /* Level loading stub — re-draw HUD on cleared screen */
        ppu_process_buffer_at(gs, 0x8CA4);
        draw_hud(gs);
        gs->game_submode = 5;
        break;

    case 5:
        /* Active gameplay — D-pad scroll placeholder */
        entities_update(gs);
        if (gs->joy1 & BTN_RIGHT) {
            if (gs->scroll_x < 255) gs->scroll_x++;
        }
        if (gs->joy1 & BTN_LEFT) {
            if (gs->scroll_x > 0) gs->scroll_x--;
        }
        if (gs->joy1 & BTN_UP) {
            if (gs->scroll_y > 0) gs->scroll_y--;
        }
        if (gs->joy1 & BTN_DOWN) {
            if (gs->scroll_y < 239) gs->scroll_y++;
        }
        break;

    default:
        gs->game_submode = 0;
        break;
    }
}

/*
 * Mode 2: Battle
 * Dispatched from $838B via L8E04
 */
void game_mode_battle(GameState *gs) {
    switch (gs->game_submode) {
    case 0:
        /* Battle init */
        ppu_clear_oam(gs);
        gs->game_submode = 1;
        break;

    case 1:
        /* Battle active */
        entities_update(gs);
        break;

    default:
        gs->game_submode = 0;
        break;
    }
}

/*
 * Mode 3: Cutscene
 * Dispatched from $9218 via L8E04
 */
void game_mode_cutscene(GameState *gs) {
    switch (gs->game_submode) {
    case 0:
        /* Cutscene init */
        gs->game_submode = 1;
        break;

    case 1:
        /* Cutscene playback - advance on button press */
        if (gs->joy1 & (BTN_A | BTN_START)) {
            gs->game_mode = MODE_GAMEPLAY;
            gs->game_submode = 0;
        }
        break;

    default:
        gs->game_submode = 0;
        break;
    }
}

/* ============================================================
 * entities_update - Entity management
 * ============================================================
 * Replaces entity update loops that iterate over entity arrays
 * in RAM ($06A1+, $0730+, etc.)
 */
void entities_update(GameState *gs) {
    /* Iterate over entity slots and update active ones */
    for (int i = 0; i < 16; i++) {
        if (gs->entity_state[i] == 0) continue;

        /* Entity is active - update based on type */
        uint8_t type = gs->entity_type[i];

        /* Placeholder: entity type dispatch would go here */
        /* Original code uses L8E04 dispatch tables per entity type */
        (void)type;
    }
}

/*
 * audio.h - Basic Sound Effects via SDL2
 *
 * Provides a simplified APU emulation for sound effects.
 * The original ROM uses SQ1, SQ2, Triangle, and Noise channels
 * with a sound driver at $F2D0+.
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include "game.h"

/* Initialize audio system */
bool audio_init(void);

/* Update sound (called once per frame, replaces LF2D0) */
void audio_update(GameState *gs);

/* Play a sound effect by ID */
void audio_play_sfx(uint8_t sfx_id);

/* Stop all sound */
void audio_stop_all(void);

/* Cleanup audio system */
void audio_cleanup(void);

#endif /* AUDIO_H */

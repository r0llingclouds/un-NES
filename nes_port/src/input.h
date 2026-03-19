/*
 * input.h - Controller Input via SDL
 *
 * Maps SDL keyboard/gamepad to NES controller button bits.
 * Replaces the JOY1/JOY2 read routine at $8E5C.
 */

#ifndef INPUT_H
#define INPUT_H

#include <SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include "game.h"

/* Initialize input system (open gamepads if available) */
void input_init(void);

/* Read input from SDL events and update GameState controller fields.
 * Returns false if the user requested quit (window close, etc.) */
bool input_update(GameState *gs);

/* Cleanup input system */
void input_cleanup(void);

#endif /* INPUT_H */

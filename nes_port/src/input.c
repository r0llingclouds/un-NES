/*
 * input.c - Controller Input via SDL
 *
 * Replaces the NES controller read routine at $8E5C.
 *
 * Original ($8E5C):
 *   Write $01 then $00 to JOY1 ($4016) to strobe
 *   Read 8 bits from JOY1 (player 1)
 *   Read 8 bits from JOY2 ($4017) (player 2)
 *   Store to $06FC (joy1) and $06FD (joy2)
 *   Handle Start+Select masking ($074A)
 *
 * NES button bit layout (MSB to LSB):
 *   A B Select Start Up Down Left Right
 *   80 40  20    10  08  04   02    01
 *
 * Default keyboard mapping:
 *   A=Z, B=X, Select=Backspace, Start=Enter
 *   D-pad=Arrow keys
 *
 * Gamepad mapping (SDL GameController):
 *   A=A, B=B/X, Select=Back, Start=Start
 *   D-pad=D-pad or left stick
 */

#include <stdio.h>
#include "input.h"

static SDL_GameController *gamepad1 = NULL;
static SDL_GameController *gamepad2 = NULL;

void input_init(void) {
    /* Open first two available game controllers */
    for (int i = 0; i < SDL_NumJoysticks() && i < 2; i++) {
        if (SDL_IsGameController(i)) {
            SDL_GameController *gc = SDL_GameControllerOpen(i);
            if (gc) {
                if (!gamepad1) {
                    gamepad1 = gc;
                    printf("Input: Player 1 gamepad: %s\n",
                           SDL_GameControllerName(gc));
                } else if (!gamepad2) {
                    gamepad2 = gc;
                    printf("Input: Player 2 gamepad: %s\n",
                           SDL_GameControllerName(gc));
                }
            }
        }
    }

    if (!gamepad1) {
        printf("Input: No gamepad found, using keyboard\n");
    }
}

/* Read keyboard state into NES button format */
static uint8_t read_keyboard_p1(void) {
    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    uint8_t buttons = 0;

    if (keys[SDL_SCANCODE_Z])         buttons |= BTN_A;
    if (keys[SDL_SCANCODE_X])         buttons |= BTN_B;
    if (keys[SDL_SCANCODE_BACKSPACE]) buttons |= BTN_SELECT;
    if (keys[SDL_SCANCODE_RETURN])    buttons |= BTN_START;
    if (keys[SDL_SCANCODE_UP])        buttons |= BTN_UP;
    if (keys[SDL_SCANCODE_DOWN])      buttons |= BTN_DOWN;
    if (keys[SDL_SCANCODE_LEFT])      buttons |= BTN_LEFT;
    if (keys[SDL_SCANCODE_RIGHT])     buttons |= BTN_RIGHT;

    return buttons;
}

/* Read keyboard state for player 2 (WASD + JK) */
static uint8_t read_keyboard_p2(void) {
    const uint8_t *keys = SDL_GetKeyboardState(NULL);
    uint8_t buttons = 0;

    if (keys[SDL_SCANCODE_J])         buttons |= BTN_A;
    if (keys[SDL_SCANCODE_K])         buttons |= BTN_B;
    if (keys[SDL_SCANCODE_RSHIFT])    buttons |= BTN_SELECT;
    if (keys[SDL_SCANCODE_L])         buttons |= BTN_START;
    if (keys[SDL_SCANCODE_W])         buttons |= BTN_UP;
    if (keys[SDL_SCANCODE_S])         buttons |= BTN_DOWN;
    if (keys[SDL_SCANCODE_A])         buttons |= BTN_LEFT;
    if (keys[SDL_SCANCODE_D])         buttons |= BTN_RIGHT;

    return buttons;
}

/* Read gamepad state into NES button format */
static uint8_t read_gamepad(SDL_GameController *gc) {
    if (!gc) return 0;

    uint8_t buttons = 0;

    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A))
        buttons |= BTN_A;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B) ||
        SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X))
        buttons |= BTN_B;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK))
        buttons |= BTN_SELECT;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START))
        buttons |= BTN_START;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP))
        buttons |= BTN_UP;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
        buttons |= BTN_DOWN;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
        buttons |= BTN_LEFT;
    if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
        buttons |= BTN_RIGHT;

    /* Left analog stick as D-pad */
    int16_t lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
    int16_t ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
    const int16_t DEADZONE = 8000;

    if (lx < -DEADZONE) buttons |= BTN_LEFT;
    if (lx >  DEADZONE) buttons |= BTN_RIGHT;
    if (ly < -DEADZONE) buttons |= BTN_UP;
    if (ly >  DEADZONE) buttons |= BTN_DOWN;

    return buttons;
}

bool input_update(GameState *gs) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            return false;

        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE)
                return false;
            break;

        case SDL_CONTROLLERDEVICEADDED:
            if (!gamepad1 && SDL_IsGameController(event.cdevice.which)) {
                gamepad1 = SDL_GameControllerOpen(event.cdevice.which);
            } else if (!gamepad2 && SDL_IsGameController(event.cdevice.which)) {
                gamepad2 = SDL_GameControllerOpen(event.cdevice.which);
            }
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            /* Handle disconnection */
            break;
        }
    }

    /* Save previous frame's input (for edge detection, like $074A) */
    gs->joy1_prev = gs->joy1;
    gs->joy2_prev = gs->joy2;

    /* Read current input */
    gs->joy1 = read_keyboard_p1() | read_gamepad(gamepad1);
    gs->joy2 = read_keyboard_p2() | read_gamepad(gamepad2);

    /* Replicate Start+Select masking from original ($8E7F-$8E91):
     * If Start or Select were held last frame AND still held,
     * mask them out (only trigger on first press) */
    uint8_t held1 = gs->joy1 & gs->joy1_prev & (BTN_START | BTN_SELECT);
    if (held1) {
        gs->joy1 &= ~(BTN_START | BTN_SELECT);
    }

    /* Store to RAM mirrors for compatibility */
    gs->ram[0x6FC] = gs->joy1;
    gs->ram[0x6FD] = gs->joy2;
    gs->ram[0x74A] = gs->joy1_prev;
    gs->ram[0x74B] = gs->joy2_prev;

    return true;
}

void input_cleanup(void) {
    if (gamepad2) { SDL_GameControllerClose(gamepad2); gamepad2 = NULL; }
    if (gamepad1) { SDL_GameControllerClose(gamepad1); gamepad1 = NULL; }
}

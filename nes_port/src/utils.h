/*
 * utils.h - Shared Utilities
 *
 * BCD conversion, 6502-style operations, and helper functions.
 */

#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stdbool.h>

/* 6502-style flag helpers for porting branch logic */

/* Compare (sets carry like CMP instruction) */
/* Returns: carry=1 if a >= b, zero=1 if a == b */
typedef struct {
    bool carry;
    bool zero;
    bool negative;
} StatusFlags;

StatusFlags cmp_6502(uint8_t a, uint8_t b);

/* BCD score operations (6-byte BCD scores, big-endian) */
void score_add(uint8_t *score, uint32_t points);
int  score_compare(const uint8_t *a, const uint8_t *b);
void score_clear(uint8_t *score);
void score_copy(uint8_t *dst, const uint8_t *src);

/* Format a 6-byte BCD score as a string (up to 13 chars including null) */
void score_to_string(const uint8_t *score, char *buf, int bufsize);

/* Clamp a value to 0-255 (uint8_t saturation) */
static inline uint8_t clamp8(int val) {
    if (val < 0) return 0;
    if (val > 255) return 255;
    return (uint8_t)val;
}

/* Read 16-bit little-endian pointer from RAM */
static inline uint16_t read16(const uint8_t *ram, uint16_t addr) {
    return ram[addr] | (ram[addr + 1] << 8);
}

#endif /* UTILS_H */

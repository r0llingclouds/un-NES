/*
 * utils.c - Shared Utilities
 *
 * BCD score operations and 6502-style helpers.
 */

#include <string.h>
#include <stdio.h>
#include "utils.h"

/*
 * cmp_6502 - Emulate 6502 CMP instruction flags
 *
 * CMP sets:
 *   Carry = (A >= operand)
 *   Zero  = (A == operand)
 *   Negative = bit 7 of (A - operand)
 */
StatusFlags cmp_6502(uint8_t a, uint8_t b) {
    StatusFlags f;
    uint8_t result = a - b;
    f.carry = (a >= b);
    f.zero = (a == b);
    f.negative = (result & 0x80) != 0;
    return f;
}

/*
 * BCD Score Operations
 *
 * Scores are stored as 6 bytes of BCD (packed, 2 digits per byte).
 * Big-endian: score[0] is the most significant pair.
 * Maximum displayable: 999999999999 (12 digits).
 *
 * Original ROM uses 6502 SEC/SBC chain at L8F97 for comparison
 * and ADC chain for addition.
 */

void score_clear(uint8_t *score) {
    memset(score, 0, 6);
}

void score_copy(uint8_t *dst, const uint8_t *src) {
    memcpy(dst, src, 6);
}

/*
 * score_compare - Compare two 6-byte BCD scores
 * Returns: >0 if a > b, 0 if equal, <0 if a < b
 */
int score_compare(const uint8_t *a, const uint8_t *b) {
    /* Big-endian comparison, byte by byte */
    for (int i = 0; i < 6; i++) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return -1;
    }
    return 0;
}

/*
 * score_add - Add points to a BCD score
 *
 * Points is a regular integer, converted to BCD and added
 * to the 6-byte score with proper BCD carry propagation.
 */
void score_add(uint8_t *score, uint32_t points) {
    /* Convert points to BCD bytes (little-endian temp) */
    uint8_t bcd[6] = {0};
    uint32_t val = points;
    for (int i = 5; i >= 0; i--) {
        bcd[i] = val % 100;
        /* Convert to packed BCD */
        bcd[i] = ((bcd[i] / 10) << 4) | (bcd[i] % 10);
        val /= 100;
    }

    /* Add BCD bytes with carry, from LSB (index 5) to MSB (index 0) */
    int carry = 0;
    for (int i = 5; i >= 0; i--) {
        int lo = (score[i] & 0x0F) + (bcd[i] & 0x0F) + carry;
        carry = 0;
        if (lo > 9) { lo -= 10; carry = 1; }

        int hi = ((score[i] >> 4) & 0x0F) + ((bcd[i] >> 4) & 0x0F) + carry;
        carry = 0;
        if (hi > 9) { hi -= 10; carry = 1; }

        score[i] = (hi << 4) | lo;
    }
}

/*
 * score_to_string - Format BCD score as string
 */
void score_to_string(const uint8_t *score, char *buf, int bufsize) {
    if (bufsize < 13) return;

    int pos = 0;
    bool leading = true;
    for (int i = 0; i < 6; i++) {
        int hi = (score[i] >> 4) & 0x0F;
        int lo = score[i] & 0x0F;

        if (leading && hi == 0 && i < 5) {
            buf[pos++] = ' ';
        } else {
            leading = false;
            buf[pos++] = '0' + hi;
        }

        if (leading && lo == 0 && i < 5) {
            buf[pos++] = ' ';
        } else {
            leading = false;
            buf[pos++] = '0' + lo;
        }
    }
    buf[pos] = '\0';
}

/**
 * @file keyboard.c
 * @brief ORIC-1 keyboard matrix emulation (8x8)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 0.3.0-alpha
 *
 * The ORIC keyboard is an 8-column x 8-row matrix.
 * Column select via VIA Port B bits 0-2 (active low).
 * Row data read via VIA Port A (active low: 0 = key pressed).
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Keyboard state: 8 columns, each 8 bits for rows */
static uint8_t key_matrix[8];

/* Selected column (from VIA Port B bits 0-2) */
static uint8_t selected_column;

void keyboard_init(void) {
    memset(key_matrix, 0, sizeof(key_matrix));
    selected_column = 0;
}

void keyboard_reset(void) {
    memset(key_matrix, 0, sizeof(key_matrix));
    selected_column = 0;
}

void keyboard_set_column(uint8_t col) {
    selected_column = col & 0x07;
}

uint8_t keyboard_read_row(void) {
    return ~key_matrix[selected_column]; /* Active low */
}

void keyboard_key_down(uint8_t col, uint8_t row) {
    if (col < 8 && row < 8) {
        key_matrix[col] |= (1 << row);
    }
}

void keyboard_key_up(uint8_t col, uint8_t row) {
    if (col < 8 && row < 8) {
        key_matrix[col] &= ~(1 << row);
    }
}

bool keyboard_is_pressed(uint8_t col, uint8_t row) {
    if (col < 8 && row < 8) {
        return (key_matrix[col] & (1 << row)) != 0;
    }
    return false;
}

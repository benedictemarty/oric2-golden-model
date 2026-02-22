/**
 * @file cassette.c
 * @brief Cassette I/O interface via VIA CB1/CB2
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 0.3.0-alpha
 */

#include <stdint.h>
#include <stdbool.h>

static bool motor_on;
static bool reading;
static uint32_t bit_counter;

void cassette_init(void) {
    motor_on = false;
    reading = false;
    bit_counter = 0;
}

void cassette_reset(void) {
    motor_on = false;
    reading = false;
    bit_counter = 0;
}

void cassette_set_motor(bool on) {
    motor_on = on;
}

bool cassette_get_motor(void) {
    return motor_on;
}

void cassette_start_read(void) {
    reading = true;
    bit_counter = 0;
}

void cassette_stop_read(void) {
    reading = false;
}

bool cassette_is_reading(void) {
    return reading;
}

uint8_t cassette_read_bit(void) {
    /* In fast-load mode, data is loaded directly from TAP file */
    bit_counter++;
    return 0;
}

/**
 * @file audio.h
 * @brief AY-3-8910 PSG and audio output interface
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>

#define AY_NUM_CHANNELS  3
#define AY_NUM_REGISTERS 16  /* 14 sound + 2 I/O ports (Port A=14, Port B=15) */
#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_BUFFER_SIZE 2048

typedef struct {
    uint8_t registers[AY_NUM_REGISTERS];
    uint8_t selected_reg;

    /* Tone generators */
    uint16_t tone_period[3];
    uint16_t tone_counter[3];
    uint8_t  tone_output[3];

    /* Noise generator */
    uint16_t noise_period;
    uint16_t noise_counter;
    uint32_t noise_shift;
    uint8_t  noise_output;

    /* Envelope */
    uint16_t env_period;
    uint16_t env_counter;
    uint8_t  env_shape;
    uint8_t  env_step;
    uint8_t  env_volume;
    bool     env_holding;

    /* Clock */
    uint32_t clock_rate;    /* 1 MHz for ORIC */
} ay3891x_t;

void ay_init(ay3891x_t* ay, uint32_t clock_rate);
void ay_reset(ay3891x_t* ay);
void ay_write_address(ay3891x_t* ay, uint8_t addr);
void ay_write_data(ay3891x_t* ay, uint8_t data);
uint8_t ay_read_data(ay3891x_t* ay);
void ay_generate(ay3891x_t* ay, int16_t* buffer, int num_samples);

#endif

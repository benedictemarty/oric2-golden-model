/**
 * @file ay3891x.c
 * @brief AY-3-8910 PSG emulation - 3 tone channels, noise, envelopes
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 0.5.0-alpha
 */

#include "audio/audio.h"
#include <string.h>

void ay_init(ay3891x_t* ay, uint32_t clock_rate) {
    memset(ay, 0, sizeof(ay3891x_t));
    ay->clock_rate = clock_rate;
    ay->noise_shift = 1;
}

void ay_reset(ay3891x_t* ay) {
    uint32_t clk = ay->clock_rate;
    memset(ay, 0, sizeof(ay3891x_t));
    ay->clock_rate = clk;
    ay->noise_shift = 1;
}

void ay_write_address(ay3891x_t* ay, uint8_t addr) {
    ay->selected_reg = addr & 0x0F;
}

void ay_write_data(ay3891x_t* ay, uint8_t data) {
    if (ay->selected_reg >= AY_NUM_REGISTERS) return;
    ay->registers[ay->selected_reg] = data;

    switch (ay->selected_reg) {
    case 0: case 1: /* Channel A period */
        ay->tone_period[0] = (ay->registers[1] & 0x0F) << 8 | ay->registers[0];
        break;
    case 2: case 3: /* Channel B period */
        ay->tone_period[1] = (ay->registers[3] & 0x0F) << 8 | ay->registers[2];
        break;
    case 4: case 5: /* Channel C period */
        ay->tone_period[2] = (ay->registers[5] & 0x0F) << 8 | ay->registers[4];
        break;
    case 6: /* Noise period */
        ay->noise_period = data & 0x1F;
        break;
    case 11: case 12: /* Envelope period */
        ay->env_period = (ay->registers[12] << 8) | ay->registers[11];
        break;
    case 13: /* Envelope shape */
        ay->env_shape = data & 0x0F;
        ay->env_step = 0;
        ay->env_counter = 0;
        ay->env_holding = false;
        break;
    }
}

uint8_t ay_read_data(ay3891x_t* ay) {
    if (ay->selected_reg >= AY_NUM_REGISTERS) return 0xFF;
    return ay->registers[ay->selected_reg];
}

static uint8_t envelope_volume(uint8_t shape, uint8_t step) {
    /* 16 shapes, 32-step cycle (2 x 16) */
    uint8_t s = step & 0x1F;
    bool first_half = s < 16;
    uint8_t pos = s & 0x0F;

    bool attack = (shape & 0x04) != 0;
    bool alternate = (shape & 0x02) != 0;
    bool hold = (shape & 0x01) != 0;

    if (!(shape & 0x08)) {
        /* Shapes 0-7: single decay then off */
        if (s >= 16) return 0;
        return attack ? pos : (15 - pos);
    }

    if (hold) {
        if (s >= 16) {
            if (alternate) return attack ? 0 : 15;
            return attack ? 15 : 0;
        }
        return attack ? pos : (15 - pos);
    }

    if (alternate) {
        if (first_half) return attack ? pos : (15 - pos);
        return attack ? (15 - pos) : pos;
    }

    return attack ? pos : (15 - pos);
}

void ay_generate(ay3891x_t* ay, int16_t* buffer, int num_samples) {
    uint8_t mixer = ay->registers[7];

    for (int i = 0; i < num_samples; i++) {
        /* Update tone generators (at clock/16 rate, we step per sample) */
        for (int ch = 0; ch < 3; ch++) {
            ay->tone_counter[ch]++;
            if (ay->tone_counter[ch] >= ay->tone_period[ch]) {
                ay->tone_counter[ch] = 0;
                ay->tone_output[ch] ^= 1;
            }
        }

        /* Update noise */
        ay->noise_counter++;
        uint16_t np = ay->noise_period ? ay->noise_period : 1;
        if (ay->noise_counter >= np) {
            ay->noise_counter = 0;
            /* 17-bit LFSR */
            uint32_t bit = ((ay->noise_shift >> 0) ^ (ay->noise_shift >> 3)) & 1;
            ay->noise_shift = (ay->noise_shift >> 1) | (bit << 16);
            ay->noise_output = ay->noise_shift & 1;
        }

        /* Update envelope */
        if (ay->env_period && !ay->env_holding) {
            ay->env_counter++;
            if (ay->env_counter >= ay->env_period) {
                ay->env_counter = 0;
                ay->env_step++;
                if (ay->env_step >= 32) {
                    if ((ay->env_shape & 0x08) && (ay->env_shape & 0x01)) {
                        ay->env_holding = true;
                    }
                    ay->env_step = (ay->env_shape & 0x08) ? (ay->env_step & 0x1F) : 0;
                }
            }
        }
        ay->env_volume = envelope_volume(ay->env_shape, ay->env_step);

        /* Mix channels */
        int32_t output = 0;
        for (int ch = 0; ch < 3; ch++) {
            bool tone_en = !(mixer & (1 << ch));
            bool noise_en = !(mixer & (1 << (ch + 3)));
            bool tone_on = ay->tone_output[ch] || !tone_en;
            bool noise_on = ay->noise_output || !noise_en;

            if (tone_on && noise_on) {
                uint8_t vol_reg = ay->registers[8 + ch];
                uint8_t vol;
                if (vol_reg & 0x10) vol = ay->env_volume;
                else vol = vol_reg & 0x0F;
                output += (int32_t)vol * 546; /* Scale to ~16-bit range */
            }
        }

        int16_t sample = (int16_t)(output / 3);
        buffer[i * 2]     = sample; /* Left */
        buffer[i * 2 + 1] = sample; /* Right */
    }
}

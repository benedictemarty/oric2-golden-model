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
    /* Port A (reg 14) and Port B (reg 15) default to 0xFF (no input active).
     * On ORIC, PSG Port A is connected to the keyboard matrix (active low),
     * so 0xFF = no keys pressed. Without this, the ROM sees ghost keypresses. */
    ay->registers[14] = 0xFF;
    ay->registers[15] = 0xFF;
}

void ay_reset(ay3891x_t* ay) {
    uint32_t clk = ay->clock_rate;
    memset(ay, 0, sizeof(ay3891x_t));
    ay->clock_rate = clk;
    ay->noise_shift = 1;
    ay->registers[14] = 0xFF;
    ay->registers[15] = 0xFF;
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

    /* Port A (reg 14): when configured as input (mixer bit 6 = 0),
     * return external input from callback (keyboard on ORIC) */
    if (ay->selected_reg == 14 && !(ay->registers[7] & 0x40)) {
        if (ay->porta_input) return ay->porta_input(ay->userdata);
        return 0xFF; /* No input = no keys pressed */
    }

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

/* Logarithmic volume table from Oricutron (matches real AY-3-8912 DAC curve) */
static const int32_t voltab[16] = {
    0, 513/4, 828/4, 1239/4, 1923/4, 3238/4, 4926/4, 9110/4,
    10344/4, 17876/4, 24682/4, 30442/4, 38844/4, 47270/4, 56402/4, 65535/4
};

void ay_generate(ay3891x_t* ay, int16_t* buffer, int num_samples) {
    uint8_t mixer = ay->registers[7];

    /* AY-3-8912 clock dividers (matching Oricutron):
     * - Tone/Noise: master clock / 8  (TONETIME=8, NOISETIME=8)
     * - Envelope:   master clock / 16 (ENVTIME=16)
     * This gives fT = fMASTER / (16 * TP) for tone frequency. */
    uint32_t tone_rate  = ay->clock_rate / 8;   /* 125000 Hz for 1 MHz clock */
    uint32_t env_rate   = ay->clock_rate / 16;  /* 62500 Hz for 1 MHz clock */

    for (int i = 0; i < num_samples; i++) {
        /* Update tone generators using fractional accumulator.
         * Each sample: accumulate tone_rate, toggle when reaching
         * period * AUDIO_SAMPLE_RATE threshold. */
        for (int ch = 0; ch < 3; ch++) {
            uint32_t period = ay->tone_period[ch];
            if (period == 0) period = 1;
            ay->tone_counter[ch] += tone_rate;
            while (ay->tone_counter[ch] >= period * AUDIO_SAMPLE_RATE) {
                ay->tone_counter[ch] -= period * AUDIO_SAMPLE_RATE;
                ay->tone_output[ch] ^= 1;
            }
        }

        /* Update noise (same rate as tone) */
        {
            uint32_t np = ay->noise_period ? ay->noise_period : 1;
            ay->noise_counter += tone_rate;
            while (ay->noise_counter >= np * AUDIO_SAMPLE_RATE) {
                ay->noise_counter -= np * AUDIO_SAMPLE_RATE;
                /* 17-bit LFSR */
                uint32_t bit = ((ay->noise_shift >> 0) ^ (ay->noise_shift >> 3)) & 1;
                ay->noise_shift = (ay->noise_shift >> 1) | (bit << 16);
                ay->noise_output = ay->noise_shift & 1;
            }
        }

        /* Update envelope (slower rate: clock/16) */
        if (ay->env_period && !ay->env_holding) {
            uint32_t ep = (uint32_t)ay->env_period;
            ay->env_counter += env_rate;
            while (ay->env_counter >= ep * AUDIO_SAMPLE_RATE) {
                ay->env_counter -= ep * AUDIO_SAMPLE_RATE;
                ay->env_step++;
                if (ay->env_step >= 32) {
                    if (!(ay->env_shape & 0x08)) {
                        /* Shapes 0-7: single cycle then off.
                         * Hold at step 31 so envelope_volume returns 0. */
                        ay->env_holding = true;
                        ay->env_step = 31;
                    } else if (ay->env_shape & 0x01) {
                        /* Shapes with HOLD bit (9,11,13,15): hold at final value */
                        ay->env_holding = true;
                        ay->env_step &= 0x1F;
                    } else {
                        /* Shapes without HOLD (8,10,12,14): cycle continuously */
                        ay->env_step &= 0x1F;
                    }
                }
            }
        }
        ay->env_volume = envelope_volume(ay->env_shape, ay->env_step);

        /* Mix channels (matching Oricutron mixer logic) */
        int32_t output = 0;
        for (int ch = 0; ch < 3; ch++) {
            bool tone_dis = (mixer >> ch) & 1;       /* 1 = tone disabled */
            bool noise_dis = (mixer >> (ch + 3)) & 1; /* 1 = noise disabled */

            /* Output is high when: (tone output OR tone disabled) AND
             * (noise output OR noise disabled). Matches Oricutron. */
            bool out = (ay->tone_output[ch] | tone_dis) &
                       (ay->noise_output | noise_dis);

            if (out) {
                uint8_t vol_reg = ay->registers[8 + ch];
                uint8_t vol_idx;
                if (vol_reg & 0x10) vol_idx = ay->env_volume;
                else vol_idx = vol_reg & 0x0F;
                output += voltab[vol_idx];
            }
        }

        int16_t sample = (int16_t)(output / 3);
        buffer[i * 2]     = sample; /* Left */
        buffer[i * 2 + 1] = sample; /* Right */
    }
}

/**
 * @file audio_output.c
 * @brief Audio output backend (SDL2 or headless)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 0.5.0-alpha
 */

#include "audio/audio.h"
#include "network/cast_server.h"

/* Cast server reference for audio forwarding */
static cast_server_t* cast_server_ref = NULL;

void audio_set_cast_server(void* server) {
    cast_server_ref = (cast_server_t*)server;
}

#ifdef HAS_SDL2
#include <SDL2/SDL.h>

static SDL_AudioDeviceID audio_device;
static ay3891x_t* psg_ref;

static void audio_callback(void* userdata, uint8_t* stream, int len) {
    (void)userdata;
    int16_t* buf = (int16_t*)stream;
    int num_samples = len / (2 * sizeof(int16_t)); /* stereo */
    if (psg_ref) ay_generate(psg_ref, buf, num_samples);
    else memset(stream, 0, len);

    /* Forward audio to cast server if connected */
    if (cast_server_ref) {
        cast_server_push_audio(cast_server_ref, buf, num_samples);
    }
}

bool audio_init(ay3891x_t* psg) {
    psg_ref = psg;
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = AUDIO_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = AUDIO_BUFFER_SIZE;
    want.callback = audio_callback;

    audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_device == 0) return false;
    SDL_PauseAudioDevice(audio_device, 0);
    return true;
}

void audio_cleanup(void) {
    if (audio_device) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }
}

void audio_pause(bool pause) {
    if (audio_device) SDL_PauseAudioDevice(audio_device, pause ? 1 : 0);
}

#else

bool audio_init(ay3891x_t* psg) { (void)psg; return true; }
void audio_cleanup(void) {}
void audio_pause(bool pause) { (void)pause; }

#endif

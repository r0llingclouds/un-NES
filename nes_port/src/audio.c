/*
 * audio.c - Basic Sound via SDL2 Audio
 *
 * Simplified APU emulation producing square wave, triangle, and noise.
 * The original ROM's sound driver at $F2D0 manages 4 APU channels.
 *
 * This implementation generates basic waveforms via SDL audio callback
 * rather than fully emulating the NES APU registers.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "audio.h"

#define SAMPLE_RATE    44100
#define AUDIO_SAMPLES  1024

/* Simple tone generator state */
typedef struct {
    float frequency;
    float volume;
    float phase;
    int   duration;     /* frames remaining */
    int   waveform;     /* 0=square, 1=triangle, 2=noise */
} Channel;

static Channel channels[4];     /* SQ1, SQ2, TRI, NOISE */
static SDL_AudioDeviceID audio_dev = 0;
static bool audio_enabled = false;

/* Pseudo-random noise generator */
static uint16_t noise_lfsr = 1;

static uint16_t noise_next(void) {
    uint16_t bit = ((noise_lfsr >> 0) ^ (noise_lfsr >> 1)) & 1;
    noise_lfsr = (noise_lfsr >> 1) | (bit << 14);
    return noise_lfsr & 1;
}

/* SDL audio callback */
static void audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    float *out = (float *)stream;
    int samples = len / sizeof(float);

    for (int i = 0; i < samples; i++) {
        float sample = 0.0f;

        for (int ch = 0; ch < 4; ch++) {
            Channel *c = &channels[ch];
            if (c->duration <= 0 || c->volume <= 0.0f) continue;

            float s = 0.0f;
            switch (c->waveform) {
            case 0: /* Square wave (50% duty) */
                s = (c->phase < 0.5f) ? 1.0f : -1.0f;
                break;
            case 1: /* Triangle wave */
                s = (c->phase < 0.5f)
                    ? (4.0f * c->phase - 1.0f)
                    : (3.0f - 4.0f * c->phase);
                break;
            case 2: /* Noise */
                s = noise_next() ? 1.0f : -1.0f;
                break;
            }

            sample += s * c->volume;
            c->phase += c->frequency / SAMPLE_RATE;
            if (c->phase >= 1.0f) c->phase -= 1.0f;
        }

        /* Mix and clamp */
        sample *= 0.15f;  /* Master volume */
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;
        out[i] = sample;
    }
}

bool audio_init(void) {
    SDL_AudioSpec want, have;

    memset(&want, 0, sizeof(want));
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_F32;
    want.channels = 1;
    want.samples = AUDIO_SAMPLES;
    want.callback = audio_callback;

    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev == 0) {
        fprintf(stderr, "Audio: SDL_OpenAudioDevice failed: %s\n",
                SDL_GetError());
        return false;
    }

    memset(channels, 0, sizeof(channels));
    SDL_PauseAudioDevice(audio_dev, 0);
    audio_enabled = true;

    printf("Audio: initialized (%d Hz)\n", have.freq);
    return true;
}

/*
 * audio_update - Called once per frame
 *
 * Replaces the sound driver at LF2D0.
 * Original driver:
 *   - If game_mode == 0: silence all channels (SND_CHN = 0)
 *   - Otherwise: enable channels (SND_CHN = $0F)
 *   - Check $07C6 and $FA for sound trigger
 *   - Manage sound timer at $07BB
 *   - Play tones via LF388 helper
 */
void audio_update(GameState *gs) {
    if (!audio_enabled) return;

    SDL_LockAudioDevice(audio_dev);

    /* Decrement channel durations */
    for (int ch = 0; ch < 4; ch++) {
        if (channels[ch].duration > 0) {
            channels[ch].duration--;
        }
    }

    /* Check for sound trigger ($FA) */
    uint8_t sound_trigger = gs->ram[0xFA];
    if (sound_trigger > 0 && gs->game_mode != MODE_INIT) {
        /* Play a basic SFX based on trigger value */
        audio_play_sfx(sound_trigger);
        gs->ram[0xFA] = 0;  /* Clear trigger */
    }

    SDL_UnlockAudioDevice(audio_dev);
}

/*
 * audio_play_sfx - Play a sound effect
 *
 * Maps SFX IDs to simple tones. The original ROM uses
 * APU frequency registers to set pitch; here we approximate.
 */
void audio_play_sfx(uint8_t sfx_id) {
    if (!audio_enabled) return;

    SDL_LockAudioDevice(audio_dev);

    switch (sfx_id) {
    case 1: /* Generic blip (menu select) */
        channels[0].frequency = 880.0f;
        channels[0].volume = 0.5f;
        channels[0].duration = 4;
        channels[0].waveform = 0;
        channels[0].phase = 0;
        break;

    case 2: /* Action sound */
        channels[0].frequency = 440.0f;
        channels[0].volume = 0.5f;
        channels[0].duration = 8;
        channels[0].waveform = 0;
        channels[0].phase = 0;
        break;

    case 3: /* Score/pickup */
        channels[1].frequency = 1200.0f;
        channels[1].volume = 0.4f;
        channels[1].duration = 6;
        channels[1].waveform = 0;
        channels[1].phase = 0;
        break;

    case 4: /* Hit/damage */
        channels[3].frequency = 200.0f;
        channels[3].volume = 0.6f;
        channels[3].duration = 10;
        channels[3].waveform = 2; /* Noise */
        channels[3].phase = 0;
        break;

    default: /* Default beep */
        channels[0].frequency = 660.0f;
        channels[0].volume = 0.3f;
        channels[0].duration = 3;
        channels[0].waveform = 0;
        channels[0].phase = 0;
        break;
    }

    SDL_UnlockAudioDevice(audio_dev);
}

void audio_stop_all(void) {
    if (!audio_enabled) return;

    SDL_LockAudioDevice(audio_dev);
    memset(channels, 0, sizeof(channels));
    SDL_UnlockAudioDevice(audio_dev);
}

void audio_cleanup(void) {
    if (audio_dev) {
        SDL_CloseAudioDevice(audio_dev);
        audio_dev = 0;
    }
    audio_enabled = false;
}

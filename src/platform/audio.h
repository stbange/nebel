#ifndef NEBEL_PLATFORM_AUDIO_H
#define NEBEL_PLATFORM_AUDIO_H

#include "core/app_state.h"
#include <SDL3/SDL.h>
#include <stdbool.h>

#define NEBEL_AUDIO_CACHE_MAX 8

typedef struct {
    char path[128];
    Uint8 *buf;
    Uint32 len;
    SDL_AudioSpec spec;
} NebelWavClip;

typedef struct {
    SDL_AudioDeviceID dev;
    SDL_AudioStream *stream;
    SDL_AudioSpec dev_spec;
    NebelWavClip cache[NEBEL_AUDIO_CACHE_MAX];
    int cache_count;
    bool ready;
} NebelAudio;

bool nebel_audio_init(NebelAudio *a);
void nebel_audio_free(NebelAudio *a);
void nebel_audio_flush(NebelAudio *a, NebelAppState *app);

#endif /* NEBEL_PLATFORM_AUDIO_H */

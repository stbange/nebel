#include "platform/audio.h"
#include "core/manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool nebel_audio_init(NebelAudio *a) {
    if (!a) return false;
    memset(a, 0, sizeof(*a));
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return false;
    }
    SDL_AudioSpec want = { SDL_AUDIO_F32, 1, 22050 };
    a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, NULL, NULL);
    if (!a->stream) {
        return false;
    }
    a->dev = SDL_GetAudioStreamDevice(a->stream);
    a->dev_spec = want;
    SDL_ResumeAudioStreamDevice(a->stream);
    a->ready = true;
    return true;
}

void nebel_audio_free(NebelAudio *a) {
    if (!a) return;
    for (int i = 0; i < a->cache_count; i++) {
        SDL_free(a->cache[i].buf);
    }
    if (a->stream) SDL_DestroyAudioStream(a->stream);
    memset(a, 0, sizeof(*a));
}

static NebelWavClip *load_or_get(NebelAudio *a, const char *asset_root, const char *rel_path) {
    for (int i = 0; i < a->cache_count; i++) {
        if (strcmp(a->cache[i].path, rel_path) == 0) return &a->cache[i];
    }
    if (a->cache_count >= NEBEL_AUDIO_CACHE_MAX) return NULL;

    char full[384];
    if (!nebel_path_join_asset(asset_root, rel_path, full, sizeof(full))) return NULL;

    NebelWavClip clip = {0};
    if (!SDL_LoadWAV(full, &clip.spec, &clip.buf, &clip.len)) {
        return NULL;
    }
    snprintf(clip.path, sizeof(clip.path), "%s", rel_path);
    a->cache[a->cache_count] = clip;
    return &a->cache[a->cache_count++];
}

void nebel_audio_flush(NebelAudio *a, NebelAppState *app) {
    if (!app) return;
    if (!a || !a->ready || app->muted) {
        app->audio_queue_count = 0;
        return;
    }
    for (int i = 0; i < app->audio_queue_count; i++) {
        const NebelAudioEvent *ev = &app->audio_queue[i];
        NebelWavClip *clip = load_or_get(a, app->asset_root, ev->path);
        if (!clip || !clip->buf || clip->len == 0) continue;

        /* Avoid piling up buffered SFX lag during rapid strokes */
        if (SDL_GetAudioStreamQueued(a->stream) > 22050 * 4) {
            SDL_ClearAudioStream(a->stream);
        }
        SDL_SetAudioStreamFormat(a->stream, &clip->spec, &a->dev_spec);
        SDL_SetAudioStreamGain(a->stream, ev->gain);
        SDL_PutAudioStreamData(a->stream, clip->buf, (int)clip->len);
    }
    app->audio_queue_count = 0;
}

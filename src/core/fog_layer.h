#ifndef NEBEL_CORE_FOG_LAYER_H
#define NEBEL_CORE_FOG_LAYER_H

#include "core/image.h"
#include <stdbool.h>
#include <stdint.h>

#define NEBEL_MAX_LAYERS 8
#define NEBEL_LAYER_NAME_LEN 48

typedef struct {
    float density;        /* [0.0, 4.0] Extinction beta */
    float height_falloff; /* [0.0, 8.0] Exponential ground/valley decay */
    float horizon_y;      /* [0.0, 1.0] Normalized Y of horizon (0=top, 1=bottom) */
    float depth_min;      /* [0.0, 1.0] Near depth cutoff */
    float depth_max;      /* [0.0, 1.0] Far depth cutoff */
    float airlight[3];    /* Linear RGB airlight tint [0.0, 1.0] */
    float sun_x;          /* [0.0, 1.0] Normalized sun X */
    float sun_y;          /* [0.0, 1.0] Normalized sun Y */
    float sun_strength;   /* [0.0, 2.0] Directional scatter boost */
    float sun_anisotropy; /* [0.0, 0.88] Henyey-Greenstein g */
    float noise_scale;    /* [1.0, 24.0] Procedural turbulence frequency */
    float noise_amount;   /* [0.0, 1.0] Density modulation amplitude */
    float bloom_amount;   /* [0.0, 1.5] Highlight veiling glare strength */
    float god_rays;       /* [0.0, 1.5] Crepuscular sunbeam scattering strength */
    float shadow_pool;    /* [0.0, 1.0] Shadow/trough fog pooling bias */
    float grain_match;    /* [0.0, 2.0] Sensor grain matching multiplier */
    float edge_refine;    /* [0.0, 1.0] Guided Filter edge snapping strength */
    uint32_t noise_seed;
    bool visible;
} NebelFogParams;

typedef struct {
    char name[NEBEL_LAYER_NAME_LEN];
    NebelFogParams params;
    uint8_t *mask; /* R8 painted fog marker mask */
    int mask_w;
    int mask_h;
} NebelFogLayer;

void nebel_fog_params_default(NebelFogParams *p);
void nebel_fog_params_clamp(NebelFogParams *p);
bool nebel_fog_layer_init(NebelFogLayer *layer, const char *name, int mask_w, int mask_h);
void nebel_fog_layer_free(NebelFogLayer *layer);
void nebel_fog_layer_clear_mask(NebelFogLayer *layer);

float nebel_henyey_greenstein(float cos_theta, float g);
float nebel_fbm2d(float x, float y, uint32_t seed);

/* Exact linear-light CPU reference renderer (full-resolution export & headless E2E tests). */
bool nebel_fog_render_cpu(const NebelImage *src,
                          const float *depth_map, int depth_w, int depth_h,
                          const uint8_t *protect_mask, int protect_w, int protect_h,
                          const NebelFogLayer *layers, int layer_count,
                          float sensor_noise, NebelImage *out);

#endif /* NEBEL_CORE_FOG_LAYER_H */

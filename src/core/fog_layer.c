#include "core/fog_layer.h"
#include "core/cv_depth.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float smoothstepf(float e0, float e1, float x) {
    if (e1 <= e0) return x >= e0 ? 1.0f : 0.0f;
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void nebel_fog_params_default(NebelFogParams *p) {
    if (!p) return;
    p->density = 1.35f;
    p->height_falloff = 1.8f;
    p->horizon_y = 0.45f;
    p->depth_min = 0.12f;
    p->depth_max = 1.0f;
    p->airlight[0] = 0.74f;
    p->airlight[1] = 0.80f;
    p->airlight[2] = 0.88f;
    p->sun_x = 0.72f;
    p->sun_y = 0.22f;
    p->sun_strength = 0.35f;
    p->sun_anisotropy = 0.62f;
    p->noise_scale = 5.0f;
    p->noise_amount = 0.38f;
    p->bloom_amount = 0.25f;
    p->god_rays = 0.35f;
    p->shadow_pool = 0.35f;
    p->fluid_flow = 0.65f;
    p->delight_strength = 0.45f;
    p->grain_match = 1.0f;
    p->edge_refine = 0.85f;
    p->noise_seed = 1337u;
    p->visible = true;
}

void nebel_fog_params_clamp(NebelFogParams *p) {
    if (!p) return;
    p->density = clampf(p->density, 0.0f, 4.0f);
    p->height_falloff = clampf(p->height_falloff, 0.0f, 8.0f);
    p->horizon_y = clampf(p->horizon_y, 0.0f, 1.0f);
    p->depth_min = clampf(p->depth_min, 0.0f, 1.0f);
    p->depth_max = clampf(p->depth_max, 0.0f, 1.0f);
    if (p->depth_max < p->depth_min) p->depth_max = p->depth_min;
    for (int i = 0; i < 3; i++) {
        p->airlight[i] = clampf(p->airlight[i], 0.0f, 1.0f);
    }
    p->sun_x = clampf(p->sun_x, 0.0f, 1.0f);
    p->sun_y = clampf(p->sun_y, 0.0f, 1.0f);
    p->sun_strength = clampf(p->sun_strength, 0.0f, 2.0f);
    p->sun_anisotropy = clampf(p->sun_anisotropy, 0.0f, 0.88f);
    p->noise_scale = clampf(p->noise_scale, 1.0f, 24.0f);
    p->noise_amount = clampf(p->noise_amount, 0.0f, 1.0f);
    p->bloom_amount = clampf(p->bloom_amount, 0.0f, 1.5f);
    p->god_rays = clampf(p->god_rays, 0.0f, 1.5f);
    p->shadow_pool = clampf(p->shadow_pool, 0.0f, 1.0f);
    p->fluid_flow = clampf(p->fluid_flow, 0.0f, 1.0f);
    p->delight_strength = clampf(p->delight_strength, 0.0f, 1.0f);
    p->grain_match = clampf(p->grain_match, 0.0f, 2.0f);
    p->edge_refine = clampf(p->edge_refine, 0.0f, 1.0f);
}

bool nebel_fog_layer_init(NebelFogLayer *layer, const char *name, int mask_w, int mask_h) {
    if (!layer || mask_w <= 0 || mask_h <= 0) return false;
    memset(layer, 0, sizeof(*layer));
    snprintf(layer->name, sizeof(layer->name), "%s", name && name[0] ? name : "Fog Layer");
    nebel_fog_params_default(&layer->params);
    layer->mask = (uint8_t *)calloc((size_t)mask_w * (size_t)mask_h, 1u);
    if (!layer->mask) return false;
    layer->mask_w = mask_w;
    layer->mask_h = mask_h;
    return true;
}

void nebel_fog_layer_free(NebelFogLayer *layer) {
    if (!layer) return;
    free(layer->mask);
    layer->mask = NULL;
    layer->mask_w = 0;
    layer->mask_h = 0;
}

void nebel_fog_layer_clear_mask(NebelFogLayer *layer) {
    if (!layer || !layer->mask) return;
    memset(layer->mask, 0, (size_t)layer->mask_w * (size_t)layer->mask_h);
}

float nebel_henyey_greenstein(float cos_theta, float g) {
    cos_theta = clampf(cos_theta, -1.0f, 1.0f);
    g = clampf(g, 0.0f, 0.88f);
    float g2 = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cos_theta;
    if (denom < 1e-4f) denom = 1e-4f;
    float p = (1.0f - g2) / (4.0f * 3.14159265f * powf(denom, 1.5f));
    float peak = (1.0f - g2) / (4.0f * 3.14159265f * powf(1.0f - g, 3.0f));
    return peak > 1e-5f ? clampf(p / peak, 0.0f, 1.0f) : 0.0f;
}

static uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static float hash2d(int ix, int iy, uint32_t seed) {
    uint32_t h = hash_u32((uint32_t)ix * 374761393u + (uint32_t)iy * 668265263u + seed * 1442695041u);
    return (float)(h & 0x00FFFFFFu) / 16777215.0f;
}

static float value_noise2d(float x, float y, uint32_t seed) {
    int ix = (int)floorf(x);
    int iy = (int)floorf(y);
    float fx = x - (float)ix;
    float fy = y - (float)iy;
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);

    float a = hash2d(ix,     iy,     seed);
    float b = hash2d(ix + 1, iy,     seed);
    float c = hash2d(ix,     iy + 1, seed);
    float d = hash2d(ix + 1, iy + 1, seed);
    return (a + (b - a) * ux) * (1.0f - uy) + (c + (d - c) * ux) * uy;
}

float nebel_fbm2d(float x, float y, uint32_t seed) {
    float val = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    for (int i = 0; i < 4; i++) {
        val += amp * value_noise2d(x * freq, y * freq, seed + (uint32_t)i * 97u);
        freq *= 2.03f;
        amp *= 0.5f;
    }
    return clampf(val, 0.0f, 1.0f);
}

static float sample_u8_bilinear(const uint8_t *buf, int w, int h, float u, float v) {
    if (!buf || w <= 0 || h <= 0) return 0.0f;
    float sx = clampf(u, 0.0f, 1.0f) * (float)(w - 1);
    float sy = clampf(v, 0.0f, 1.0f) * (float)(h - 1);
    int x0 = (int)floorf(sx);
    int y0 = (int)floorf(sy);
    int x1 = x0 + 1 < w ? x0 + 1 : w - 1;
    int y1 = y0 + 1 < h ? y0 + 1 : h - 1;
    float fx = sx - (float)x0;
    float fy = sy - (float)y0;
    float a = (float)buf[(size_t)y0 * (size_t)w + (size_t)x0] / 255.0f;
    float b = (float)buf[(size_t)y0 * (size_t)w + (size_t)x1] / 255.0f;
    float c = (float)buf[(size_t)y1 * (size_t)w + (size_t)x0] / 255.0f;
    float d = (float)buf[(size_t)y1 * (size_t)w + (size_t)x1] / 255.0f;
    return (a + (b - a) * fx) * (1.0f - fy) + (c + (d - c) * fx) * fy;
}

static float sample_f32_bilinear(const float *buf, int w, int h, float u, float v) {
    if (!buf || w <= 0 || h <= 0) return 0.5f;
    float sx = clampf(u, 0.0f, 1.0f) * (float)(w - 1);
    float sy = clampf(v, 0.0f, 1.0f) * (float)(h - 1);
    int x0 = (int)floorf(sx);
    int y0 = (int)floorf(sy);
    int x1 = x0 + 1 < w ? x0 + 1 : w - 1;
    int y1 = y0 + 1 < h ? y0 + 1 : h - 1;
    float fx = sx - (float)x0;
    float fy = sy - (float)y0;
    float a = buf[(size_t)y0 * (size_t)w + (size_t)x0];
    float b = buf[(size_t)y0 * (size_t)w + (size_t)x1];
    float c = buf[(size_t)y1 * (size_t)w + (size_t)x0];
    float d = buf[(size_t)y1 * (size_t)w + (size_t)x1];
    return (a + (b - a) * fx) * (1.0f - fy) + (c + (d - c) * fx) * fy;
}

static void sample_rg_bilinear(const float *buf, int w, int h, float u, float v, float out[2]) {
    float sx = clampf(u, 0.0f, 1.0f) * (float)(w - 1);
    float sy = clampf(v, 0.0f, 1.0f) * (float)(h - 1);
    int x0 = (int)floorf(sx);
    int y0 = (int)floorf(sy);
    int x1 = x0 + 1 < w ? x0 + 1 : w - 1;
    int y1 = y0 + 1 < h ? y0 + 1 : h - 1;
    float fx = sx - (float)x0;
    float fy = sy - (float)y0;
    const float *p00 = &buf[((size_t)y0 * (size_t)w + (size_t)x0) * 2u];
    const float *p10 = &buf[((size_t)y0 * (size_t)w + (size_t)x1) * 2u];
    const float *p01 = &buf[((size_t)y1 * (size_t)w + (size_t)x0) * 2u];
    const float *p11 = &buf[((size_t)y1 * (size_t)w + (size_t)x1) * 2u];
    for (int c = 0; c < 2; c++) {
        float top = p00[c] + (p10[c] - p00[c]) * fx;
        float bot = p01[c] + (p11[c] - p01[c]) * fx;
        out[c] = top + (bot - top) * fy;
    }
}

static void sample_rgb_bilinear(const float *buf, int w, int h, float u, float v, float out[3]) {
    float sx = clampf(u, 0.0f, 1.0f) * (float)(w - 1);
    float sy = clampf(v, 0.0f, 1.0f) * (float)(h - 1);
    int x0 = (int)floorf(sx);
    int y0 = (int)floorf(sy);
    int x1 = x0 + 1 < w ? x0 + 1 : w - 1;
    int y1 = y0 + 1 < h ? y0 + 1 : h - 1;
    float fx = sx - (float)x0;
    float fy = sy - (float)y0;
    const float *p00 = &buf[((size_t)y0 * (size_t)w + (size_t)x0) * 3u];
    const float *p10 = &buf[((size_t)y0 * (size_t)w + (size_t)x1) * 3u];
    const float *p01 = &buf[((size_t)y1 * (size_t)w + (size_t)x0) * 3u];
    const float *p11 = &buf[((size_t)y1 * (size_t)w + (size_t)x1) * 3u];
    for (int c = 0; c < 3; c++) {
        float top = p00[c] + (p10[c] - p00[c]) * fx;
        float bot = p01[c] + (p11[c] - p01[c]) * fx;
        out[c] = top + (bot - top) * fy;
    }
}

static void scaled_dims(int w, int h, int div, int max_dim, int *ow, int *oh) {
    int dw = w / div;
    int dh = h / div;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int mx = dw > dh ? dw : dh;
    if (mx > max_dim) {
        float s = (float)max_dim / (float)mx;
        dw = (int)((float)dw * s + 0.5f);
        dh = (int)((float)dh * s + 0.5f);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
    }
    *ow = dw;
    *oh = dh;
}

static void separable_box_blur_rgb(float *rgb, int w, int h, int radius) {
    if (radius <= 0) return;
    float *tmp = (float *)malloc((size_t)w * (size_t)h * 3u * sizeof(float));
    if (!tmp) return;

    for (int y = 0; y < h; y++) {
        double sr = 0.0, sg = 0.0, sb = 0.0;
        int x_end = radius < w - 1 ? radius : w - 1;
        for (int x = 0; x <= x_end; x++) {
            const float *p = &rgb[((size_t)y * (size_t)w + (size_t)x) * 3u];
            sr += p[0]; sg += p[1]; sb += p[2];
        }
        for (int x = 0; x < w; x++) {
            int l = x - radius, r = x + radius;
            if (x > 0) {
                if (r < w) {
                    const float *p = &rgb[((size_t)y * (size_t)w + (size_t)r) * 3u];
                    sr += p[0]; sg += p[1]; sb += p[2];
                }
                if (l - 1 >= 0) {
                    const float *p = &rgb[((size_t)y * (size_t)w + (size_t)(l - 1)) * 3u];
                    sr -= p[0]; sg -= p[1]; sb -= p[2];
                }
            }
            int c0 = l < 0 ? 0 : l;
            int c1 = r >= w ? w - 1 : r;
            float inv = 1.0f / (float)(c1 - c0 + 1);
            float *o = &tmp[((size_t)y * (size_t)w + (size_t)x) * 3u];
            o[0] = (float)sr * inv;
            o[1] = (float)sg * inv;
            o[2] = (float)sb * inv;
        }
    }

    for (int x = 0; x < w; x++) {
        double sr = 0.0, sg = 0.0, sb = 0.0;
        int y_end = radius < h - 1 ? radius : h - 1;
        for (int y = 0; y <= y_end; y++) {
            const float *p = &tmp[((size_t)y * (size_t)w + (size_t)x) * 3u];
            sr += p[0]; sg += p[1]; sb += p[2];
        }
        for (int y = 0; y < h; y++) {
            int t = y - radius, b = y + radius;
            if (y > 0) {
                if (b < h) {
                    const float *p = &tmp[((size_t)b * (size_t)w + (size_t)x) * 3u];
                    sr += p[0]; sg += p[1]; sb += p[2];
                }
                if (t - 1 >= 0) {
                    const float *p = &tmp[((size_t)(t - 1) * (size_t)w + (size_t)x) * 3u];
                    sr -= p[0]; sg -= p[1]; sb -= p[2];
                }
            }
            int r0 = t < 0 ? 0 : t;
            int r1 = b >= h ? h - 1 : b;
            float inv = 1.0f / (float)(r1 - r0 + 1);
            float *o = &rgb[((size_t)y * (size_t)w + (size_t)x) * 3u];
            o[0] = (float)sr * inv;
            o[1] = (float)sg * inv;
            o[2] = (float)sb * inv;
        }
    }

    free(tmp);
}

/* Terrain-guided valley & obstacle curl flow advection (matches fluid_advect.frag 1:1) */
static void compute_fluid_flow_cpu(const float *depth_map, int dw, int dh,
                                   float *flow_rg, int fw, int fh, int steps) {
    size_t fn = (size_t)fw * (size_t)fh;
    memset(flow_rg, 0, fn * 2u * sizeof(float));
    if (!depth_map || dw <= 1 || dh <= 1 || steps <= 0) return;

    float *tmp = (float *)calloc(fn * 2u, sizeof(float));
    if (!tmp) return;

    float du_step = 2.0f / (float)(fw > 1 ? fw : 1);
    float dv_step = 2.0f / (float)(fh > 1 ? fh : 1);
    float dt = 0.045f / (float)(steps > 4 ? steps : 4);

    for (int s = 0; s < steps; s++) {
        for (int y = 0; y < fh; y++) {
            float v = fh > 1 ? (float)y / (float)(fh - 1) : 0.0f;
            for (int x = 0; x < fw; x++) {
                float u = fw > 1 ? (float)x / (float)(fw - 1) : 0.0f;
                size_t idx = ((size_t)y * (size_t)fw + (size_t)x) * 2u;
                float cu = flow_rg[idx + 0];
                float cv = flow_rg[idx + 1];
                float su = clampf(u + cu, 0.0f, 1.0f);
                float sv = clampf(v + cv, 0.0f, 1.0f);

                float dl = sample_f32_bilinear(depth_map, dw, dh, su - du_step, sv);
                float dr = sample_f32_bilinear(depth_map, dw, dh, su + du_step, sv);
                float dt_d = sample_f32_bilinear(depth_map, dw, dh, su, sv - dv_step);
                float db_d = sample_f32_bilinear(depth_map, dw, dh, su, sv + dv_step);
                float gx = (dr - dl) * 0.5f;
                float gy = (db_d - dt_d) * 0.5f;

                /* Curl around foreground obstacles + valley pooling + cold-air settling */
                float vx = -gy * 0.85f + gx * 0.55f;
                float vy =  gx * 0.85f + gy * 0.55f + 0.025f;

                float nu = clampf(cu + clampf(vx * dt, -0.008f, 0.008f), -0.065f, 0.065f);
                float nv = clampf(cv + clampf(vy * dt, -0.008f, 0.008f), -0.065f, 0.065f);
                /* Gentle spatial smoothing along advected flow */
                float prev_f[2];
                sample_rg_bilinear(flow_rg, fw, fh, clampf(u - vx * dt * 0.5f, 0.0f, 1.0f),
                                   clampf(v - vy * dt * 0.5f, 0.0f, 1.0f), prev_f);
                tmp[idx + 0] = nu * 0.72f + prev_f[0] * 0.28f;
                tmp[idx + 1] = nv * 0.72f + prev_f[1] * 0.28f;
            }
        }
        memcpy(flow_rg, tmp, fn * 2u * sizeof(float));
    }

    free(tmp);
}

static float hash3d(int ix, int iy, int iz, uint32_t seed) {
    uint32_t h = hash_u32((uint32_t)ix * 374761393u +
                          (uint32_t)iy * 668265263u +
                          (uint32_t)iz * 2147483647u +
                          seed * 1442695041u);
    return (float)(h & 0x00FFFFFFu) / 16777215.0f;
}

static float value_noise3d(float x, float y, float z, uint32_t seed) {
    int ix = (int)floorf(x);
    int iy = (int)floorf(y);
    int iz = (int)floorf(z);
    float fx = x - (float)ix;
    float fy = y - (float)iy;
    float fz = z - (float)iz;
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);
    float uz = fz * fz * (3.0f - 2.0f * fz);

    float n000 = hash3d(ix,     iy,     iz,     seed);
    float n100 = hash3d(ix + 1, iy,     iz,     seed);
    float n010 = hash3d(ix,     iy + 1, iz,     seed);
    float n110 = hash3d(ix + 1, iy + 1, iz,     seed);
    float n001 = hash3d(ix,     iy,     iz + 1, seed);
    float n101 = hash3d(ix + 1, iy,     iz + 1, seed);
    float n011 = hash3d(ix,     iy + 1, iz + 1, seed);
    float n111 = hash3d(ix + 1, iy + 1, iz + 1, seed);

    float nx00 = n000 + (n100 - n000) * ux;
    float nx10 = n010 + (n110 - n010) * ux;
    float nx01 = n001 + (n101 - n001) * ux;
    float nx11 = n011 + (n111 - n011) * ux;
    float nxy0 = nx00 + (nx10 - nx00) * uy;
    float nxy1 = nx01 + (nx11 - nx01) * uy;
    return nxy0 + (nxy1 - nxy0) * uz;
}

static float fbm3d_2(float x, float y, float z, uint32_t seed) {
    float v = 0.667f * value_noise3d(x, y, z, seed)
            + 0.333f * value_noise3d(x * 2.07f, y * 2.07f, z * 2.07f, seed + 97u);
    return clampf((v - 0.34f) / 0.32f, 0.0f, 1.0f);
}

static float fbm3d_3(float x, float y, float z, uint32_t seed) {
    float v = 0.560f * value_noise3d(x, y, z, seed)
            + 0.290f * value_noise3d(x * 2.07f, y * 2.07f, z * 2.07f, seed + 97u)
            + 0.150f * value_noise3d(x * 4.19f, y * 4.19f, z * 4.19f, seed + 194u);
    return clampf((v - 0.35f) / 0.30f, 0.0f, 1.0f);
}

static void fog_world_coord(float u, float v, float z, const NebelFogParams *p, uint32_t seed,
                            float *ox, float *oy, float *oz) {
    float persp = 0.56f + (1.0f - z) * 0.64f;
    float s_f = (float)(seed % 10000u);
    float sc = p->noise_scale * 0.56f;
    *ox = ((u - 0.5f) / persp) * 0.80f * sc + s_f * 0.13f;
    *oy = ((v - p->horizon_y) / persp) * 1.40f * sc - s_f * 0.09f;
    *oz = z * 1.25f + s_f * 0.07f;
}

/* Continuous 3D volumetric fog field with wispy erosion matching fog_layer.frag 1:1 */
static void fog_volume_3d(float u, float v, float z, const NebelFogParams *p, uint32_t seed,
                          float *out_density, float *out_crest) {
    float px, py, pz;
    fog_world_coord(u, v, z, p, seed, &px, &py, &pz);

    float qx = fbm3d_2(px * 0.58f, py * 0.58f, pz * 0.58f, seed) - 0.5f;
    float qy = fbm3d_2(px * 0.58f + 4.7f, py * 0.58f + 1.9f, pz * 0.58f + 2.3f, seed + 17u) - 0.5f;
    float wx = px + qx * 1.35f * p->noise_amount;
    float wy = py + qy * 0.72f * p->noise_amount;
    float wz = pz + qx * 0.32f * p->noise_amount;

    float macro = fbm3d_3(wx * 0.78f, wy * 0.78f, wz * 0.78f, seed + 31u);
    float detail = fbm3d_2(wx * 2.35f, wy * 2.35f, wz * 2.10f, seed + 67u);
    float fine = clampf((value_noise3d(wx * 4.4f, wy * 4.4f, wz * 3.2f, seed + 131u) - 0.30f) / 0.40f, 0.0f, 1.0f);
    float wisp = 1.0f - fabsf(detail * 2.0f - 1.0f);
    float eroded = clampf(macro - (1.0f - detail) * 0.26f * p->noise_amount
                          + (wisp * 0.14f + (fine - 0.5f) * 0.16f) * p->noise_amount, 0.0f, 1.0f);

    float patchy = smoothstepf(0.10f * p->noise_amount, 0.86f - 0.08f * p->noise_amount, eroded);
    float billow = patchy * sqrtf(patchy > 0.0001f ? patchy : 0.0001f);
    float turb = (1.0f - p->noise_amount) + (0.24f + 1.85f * billow) * p->noise_amount;

    float ceiling = p->horizon_y + (1.0f - z) * 0.14f + (macro - 0.5f) * 0.24f * p->noise_amount;
    float above = ceiling - v;
    if (above < 0.0f) above = 0.0f;
    float h_att = expf(-p->height_falloff * above * 2.8f);

    float below = v - ceiling;
    if (below < 0.0f) below = 0.0f;
    float crest = clampf(1.0f - below * 3.4f, 0.0f, 1.0f) * (0.35f + 0.65f * eroded);
    *out_density = turb * h_att;
    *out_crest = crest;
}

static float fog_volume_macro_3d(float u, float v, float z, const NebelFogParams *p, uint32_t seed) {
    float px, py, pz;
    fog_world_coord(u, v, z, p, seed, &px, &py, &pz);
    float qx = fbm3d_2(px * 0.58f, py * 0.58f, pz * 0.58f, seed) - 0.5f;
    float qy = fbm3d_2(px * 0.58f + 4.7f, py * 0.58f + 1.9f, pz * 0.58f + 2.3f, seed + 17u) - 0.5f;
    float wx = px + qx * 1.35f * p->noise_amount;
    float wy = py + qy * 0.72f * p->noise_amount;
    float wz = pz + qx * 0.32f * p->noise_amount;
    float macro = fbm3d_2(wx * 0.78f, wy * 0.78f, wz * 0.78f, seed + 31u);
    float patchy = smoothstepf(0.10f * p->noise_amount, 0.86f - 0.08f * p->noise_amount, macro);
    float billow = patchy * sqrtf(patchy > 0.0001f ? patchy : 0.0001f);
    float turb = (1.0f - p->noise_amount) + (0.24f + 1.85f * billow) * p->noise_amount;
    float ceiling = p->horizon_y + (1.0f - z) * 0.14f + (macro - 0.5f) * 0.24f * p->noise_amount;
    float above = ceiling - v;
    if (above < 0.0f) above = 0.0f;
    return turb * expf(-p->height_falloff * above * 2.8f);
}

bool nebel_fog_render_cpu_ex(const NebelImage *src,
                             const float *depth_map, int depth_w, int depth_h,
                             const uint8_t *protect_mask, int protect_w, int protect_h,
                             const NebelFogLayer *layers, int layer_count,
                             float sensor_noise, const NebelRenderQuality *quality,
                             NebelImage *out) {
    if (!src || !src->rgba || !out || src->width <= 0 || src->height <= 0) return false;
    if (!nebel_image_clone(src, out)) return false;
    if (!layers || layer_count <= 0) return true;

    int ray_slices = quality ? quality->ray_slices : 12;
    int shadow_steps = quality ? quality->shadow_steps : 8;
    int fluid_steps = quality ? quality->fluid_steps : 12;
    if (ray_slices < 5) ray_slices = 5;
    if (ray_slices > 32) ray_slices = 32;
    if (shadow_steps < 0) shadow_steps = 0;
    if (shadow_steps > 16) shadow_steps = 16;
    if (fluid_steps < 0) fluid_steps = 0;
    if (fluid_steps > 32) fluid_steps = 32;

    int w = src->width;
    int h = src->height;
    size_t n = (size_t)w * (size_t)h;

    int aw = 1, ah = 1;
    scaled_dims(w, h, 4, 512, &aw, &ah);

    float *luma = (float *)calloc(n * 2u, sizeof(float));
    float *amb_buf = (float *)calloc((size_t)aw * (size_t)ah * 3u, sizeof(float));
    float *flow_buf = (float *)calloc((size_t)aw * (size_t)ah * 2u, sizeof(float));
    if (!luma || !amb_buf || !flow_buf) {
        free(luma);
        free(amb_buf);
        free(flow_buf);
        nebel_image_free(out);
        return false;
    }
    float *fog_accum = luma + n;

    for (size_t i = 0; i < n; i++) {
        const float *p = &src->rgba[i * 4u];
        luma[i] = clampf(0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2], 0.0f, 1.0f);
    }

    /* Quarter-res (<=512px) wide scene ambient light map + terrain-guided fluid flow */
    for (int y = 0; y < ah; y++) {
        int sy = ah > 1 ? (int)((size_t)y * (size_t)(h - 1) / (size_t)(ah - 1)) : 0;
        for (int x = 0; x < aw; x++) {
            int sx = aw > 1 ? (int)((size_t)x * (size_t)(w - 1) / (size_t)(aw - 1)) : 0;
            const float *p = &src->rgba[((size_t)sy * (size_t)w + (size_t)sx) * 4u];
            float *d = &amb_buf[((size_t)y * (size_t)aw + (size_t)x) * 3u];
            d[0] = p[0]; d[1] = p[1]; d[2] = p[2];
        }
    }
    int arad = (aw < ah ? aw : ah) / 14;
    if (arad < 2) arad = 2;
    if (arad > 12) arad = 12;
    separable_box_blur_rgb(amb_buf, aw, ah, arad);
    separable_box_blur_rgb(amb_buf, aw, ah, arad);

    compute_fluid_flow_cpu(depth_map, depth_w, depth_h, flow_buf, aw, ah, fluid_steps);

    float max_bloom = 0.0f;
    float max_grain = 0.0f;
    float max_god_rays = 0.0f;
    float rays_sun_x = 0.72f, rays_sun_y = 0.22f;

    for (int li = 0; li < layer_count; li++) {
        const NebelFogLayer *layer = &layers[li];
        if (!layer->params.visible || !layer->mask) continue;

        NebelFogParams p = layer->params;
        nebel_fog_params_clamp(&p);
        if (p.density <= 1e-5f) continue;

        if (p.bloom_amount > max_bloom) max_bloom = p.bloom_amount;
        if (p.grain_match > max_grain) max_grain = p.grain_match;
        if (p.god_rays > max_god_rays) {
            max_god_rays = p.god_rays;
            rays_sun_x = p.sun_x;
            rays_sun_y = p.sun_y;
        }

        /* Refine fog mask at work/mask resolution (<=2048px, matching GPU) */
        int mw = layer->mask_w < w ? layer->mask_w : w;
        int mh = layer->mask_h < h ? layer->mask_h : h;
        scaled_dims(mw, mh, 1, 2048, &mw, &mh);
        size_t mn = (size_t)mw * (size_t)mh;
        float *mask_work = (float *)calloc(mn * 3u, sizeof(float));
        if (!mask_work) {
            free(luma);
            free(amb_buf);
            free(flow_buf);
            nebel_image_free(out);
            return false;
        }
        float *mluma = mask_work;
        float *mraw  = mask_work + mn;
        float *mref  = mask_work + 2u * mn;

        for (int y = 0; y < mh; y++) {
            float v = mh > 1 ? (float)y / (float)(mh - 1) : 0.0f;
            int sy = mh > 1 ? (int)((size_t)y * (size_t)(h - 1) / (size_t)(mh - 1)) : 0;
            for (int x = 0; x < mw; x++) {
                float u = mw > 1 ? (float)x / (float)(mw - 1) : 0.0f;
                int sx = mw > 1 ? (int)((size_t)x * (size_t)(w - 1) / (size_t)(mw - 1)) : 0;
                size_t midx = (size_t)y * (size_t)mw + (size_t)x;
                mluma[midx] = luma[(size_t)sy * (size_t)w + (size_t)sx];
                float m = sample_u8_bilinear(layer->mask, layer->mask_w, layer->mask_h, u, v);
                float prot = protect_mask ? sample_u8_bilinear(protect_mask, protect_w, protect_h, u, v) : 0.0f;
                mraw[midx] = clampf(m * (1.0f - prot), 0.0f, 1.0f);
            }
        }

        int gf_rad = (mw < mh ? mw : mh) / 64;
        if (gf_rad < 2) gf_rad = 2;
        if (gf_rad > 10) gf_rad = 10;
        if (p.edge_refine > 0.01f && nebel_guided_filter(mluma, mraw, mref, mw, mh, gf_rad, 0.0015f)) {
            for (size_t i = 0; i < mn; i++) {
                mraw[i] = clampf(mraw[i] * (1.0f - p.edge_refine) + mref[i] * p.edge_refine, 0.0f, 1.0f);
            }
        }

        uint32_t base_seed = p.noise_seed % 10000u;
        bool do_shadows = (depth_map != NULL && shadow_steps > 0);
        float slice_norm = 1.12f / (float)ray_slices;
        static const float sh_col[3]   = { 0.60f, 0.72f, 0.90f };
        static const float hi_col[3]   = { 1.22f, 1.15f, 1.04f };
        static const float near_col[3] = { 1.06f, 1.03f, 1.00f };
        static const float far_col[3]  = { 0.90f, 0.96f, 1.04f };
        static const float warm_c[3]   = { 1.18f, 1.03f, 0.80f };

        for (int y = 0; y < h; y++) {
            float v = h > 1 ? (float)y / (float)(h - 1) : 0.0f;
            for (int x = 0; x < w; x++) {
                float u = w > 1 ? (float)x / (float)(w - 1) : 0.0f;
                size_t idx = (size_t)y * (size_t)w + (size_t)x;

                float m = (mw == w && mh == h) ? mraw[idx] : sample_f32_bilinear(mraw, mw, mh, u, v);
                if (m <= 1e-4f) continue;

                float d = depth_map ? sample_f32_bilinear(depth_map, depth_w, depth_h, u, v) : 0.65f;
                float d_enter = smoothstepf(p.depth_min - 0.03f, p.depth_min + 0.06f, d);
                if (d_enter <= 1e-4f) continue;
                float d_exit = (p.depth_max < 0.985f)
                    ? (1.0f - smoothstepf(p.depth_max - 0.08f, p.depth_max + 0.04f, d))
                    : 1.0f;

                float flow[2] = { 0.0f, 0.0f };
                if (p.fluid_flow > 0.001f && fluid_steps > 0) {
                    sample_rg_bilinear(flow_buf, aw, ah, u, v, flow);
                }
                float adv_u = clampf(u + flow[0] * p.fluid_flow, 0.0f, 1.0f);
                float adv_v = clampf(v + flow[1] * p.fluid_flow, 0.0f, 1.0f);

                float *px = &out->rgba[idx * 4u];
                float px_lum = clampf(0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2], 0.0f, 1.5f);
                float darkness = 1.0f - smoothstepf(0.06f, 0.78f, sqrtf(px_lum));
                float pool_factor = (1.0f - p.shadow_pool) + (0.35f + 1.15f * darkness) * p.shadow_pool;

                float amb[3];
                sample_rgb_bilinear(amb_buf, aw, ah, u, v, amb);
                float amb_lum = clampf(0.2126f * amb[0] + 0.7152f * amb[1] + 0.0722f * amb[2], 0.0f, 1.5f);
                float s_lum = sqrtf(amb_lum);

                float to_sx = p.sun_x - u;
                float to_sy = p.sun_y - v;
                float sun_dist = sqrtf(to_sx * to_sx + to_sy * to_sy);
                float sdir_x = (sun_dist > 0.001f) ? (to_sx / sun_dist) : 0.0f;
                float sdir_y = (sun_dist > 0.001f) ? (to_sy / sun_dist) : -1.0f;
                float cos_theta = cosf(clampf(sun_dist * 2.2f, 0.0f, 3.14159265f));
                float p1 = nebel_henyey_greenstein(cos_theta, p.sun_anisotropy);
                float p2 = nebel_henyey_greenstein(cos_theta, p.sun_anisotropy * 0.45f);

                /* Front-to-back 3D volumetric radiative transfer with directional self-shadowing */
                float d_span = (p.depth_max - p.depth_min) > 0.08f ? (p.depth_max - p.depth_min) : 0.08f;
                float T = 1.0f;
                float accum_L[3] = { 0.0f, 0.0f, 0.0f };

                for (int s = 0; s < ray_slices && T >= 0.004f; s++) {
                    float frac = ((float)s + 0.5f) / (float)ray_slices;
                    float z_s = p.depth_min + d_span * frac;
                    float in_front = smoothstepf(z_s - 0.055f, z_s + 0.045f, d);
                    if (in_front <= 1e-4f) continue;

                    float sheet = 0.0f, crest = 0.0f;
                    fog_volume_3d(adv_u, adv_v, z_s, &p, base_seed, &sheet, &crest);
                    float weight = (0.76f + 0.48f * frac) * slice_norm;

                    float edge_s = smoothstepf(0.04f, 0.88f, m + (sheet - 0.72f) * p.noise_amount * 0.88f);
                    float m_org_s = m * (1.0f - p.noise_amount) + (m * (0.20f + 1.18f * edge_s)) * p.noise_amount;

                    float dt_s = p.density * m_org_s * d_enter * d_exit * pool_factor * sheet * in_front * weight;
                    if (dt_s <= 1e-4f) continue;

                    float terrain_vis = 1.0f;
                    if (do_shadows) {
                        float occ = 0.0f;
                        for (int k = 0; k < shadow_steps; k++) {
                            float tk = (((float)k + 0.5f) / (float)shadow_steps) * 0.26f;
                            float su = clampf(u + to_sx * tk, 0.0f, 1.0f);
                            float sv = clampf(v + to_sy * tk, 0.0f, 1.0f);
                            float sd = sample_f32_bilinear(depth_map, depth_w, depth_h, su, sv);
                            occ += smoothstepf(z_s - 0.02f, z_s - 0.09f, sd);
                        }
                        terrain_vis = expf(-(occ / (float)shadow_steps) * 1.75f);
                    }

                    float sun_u = clampf(adv_u + sdir_x * 0.042f, 0.0f, 1.0f);
                    float sun_v = clampf(adv_v + sdir_y * 0.042f - 0.022f, 0.0f, 1.0f);
                    float z_sun = (z_s > 0.045f) ? (z_s - 0.045f) : 0.0f;
                    float sheet_sun = fog_volume_macro_3d(sun_u, sun_v, z_sun, &p, base_seed);
                    float self_shadow = expf(-sheet_sun * p.density * (0.25f + 0.65f * p.noise_amount));
                    float powder = 1.0f - 0.62f * expf(-sheet * p.density * 1.8f);
                    float slice_vis = clampf(terrain_vis * (0.28f + 0.72f * self_shadow), 0.0f, 1.0f);

                    float relief_t = clampf(slice_vis * 0.72f + crest * 0.34f, 0.0f, 1.0f);
                    float scatter = p1 * slice_vis * (0.82f + 0.52f * powder)
                                  + p2 * (0.32f + 0.68f * slice_vis) * 0.36f;
                    float sun_boost = p.sun_strength * scatter * (0.38f + 0.62f * s_lum);

                    float step_op = 1.0f - expf(-dt_s);
                    for (int c = 0; c < 3; c++) {
                        float rt = sh_col[c] + (hi_col[c] - sh_col[c]) * relief_t;
                        float dtint = near_col[c] + (far_col[c] - near_col[c]) * frac;
                        float ta = p.airlight[c] * rt * dtint;
                        float lit = ta * (0.40f + 1.16f * s_lum) + amb[c] * 0.28f;
                        float local_air = ta * 0.40f + lit * 0.60f;
                        float slice_air = local_air + sun_boost * 0.64f * warm_c[c];
                        accum_L[c] += T * step_op * slice_air;
                    }
                    T *= (1.0f - step_op);
                }

                float opacity = 1.0f - T;
                fog_accum[idx] = clampf(fog_accum[idx] + opacity * (1.0f - fog_accum[idx]), 0.0f, 1.0f);

                /* Intrinsic shadow softening (de-lighting harsh sun shadows inside thick fog) */
                float base_rgb[3] = { px[0], px[1], px[2] };
                if (p.delight_strength > 0.001f && opacity > 0.001f) {
                    float dl_w = clampf(p.delight_strength * opacity * 0.56f, 0.0f, 0.60f);
                    float soft_lum = px_lum + (amb_lum - px_lum) * 0.65f;
                    float l_scale = clampf(soft_lum / (px_lum > 0.04f ? px_lum : 0.04f), 0.55f, 2.2f);
                    for (int c = 0; c < 3; c++) {
                        float delit = px[c] * l_scale * 0.78f + amb[c] * 0.22f;
                        base_rgb[c] = clampf(px[c] * (1.0f - dl_w) + delit * dl_w, 0.0f, 1.5f);
                    }
                }

                for (int c = 0; c < 3; c++) {
                    px[c] = clampf(base_rgb[c] * T + accum_L[c], 0.0f, 1.5f);
                }
            }
        }
        free(mask_work);
    }

    free(amb_buf);
    free(flow_buf);

    /* Half-res (<=1024px) depth-weighted optical halation / bloom */
    if (max_bloom > 1e-3f) {
        int bw = 1, bh = 1;
        scaled_dims(w, h, 2, 1024, &bw, &bh);
        float *bloom_buf = (float *)calloc((size_t)bw * (size_t)bh * 3u, sizeof(float));
        if (bloom_buf) {
            for (int y = 0; y < bh; y++) {
                int sy = bh > 1 ? (int)((size_t)y * (size_t)(h - 1) / (size_t)(bh - 1)) : 0;
                for (int x = 0; x < bw; x++) {
                    int sx = bw > 1 ? (int)((size_t)x * (size_t)(w - 1) / (size_t)(bw - 1)) : 0;
                    size_t si = (size_t)sy * (size_t)w + (size_t)sx;
                    const float *p = &src->rgba[si * 4u];
                    float hi = smoothstepf(0.45f, 1.0f, luma[si]);
                    float *d = &bloom_buf[((size_t)y * (size_t)bw + (size_t)x) * 3u];
                    d[0] = p[0] * hi;
                    d[1] = p[1] * hi;
                    d[2] = p[2] * hi;
                }
            }
            int brad = (bw < bh ? bw : bh) / 36;
            if (brad < 2) brad = 2;
            if (brad > 10) brad = 10;
            separable_box_blur_rgb(bloom_buf, bw, bh, brad);
            separable_box_blur_rgb(bloom_buf, bw, bh, brad);

            for (int y = 0; y < h; y++) {
                float v = h > 1 ? (float)y / (float)(h - 1) : 0.0f;
                for (int x = 0; x < w; x++) {
                    size_t idx = (size_t)y * (size_t)w + (size_t)x;
                    float f = fog_accum[idx] * max_bloom;
                    if (f <= 1e-4f) continue;
                    float u = w > 1 ? (float)x / (float)(w - 1) : 0.0f;
                    float d_here = depth_map ? sample_f32_bilinear(depth_map, depth_w, depth_h, u, v) : 0.7f;
                    float depth_w_psf = 0.68f + 0.32f * smoothstepf(0.20f, 0.60f, d_here);
                    float bl[3];
                    sample_rgb_bilinear(bloom_buf, bw, bh, u, v, bl);
                    float *px = &out->rgba[idx * 4u];
                    float gain = f * depth_w_psf * 0.75f;
                    px[0] = clampf(px[0] + bl[0] * gain, 0.0f, 1.0f);
                    px[1] = clampf(px[1] + bl[1] * gain, 0.0f, 1.0f);
                    px[2] = clampf(px[2] + bl[2] * gain, 0.0f, 1.0f);
                }
            }
            free(bloom_buf);
        }
    }

    /* Crepuscular sunbeams (volumetric god rays) occluded by near foreground depth */
    if (max_god_rays > 1e-3f) {
        for (int y = 0; y < h; y++) {
            float v = h > 1 ? (float)y / (float)(h - 1) : 0.0f;
            for (int x = 0; x < w; x++) {
                float u = w > 1 ? (float)x / (float)(w - 1) : 0.0f;
                size_t idx = (size_t)y * (size_t)w + (size_t)x;
                float fog_a = fog_accum[idx];
                if (fog_a <= 1e-3f) continue;
                float d_here = depth_map ? sample_f32_bilinear(depth_map, depth_w, depth_h, u, v) : 0.65f;
                float fg_vis = smoothstepf(0.18f, 0.46f, d_here);
                if (fg_vis <= 1e-3f) continue;

                float jitter = (float)(hash_u32((uint32_t)idx * 2654435761u) & 0xFFu) / 255.0f;
                float du = (rays_sun_x - u) * 0.85f / 16.0f;
                float dv = (rays_sun_y - v) * 0.85f / 16.0f;
                float su = u + du * jitter;
                float sv = v + dv * jitter;
                float decay = 1.0f;
                float shaft = 0.0f;
                for (int s = 0; s < 16; s++) {
                    su += du; sv += dv;
                    if (su < 0.0f || su > 1.0f || sv < 0.0f || sv > 1.0f) break;
                    int sx = (int)(su * (float)(w - 1) + 0.5f);
                    int sy = (int)(sv * (float)(h - 1) + 0.5f);
                    size_t sidx = (size_t)sy * (size_t)w + (size_t)sx;
                    float sd = depth_map ? sample_f32_bilinear(depth_map, depth_w, depth_h, su, sv) : 0.7f;
                    float open = smoothstepf(0.38f, 0.68f, sd);
                    float bright = smoothstepf(0.28f, 0.88f, sqrtf(luma[sidx]));
                    shaft += open * bright * decay;
                    decay *= 0.90f;
                }
                float fog_w = smoothstepf(0.0f, 0.25f, fog_a) * (0.30f + 0.70f * fog_a);
                float gain = (shaft / 16.0f) * max_god_rays * fog_w * fg_vis * 0.65f;
                float *px = &out->rgba[idx * 4u];
                px[0] = clampf(px[0] + gain * 1.18f, 0.0f, 1.0f);
                px[1] = clampf(px[1] + gain * 0.98f, 0.0f, 1.0f);
                px[2] = clampf(px[2] + gain * 0.70f, 0.0f, 1.0f);
            }
        }
    }

    /* Sensor grain matching over fogged pixels */
    float sigma = sensor_noise * max_grain;
    for (size_t i = 0; i < n; i++) {
        float *px = &out->rgba[i * 4u];
        if (sigma > 1e-5f && fog_accum[i] > 1e-3f) {
            float u1 = (float)(hash_u32((uint32_t)i * 747796405u + 2891336453u) & 0xFFFFu) / 65535.0f;
            float u2 = (float)(hash_u32((uint32_t)i * 277803737u + 1013904223u) & 0xFFFFu) / 65535.0f;
            float g = (u1 + u2 - 1.0f) * sigma * fog_accum[i];
            px[0] = clampf(px[0] + g, 0.0f, 1.0f);
            px[1] = clampf(px[1] + g, 0.0f, 1.0f);
            px[2] = clampf(px[2] + g, 0.0f, 1.0f);
        } else {
            px[0] = clampf(px[0], 0.0f, 1.0f);
            px[1] = clampf(px[1], 0.0f, 1.0f);
            px[2] = clampf(px[2], 0.0f, 1.0f);
        }
    }

    free(luma);
    return true;
}

bool nebel_fog_render_cpu(const NebelImage *src,
                          const float *depth_map, int depth_w, int depth_h,
                          const uint8_t *protect_mask, int protect_w, int protect_h,
                          const NebelFogLayer *layers, int layer_count,
                          float sensor_noise, NebelImage *out) {
    NebelRenderQuality q = { 12, 8, 10 };
    return nebel_fog_render_cpu_ex(src, depth_map, depth_w, depth_h,
                                   protect_mask, protect_w, protect_h,
                                   layers, layer_count, sensor_noise, &q, out);
}

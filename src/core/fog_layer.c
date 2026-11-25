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
    /* Normalize relative to peak forward scatter at cos_theta = 1 */
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

bool nebel_fog_render_cpu(const NebelImage *src,
                          const float *depth_map, int depth_w, int depth_h,
                          const uint8_t *protect_mask, int protect_w, int protect_h,
                          const NebelFogLayer *layers, int layer_count,
                          float sensor_noise, NebelImage *out) {
    if (!src || !src->rgba || !out || layer_count < 0 || layer_count > NEBEL_MAX_LAYERS) {
        return false;
    }
    if (!nebel_image_clone(src, out)) return false;
    if (!layers || layer_count == 0) return true;

    int w = src->width;
    int h = src->height;
    size_t n = (size_t)w * (size_t)h;

    float *luma = (float *)malloc(n * 4u * sizeof(float));
    float *bloom_buf = (float *)malloc(n * 3u * sizeof(float));
    float *amb_buf = (float *)malloc(n * 3u * sizeof(float));
    if (!luma || !bloom_buf || !amb_buf) {
        free(luma);
        free(bloom_buf);
        free(amb_buf);
        nebel_image_free(out);
        return false;
    }
    float *raw_mask = luma + n;
    float *ref_mask = luma + 2u * n;
    float *fog_accum = luma + 3u * n;

    for (size_t i = 0; i < n; i++) {
        const float *p = &src->rgba[i * 4u];
        luma[i] = clampf(0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2], 0.0f, 1.0f);
        amb_buf[i * 3 + 0] = p[0];
        amb_buf[i * 3 + 1] = p[1];
        amb_buf[i * 3 + 2] = p[2];
        fog_accum[i] = 0.0f;
    }

    int arad = (w < h ? w : h) / 16;
    if (arad < 2) arad = 2;
    if (arad > 32) arad = 32;
    separable_box_blur_rgb(amb_buf, w, h, arad);
    separable_box_blur_rgb(amb_buf, w, h, arad);

    int gf_rad = (w < h ? w : h) / 64;
    if (gf_rad < 2) gf_rad = 2;
    if (gf_rad > 12) gf_rad = 12;

    float max_bloom = 0.0f;
    float max_grain = 0.0f;

    for (int li = 0; li < layer_count; li++) {
        const NebelFogLayer *layer = &layers[li];
        if (!layer->params.visible || !layer->mask) continue;

        NebelFogParams p = layer->params;
        nebel_fog_params_clamp(&p);
        if (p.density <= 1e-5f) continue;

        if (p.bloom_amount > max_bloom) max_bloom = p.bloom_amount;
        if (p.grain_match > max_grain) max_grain = p.grain_match;

        /* Combine painted fog mask minus foreground-protect mask, then edge-refine */
        for (int y = 0; y < h; y++) {
            float v = h > 1 ? (float)y / (float)(h - 1) : 0.0f;
            for (int x = 0; x < w; x++) {
                float u = w > 1 ? (float)x / (float)(w - 1) : 0.0f;
                float m = sample_u8_bilinear(layer->mask, layer->mask_w, layer->mask_h, u, v);
                float prot = protect_mask ? sample_u8_bilinear(protect_mask, protect_w, protect_h, u, v) : 0.0f;
                raw_mask[(size_t)y * (size_t)w + (size_t)x] = clampf(m * (1.0f - prot), 0.0f, 1.0f);
            }
        }

        if (p.edge_refine > 0.01f) {
            nebel_guided_filter(luma, raw_mask, ref_mask, w, h, gf_rad, 0.0015f);
        }

        for (int y = 0; y < h; y++) {
            float v = h > 1 ? (float)y / (float)(h - 1) : 0.0f;
            /* Perspective-foreshortened ground coordinates */
            float persp = 0.22f + fabsf(v - p.horizon_y) * 2.4f;
            float ny_ground = (v - p.horizon_y) / (persp + 0.15f);

            for (int x = 0; x < w; x++) {
                float u = w > 1 ? (float)x / (float)(w - 1) : 0.0f;
                size_t idx = (size_t)y * (size_t)w + (size_t)x;

                float m = p.edge_refine > 0.01f
                    ? (raw_mask[idx] * (1.0f - p.edge_refine) + ref_mask[idx] * p.edge_refine)
                    : raw_mask[idx];
                if (m <= 1e-4f) continue;

                float d = depth_map ? sample_f32_bilinear(depth_map, depth_w, depth_h, u, v) : 0.65f;
                /* Zero foreground bleed: fog starts cleanly at depth_min and grows with depth */
                float d_enter = smoothstepf(p.depth_min - 0.035f, p.depth_min + 0.075f, d);
                if (d_enter <= 1e-4f) continue;
                float d_exit = (p.depth_max < 0.985f)
                    ? (1.0f - smoothstepf(p.depth_max - 0.08f, p.depth_max + 0.04f, d))
                    : 1.0f;
                float d_span = (p.depth_max - p.depth_min) > 0.08f ? (p.depth_max - p.depth_min) : 0.08f;
                float d_prog = clampf((d - p.depth_min) / d_span, 0.0f, 1.0f);
                float depth_weight = d_enter * d_exit * (0.15f + 0.85f * powf(d_prog, 1.3f));

                float nx_ground = (u - 0.5f) / (persp + 0.25f);
                float p3x = nx_ground * p.noise_scale + d * 2.3f;
                float p3y = ny_ground * p.noise_scale - d * 1.4f;
                float n1 = nebel_fbm2d(p3x, p3y, p.noise_seed);
                float n2 = nebel_fbm2d(p3x * 2.1f + n1 * 1.1f - d * 1.7f,
                                       p3y * 2.1f - n1 * 0.8f + d * 2.0f, p.noise_seed + 41u);
                float n = n1 * 0.55f + n2 * 0.45f;
                float wispy = smoothstepf(0.06f, 0.90f, m + (n - 0.52f) * p.noise_amount * 1.2f);
                float m_org = m * (1.0f - p.noise_amount) + (m * (0.28f + 1.08f * wispy)) * p.noise_amount;
                float turb = 1.0f - p.noise_amount * 0.52f + p.noise_amount * 0.98f * n;

                float ceiling = p.horizon_y + (1.0f - d) * 0.10f;
                float above = ceiling > v ? (ceiling - v) : 0.0f;
                float h_term = above * 2.8f - (n - 0.48f) * 0.22f * p.noise_amount;
                float h_att = expf(-p.height_falloff * (h_term > 0.0f ? h_term : 0.0f));

                float tau = p.density * m_org * depth_weight * h_att * turb;
                float trans = expf(-tau);
                float opacity = 1.0f - trans;
                fog_accum[idx] = clampf(fog_accum[idx] + opacity * (1.0f - fog_accum[idx]), 0.0f, 1.0f);

                /* Scene-illumination-coupled airlight + Henyey-Greenstein sun scatter */
                const float *amb = &amb_buf[idx * 3u];
                float amb_lum = clampf(0.2126f * amb[0] + 0.7152f * amb[1] + 0.0722f * amb[2], 0.0f, 1.0f);
                float s_lum = sqrtf(amb_lum);

                float dx = u - p.sun_x;
                float dy = v - p.sun_y;
                float ang_dist = sqrtf(dx * dx + dy * dy);
                float cos_theta = cosf(clampf(ang_dist * 2.2f, 0.0f, 3.14159265f));
                float sun_phase = nebel_henyey_greenstein(cos_theta, p.sun_anisotropy);
                float sun_boost = p.sun_strength * sun_phase * (0.35f + 0.65f * s_lum);

                float *px = &out->rgba[idx * 4u];
                for (int c = 0; c < 3; c++) {
                    float warm = (c == 0) ? 1.15f : ((c == 1) ? 1.02f : 0.84f);
                    float lit = p.airlight[c] * (0.38f + 1.15f * s_lum) + amb[c] * 0.30f;
                    float local_air = p.airlight[c] * 0.35f + lit * 0.65f;
                    float air = local_air + sun_boost * 0.55f * warm;
                    px[c] = clampf(px[c] * trans + air * opacity, 0.0f, 1.5f);
                }
            }
        }
    }

    free(amb_buf);

    /* Highlight veiling glare / bloom weighted by accumulated fog opacity */
    if (max_bloom > 1e-3f) {
        for (size_t i = 0; i < n; i++) {
            const float *p = &src->rgba[i * 4u];
            float lum = luma[i];
            float hi = smoothstepf(0.55f, 1.0f, lum);
            bloom_buf[i * 3 + 0] = p[0] * hi;
            bloom_buf[i * 3 + 1] = p[1] * hi;
            bloom_buf[i * 3 + 2] = p[2] * hi;
        }
        int brad = (w < h ? w : h) / 28;
        if (brad < 2) brad = 2;
        if (brad > 24) brad = 24;
        separable_box_blur_rgb(bloom_buf, w, h, brad);
        separable_box_blur_rgb(bloom_buf, w, h, brad);

        for (size_t i = 0; i < n; i++) {
            float f = fog_accum[i] * max_bloom;
            if (f <= 1e-4f) continue;
            float *px = &out->rgba[i * 4u];
            px[0] = clampf(px[0] + bloom_buf[i * 3 + 0] * f * 0.6f, 0.0f, 1.0f);
            px[1] = clampf(px[1] + bloom_buf[i * 3 + 1] * f * 0.6f, 0.0f, 1.0f);
            px[2] = clampf(px[2] + bloom_buf[i * 3 + 2] * f * 0.6f, 0.0f, 1.0f);
        }
    }

    /* Sensor grain matching over fogged pixels */
    float sigma = sensor_noise * max_grain;
    for (size_t i = 0; i < n; i++) {
        float *px = &out->rgba[i * 4u];
        if (sigma > 1e-5f && fog_accum[i] > 1e-3f) {
            float u1 = (float)(hash_u32((uint32_t)i * 747796405u + 2891336453u) & 0xFFFFu) / 65535.0f;
            float u2 = (float)(hash_u32((uint32_t)i * 277803737u + 1013904223u) & 0xFFFFu) / 65535.0f;
            /* Triangular zero-mean noise approximating Gaussian */
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
    free(bloom_buf);
    return true;
}

#include "core/cv_depth.h"

#include <math.h>
#include <stdlib.h>

static float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static bool box_filter(const float *src, float *dst, int w, int h, int r) {
    if (r <= 0) {
        for (int i = 0; i < w * h; i++) dst[i] = src[i];
        return true;
    }
    float *tmp = (float *)malloc((size_t)w * (size_t)h * sizeof(float));
    if (!tmp) return false;

    /* Horizontal sliding sum */
    for (int y = 0; y < h; y++) {
        const float *row = src + (size_t)y * (size_t)w;
        float *out = tmp + (size_t)y * (size_t)w;
        double sum = 0.0;
        int x_end = r < w - 1 ? r : w - 1;
        for (int x = 0; x <= x_end; x++) sum += (double)row[x];
        for (int x = 0; x < w; x++) {
            int left = x - r;
            int right = x + r;
            if (x > 0) {
                if (right < w) sum += (double)row[right];
                if (left - 1 >= 0) sum -= (double)row[left - 1];
            }
            int c0 = left < 0 ? 0 : left;
            int c1 = right >= w ? w - 1 : right;
            out[x] = (float)(sum / (double)(c1 - c0 + 1));
        }
    }

    /* Vertical sliding sum */
    for (int x = 0; x < w; x++) {
        double sum = 0.0;
        int y_end = r < h - 1 ? r : h - 1;
        for (int y = 0; y <= y_end; y++) sum += (double)tmp[(size_t)y * (size_t)w + (size_t)x];
        for (int y = 0; y < h; y++) {
            int top = y - r;
            int bot = y + r;
            if (y > 0) {
                if (bot < h) sum += (double)tmp[(size_t)bot * (size_t)w + (size_t)x];
                if (top - 1 >= 0) sum -= (double)tmp[(size_t)(top - 1) * (size_t)w + (size_t)x];
            }
            int r0 = top < 0 ? 0 : top;
            int r1 = bot >= h ? h - 1 : bot;
            dst[(size_t)y * (size_t)w + (size_t)x] = (float)(sum / (double)(r1 - r0 + 1));
        }
    }

    free(tmp);
    return true;
}

bool nebel_guided_filter(const float *guide, const float *src, float *dst,
                         int width, int height, int radius, float eps) {
    if (!guide || !src || !dst || width <= 0 || height <= 0 || radius < 0) return false;
    if (eps < 1e-6f) eps = 1e-6f;
    size_t n = (size_t)width * (size_t)height;

    float *buf = (float *)malloc(n * 8u * sizeof(float));
    if (!buf) return false;
    float *mean_I = buf + 0 * n;
    float *mean_p = buf + 1 * n;
    float *I_sq   = buf + 2 * n;
    float *I_p    = buf + 3 * n;
    float *corr_I = buf + 4 * n;
    float *corr_Ip= buf + 5 * n;
    float *a      = buf + 6 * n;
    float *b      = buf + 7 * n;

    for (size_t i = 0; i < n; i++) {
        I_sq[i] = guide[i] * guide[i];
        I_p[i]  = guide[i] * src[i];
    }

    bool ok = box_filter(guide, mean_I, width, height, radius) &&
              box_filter(src,   mean_p, width, height, radius) &&
              box_filter(I_sq,  corr_I, width, height, radius) &&
              box_filter(I_p,   corr_Ip, width, height, radius);

    if (ok) {
        for (size_t i = 0; i < n; i++) {
            float var_I = corr_I[i] - mean_I[i] * mean_I[i];
            float cov_Ip = corr_Ip[i] - mean_I[i] * mean_p[i];
            a[i] = cov_Ip / (var_I + eps);
            b[i] = mean_p[i] - a[i] * mean_I[i];
        }
        /* Reuse mean_I / mean_p for mean_a / mean_b */
        ok = box_filter(a, mean_I, width, height, radius) &&
             box_filter(b, mean_p, width, height, radius);
        if (ok) {
            for (size_t i = 0; i < n; i++) {
                dst[i] = clamp01(mean_I[i] * guide[i] + mean_p[i]);
            }
        }
    }

    free(buf);
    return ok;
}

static void extract_luma(const NebelImage *img, float *luma) {
    size_t n = (size_t)img->width * (size_t)img->height;
    for (size_t i = 0; i < n; i++) {
        const float *p = &img->rgba[i * 4u];
        luma[i] = clamp01(0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]);
    }
}

bool nebel_depth_estimate_classical(const NebelImage *img, float horizon_y, float *out_depth) {
    if (!img || !img->rgba || !out_depth || img->width <= 0 || img->height <= 0) return false;
    int w = img->width;
    int h = img->height;
    size_t n = (size_t)w * (size_t)h;
    horizon_y = clamp01(horizon_y);

    float *luma = (float *)malloc(n * 4u * sizeof(float));
    if (!luma) return false;
    float *sharp = luma + n;
    float *dark  = luma + 2u * n;
    float *raw   = luma + 3u * n;

    extract_luma(img, luma);

    /* 1. Defocus sharpness cue (abs Laplacian) + Dark Channel cue (min RGB) */
    for (int y = 0; y < h; y++) {
        int ym = y > 0 ? y - 1 : 0;
        int yp = y + 1 < h ? y + 1 : h - 1;
        for (int x = 0; x < w; x++) {
            int xm = x > 0 ? x - 1 : 0;
            int xp = x + 1 < w ? x + 1 : w - 1;
            size_t idx = (size_t)y * (size_t)w + (size_t)x;
            float lap = fabsf(
                4.0f * luma[idx]
                - luma[(size_t)y * (size_t)w + (size_t)xm]
                - luma[(size_t)y * (size_t)w + (size_t)xp]
                - luma[(size_t)ym * (size_t)w + (size_t)x]
                - luma[(size_t)yp * (size_t)w + (size_t)x]
            );
            sharp[idx] = clamp01(lap * 4.0f);

            const float *p = &img->rgba[idx * 4u];
            float m = p[0] < p[1] ? p[0] : p[1];
            if (p[2] < m) m = p[2];
            dark[idx] = clamp01(m);
        }
    }

    int rad = (w < h ? w : h) / 48;
    if (rad < 2) rad = 2;
    if (rad > 16) rad = 16;

    /* Smooth sharpness and dark channel locally */
    box_filter(sharp, raw, w, h, rad);
    for (size_t i = 0; i < n; i++) sharp[i] = raw[i];
    box_filter(dark, raw, w, h, rad);
    for (size_t i = 0; i < n; i++) dark[i] = raw[i];

    /* 2. Fuse horizon ground-plane + defocus (low sharpness = farther) + aerial haze */
    float min_d = 1e9f, max_d = -1e9f;
    for (int y = 0; y < h; y++) {
        float ny = h > 1 ? (float)y / (float)(h - 1) : 0.0f;
        float horiz_cue;
        if (ny <= horizon_y) {
            horiz_cue = 1.0f; /* Sky / above horizon is far */
        } else {
            float denom = 1.0f - horizon_y;
            float t = denom > 1e-4f ? (ny - horizon_y) / denom : 1.0f;
            /* Perspective ground-plane falloff: near horizon stays far, bottom is near */
            horiz_cue = powf(1.0f - clamp01(t), 1.35f);
        }
        for (int x = 0; x < w; x++) {
            size_t idx = (size_t)y * (size_t)w + (size_t)x;
            float defocus_cue = 1.0f - clamp01(sharp[idx] * 2.2f);
            float haze_cue = dark[idx];
            float d = 0.55f * horiz_cue + 0.25f * defocus_cue + 0.20f * haze_cue;
            raw[idx] = d;
            if (d < min_d) min_d = d;
            if (d > max_d) max_d = d;
        }
    }

    float range = max_d - min_d;
    for (size_t i = 0; i < n; i++) {
        raw[i] = range > 1e-5f ? clamp01((raw[i] - min_d) / range) : 0.5f;
    }

    /* 3. Lock depth boundaries onto real photo edges with Guided Filter */
    bool ok = nebel_guided_filter(luma, raw, out_depth, w, h, rad, 0.002f);
    free(luma);
    return ok;
}

bool nebel_depth_refine_external(const NebelImage *img, const float *raw_depth,
                                 int raw_w, int raw_h, float *out_depth) {
    if (!img || !img->rgba || !raw_depth || !out_depth ||
        raw_w <= 0 || raw_h <= 0 || img->width <= 0 || img->height <= 0) {
        return false;
    }
    int w = img->width;
    int h = img->height;
    size_t n = (size_t)w * (size_t)h;

    float *luma = (float *)malloc(n * 2u * sizeof(float));
    if (!luma) return false;
    float *up = luma + n;
    extract_luma(img, luma);

    float min_v = 1e9f, max_v = -1e9f;
    size_t rn = (size_t)raw_w * (size_t)raw_h;
    for (size_t i = 0; i < rn; i++) {
        if (raw_depth[i] < min_v) min_v = raw_depth[i];
        if (raw_depth[i] > max_v) max_v = raw_depth[i];
    }
    float span = max_v - min_v;

    for (int y = 0; y < h; y++) {
        float sy = h > 1 ? ((float)y / (float)(h - 1)) * (float)(raw_h - 1) : 0.0f;
        int y0 = (int)floorf(sy);
        int y1 = y0 + 1 < raw_h ? y0 + 1 : raw_h - 1;
        float fy = sy - (float)y0;
        for (int x = 0; x < w; x++) {
            float sx = w > 1 ? ((float)x / (float)(w - 1)) * (float)(raw_w - 1) : 0.0f;
            int x0 = (int)floorf(sx);
            int x1 = x0 + 1 < raw_w ? x0 + 1 : raw_w - 1;
            float fx = sx - (float)x0;
            float d00 = raw_depth[(size_t)y0 * (size_t)raw_w + (size_t)x0];
            float d10 = raw_depth[(size_t)y0 * (size_t)raw_w + (size_t)x1];
            float d01 = raw_depth[(size_t)y1 * (size_t)raw_w + (size_t)x0];
            float d11 = raw_depth[(size_t)y1 * (size_t)raw_w + (size_t)x1];
            float d = (d00 + (d10 - d00) * fx) * (1.0f - fy) +
                      (d01 + (d11 - d01) * fx) * fy;
            up[(size_t)y * (size_t)w + (size_t)x] = span > 1e-5f ? clamp01((d - min_v) / span) : 0.5f;
        }
    }

    int rad = (w < h ? w : h) / 64;
    if (rad < 2) rad = 2;
    if (rad > 12) rad = 12;
    bool ok = nebel_guided_filter(luma, up, out_depth, w, h, rad, 0.001f);
    free(luma);
    return ok;
}

#include "core/image.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

float nebel_srgb_to_linear(float c) {
    c = clamp01(c);
    if (c <= 0.04045f) {
        return c / 12.92f;
    }
    return powf((c + 0.055f) / 1.055f, 2.4f);
}

float nebel_linear_to_srgb(float c) {
    c = clamp01(c);
    if (c <= 0.0031308f) {
        return c * 12.92f;
    }
    return 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

bool nebel_image_alloc(NebelImage *img, int width, int height) {
    if (!img || width <= 0 || height <= 0 ||
        width > NEBEL_MAX_IMAGE_DIM || height > NEBEL_MAX_IMAGE_DIM) {
        return false;
    }
    size_t count = (size_t)width * (size_t)height * 4u;
    float *buf = (float *)calloc(count, sizeof(float));
    if (!buf) {
        return false;
    }
    img->width = width;
    img->height = height;
    img->rgba = buf;
    return true;
}

void nebel_image_free(NebelImage *img) {
    if (!img) return;
    free(img->rgba);
    img->rgba = NULL;
    img->width = 0;
    img->height = 0;
}

bool nebel_image_clone(const NebelImage *src, NebelImage *dst) {
    if (!src || !src->rgba || !dst) return false;
    if (!nebel_image_alloc(dst, src->width, src->height)) return false;
    memcpy(dst->rgba, src->rgba, (size_t)src->width * (size_t)src->height * 4u * sizeof(float));
    return true;
}

bool nebel_image_from_rgba8(NebelImage *img, const uint8_t *rgba8, int width, int height) {
    if (!img || !rgba8) return false;
    if (!nebel_image_alloc(img, width, height)) return false;
    size_t n = (size_t)width * (size_t)height;
    for (size_t i = 0; i < n; i++) {
        img->rgba[i * 4 + 0] = nebel_srgb_to_linear((float)rgba8[i * 4 + 0] / 255.0f);
        img->rgba[i * 4 + 1] = nebel_srgb_to_linear((float)rgba8[i * 4 + 1] / 255.0f);
        img->rgba[i * 4 + 2] = nebel_srgb_to_linear((float)rgba8[i * 4 + 2] / 255.0f);
        img->rgba[i * 4 + 3] = (float)rgba8[i * 4 + 3] / 255.0f;
    }
    return true;
}

void nebel_image_to_rgba8(const NebelImage *img, uint8_t *out_rgba8) {
    if (!img || !img->rgba || !out_rgba8) return;
    size_t n = (size_t)img->width * (size_t)img->height;
    for (size_t i = 0; i < n; i++) {
        out_rgba8[i * 4 + 0] = (uint8_t)(nebel_linear_to_srgb(img->rgba[i * 4 + 0]) * 255.0f + 0.5f);
        out_rgba8[i * 4 + 1] = (uint8_t)(nebel_linear_to_srgb(img->rgba[i * 4 + 1]) * 255.0f + 0.5f);
        out_rgba8[i * 4 + 2] = (uint8_t)(nebel_linear_to_srgb(img->rgba[i * 4 + 2]) * 255.0f + 0.5f);
        out_rgba8[i * 4 + 3] = (uint8_t)(clamp01(img->rgba[i * 4 + 3]) * 255.0f + 0.5f);
    }
}

bool nebel_image_load_file(NebelImage *img, const char *path) {
    if (!img || !path || path[0] == '\0') return false;
    int w = 0, h = 0, comp = 0;
    uint8_t *data = stbi_load(path, &w, &h, &comp, 4);
    if (!data) return false;
    bool ok = nebel_image_from_rgba8(img, data, w, h);
    stbi_image_free(data);
    return ok;
}

bool nebel_image_load_memory(NebelImage *img, const uint8_t *bytes, size_t len) {
    if (!img || !bytes || len == 0 || len > (size_t)INT32_MAX) return false;
    int w = 0, h = 0, comp = 0;
    uint8_t *data = stbi_load_from_memory(bytes, (int)len, &w, &h, &comp, 4);
    if (!data) return false;
    bool ok = nebel_image_from_rgba8(img, data, w, h);
    stbi_image_free(data);
    return ok;
}

bool nebel_image_save_png(const NebelImage *img, const char *path) {
    if (!img || !img->rgba || !path || path[0] == '\0') return false;
    size_t sz = (size_t)img->width * (size_t)img->height * 4u;
    uint8_t *rgba8 = (uint8_t *)malloc(sz);
    if (!rgba8) return false;
    nebel_image_to_rgba8(img, rgba8);
    int ok = stbi_write_png(path, img->width, img->height, 4, rgba8, img->width * 4);
    free(rgba8);
    return ok != 0;
}

bool nebel_image_save_jpg(const NebelImage *img, const char *path, int quality) {
    if (!img || !img->rgba || !path || path[0] == '\0') return false;
    if (quality < 10) quality = 10;
    if (quality > 100) quality = 100;
    size_t sz = (size_t)img->width * (size_t)img->height * 4u;
    uint8_t *rgba8 = (uint8_t *)malloc(sz);
    if (!rgba8) return false;
    nebel_image_to_rgba8(img, rgba8);
    int ok = stbi_write_jpg(path, img->width, img->height, 4, rgba8, quality);
    free(rgba8);
    return ok != 0;
}

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    bool oom;
} MemWriter;

static void mem_write_cb(void *ctx, void *data, int size) {
    MemWriter *w = (MemWriter *)ctx;
    if (w->oom || size <= 0) return;
    size_t need = w->len + (size_t)size;
    if (need > w->cap) {
        size_t next = w->cap ? w->cap * 2u : 16384u;
        while (next < need) next *= 2u;
        uint8_t *p = (uint8_t *)realloc(w->data, next);
        if (!p) {
            w->oom = true;
            return;
        }
        w->data = p;
        w->cap = next;
    }
    memcpy(w->data + w->len, data, (size_t)size);
    w->len += (size_t)size;
}

bool nebel_image_encode_png_mem(const NebelImage *img, uint8_t **out_buf, size_t *out_len) {
    if (!img || !img->rgba || !out_buf || !out_len) return false;
    size_t sz = (size_t)img->width * (size_t)img->height * 4u;
    uint8_t *rgba8 = (uint8_t *)malloc(sz);
    if (!rgba8) return false;
    nebel_image_to_rgba8(img, rgba8);
    MemWriter w = {0};
    int ok = stbi_write_png_to_func(mem_write_cb, &w, img->width, img->height, 4, rgba8, img->width * 4);
    free(rgba8);
    if (!ok || w.oom) {
        free(w.data);
        return false;
    }
    *out_buf = w.data;
    *out_len = w.len;
    return true;
}

bool nebel_image_encode_jpg_mem(const NebelImage *img, int quality, uint8_t **out_buf, size_t *out_len) {
    if (!img || !img->rgba || !out_buf || !out_len) return false;
    if (quality < 10) quality = 10;
    if (quality > 100) quality = 100;
    size_t sz = (size_t)img->width * (size_t)img->height * 4u;
    uint8_t *rgba8 = (uint8_t *)malloc(sz);
    if (!rgba8) return false;
    nebel_image_to_rgba8(img, rgba8);
    MemWriter w = {0};
    int ok = stbi_write_jpg_to_func(mem_write_cb, &w, img->width, img->height, 4, rgba8, quality);
    free(rgba8);
    if (!ok || w.oom) {
        free(w.data);
        return false;
    }
    *out_buf = w.data;
    *out_len = w.len;
    return true;
}

bool nebel_image_downsample(const NebelImage *src, NebelImage *dst, int max_dim) {
    if (!src || !src->rgba || !dst || max_dim <= 0) return false;
    int max_s = src->width > src->height ? src->width : src->height;
    if (max_s <= max_dim) {
        return nebel_image_clone(src, dst);
    }
    float scale = (float)max_dim / (float)max_s;
    int dw = (int)(src->width * scale + 0.5f);
    int dh = (int)(src->height * scale + 0.5f);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    if (!nebel_image_alloc(dst, dw, dh)) return false;

    for (int y = 0; y < dh; y++) {
        float sy = ((float)y + 0.5f) * (float)src->height / (float)dh - 0.5f;
        int y0 = (int)floorf(sy);
        int y1 = y0 + 1;
        float fy = sy - (float)y0;
        if (y0 < 0) y0 = 0;
        if (y1 >= src->height) y1 = src->height - 1;
        for (int x = 0; x < dw; x++) {
            float sx = ((float)x + 0.5f) * (float)src->width / (float)dw - 0.5f;
            int x0 = (int)floorf(sx);
            int x1 = x0 + 1;
            float fx = sx - (float)x0;
            if (x0 < 0) x0 = 0;
            if (x1 >= src->width) x1 = src->width - 1;
            const float *p00 = &src->rgba[(size_t)(y0 * src->width + x0) * 4u];
            const float *p10 = &src->rgba[(size_t)(y0 * src->width + x1) * 4u];
            const float *p01 = &src->rgba[(size_t)(y1 * src->width + x0) * 4u];
            const float *p11 = &src->rgba[(size_t)(y1 * src->width + x1) * 4u];
            float *o = &dst->rgba[(size_t)(y * dw + x) * 4u];
            for (int c = 0; c < 4; c++) {
                float top = p00[c] + (p10[c] - p00[c]) * fx;
                float bot = p01[c] + (p11[c] - p01[c]) * fx;
                o[c] = top + (bot - top) * fy;
            }
        }
    }
    return true;
}

/* Immerkaer 3x3 noise variance estimator in flat regions */
float nebel_image_estimate_noise(const NebelImage *img) {
    if (!img || !img->rgba || img->width < 3 || img->height < 3) return 0.0f;
    int w = img->width;
    int h = img->height;
    double sum = 0.0;
    size_t count = 0;
    int step_x = w > 512 ? w / 256 : 1;
    int step_y = h > 512 ? h / 256 : 1;

    for (int y = 1; y < h - 1; y += step_y) {
        for (int x = 1; x < w - 1; x += step_x) {
            float l[9];
            int k = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    const float *p = &img->rgba[(size_t)((y + dy) * w + (x + dx)) * 4u];
                    l[k++] = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
                }
            }
            /* Skip strong edges so detail isn't misread as sensor noise */
            float gx = fabsf(l[5] - l[3]);
            float gy = fabsf(l[7] - l[1]);
            if (gx + gy > 0.15f) continue;

            float resp = fabsf(
                l[0] - 2.0f * l[1] + l[2]
                - 2.0f * l[3] + 4.0f * l[4] - 2.0f * l[5]
                + l[6] - 2.0f * l[7] + l[8]
            );
            sum += (double)resp;
            count++;
        }
    }
    if (count == 0) return 0.004f;
    /* Immerkaer scale factor sqrt(pi/2) / 6 ≈ 0.208885 */
    float sigma = (float)((sum / (double)count) * 0.208885);
    if (sigma > 0.08f) sigma = 0.08f;
    return sigma;
}

void nebel_image_sample_airlight(const NebelImage *img, float norm_x, float norm_y, float out_rgb[3]) {
    if (!out_rgb) return;
    out_rgb[0] = 0.76f;
    out_rgb[1] = 0.81f;
    out_rgb[2] = 0.87f;
    if (!img || !img->rgba || img->width <= 0 || img->height <= 0) return;

    if (norm_x < 0.0f || norm_y < 0.0f) {
        /* Auto-sample upper third brightest 10% pixels for atmospheric sky tint */
        int y_end = img->height / 3;
        if (y_end < 1) y_end = img->height;
        double r = 0.0, g = 0.0, b = 0.0;
        double wsum = 0.0;
        for (int y = 0; y < y_end; y++) {
            for (int x = 0; x < img->width; x++) {
                const float *p = &img->rgba[(size_t)(y * img->width + x) * 4u];
                float lum = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
                float wt = 0.1f + lum * lum;
                r += (double)p[0] * wt;
                g += (double)p[1] * wt;
                b += (double)p[2] * wt;
                wsum += (double)wt;
            }
        }
        if (wsum > 0.0) {
            /* Keep fog slightly luminous so dark night skies still create readable mist */
            float sr = (float)(r / wsum);
            float sg = (float)(g / wsum);
            float sb = (float)(b / wsum);
            out_rgb[0] = clamp01(0.25f + 0.75f * sr);
            out_rgb[1] = clamp01(0.25f + 0.75f * sg);
            out_rgb[2] = clamp01(0.27f + 0.73f * sb);
        }
        return;
    }

    int cx = (int)(clamp01(norm_x) * (float)(img->width - 1) + 0.5f);
    int cy = (int)(clamp01(norm_y) * (float)(img->height - 1) + 0.5f);
    int rad = 4;
    double r = 0.0, g = 0.0, b = 0.0;
    int n = 0;
    for (int dy = -rad; dy <= rad; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= img->height) continue;
        for (int dx = -rad; dx <= rad; dx++) {
            int x = cx + dx;
            if (x < 0 || x >= img->width) continue;
            const float *p = &img->rgba[(size_t)(y * img->width + x) * 4u];
            r += (double)p[0];
            g += (double)p[1];
            b += (double)p[2];
            n++;
        }
    }
    if (n > 0) {
        out_rgb[0] = clamp01((float)(r / n));
        out_rgb[1] = clamp01((float)(g / n));
        out_rgb[2] = clamp01((float)(b / n));
    }
}

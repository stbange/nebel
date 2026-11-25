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

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)(v & 0xFFu);
}

static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu); p[1] = (uint8_t)(v >> 8);
}

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);        p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu); p[3] = (uint8_t)(v >> 24);
}

static uint8_t *write_png_chunk(uint8_t *dst, const char tag[4], const uint8_t *data, uint32_t len) {
    put_be32(dst, len);
    memcpy(dst + 4, tag, 4);
    if (len > 0 && data) memcpy(dst + 8, data, len);
    uint32_t crc = stbiw__crc32(dst + 4, (int)(len + 4u));
    put_be32(dst + 8u + len, crc);
    return dst + 12u + len;
}

bool nebel_image_encode_png16_mem(const NebelImage *img, uint8_t **out_buf, size_t *out_len) {
    if (!img || !img->rgba || !out_buf || !out_len || img->width <= 0 || img->height <= 0) {
        return false;
    }
    size_t row_bytes = 1u + (size_t)img->width * 8u;
    size_t raw_len = row_bytes * (size_t)img->height;
    if (raw_len > (size_t)INT32_MAX / 2u) return false;

    uint8_t *raw = (uint8_t *)malloc(raw_len);
    if (!raw) return false;
    for (int y = 0; y < img->height; y++) {
        uint8_t *row = raw + (size_t)y * row_bytes;
        row[0] = 0;
        uint8_t *dst = row + 1;
        for (int x = 0; x < img->width; x++) {
            const float *p = &img->rgba[((size_t)y * (size_t)img->width + (size_t)x) * 4u];
            uint16_t r = (uint16_t)(nebel_linear_to_srgb(p[0]) * 65535.0f + 0.5f);
            uint16_t g = (uint16_t)(nebel_linear_to_srgb(p[1]) * 65535.0f + 0.5f);
            uint16_t b = (uint16_t)(nebel_linear_to_srgb(p[2]) * 65535.0f + 0.5f);
            uint16_t a = (uint16_t)(clamp01(p[3]) * 65535.0f + 0.5f);
            dst[0] = (uint8_t)(r >> 8); dst[1] = (uint8_t)(r & 0xFFu);
            dst[2] = (uint8_t)(g >> 8); dst[3] = (uint8_t)(g & 0xFFu);
            dst[4] = (uint8_t)(b >> 8); dst[5] = (uint8_t)(b & 0xFFu);
            dst[6] = (uint8_t)(a >> 8); dst[7] = (uint8_t)(a & 0xFFu);
            dst += 8;
        }
    }

    int zlen = 0;
    unsigned char *zdata = stbi_zlib_compress(raw, (int)raw_len, &zlen, 6);
    free(raw);
    if (!zdata || zlen <= 0) {
        STBIW_FREE(zdata);
        return false;
    }

    size_t total = 8u + (12u + 13u) + (12u + (size_t)zlen) + 12u;
    uint8_t *png = (uint8_t *)malloc(total);
    if (!png) {
        STBIW_FREE(zdata);
        return false;
    }

    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    memcpy(png, sig, 8);
    uint8_t ihdr[13];
    put_be32(ihdr + 0, (uint32_t)img->width);
    put_be32(ihdr + 4, (uint32_t)img->height);
    ihdr[8] = 16; /* 16-bit per channel */
    ihdr[9] = 6;  /* RGBA */
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;

    uint8_t *cur = png + 8;
    cur = write_png_chunk(cur, "IHDR", ihdr, 13);
    cur = write_png_chunk(cur, "IDAT", zdata, (uint32_t)zlen);
    cur = write_png_chunk(cur, "IEND", NULL, 0);
    STBIW_FREE(zdata);

    *out_buf = png;
    *out_len = (size_t)(cur - png);
    return true;
}

static void write_tiff_tag(uint8_t *p, uint16_t tag, uint16_t type, uint32_t count, uint32_t val) {
    put_le16(p + 0, tag);
    put_le16(p + 2, type);
    put_le32(p + 4, count);
    put_le32(p + 8, val);
}

bool nebel_image_encode_tiff16_mem(const NebelImage *img, uint8_t **out_buf, size_t *out_len) {
    if (!img || !img->rgba || !out_buf || !out_len || img->width <= 0 || img->height <= 0) {
        return false;
    }
    size_t npix = (size_t)img->width * (size_t)img->height;
    size_t pix_bytes = npix * 6u;
    if (pix_bytes > 0xE0000000u) return false;

    const uint32_t num_tags = 11u;
    const uint32_t ifd_off = 8u;
    const uint32_t bps_off = ifd_off + 2u + num_tags * 12u + 4u;
    const uint32_t fmt_off = bps_off + 6u;
    const uint32_t data_off = fmt_off + 6u;
    size_t total = (size_t)data_off + pix_bytes;

    uint8_t *buf = (uint8_t *)calloc(1u, total);
    if (!buf) return false;

    buf[0] = 'I'; buf[1] = 'I';
    put_le16(buf + 2, 42);
    put_le32(buf + 4, ifd_off);

    uint8_t *t = buf + ifd_off;
    put_le16(t, (uint16_t)num_tags);
    t += 2;
    write_tiff_tag(t, 256, 4, 1, (uint32_t)img->width);  t += 12;
    write_tiff_tag(t, 257, 4, 1, (uint32_t)img->height); t += 12;
    write_tiff_tag(t, 258, 3, 3, bps_off);               t += 12;
    write_tiff_tag(t, 259, 3, 1, 1);                     t += 12;
    write_tiff_tag(t, 262, 3, 1, 2);                     t += 12;
    write_tiff_tag(t, 273, 4, 1, data_off);              t += 12;
    write_tiff_tag(t, 277, 3, 1, 3);                     t += 12;
    write_tiff_tag(t, 278, 4, 1, (uint32_t)img->height); t += 12;
    write_tiff_tag(t, 279, 4, 1, (uint32_t)pix_bytes);   t += 12;
    write_tiff_tag(t, 284, 3, 1, 1);                     t += 12;
    write_tiff_tag(t, 339, 3, 3, fmt_off);               t += 12;
    put_le32(t, 0);

    put_le16(buf + bps_off + 0, 16);
    put_le16(buf + bps_off + 2, 16);
    put_le16(buf + bps_off + 4, 16);
    put_le16(buf + fmt_off + 0, 1);
    put_le16(buf + fmt_off + 2, 1);
    put_le16(buf + fmt_off + 4, 1);

    uint8_t *px = buf + data_off;
    for (size_t i = 0; i < npix; i++) {
        const float *p = &img->rgba[i * 4u];
        put_le16(px + 0, (uint16_t)(nebel_linear_to_srgb(p[0]) * 65535.0f + 0.5f));
        put_le16(px + 2, (uint16_t)(nebel_linear_to_srgb(p[1]) * 65535.0f + 0.5f));
        put_le16(px + 4, (uint16_t)(nebel_linear_to_srgb(p[2]) * 65535.0f + 0.5f));
        px += 6;
    }

    *out_buf = buf;
    *out_len = total;
    return true;
}

static bool write_bytes_file(const char *path, const uint8_t *buf, size_t len) {
    if (!path || path[0] == '\0' || !buf || len == 0) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = (fwrite(buf, 1, len, f) == len);
    fclose(f);
    return ok;
}

bool nebel_image_save_png16(const NebelImage *img, const char *path) {
    if (!img || !path || path[0] == '\0') return false;
    uint8_t *buf = NULL;
    size_t len = 0;
    if (!nebel_image_encode_png16_mem(img, &buf, &len)) return false;
    bool ok = write_bytes_file(path, buf, len);
    free(buf);
    return ok;
}

bool nebel_image_save_tiff16(const NebelImage *img, const char *path) {
    if (!img || !path || path[0] == '\0') return false;
    uint8_t *buf = NULL;
    size_t len = 0;
    if (!nebel_image_encode_tiff16_mem(img, &buf, &len)) return false;
    bool ok = write_bytes_file(path, buf, len);
    free(buf);
    return ok;
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

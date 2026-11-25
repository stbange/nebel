#ifndef NEBEL_CORE_IMAGE_H
#define NEBEL_CORE_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEBEL_MAX_IMAGE_DIM 16384

typedef struct {
    int width;
    int height;
    float *rgba; /* Linear RGBA32F, 4 * width * height floats */
} NebelImage;

float nebel_srgb_to_linear(float c);
float nebel_linear_to_srgb(float c);

bool nebel_image_alloc(NebelImage *img, int width, int height);
void nebel_image_free(NebelImage *img);
bool nebel_image_clone(const NebelImage *src, NebelImage *dst);

bool nebel_image_from_rgba8(NebelImage *img, const uint8_t *rgba8, int width, int height);
void nebel_image_to_rgba8(const NebelImage *img, uint8_t *out_rgba8);

bool nebel_image_load_file(NebelImage *img, const char *path);
bool nebel_image_load_memory(NebelImage *img, const uint8_t *bytes, size_t len);
bool nebel_image_save_png(const NebelImage *img, const char *path);
bool nebel_image_save_jpg(const NebelImage *img, const char *path, int quality);
bool nebel_image_save_png16(const NebelImage *img, const char *path);
bool nebel_image_save_tiff16(const NebelImage *img, const char *path);
bool nebel_image_encode_png_mem(const NebelImage *img, uint8_t **out_buf, size_t *out_len);
bool nebel_image_encode_jpg_mem(const NebelImage *img, int quality, uint8_t **out_buf, size_t *out_len);
bool nebel_image_encode_png16_mem(const NebelImage *img, uint8_t **out_buf, size_t *out_len);
bool nebel_image_encode_tiff16_mem(const NebelImage *img, uint8_t **out_buf, size_t *out_len);

bool nebel_image_downsample(const NebelImage *src, NebelImage *dst, int max_dim);
float nebel_image_estimate_noise(const NebelImage *img);
void nebel_image_sample_airlight(const NebelImage *img, float norm_x, float norm_y, float out_rgb[3]);

#endif /* NEBEL_CORE_IMAGE_H */

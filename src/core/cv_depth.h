#ifndef NEBEL_CORE_CV_DEPTH_H
#define NEBEL_CORE_CV_DEPTH_H

#include "core/image.h"
#include <stdbool.h>

typedef struct {
    const float *data;
    int width;
    int height;
    float u0, v0, u1, v1; /* Normalized crop bounds in [0, 1] */
} NebelDepthTile;

/* O(1) integral-image scalar Guided Filter preserving edges of `guide` in `dst`. */
bool nebel_guided_filter(const float *guide, const float *src, float *dst,
                         int width, int height, int radius, float eps);

/* Full 3-channel RGB covariance Guided Filter (preserves isoluminant color boundaries). */
bool nebel_guided_filter_rgb(const NebelImage *guide, const float *src, float *dst,
                             int radius, float eps);

/* Pure-C classical depth estimation (horizon + defocus sharpness + Dark Channel Prior). */
bool nebel_depth_estimate_classical(const NebelImage *img, float horizon_y, float *out_depth);

/* Upsample and RGB edge-refine an external monocular depth map (e.g. DA2/DA3 ONNX). */
bool nebel_depth_refine_external(const NebelImage *img, const float *raw_depth,
                                 int raw_w, int raw_h, float *out_depth);

/* Multi-scale PatchFusion: least-squares affine align overlapping tiles onto global depth. */
bool nebel_depth_fuse_tiles(const float *global_depth, int gw, int gh,
                            const NebelDepthTile *tiles, int tile_count,
                            int out_w, int out_h, float *out_depth);

/* Smooth interactive depth touch-up brush (nudge local depth nearer or farther). */
void nebel_depth_paint_brush(float *depth, int w, int h,
                             float u0, float v0, float u1, float v1,
                             float radius, float strength, bool nearer);

#endif /* NEBEL_CORE_CV_DEPTH_H */

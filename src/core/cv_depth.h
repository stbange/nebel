#ifndef NEBEL_CORE_CV_DEPTH_H
#define NEBEL_CORE_CV_DEPTH_H

#include "core/image.h"
#include <stdbool.h>

/* O(1) integral-image Guided Filter preserving edges of `guide` in `dst`. */
bool nebel_guided_filter(const float *guide, const float *src, float *dst,
                         int width, int height, int radius, float eps);

/* Pure-C classical depth estimation (horizon + defocus sharpness + Dark Channel Prior). */
bool nebel_depth_estimate_classical(const NebelImage *img, float horizon_y, float *out_depth);

/* Upsample and edge-refine an external monocular depth map (e.g. Depth Anything V2 ONNX). */
bool nebel_depth_refine_external(const NebelImage *img, const float *raw_depth,
                                 int raw_w, int raw_h, float *out_depth);

#endif /* NEBEL_CORE_CV_DEPTH_H */

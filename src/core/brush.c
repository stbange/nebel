#include "core/brush.h"

#include <math.h>
#include <stddef.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void nebel_brush_defaults(NebelBrushParams *p) {
    if (!p) return;
    p->radius = 0.08f;
    p->opacity = 0.65f;
    p->softness = 0.75f;
}

void nebel_brush_clamp(NebelBrushParams *p) {
    if (!p) return;
    p->radius = clampf(p->radius, 0.005f, 0.5f);
    p->opacity = clampf(p->opacity, 0.02f, 1.0f);
    p->softness = clampf(p->softness, 0.0f, 1.0f);
}

void nebel_brush_stamp(uint8_t *mask, int w, int h,
                       float cx, float cy, float radius_px,
                       float opacity, float softness, bool erase) {
    if (!mask || w <= 0 || h <= 0 || radius_px <= 0.0f) return;
    opacity = clampf(opacity, 0.0f, 1.0f);
    softness = clampf(softness, 0.0f, 1.0f);

    int x0 = (int)floorf(cx - radius_px);
    int y0 = (int)floorf(cy - radius_px);
    int x1 = (int)ceilf(cx + radius_px);
    int y1 = (int)ceilf(cy + radius_px);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= w) x1 = w - 1;
    if (y1 >= h) y1 = h - 1;

    float inner = (1.0f - softness) * radius_px;
    float denom = radius_px - inner;

    for (int y = y0; y <= y1; y++) {
        float dy = ((float)y + 0.5f) - cy;
        for (int x = x0; x <= x1; x++) {
            float dx = ((float)x + 0.5f) - cx;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist >= radius_px) continue;

            float falloff = 1.0f;
            if (dist > inner && denom > 1e-4f) {
                float t = clampf((dist - inner) / denom, 0.0f, 1.0f);
                float smooth = 1.0f - (t * t * (3.0f - 2.0f * t));
                falloff = powf(smooth, 1.0f + softness * 1.25f);
            }
            float a = falloff * opacity;
            uint8_t *cell = &mask[(size_t)y * (size_t)w + (size_t)x];
            float cur = (float)(*cell) / 255.0f;
            float next = erase ? cur * (1.0f - a) : (cur + a * (1.0f - cur));
            *cell = (uint8_t)(clampf(next, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
}

void nebel_brush_stroke(uint8_t *mask, int w, int h,
                        float x0, float y0, float x1, float y1,
                        const NebelBrushParams *p, bool erase) {
    if (!mask || !p || w <= 0 || h <= 0) return;
    NebelBrushParams cp = *p;
    nebel_brush_clamp(&cp);

    float min_dim = (float)(w < h ? w : h);
    float radius_px = cp.radius * min_dim;
    if (radius_px < 1.5f) radius_px = 1.5f;

    float px0 = x0 * (float)w;
    float py0 = y0 * (float)h;
    float px1 = x1 * (float)w;
    float py1 = y1 * (float)h;
    float dx = px1 - px0;
    float dy = py1 - py0;
    float dist = sqrtf(dx * dx + dy * dy);
    float step = radius_px * 0.25f;
    if (step < 1.0f) step = 1.0f;

    int steps = (int)ceilf(dist / step);
    if (steps < 1) {
        nebel_brush_stamp(mask, w, h, px0, py0, radius_px, cp.opacity, cp.softness, erase);
        return;
    }
    /* Distribute per-stamp opacity cleanly so slow vs. fast strokes stay consistent */
    float stamp_op = clampf(cp.opacity * 0.45f, 0.02f, 1.0f);
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        nebel_brush_stamp(mask, w, h, px0 + dx * t, py0 + dy * t,
                          radius_px, stamp_op, cp.softness, erase);
    }
}

void nebel_brush_stroke_envelope(uint8_t *mask, const uint8_t *backup, uint8_t *envelope,
                                 int w, int h, float x0, float y0, float x1, float y1,
                                 const NebelBrushParams *p, bool erase) {
    if (!mask || !backup || !envelope || !p || w <= 0 || h <= 0) return;
    NebelBrushParams cp = *p;
    nebel_brush_clamp(&cp);

    float min_dim = (float)(w < h ? w : h);
    float radius_px = cp.radius * min_dim;
    if (radius_px < 1.5f) radius_px = 1.5f;

    float px0 = x0 * (float)w;
    float py0 = y0 * (float)h;
    float px1 = x1 * (float)w;
    float py1 = y1 * (float)h;
    float dx = px1 - px0;
    float dy = py1 - py0;
    float len2 = dx * dx + dy * dy;

    float min_x = px0 < px1 ? px0 : px1;
    float max_x = px0 > px1 ? px0 : px1;
    float min_y = py0 < py1 ? py0 : py1;
    float max_y = py0 > py1 ? py0 : py1;

    int ix0 = (int)floorf(min_x - radius_px);
    int iy0 = (int)floorf(min_y - radius_px);
    int ix1 = (int)ceilf(max_x + radius_px);
    int iy1 = (int)ceilf(max_y + radius_px);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 >= w) ix1 = w - 1;
    if (iy1 >= h) iy1 = h - 1;

    float inner = (1.0f - cp.softness) * radius_px;
    float denom = radius_px - inner;
    float exp_k = 1.0f + cp.softness * 1.25f;

    for (int y = iy0; y <= iy1; y++) {
        float py = (float)y + 0.5f;
        for (int x = ix0; x <= ix1; x++) {
            float px = (float)x + 0.5f;
            float t_seg = 0.0f;
            if (len2 > 1e-6f) {
                t_seg = clampf(((px - px0) * dx + (py - py0) * dy) / len2, 0.0f, 1.0f);
            }
            float cx = px0 + dx * t_seg;
            float cy = py0 + dy * t_seg;
            float dist = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
            if (dist >= radius_px) continue;

            float falloff = 1.0f;
            if (dist > inner && denom > 1e-4f) {
                float t = clampf((dist - inner) / denom, 0.0f, 1.0f);
                float smooth = 1.0f - (t * t * (3.0f - 2.0f * t));
                falloff = powf(smooth, exp_k);
            }
            uint8_t f8 = (uint8_t)(clampf(falloff, 0.0f, 1.0f) * 255.0f + 0.5f);
            size_t idx = (size_t)y * (size_t)w + (size_t)x;
            if (f8 > envelope[idx]) {
                envelope[idx] = f8;
            }
            float a = ((float)envelope[idx] / 255.0f) * cp.opacity;
            float base = (float)backup[idx] / 255.0f;
            float next = erase ? base * (1.0f - a) : (base + a * (1.0f - base));
            mask[idx] = (uint8_t)(clampf(next, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
}

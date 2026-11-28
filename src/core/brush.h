#ifndef NEBEL_CORE_BRUSH_H
#define NEBEL_CORE_BRUSH_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    NEBEL_TOOL_FOG = 0,
    NEBEL_TOOL_ERASER = 1,
    NEBEL_TOOL_PROTECT = 2,
    NEBEL_TOOL_SUN = 3,
    NEBEL_TOOL_DEPTH_NEAR = 4,
    NEBEL_TOOL_DEPTH_FAR = 5
} NebelTool;

typedef struct {
    float radius;   /* Normalized to min(width, height), range [0.005, 0.5] */
    float opacity;  /* [0.02, 1.0] */
    float softness; /* [0.0, 1.0] */
} NebelBrushParams;

void nebel_brush_defaults(NebelBrushParams *p);
void nebel_brush_clamp(NebelBrushParams *p);

void nebel_brush_stamp(uint8_t *mask, int w, int h,
                       float cx, float cy, float radius_px,
                       float opacity, float softness, bool erase);

/* Normalized coords [0, 1] for endpoints (x0, y0) -> (x1, y1). */
void nebel_brush_stroke(uint8_t *mask, int w, int h,
                        float x0, float y0, float x1, float y1,
                        const NebelBrushParams *p, bool erase);

/* Non-compounding stroke-envelope brush for Fog, Eraser, and Protect:
 * accumulates MAX soft-edge falloff into `envelope` across slow/fast mouse moves
 * and composites once against pre-stroke `backup` so soft borders never harden. */
void nebel_brush_stroke_envelope(uint8_t *mask, const uint8_t *backup, uint8_t *envelope,
                                 int w, int h, float x0, float y0, float x1, float y1,
                                 const NebelBrushParams *p, bool erase);

#endif /* NEBEL_CORE_BRUSH_H */

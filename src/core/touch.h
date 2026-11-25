#ifndef NEBEL_CORE_TOUCH_H
#define NEBEL_CORE_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#define NEBEL_MAX_FINGERS 4

typedef enum {
    NEBEL_GESTURE_NONE = 0,
    NEBEL_GESTURE_STROKE_BEGIN,
    NEBEL_GESTURE_STROKE_MOVE,
    NEBEL_GESTURE_STROKE_END,
    NEBEL_GESTURE_STROKE_CANCEL,
    NEBEL_GESTURE_ZOOM_PAN,
    NEBEL_GESTURE_TAP_UNDO,
    NEBEL_GESTURE_TAP_REDO
} NebelGestureType;

typedef struct {
    NebelGestureType type;
    float x, y;         /* Pixel coordinates (canvas/screen space) */
    float zoom_scale;   /* Multiplicative pinch factor */
    float pan_dx, pan_dy;
} NebelGestureAction;

typedef struct {
    bool active;
    int64_t id;
    float x, y;
    float start_x, start_y;
    uint64_t down_ms;
} NebelFinger;

typedef struct {
    bool touch_mode;
    bool stroke_active;
    bool multi_active;
    int peak_fingers;
    uint64_t gesture_start_ms;
    float prev_pinch_dist;
    float prev_mid_x, prev_mid_y;
    NebelFinger fingers[NEBEL_MAX_FINGERS];
} NebelTouchState;

void nebel_touch_init(NebelTouchState *st);
void nebel_touch_update_viewport(NebelTouchState *st, int width, int height);

NebelGestureAction nebel_touch_finger_down(NebelTouchState *st, int64_t id,
                                           float x, float y, uint64_t now_ms);
NebelGestureAction nebel_touch_finger_move(NebelTouchState *st, int64_t id,
                                           float x, float y, uint64_t now_ms);
NebelGestureAction nebel_touch_finger_up(NebelTouchState *st, int64_t id,
                                         float x, float y, uint64_t now_ms);

#endif /* NEBEL_CORE_TOUCH_H */

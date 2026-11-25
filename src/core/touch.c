#include "core/touch.h"

#include <math.h>
#include <string.h>

void nebel_touch_init(NebelTouchState *st) {
    if (!st) return;
    memset(st, 0, sizeof(*st));
}

void nebel_touch_update_viewport(NebelTouchState *st, int width, int height) {
    if (!st) return;
    if (width > 0 && width < 768 && height > 0) {
        st->touch_mode = true;
    }
}

static int count_active(const NebelTouchState *st) {
    int n = 0;
    for (int i = 0; i < NEBEL_MAX_FINGERS; i++) {
        if (st->fingers[i].active) n++;
    }
    return n;
}

static NebelFinger *find_finger(NebelTouchState *st, int64_t id) {
    for (int i = 0; i < NEBEL_MAX_FINGERS; i++) {
        if (st->fingers[i].active && st->fingers[i].id == id) return &st->fingers[i];
    }
    return NULL;
}

static void get_first_two(const NebelTouchState *st, const NebelFinger **a, const NebelFinger **b) {
    *a = NULL;
    *b = NULL;
    for (int i = 0; i < NEBEL_MAX_FINGERS; i++) {
        if (!st->fingers[i].active) continue;
        if (!*a) *a = &st->fingers[i];
        else if (!*b) { *b = &st->fingers[i]; break; }
    }
}

static float finger_dist(const NebelFinger *a, const NebelFinger *b) {
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    return sqrtf(dx * dx + dy * dy);
}

NebelGestureAction nebel_touch_finger_down(NebelTouchState *st, int64_t id,
                                           float x, float y, uint64_t now_ms) {
    NebelGestureAction act = {0};
    if (!st) return act;
    st->touch_mode = true;

    NebelFinger *slot = find_finger(st, id);
    if (!slot) {
        for (int i = 0; i < NEBEL_MAX_FINGERS; i++) {
            if (!st->fingers[i].active) { slot = &st->fingers[i]; break; }
        }
    }
    if (!slot) return act;

    slot->active = true;
    slot->id = id;
    slot->x = slot->start_x = x;
    slot->y = slot->start_y = y;
    slot->down_ms = now_ms;

    int active = count_active(st);
    if (active == 1) {
        st->peak_fingers = 1;
        st->gesture_start_ms = now_ms;
        st->multi_active = false;
        st->stroke_active = true;
        act.type = NEBEL_GESTURE_STROKE_BEGIN;
        act.x = x;
        act.y = y;
    } else {
        if (active > st->peak_fingers) st->peak_fingers = active;
        const NebelFinger *a = NULL, *b = NULL;
        get_first_two(st, &a, &b);
        if (a && b) {
            st->prev_pinch_dist = finger_dist(a, b);
            st->prev_mid_x = 0.5f * (a->x + b->x);
            st->prev_mid_y = 0.5f * (a->y + b->y);
        }
        st->multi_active = true;
        if (st->stroke_active) {
            st->stroke_active = false;
            act.type = NEBEL_GESTURE_STROKE_CANCEL;
        }
    }
    return act;
}

NebelGestureAction nebel_touch_finger_move(NebelTouchState *st, int64_t id,
                                           float x, float y, uint64_t now_ms) {
    (void)now_ms;
    NebelGestureAction act = {0};
    if (!st) return act;

    NebelFinger *f = find_finger(st, id);
    if (!f) return act;
    f->x = x;
    f->y = y;

    int active = count_active(st);
    if (active == 1 && st->stroke_active && !st->multi_active) {
        act.type = NEBEL_GESTURE_STROKE_MOVE;
        act.x = x;
        act.y = y;
        return act;
    }

    if (active >= 2) {
        const NebelFinger *a = NULL, *b = NULL;
        get_first_two(st, &a, &b);
        if (a && b) {
            float dist = finger_dist(a, b);
            float mx = 0.5f * (a->x + b->x);
            float my = 0.5f * (a->y + b->y);
            act.type = NEBEL_GESTURE_ZOOM_PAN;
            act.x = mx;
            act.y = my;
            act.zoom_scale = (st->prev_pinch_dist > 8.0f && dist > 8.0f)
                ? (dist / st->prev_pinch_dist) : 1.0f;
            act.pan_dx = mx - st->prev_mid_x;
            act.pan_dy = my - st->prev_mid_y;
            st->prev_pinch_dist = dist;
            st->prev_mid_x = mx;
            st->prev_mid_y = my;
        }
    }
    return act;
}

NebelGestureAction nebel_touch_finger_up(NebelTouchState *st, int64_t id,
                                         float x, float y, uint64_t now_ms) {
    NebelGestureAction act = {0};
    if (!st) return act;

    NebelFinger *f = find_finger(st, id);
    if (!f) return act;
    f->x = x;
    f->y = y;
    float moved = hypotf(x - f->start_x, y - f->start_y);
    f->active = false;

    int remaining = count_active(st);
    if (remaining == 0) {
        uint64_t elapsed = now_ms >= st->gesture_start_ms ? (now_ms - st->gesture_start_ms) : 0;
        if (st->stroke_active) {
            st->stroke_active = false;
            act.type = NEBEL_GESTURE_STROKE_END;
            act.x = x;
            act.y = y;
        } else if (st->multi_active && elapsed <= 320 && moved < 24.0f) {
            if (st->peak_fingers == 2) act.type = NEBEL_GESTURE_TAP_UNDO;
            else if (st->peak_fingers >= 3) act.type = NEBEL_GESTURE_TAP_REDO;
        }
        st->multi_active = false;
        st->peak_fingers = 0;
    }
    return act;
}

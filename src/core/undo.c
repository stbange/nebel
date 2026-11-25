#include "core/undo.h"

#include <stdlib.h>
#include <string.h>

static void snapshot_free(NebelUndoSnapshot *s) {
    if (!s) return;
    for (int i = 0; i < NEBEL_MAX_LAYERS; i++) {
        free(s->layer_masks[i]);
        s->layer_masks[i] = NULL;
    }
    free(s->protect_mask);
    s->protect_mask = NULL;
    s->layer_count = 0;
    s->mask_w = 0;
    s->mask_h = 0;
}

void nebel_undo_init(NebelUndoStack *stack) {
    if (!stack) return;
    memset(stack, 0, sizeof(*stack));
    stack->cursor = -1;
}

void nebel_undo_clear(NebelUndoStack *stack) {
    if (!stack) return;
    for (int i = 0; i < NEBEL_MAX_UNDO; i++) {
        snapshot_free(&stack->slots[i]);
    }
    stack->count = 0;
    stack->cursor = -1;
}

void nebel_undo_free(NebelUndoStack *stack) {
    nebel_undo_clear(stack);
}

bool nebel_undo_push(NebelUndoStack *stack,
                     const NebelFogLayer *layers, int layer_count, int active_layer,
                     const uint8_t *protect_mask, int mask_w, int mask_h) {
    if (!stack || !layers || layer_count <= 0 || layer_count > NEBEL_MAX_LAYERS ||
        mask_w <= 0 || mask_h <= 0) {
        return false;
    }

    /* Drop any redo branch past cursor */
    for (int i = stack->cursor + 1; i < stack->count; i++) {
        snapshot_free(&stack->slots[i]);
    }
    stack->count = stack->cursor + 1;

    /* Evict oldest if full */
    if (stack->count == NEBEL_MAX_UNDO) {
        snapshot_free(&stack->slots[0]);
        memmove(&stack->slots[0], &stack->slots[1],
                (size_t)(NEBEL_MAX_UNDO - 1) * sizeof(NebelUndoSnapshot));
        memset(&stack->slots[NEBEL_MAX_UNDO - 1], 0, sizeof(NebelUndoSnapshot));
        stack->count--;
        stack->cursor--;
    }

    NebelUndoSnapshot *s = &stack->slots[stack->count];
    memset(s, 0, sizeof(*s));
    size_t sz = (size_t)mask_w * (size_t)mask_h;

    s->layer_count = layer_count;
    s->active_layer = active_layer;
    s->mask_w = mask_w;
    s->mask_h = mask_h;

    for (int i = 0; i < layer_count; i++) {
        s->params[i] = layers[i].params;
        memcpy(s->names[i], layers[i].name, NEBEL_LAYER_NAME_LEN);
        s->layer_masks[i] = (uint8_t *)malloc(sz);
        if (!s->layer_masks[i]) {
            snapshot_free(s);
            return false;
        }
        if (layers[i].mask && layers[i].mask_w == mask_w && layers[i].mask_h == mask_h) {
            memcpy(s->layer_masks[i], layers[i].mask, sz);
        } else {
            memset(s->layer_masks[i], 0, sz);
        }
    }

    if (protect_mask) {
        s->protect_mask = (uint8_t *)malloc(sz);
        if (!s->protect_mask) {
            snapshot_free(s);
            return false;
        }
        memcpy(s->protect_mask, protect_mask, sz);
    }

    stack->cursor = stack->count;
    stack->count++;
    return true;
}

bool nebel_undo_can_undo(const NebelUndoStack *stack) {
    return stack && stack->cursor > 0;
}

bool nebel_undo_can_redo(const NebelUndoStack *stack) {
    return stack && stack->cursor >= 0 && (stack->cursor + 1) < stack->count;
}

bool nebel_undo_apply(NebelUndoStack *stack, int delta,
                      NebelFogLayer *layers, int *layer_count, int *active_layer,
                      uint8_t *protect_mask, int mask_w, int mask_h) {
    if (!stack || !layers || !layer_count || !active_layer) return false;
    int target = stack->cursor + delta;
    if (target < 0 || target >= stack->count) return false;

    const NebelUndoSnapshot *s = &stack->slots[target];
    if (s->mask_w != mask_w || s->mask_h != mask_h) return false;
    size_t sz = (size_t)mask_w * (size_t)mask_h;

    /* Free excess layers or allocate missing ones */
    for (int i = s->layer_count; i < *layer_count; i++) {
        nebel_fog_layer_free(&layers[i]);
    }
    for (int i = 0; i < s->layer_count; i++) {
        if (i >= *layer_count || !layers[i].mask ||
            layers[i].mask_w != mask_w || layers[i].mask_h != mask_h) {
            nebel_fog_layer_free(&layers[i]);
            if (!nebel_fog_layer_init(&layers[i], s->names[i], mask_w, mask_h)) {
                return false;
            }
        }
        memcpy(layers[i].name, s->names[i], NEBEL_LAYER_NAME_LEN);
        layers[i].params = s->params[i];
        memcpy(layers[i].mask, s->layer_masks[i], sz);
    }

    if (protect_mask && s->protect_mask) {
        memcpy(protect_mask, s->protect_mask, sz);
    }
    *layer_count = s->layer_count;
    *active_layer = s->active_layer;
    stack->cursor = target;
    return true;
}

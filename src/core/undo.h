#ifndef NEBEL_CORE_UNDO_H
#define NEBEL_CORE_UNDO_H

#include "core/fog_layer.h"
#include <stdbool.h>
#include <stdint.h>

#define NEBEL_MAX_UNDO 20

typedef struct {
    int layer_count;
    int active_layer;
    int mask_w;
    int mask_h;
    NebelFogParams params[NEBEL_MAX_LAYERS];
    char names[NEBEL_MAX_LAYERS][NEBEL_LAYER_NAME_LEN];
    uint8_t *layer_masks[NEBEL_MAX_LAYERS];
    uint8_t *protect_mask;
} NebelUndoSnapshot;

typedef struct {
    NebelUndoSnapshot slots[NEBEL_MAX_UNDO];
    int count;
    int cursor; /* Points to current state index, or -1 if empty */
} NebelUndoStack;

void nebel_undo_init(NebelUndoStack *stack);
void nebel_undo_free(NebelUndoStack *stack);
void nebel_undo_clear(NebelUndoStack *stack);

bool nebel_undo_push(NebelUndoStack *stack,
                     const NebelFogLayer *layers, int layer_count, int active_layer,
                     const uint8_t *protect_mask, int mask_w, int mask_h);

bool nebel_undo_can_undo(const NebelUndoStack *stack);
bool nebel_undo_can_redo(const NebelUndoStack *stack);

bool nebel_undo_apply(NebelUndoStack *stack, int delta,
                      NebelFogLayer *layers, int *layer_count, int *active_layer,
                      uint8_t *protect_mask, int mask_w, int mask_h);

#endif /* NEBEL_CORE_UNDO_H */

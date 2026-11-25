#ifndef NEBEL_CORE_APP_STATE_H
#define NEBEL_CORE_APP_STATE_H

#include "core/brush.h"
#include "core/fog_layer.h"
#include "core/image.h"
#include "core/touch.h"
#include "core/ui_state.h"
#include "core/undo.h"

#include <stdbool.h>
#include <stdint.h>

#define NEBEL_WORK_MAX_DIM 2048
#define NEBEL_AUDIO_QUEUE_MAX 8

typedef struct {
    char path[128];
    float gain;
} NebelAudioEvent;

typedef struct {
    char asset_root[256];
    NebelImage orig_img;
    NebelImage work_img;
    float sensor_noise;

    float *depth_map;     /* work_img.width * work_img.height */
    bool depth_is_neural;
    uint8_t *protect_mask;/* work_img.width * work_img.height */

    NebelFogLayer layers[NEBEL_MAX_LAYERS];
    int layer_count;
    int active_layer;

    NebelTool active_tool;
    NebelBrushParams brush;
    bool stroke_in_progress;
    float last_stroke_x, last_stroke_y;
    uint8_t *stroke_backup; /* Restored if multi-touch cancels a tentative stroke */

    float zoom;
    float pan_x, pan_y;
    bool split_enabled;
    float split_pos; /* [0, 1] */
    bool show_depth;

    NebelUndoStack undo;
    NebelTouchState touch;
    NebelUiState ui;

    bool muted;
    NebelAudioEvent audio_queue[NEBEL_AUDIO_QUEUE_MAX];
    int audio_queue_count;

    /* Requests queued by Lua for platform host (file dialog, ONNX auto-depth, script reload) */
    bool req_open_dialog;
    bool req_export_png;
    bool req_export_jpg;
    int req_jpg_quality;
    bool req_auto_depth;
    bool req_reload_scripts;

    bool dirty_photo_gpu;
    bool dirty_masks_gpu;
    bool dirty_depth_gpu;
    char status_msg[128];
} NebelAppState;

void nebel_app_init(NebelAppState *app, const char *asset_root);
void nebel_app_free(NebelAppState *app);

bool nebel_app_load_image(NebelAppState *app, const NebelImage *img);
bool nebel_app_load_image_file(NebelAppState *app, const char *path);
bool nebel_app_load_image_mem(NebelAppState *app, const uint8_t *bytes, size_t len);

int nebel_app_add_layer(NebelAppState *app, const char *name);
bool nebel_app_remove_layer(NebelAppState *app, int index);
bool nebel_app_select_layer(NebelAppState *app, int index);
bool nebel_app_set_layer_param(NebelAppState *app, int index, const char *key, float value);
bool nebel_app_get_layer_param(const NebelAppState *app, int index, const char *key, float *out_val);
bool nebel_app_clear_layer_mask(NebelAppState *app, int index);

/* Canvas coordinate transform between viewport pixels and normalized photo [0, 1]. */
void nebel_app_canvas_rect(const NebelAppState *app, int vw, int vh,
                           float *out_x, float *out_y, float *out_w, float *out_h);
bool nebel_app_screen_to_photo(const NebelAppState *app, int vw, int vh,
                               float sx, float sy, float *out_u, float *out_v);

void nebel_app_stroke_begin(NebelAppState *app, float u, float v);
void nebel_app_stroke_move(NebelAppState *app, float u, float v);
void nebel_app_stroke_end(NebelAppState *app);
void nebel_app_stroke_cancel(NebelAppState *app);

bool nebel_app_undo(NebelAppState *app);
bool nebel_app_redo(NebelAppState *app);

bool nebel_app_compute_classical_depth(NebelAppState *app);
bool nebel_app_set_external_depth(NebelAppState *app, const float *raw_depth, int w, int h);
void nebel_app_sample_airlight(NebelAppState *app, float u, float v);

void nebel_app_handle_gesture(NebelAppState *app, int vw, int vh, NebelGestureAction act);
void nebel_app_queue_audio(NebelAppState *app, const char *rel_path, float gain);
void nebel_app_set_status(NebelAppState *app, const char *msg);

/* Full-resolution linear-light CPU export (matches GPU shader physics). */
bool nebel_app_render_full(const NebelAppState *app, NebelImage *out);
bool nebel_app_export_png(const NebelAppState *app, const char *path);
bool nebel_app_export_jpg(const NebelAppState *app, const char *path, int quality);

#endif /* NEBEL_CORE_APP_STATE_H */

#include "core/app_state.h"
#include "core/cv_depth.h"
#include "core/manifest.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void free_image_buffers(NebelAppState *app) {
    nebel_image_free(&app->orig_img);
    nebel_image_free(&app->work_img);
    free(app->depth_map);
    app->depth_map = NULL;
    free(app->protect_mask);
    app->protect_mask = NULL;
    free(app->stroke_backup);
    app->stroke_backup = NULL;
    free(app->stroke_envelope);
    app->stroke_envelope = NULL;
    for (int i = 0; i < app->layer_count; i++) {
        nebel_fog_layer_free(&app->layers[i]);
    }
    app->layer_count = 0;
    app->active_layer = 0;
    nebel_undo_clear(&app->undo);
}

void nebel_app_init(NebelAppState *app, const char *asset_root) {
    if (!app) return;
    memset(app, 0, sizeof(*app));
    snprintf(app->asset_root, sizeof(app->asset_root), "%s",
             (asset_root && asset_root[0]) ? asset_root : ".");
    nebel_brush_defaults(&app->brush);
    nebel_undo_init(&app->undo);
    nebel_touch_init(&app->touch);
    nebel_ui_init(&app->ui);
    app->zoom = 1.0f;
    app->split_pos = 0.5f;
    app->req_jpg_quality = 92;
#if defined(__EMSCRIPTEN__)
    app->platform_tier = NEBEL_TIER_WEB_DESKTOP;
    snprintf(app->gpu_backend, sizeof(app->gpu_backend), "WebGL2");
#else
    app->platform_tier = NEBEL_TIER_DESKTOP;
    snprintf(app->gpu_backend, sizeof(app->gpu_backend), "OpenGL 3.3 GPU");
#endif
    app->quality = NEBEL_QUALITY_STUDIO;
    app->render_phase = NEBEL_PHASE_MARK;
    app->render_dirty = true;
    app->show_mask_overlay = true;
    snprintf(app->lang, sizeof(app->lang), "en");
    nebel_app_set_status(app, "Ready - mark fog area and press Render");
}

void nebel_app_free(NebelAppState *app) {
    if (!app) return;
    free_image_buffers(app);
    nebel_undo_free(&app->undo);
}

void nebel_app_set_status(NebelAppState *app, const char *msg) {
    if (!app || !msg) return;
    snprintf(app->status_msg, sizeof(app->status_msg), "%s", msg);
}

void nebel_app_set_platform_tier(NebelAppState *app, NebelPlatformTier tier) {
    if (!app) return;
    app->platform_tier = tier;
    if (tier == NEBEL_TIER_WEB_MOBILE) {
        app->touch.touch_mode = true;
    }
}

void nebel_app_set_quality(NebelAppState *app, NebelQualityPreset quality) {
    if (!app) return;
    if (quality < NEBEL_QUALITY_FAST) quality = NEBEL_QUALITY_FAST;
    if (quality > NEBEL_QUALITY_ULTRA) quality = NEBEL_QUALITY_ULTRA;
    if (app->quality != quality) {
        app->quality = quality;
        nebel_app_mark_dirty(app);
    }
}

void nebel_app_set_lang(NebelAppState *app, const char *lang) {
    if (!app || !lang) return;
    if (strncmp(lang, "de", 2) == 0) {
        snprintf(app->lang, sizeof(app->lang), "de");
    } else if (strncmp(lang, "pl", 2) == 0) {
        snprintf(app->lang, sizeof(app->lang), "pl");
    } else {
        snprintf(app->lang, sizeof(app->lang), "en");
    }
}

void nebel_app_set_gpu_backend(NebelAppState *app, const char *backend) {
    if (!app || !backend || !backend[0]) return;
    snprintf(app->gpu_backend, sizeof(app->gpu_backend), "%s", backend);
}

void nebel_app_get_render_budget(const NebelAppState *app,
                                 int *out_samples, int *out_slices,
                                 int *out_shadow_steps, int *out_fluid_steps,
                                 int *out_tile_grid) {
    NebelPlatformTier tier = app ? app->platform_tier : NEBEL_TIER_DESKTOP;
    NebelQualityPreset q = app ? app->quality : NEBEL_QUALITY_STUDIO;

    int samples = 32, slices = 16, shadows = 12, fluid = 20, tiles = 2;
    if (tier == NEBEL_TIER_DESKTOP) {
        if (q == NEBEL_QUALITY_FAST)        { samples = 8;  slices = 10; shadows = 8;  fluid = 8;  tiles = 1; }
        else if (q == NEBEL_QUALITY_STUDIO) { samples = 32; slices = 16; shadows = 12; fluid = 20; tiles = 2; }
        else                                { samples = 64; slices = 24; shadows = 16; fluid = 32; tiles = 3; }
    } else if (tier == NEBEL_TIER_WEB_DESKTOP) {
        if (q == NEBEL_QUALITY_FAST)        { samples = 2;  slices = 8;  shadows = 6;  fluid = 4;  tiles = 1; }
        else if (q == NEBEL_QUALITY_STUDIO) { samples = 6;  slices = 12; shadows = 8;  fluid = 10; tiles = 2; }
        else                                { samples = 12; slices = 16; shadows = 10; fluid = 14; tiles = 2; }
    } else {
        /* Web Mobile: thermal & RAM safe */
        if (q == NEBEL_QUALITY_FAST)        { samples = 1;  slices = 6;  shadows = 4;  fluid = 2;  tiles = 1; }
        else if (q == NEBEL_QUALITY_STUDIO) { samples = 2;  slices = 8;  shadows = 6;  fluid = 4;  tiles = 1; }
        else                                { samples = 4;  slices = 10; shadows = 8;  fluid = 6;  tiles = 1; }
    }

    if (out_samples)      *out_samples = samples;
    if (out_slices)       *out_slices = slices;
    if (out_shadow_steps) *out_shadow_steps = shadows;
    if (out_fluid_steps)  *out_fluid_steps = fluid;
    if (out_tile_grid)    *out_tile_grid = tiles;
}

void nebel_app_mark_dirty(NebelAppState *app) {
    if (!app) return;
    app->render_dirty = true;
    app->dirty_fluid_gpu = true;
    if (app->render_phase == NEBEL_PHASE_DONE || app->render_phase == NEBEL_PHASE_RENDERING ||
        app->render_phase == NEBEL_PHASE_FLUID) {
        app->render_phase = NEBEL_PHASE_MARK;
        app->render_progress = 0.0f;
        app->render_samples_done = 0;
        app->show_mask_overlay = true;
    }
}

void nebel_app_render_start(NebelAppState *app) {
    if (!app || app->work_img.width <= 0) return;
    int samples = 16, tiles = 1;
    nebel_app_get_render_budget(app, &samples, NULL, NULL, NULL, &tiles);
    app->render_samples_total = samples;
    app->render_samples_done = 0;
    app->render_progress = 0.02f;
    app->dirty_fluid_gpu = true;
    app->show_mask_overlay = false;

    /* Run HD depth pass first if not yet computed at neural/tiled HD quality */
    if (!app->depth_is_hd && !app->depth_job_running && tiles >= 1) {
        app->req_hd_depth = true;
        app->render_phase = NEBEL_PHASE_DEPTH;
    } else {
        app->render_phase = NEBEL_PHASE_FLUID;
    }
}

void nebel_app_render_cancel(NebelAppState *app) {
    if (!app) return;
    if (app->render_phase == NEBEL_PHASE_DEPTH ||
        app->render_phase == NEBEL_PHASE_FLUID ||
        app->render_phase == NEBEL_PHASE_RENDERING) {
        app->render_phase = NEBEL_PHASE_MARK;
        app->render_progress = 0.0f;
        app->render_samples_done = 0;
        app->render_dirty = true;
        app->req_hd_depth = false;
        app->show_mask_overlay = true;
    }
}

static void push_undo(NebelAppState *app) {
    if (app->work_img.width <= 0 || app->layer_count <= 0) return;
    nebel_undo_push(&app->undo, app->layers, app->layer_count, app->active_layer,
                    app->protect_mask, app->work_img.width, app->work_img.height);
}

bool nebel_app_load_image(NebelAppState *app, const NebelImage *img) {
    if (!app || !img || !img->rgba || img->width <= 0 || img->height <= 0) return false;

    int max_dim = (app->platform_tier == NEBEL_TIER_WEB_MOBILE)
        ? NEBEL_MOBILE_WORK_MAX_DIM : NEBEL_WORK_MAX_DIM;

    NebelImage orig_copy = {0};
    NebelImage work_copy = {0};
    if (!nebel_image_clone(img, &orig_copy)) return false;
    if (!nebel_image_downsample(&orig_copy, &work_copy, max_dim)) {
        nebel_image_free(&orig_copy);
        return false;
    }

    size_t wn = (size_t)work_copy.width * (size_t)work_copy.height;
    float *depth = (float *)calloc(wn, sizeof(float));
    uint8_t *prot = (uint8_t *)calloc(wn, 1u);
    uint8_t *bak  = (uint8_t *)calloc(wn, 1u);
    uint8_t *env  = (uint8_t *)calloc(wn, 1u);
    if (!depth || !prot || !bak || !env) {
        free(depth); free(prot); free(bak); free(env);
        nebel_image_free(&orig_copy);
        nebel_image_free(&work_copy);
        return false;
    }

    free_image_buffers(app);
    app->orig_img = orig_copy;
    app->work_img = work_copy;
    app->depth_map = depth;
    app->protect_mask = prot;
    app->stroke_backup = bak;
    app->stroke_envelope = env;
    app->depth_is_neural = false;
    app->depth_is_hd = false;
    app->depth_job_running = false;
    app->sensor_noise = nebel_image_estimate_noise(&app->work_img);
    app->zoom = 1.0f;
    app->pan_x = 0.0f;
    app->pan_y = 0.0f;

    nebel_fog_layer_init(&app->layers[0], "Valley Mist", app->work_img.width, app->work_img.height);
    app->layers[0].params.depth_min = 0.40f;
    nebel_image_sample_airlight(&app->work_img, -1.0f, -1.0f, app->layers[0].params.airlight);
    app->layer_count = 1;
    app->active_layer = 0;

    nebel_depth_estimate_classical(&app->work_img, app->layers[0].params.horizon_y, app->depth_map);
    push_undo(app);

    app->dirty_photo_gpu = true;
    app->dirty_masks_gpu = true;
    app->dirty_depth_gpu = true;
    app->dirty_fluid_gpu = true;
    app->render_phase = NEBEL_PHASE_MARK;
    app->render_dirty = true;
    app->render_progress = 0.0f;
    app->render_samples_done = 0;
    app->show_mask_overlay = true;

#if defined(__EMSCRIPTEN__)
    app->req_auto_depth = true;
    nebel_app_set_status(app, "Photo loaded - mark fog area and click Render");
#else
    app->req_auto_depth = true;
    nebel_app_set_status(app, "Photo loaded - mark fog area and click Render");
#endif
    return true;
}

bool nebel_app_load_image_file(NebelAppState *app, const char *path) {
    if (!app || !path) return false;
    NebelImage img = {0};
    if (!nebel_image_load_file(&img, path)) {
        nebel_app_set_status(app, "Failed to load image file");
        return false;
    }
    bool ok = nebel_app_load_image(app, &img);
    nebel_image_free(&img);
    return ok;
}

bool nebel_app_load_image_mem(NebelAppState *app, const uint8_t *bytes, size_t len) {
    if (!app || !bytes || len == 0) return false;
    NebelImage img = {0};
    if (!nebel_image_load_memory(&img, bytes, len)) {
        nebel_app_set_status(app, "Failed to decode image");
        return false;
    }
    bool ok = nebel_app_load_image(app, &img);
    nebel_image_free(&img);
    return ok;
}

int nebel_app_add_layer(NebelAppState *app, const char *name) {
    if (!app || app->work_img.width <= 0 || app->layer_count >= NEBEL_MAX_LAYERS) return -1;
    int idx = app->layer_count;
    char fallback[32];
    snprintf(fallback, sizeof(fallback), "Fog Layer %d", idx + 1);
    if (!nebel_fog_layer_init(&app->layers[idx], (name && name[0]) ? name : fallback,
                              app->work_img.width, app->work_img.height)) {
        return -1;
    }
    nebel_image_sample_airlight(&app->work_img, -1.0f, -1.0f, app->layers[idx].params.airlight);
    app->layers[idx].params.noise_seed = 1337u + (uint32_t)idx * 7919u;
    app->layer_count++;
    app->active_layer = idx;
    app->dirty_masks_gpu = true;
    nebel_app_mark_dirty(app);
    push_undo(app);
    return idx;
}

bool nebel_app_remove_layer(NebelAppState *app, int index) {
    if (!app || app->layer_count <= 1 || index < 0 || index >= app->layer_count) return false;
    nebel_fog_layer_free(&app->layers[index]);
    for (int i = index; i + 1 < app->layer_count; i++) {
        app->layers[i] = app->layers[i + 1];
    }
    memset(&app->layers[app->layer_count - 1], 0, sizeof(NebelFogLayer));
    app->layer_count--;
    if (app->active_layer >= app->layer_count) app->active_layer = app->layer_count - 1;
    app->dirty_masks_gpu = true;
    nebel_app_mark_dirty(app);
    push_undo(app);
    return true;
}

bool nebel_app_select_layer(NebelAppState *app, int index) {
    if (!app || index < 0 || index >= app->layer_count) return false;
    app->active_layer = index;
    return true;
}

bool nebel_app_set_layer_param(NebelAppState *app, int index, const char *key, float value) {
    if (!app || !key || index < 0 || index >= app->layer_count) return false;
    NebelFogParams *p = &app->layers[index].params;

    if (strcmp(key, "density") == 0) p->density = value;
    else if (strcmp(key, "height_falloff") == 0) p->height_falloff = value;
    else if (strcmp(key, "horizon_y") == 0) {
        p->horizon_y = value;
        if (!app->depth_is_neural) app->dirty_depth_gpu = true;
    }
    else if (strcmp(key, "depth_min") == 0) p->depth_min = value;
    else if (strcmp(key, "depth_max") == 0) p->depth_max = value;
    else if (strcmp(key, "airlight_r") == 0) p->airlight[0] = value;
    else if (strcmp(key, "airlight_g") == 0) p->airlight[1] = value;
    else if (strcmp(key, "airlight_b") == 0) p->airlight[2] = value;
    else if (strcmp(key, "sun_x") == 0) p->sun_x = value;
    else if (strcmp(key, "sun_y") == 0) p->sun_y = value;
    else if (strcmp(key, "sun_strength") == 0) p->sun_strength = value;
    else if (strcmp(key, "sun_anisotropy") == 0) p->sun_anisotropy = value;
    else if (strcmp(key, "noise_scale") == 0) p->noise_scale = value;
    else if (strcmp(key, "noise_amount") == 0) p->noise_amount = value;
    else if (strcmp(key, "bloom_amount") == 0) p->bloom_amount = value;
    else if (strcmp(key, "god_rays") == 0) p->god_rays = value;
    else if (strcmp(key, "shadow_pool") == 0) p->shadow_pool = value;
    else if (strcmp(key, "fluid_flow") == 0) p->fluid_flow = value;
    else if (strcmp(key, "delight_strength") == 0) p->delight_strength = value;
    else if (strcmp(key, "grain_match") == 0) p->grain_match = value;
    else if (strcmp(key, "edge_refine") == 0) {
        p->edge_refine = value;
        app->dirty_masks_gpu = true;
    }
    else if (strcmp(key, "noise_seed") == 0) p->noise_seed = (uint32_t)(value > 0.0f ? value : 1.0f);
    else if (strcmp(key, "visible") == 0) p->visible = (value >= 0.5f);
    else return false;

    nebel_fog_params_clamp(p);
    nebel_app_mark_dirty(app);
    return true;
}

bool nebel_app_get_layer_param(const NebelAppState *app, int index, const char *key, float *out_val) {
    if (!app || !key || !out_val || index < 0 || index >= app->layer_count) return false;
    const NebelFogParams *p = &app->layers[index].params;

    if (strcmp(key, "density") == 0) *out_val = p->density;
    else if (strcmp(key, "height_falloff") == 0) *out_val = p->height_falloff;
    else if (strcmp(key, "horizon_y") == 0) *out_val = p->horizon_y;
    else if (strcmp(key, "depth_min") == 0) *out_val = p->depth_min;
    else if (strcmp(key, "depth_max") == 0) *out_val = p->depth_max;
    else if (strcmp(key, "airlight_r") == 0) *out_val = p->airlight[0];
    else if (strcmp(key, "airlight_g") == 0) *out_val = p->airlight[1];
    else if (strcmp(key, "airlight_b") == 0) *out_val = p->airlight[2];
    else if (strcmp(key, "sun_x") == 0) *out_val = p->sun_x;
    else if (strcmp(key, "sun_y") == 0) *out_val = p->sun_y;
    else if (strcmp(key, "sun_strength") == 0) *out_val = p->sun_strength;
    else if (strcmp(key, "sun_anisotropy") == 0) *out_val = p->sun_anisotropy;
    else if (strcmp(key, "noise_scale") == 0) *out_val = p->noise_scale;
    else if (strcmp(key, "noise_amount") == 0) *out_val = p->noise_amount;
    else if (strcmp(key, "bloom_amount") == 0) *out_val = p->bloom_amount;
    else if (strcmp(key, "god_rays") == 0) *out_val = p->god_rays;
    else if (strcmp(key, "shadow_pool") == 0) *out_val = p->shadow_pool;
    else if (strcmp(key, "fluid_flow") == 0) *out_val = p->fluid_flow;
    else if (strcmp(key, "delight_strength") == 0) *out_val = p->delight_strength;
    else if (strcmp(key, "grain_match") == 0) *out_val = p->grain_match;
    else if (strcmp(key, "edge_refine") == 0) *out_val = p->edge_refine;
    else if (strcmp(key, "noise_seed") == 0) *out_val = (float)p->noise_seed;
    else if (strcmp(key, "visible") == 0) *out_val = p->visible ? 1.0f : 0.0f;
    else return false;
    return true;
}

bool nebel_app_clear_layer_mask(NebelAppState *app, int index) {
    if (!app || index < 0 || index >= app->layer_count) return false;
    nebel_fog_layer_clear_mask(&app->layers[index]);
    app->dirty_masks_gpu = true;
    nebel_app_mark_dirty(app);
    push_undo(app);
    return true;
}

void nebel_app_canvas_rect(const NebelAppState *app, int vw, int vh,
                           float *out_x, float *out_y, float *out_w, float *out_h) {
    float top_bar = (app && app->touch.touch_mode) ? 54.0f : 48.0f;
    float side_w = (app && app->touch.touch_mode) ? 0.0f : 292.0f;
    float bot_h = (app && app->touch.touch_mode) ? 148.0f : 30.0f;
    float avail_w = (float)vw - side_w - 24.0f;
    float avail_h = (float)vh - top_bar - bot_h - 14.0f;
    if (avail_w < 64.0f) avail_w = 64.0f;
    if (avail_h < 64.0f) avail_h = 64.0f;

    float iw = (app && app->work_img.width > 0) ? (float)app->work_img.width : 16.0f;
    float ih = (app && app->work_img.height > 0) ? (float)app->work_img.height : 9.0f;
    float fit = fminf(avail_w / iw, avail_h / ih);
    float zoom = app ? clampf(app->zoom, 0.25f, 8.0f) : 1.0f;
    float cw = iw * fit * zoom;
    float ch = ih * fit * zoom;
    float cx = 12.0f + (avail_w - cw) * 0.5f + (app ? app->pan_x : 0.0f);
    float cy = top_bar + 6.0f + (avail_h - ch) * 0.5f + (app ? app->pan_y : 0.0f);
    if (out_x) *out_x = cx;
    if (out_y) *out_y = cy;
    if (out_w) *out_w = cw;
    if (out_h) *out_h = ch;
}

bool nebel_app_screen_to_photo(const NebelAppState *app, int vw, int vh,
                               float sx, float sy, float *out_u, float *out_v) {
    if (!app || app->work_img.width <= 0) return false;
    float cx, cy, cw, ch;
    nebel_app_canvas_rect(app, vw, vh, &cx, &cy, &cw, &ch);
    if (cw <= 1.0f || ch <= 1.0f) return false;
    float u = (sx - cx) / cw;
    float v = (sy - cy) / ch;
    if (out_u) *out_u = u;
    if (out_v) *out_v = v;
    return u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
}

static uint8_t *active_stroke_mask(NebelAppState *app) {
    if (!app || app->layer_count <= 0) return NULL;
    if (app->active_tool == NEBEL_TOOL_PROTECT) return app->protect_mask;
    return app->layers[app->active_layer].mask;
}

void nebel_app_stroke_begin(NebelAppState *app, float u, float v) {
    if (!app || app->work_img.width <= 0 || app->layer_count <= 0) return;
    if (app->active_tool == NEBEL_TOOL_SUN) {
        app->layers[app->active_layer].params.sun_x = clampf(u, 0.0f, 1.0f);
        app->layers[app->active_layer].params.sun_y = clampf(v, 0.0f, 1.0f);
        app->stroke_in_progress = true;
        nebel_app_mark_dirty(app);
        return;
    }

    if ((app->active_tool == NEBEL_TOOL_DEPTH_NEAR || app->active_tool == NEBEL_TOOL_DEPTH_FAR) &&
        app->depth_map) {
        app->stroke_in_progress = true;
        app->last_stroke_x = u;
        app->last_stroke_y = v;
        nebel_depth_paint_brush(app->depth_map, app->work_img.width, app->work_img.height,
                                u, v, u, v, app->brush.radius, app->brush.opacity,
                                app->active_tool == NEBEL_TOOL_DEPTH_NEAR);
        app->dirty_depth_gpu = true;
        app->depth_is_neural = true; /* Prevent classical depth recompute from wiping manual edits */
        nebel_app_mark_dirty(app);
        return;
    }

    if (app->auto_depth_brush && app->active_tool == NEBEL_TOOL_FOG && app->depth_map) {
        int w = app->work_img.width;
        int h = app->work_img.height;
        int cx = (int)(clampf(u, 0.0f, 1.0f) * (float)(w - 1) + 0.5f);
        int cy = (int)(clampf(v, 0.0f, 1.0f) * (float)(h - 1) + 0.5f);
        float sum = 0.0f;
        int cnt = 0;
        for (int dy = -2; dy <= 2; dy++) {
            int py = cy + dy;
            if (py < 0 || py >= h) continue;
            for (int dx = -2; dx <= 2; dx++) {
                int px = cx + dx;
                if (px < 0 || px >= w) continue;
                sum += app->depth_map[(size_t)py * (size_t)w + (size_t)px];
                cnt++;
            }
        }
        if (cnt > 0) {
            float d_click = sum / (float)cnt;
            float d_min = clampf(d_click - 0.14f, 0.0f, 0.85f);
            app->layers[app->active_layer].params.depth_min = d_min;
            nebel_fog_params_clamp(&app->layers[app->active_layer].params);
            char msg[128];
            snprintf(msg, sizeof(msg), "AutoLock: Near Depth %.2f (sampled %.2f)", d_min, d_click);
            nebel_app_set_status(app, msg);
        }
    }

    uint8_t *mask = active_stroke_mask(app);
    if (!mask) return;
    size_t sz = (size_t)app->work_img.width * (size_t)app->work_img.height;
    if (app->stroke_backup) memcpy(app->stroke_backup, mask, sz);
    if (app->stroke_envelope) memset(app->stroke_envelope, 0, sz);

    app->stroke_in_progress = true;
    app->last_stroke_x = u;
    app->last_stroke_y = v;
    bool erase = (app->active_tool == NEBEL_TOOL_ERASER);
    if (app->stroke_backup && app->stroke_envelope) {
        nebel_brush_stroke_envelope(mask, app->stroke_backup, app->stroke_envelope,
                                    app->work_img.width, app->work_img.height,
                                    u, v, u, v, &app->brush, erase);
    } else {
        nebel_brush_stroke(mask, app->work_img.width, app->work_img.height,
                           u, v, u, v, &app->brush, erase);
    }
    app->dirty_masks_gpu = true;
    nebel_app_mark_dirty(app);
    nebel_app_queue_audio(app, "assets/sounds/brush_soft.wav", 0.35f);
}

void nebel_app_stroke_move(NebelAppState *app, float u, float v) {
    if (!app || !app->stroke_in_progress || app->work_img.width <= 0) return;
    if (app->active_tool == NEBEL_TOOL_SUN) {
        app->layers[app->active_layer].params.sun_x = clampf(u, 0.0f, 1.0f);
        app->layers[app->active_layer].params.sun_y = clampf(v, 0.0f, 1.0f);
        nebel_app_mark_dirty(app);
        return;
    }

    if ((app->active_tool == NEBEL_TOOL_DEPTH_NEAR || app->active_tool == NEBEL_TOOL_DEPTH_FAR) &&
        app->depth_map) {
        nebel_depth_paint_brush(app->depth_map, app->work_img.width, app->work_img.height,
                                app->last_stroke_x, app->last_stroke_y, u, v,
                                app->brush.radius, app->brush.opacity,
                                app->active_tool == NEBEL_TOOL_DEPTH_NEAR);
        app->last_stroke_x = u;
        app->last_stroke_y = v;
        app->dirty_depth_gpu = true;
        nebel_app_mark_dirty(app);
        return;
    }

    uint8_t *mask = active_stroke_mask(app);
    if (!mask) return;
    bool erase = (app->active_tool == NEBEL_TOOL_ERASER);
    if (app->stroke_backup && app->stroke_envelope) {
        nebel_brush_stroke_envelope(mask, app->stroke_backup, app->stroke_envelope,
                                    app->work_img.width, app->work_img.height,
                                    app->last_stroke_x, app->last_stroke_y, u, v, &app->brush, erase);
    } else {
        nebel_brush_stroke(mask, app->work_img.width, app->work_img.height,
                           app->last_stroke_x, app->last_stroke_y, u, v, &app->brush, erase);
    }
    app->last_stroke_x = u;
    app->last_stroke_y = v;
    app->dirty_masks_gpu = true;
    nebel_app_mark_dirty(app);
}

void nebel_app_stroke_end(NebelAppState *app) {
    if (!app || !app->stroke_in_progress) return;
    app->stroke_in_progress = false;
    push_undo(app);
}

void nebel_app_stroke_cancel(NebelAppState *app) {
    if (!app || !app->stroke_in_progress) return;
    app->stroke_in_progress = false;
    if (app->active_tool != NEBEL_TOOL_SUN &&
        app->active_tool != NEBEL_TOOL_DEPTH_NEAR &&
        app->active_tool != NEBEL_TOOL_DEPTH_FAR &&
        app->stroke_backup) {
        uint8_t *mask = active_stroke_mask(app);
        if (mask) {
            size_t sz = (size_t)app->work_img.width * (size_t)app->work_img.height;
            memcpy(mask, app->stroke_backup, sz);
            app->dirty_masks_gpu = true;
        }
    }
}

bool nebel_app_undo(NebelAppState *app) {
    if (!app || app->work_img.width <= 0) return false;
    if (!nebel_undo_apply(&app->undo, -1, app->layers, &app->layer_count, &app->active_layer,
                          app->protect_mask, app->work_img.width, app->work_img.height)) {
        return false;
    }
    app->dirty_masks_gpu = true;
    nebel_app_mark_dirty(app);
    nebel_app_queue_audio(app, "assets/sounds/ui_click.wav", 0.35f);
    return true;
}

bool nebel_app_redo(NebelAppState *app) {
    if (!app || app->work_img.width <= 0) return false;
    if (!nebel_undo_apply(&app->undo, 1, app->layers, &app->layer_count, &app->active_layer,
                          app->protect_mask, app->work_img.width, app->work_img.height)) {
        return false;
    }
    app->dirty_masks_gpu = true;
    nebel_app_mark_dirty(app);
    nebel_app_queue_audio(app, "assets/sounds/ui_click.wav", 0.35f);
    return true;
}

bool nebel_app_compute_classical_depth(NebelAppState *app) {
    if (!app || !app->work_img.rgba || !app->depth_map || app->layer_count <= 0) return false;
    bool ok = nebel_depth_estimate_classical(&app->work_img,
                                             app->layers[app->active_layer].params.horizon_y,
                                             app->depth_map);
    if (ok) {
        app->depth_is_neural = false;
        app->dirty_depth_gpu = true;
        app->dirty_fluid_gpu = true;
    }
    return ok;
}

bool nebel_app_set_external_depth(NebelAppState *app, const float *raw_depth, int w, int h) {
    if (!app || !app->work_img.rgba || !app->depth_map || !raw_depth) return false;
    bool ok = nebel_depth_refine_external(&app->work_img, raw_depth, w, h, app->depth_map);
    if (ok) {
        app->depth_is_neural = true;
        app->dirty_depth_gpu = true;
        app->dirty_fluid_gpu = true;
        app->depth_job_running = false;
        if (app->render_phase == NEBEL_PHASE_DEPTH) {
            app->depth_is_hd = true;
            app->render_phase = NEBEL_PHASE_FLUID;
            app->render_progress = 0.35f;
        }
        nebel_app_set_status(app, "AI Depth applied (RGB Guided Filter)");
    }
    return ok;
}

bool nebel_app_set_external_depth_fused(NebelAppState *app,
                                        const float *global_depth, int gw, int gh,
                                        const NebelDepthTile *tiles, int tile_count) {
    if (!app || !app->work_img.rgba || !app->depth_map || !global_depth) return false;
    int w = app->work_img.width;
    int h = app->work_img.height;
    float *fused = (float *)malloc((size_t)w * (size_t)h * sizeof(float));
    if (!fused) return false;

    bool ok = nebel_depth_fuse_tiles(global_depth, gw, gh, tiles, tile_count, w, h, fused);
    if (ok) {
        ok = nebel_depth_refine_external(&app->work_img, fused, w, h, app->depth_map);
    }
    free(fused);

    if (ok) {
        app->depth_is_neural = true;
        app->depth_is_hd = true;
        app->dirty_depth_gpu = true;
        app->dirty_fluid_gpu = true;
        app->depth_job_running = false;
        if (app->render_phase == NEBEL_PHASE_DEPTH) {
            app->render_phase = NEBEL_PHASE_FLUID;
            app->render_progress = 0.38f;
        }
        nebel_app_set_status(app, "HD PatchFusion Depth applied (RGB Guided Filter)");
    }
    return ok;
}

void nebel_app_sample_airlight(NebelAppState *app, float u, float v) {
    if (!app || !app->work_img.rgba || app->layer_count <= 0) return;
    nebel_image_sample_airlight(&app->work_img, u, v, app->layers[app->active_layer].params.airlight);
    nebel_app_mark_dirty(app);
}

void nebel_app_handle_gesture(NebelAppState *app, int vw, int vh, NebelGestureAction act) {
    if (!app) return;
    float u = 0.0f, v = 0.0f;
    switch (act.type) {
    case NEBEL_GESTURE_STROKE_BEGIN:
        if (!app->ui.pointer_over_ui && !app->ui.modal_active &&
            nebel_app_screen_to_photo(app, vw, vh, act.x, act.y, &u, &v)) {
            nebel_app_stroke_begin(app, u, v);
        }
        break;
    case NEBEL_GESTURE_STROKE_MOVE:
        if (app->stroke_in_progress) {
            nebel_app_screen_to_photo(app, vw, vh, act.x, act.y, &u, &v);
            nebel_app_stroke_move(app, clampf(u, 0.0f, 1.0f), clampf(v, 0.0f, 1.0f));
        }
        break;
    case NEBEL_GESTURE_STROKE_END:
        nebel_app_stroke_end(app);
        break;
    case NEBEL_GESTURE_STROKE_CANCEL:
        nebel_app_stroke_cancel(app);
        break;
    case NEBEL_GESTURE_ZOOM_PAN:
        app->zoom = clampf(app->zoom * act.zoom_scale, 0.25f, 8.0f);
        app->pan_x += act.pan_dx;
        app->pan_y += act.pan_dy;
        break;
    case NEBEL_GESTURE_TAP_UNDO:
        nebel_app_undo(app);
        break;
    case NEBEL_GESTURE_TAP_REDO:
        nebel_app_redo(app);
        break;
    default:
        break;
    }
}

void nebel_app_queue_audio(NebelAppState *app, const char *rel_path, float gain) {
    if (!app || app->muted || !nebel_path_is_safe(rel_path)) return;
    if (app->audio_queue_count >= NEBEL_AUDIO_QUEUE_MAX) return;
    NebelAudioEvent *ev = &app->audio_queue[app->audio_queue_count++];
    snprintf(ev->path, sizeof(ev->path), "%s", rel_path);
    ev->gain = clampf(gain, 0.0f, 1.0f);
}

bool nebel_app_render_full(const NebelAppState *app, NebelImage *out) {
    if (!app || !app->orig_img.rgba || !out) return false;
    NebelRenderQuality rq = {0};
    nebel_app_get_render_budget(app, NULL, &rq.ray_slices, &rq.shadow_steps, &rq.fluid_steps, NULL);
    return nebel_fog_render_cpu_ex(&app->orig_img,
                                   app->depth_map, app->work_img.width, app->work_img.height,
                                   app->protect_mask, app->work_img.width, app->work_img.height,
                                   app->layers, app->layer_count,
                                   app->sensor_noise, &rq, out);
}

bool nebel_app_export_png(const NebelAppState *app, const char *path) {
    if (!app || !path) return false;
    NebelImage rendered = {0};
    if (!nebel_app_render_full(app, &rendered)) return false;
    bool ok = nebel_image_save_png(&rendered, path);
    nebel_image_free(&rendered);
    return ok;
}

bool nebel_app_export_jpg(const NebelAppState *app, const char *path, int quality) {
    if (!app || !path) return false;
    NebelImage rendered = {0};
    if (!nebel_app_render_full(app, &rendered)) return false;
    bool ok = nebel_image_save_jpg(&rendered, path, quality);
    nebel_image_free(&rendered);
    return ok;
}

bool nebel_app_export_png16(const NebelAppState *app, const char *path) {
    if (!app || !path) return false;
    NebelImage rendered = {0};
    if (!nebel_app_render_full(app, &rendered)) return false;
    bool ok = nebel_image_save_png16(&rendered, path);
    nebel_image_free(&rendered);
    return ok;
}

bool nebel_app_export_tiff16(const NebelAppState *app, const char *path) {
    if (!app || !path) return false;
    NebelImage rendered = {0};
    if (!nebel_app_render_full(app, &rendered)) return false;
    bool ok = nebel_image_save_tiff16(&rendered, path);
    nebel_image_free(&rendered);
    return ok;
}

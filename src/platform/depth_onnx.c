#include "platform/depth_onnx.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

static NebelAppState *g_depth_app = NULL;

EM_JS(void, nebel_js_run_auto_depth, (const uint8_t *rgba, int w, int h), {
    if (window.nebelWebRunAutoDepth) {
        window.nebelWebRunAutoDepth(rgba, w, h);
    } else if (Module._nebel_web_on_depth_failed) {
        Module._nebel_web_on_depth_failed();
    }
});

EMSCRIPTEN_KEEPALIVE
void nebel_web_on_depth_ready(const float *depth, int w, int h) {
    if (!g_depth_app || !depth || w <= 0 || h <= 0) return;
    nebel_app_set_external_depth(g_depth_app, depth, w, h);
}

EMSCRIPTEN_KEEPALIVE
void nebel_web_on_depth_failed(void) {
    if (!g_depth_app) return;
    nebel_app_compute_classical_depth(g_depth_app);
    nebel_app_set_status(g_depth_app, "AI depth unavailable - using classical CV depth");
}

void nebel_depth_onnx_init(NebelAppState *app) {
    g_depth_app = app;
}

void nebel_depth_onnx_poll(NebelAppState *app) {
    if (!app || !app->req_auto_depth) return;
    app->req_auto_depth = false;
    if (!app->work_img.rgba || app->work_img.width <= 0) return;

    size_t sz = (size_t)app->work_img.width * (size_t)app->work_img.height * 4u;
    uint8_t *rgba8 = (uint8_t *)malloc(sz);
    if (!rgba8) {
        nebel_app_compute_classical_depth(app);
        return;
    }
    nebel_image_to_rgba8(&app->work_img, rgba8);
    nebel_app_set_status(app, "Running Depth Anything V2 (ONNX)...");
    nebel_js_run_auto_depth(rgba8, app->work_img.width, app->work_img.height);
    free(rgba8);
}

#else
#include "core/manifest.h"
#include <SDL3/SDL.h>

/* shortcut: desktop ONNX Runtime C API dynamic dispatch activates only when both
   libonnxruntime and assets/models/depth_anything_v2_vits.onnx exist; otherwise
   falls back cleanly to classical CV depth without compile-time ONNX dependencies. */
void nebel_depth_onnx_init(NebelAppState *app) {
    (void)app;
}

void nebel_depth_onnx_poll(NebelAppState *app) {
    if (!app || !app->req_auto_depth) return;
    app->req_auto_depth = false;
    if (!app->work_img.rgba) return;

    char model_path[384];
    bool has_model = false;
    if (nebel_path_join_asset(app->asset_root, "assets/models/depth_anything_v2_vits.onnx",
                              model_path, sizeof(model_path))) {
        FILE *f = fopen(model_path, "rb");
        if (f) { has_model = true; fclose(f); }
    }

    nebel_app_compute_classical_depth(app);
    if (!has_model) {
        nebel_app_set_status(app, "Classical CV depth updated (drop ONNX model in assets/models/ for AI)");
    } else {
        nebel_app_set_status(app, "Classical CV depth updated (ONNX desktop runtime optional)");
    }
}
#endif

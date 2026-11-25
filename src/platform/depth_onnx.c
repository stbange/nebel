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
#include "onnxruntime_c_api.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

typedef const OrtApiBase *(ORT_API_CALL *NebelOrtGetApiBaseFn)(void);

static bool ort_ok(const OrtApi *api, OrtStatus *st) {
    if (!st) return true;
    api->ReleaseStatus(st);
    return false;
}

static bool find_onnx_model(const char *asset_root, char *out, size_t out_sz) {
    static const char *const candidates[] = {
        "assets/models/depth_anything_v2_vits.onnx",
        "assets/models/model_quantized.onnx",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (!nebel_path_join_asset(asset_root, candidates[i], out, out_sz)) continue;
        FILE *f = fopen(out, "rb");
        if (f) {
            fclose(f);
            return true;
        }
    }
    return false;
}

static SDL_SharedObject *load_ort_library(const char *asset_root) {
    const char *env_lib = getenv("NEBEL_ONNXRUNTIME_LIB");
    if (env_lib && env_lib[0]) {
        SDL_SharedObject *lib = SDL_LoadObject(env_lib);
        if (lib) return lib;
    }

#if defined(_WIN32)
    static const char *const names[] = { "onnxruntime.dll" };
#elif defined(__APPLE__)
    static const char *const names[] = { "libonnxruntime.dylib", "libonnxruntime.1.dylib" };
#else
    static const char *const names[] = { "libonnxruntime.so", "libonnxruntime.so.1" };
#endif

    char buf[512];
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (asset_root && asset_root[0]) {
            snprintf(buf, sizeof(buf), "%s/%s", asset_root, names[i]);
            SDL_SharedObject *lib = SDL_LoadObject(buf);
            if (lib) return lib;
            snprintf(buf, sizeof(buf), "%s/assets/models/%s", asset_root, names[i]);
            lib = SDL_LoadObject(buf);
            if (lib) return lib;
        }
        SDL_SharedObject *lib = SDL_LoadObject(names[i]);
        if (lib) return lib;
    }
    return NULL;
}

static void preprocess_imagenet(const NebelImage *img, float *dst, int size) {
    static const float mean[3] = { 0.485f, 0.456f, 0.406f };
    static const float inv_std[3] = { 1.0f / 0.229f, 1.0f / 0.224f, 1.0f / 0.225f };
    int w = img->width;
    int h = img->height;
    size_t plane = (size_t)size * (size_t)size;

    for (int y = 0; y < size; y++) {
        float sy = size > 1 ? ((float)y / (float)(size - 1)) * (float)(h - 1) : 0.0f;
        int y0 = (int)floorf(sy);
        int y1 = y0 + 1 < h ? y0 + 1 : h - 1;
        float fy = sy - (float)y0;

        for (int x = 0; x < size; x++) {
            float sx = size > 1 ? ((float)x / (float)(size - 1)) * (float)(w - 1) : 0.0f;
            int x0 = (int)floorf(sx);
            int x1 = x0 + 1 < w ? x0 + 1 : w - 1;
            float fx = sx - (float)x0;

            const float *p00 = &img->rgba[((size_t)y0 * (size_t)w + (size_t)x0) * 4u];
            const float *p10 = &img->rgba[((size_t)y0 * (size_t)w + (size_t)x1) * 4u];
            const float *p01 = &img->rgba[((size_t)y1 * (size_t)w + (size_t)x0) * 4u];
            const float *p11 = &img->rgba[((size_t)y1 * (size_t)w + (size_t)x1) * 4u];

            size_t idx = (size_t)y * (size_t)size + (size_t)x;
            for (int c = 0; c < 3; c++) {
                float top = p00[c] + (p10[c] - p00[c]) * fx;
                float bot = p01[c] + (p11[c] - p01[c]) * fx;
                float lin = top + (bot - top) * fy;
                float srgb = nebel_linear_to_srgb(lin);
                dst[(size_t)c * plane + idx] = (srgb - mean[c]) * inv_std[c];
            }
        }
    }
}

static bool run_desktop_onnx(NebelAppState *app, SDL_SharedObject *lib, const char *model_path) {
    NebelOrtGetApiBaseFn get_base = (NebelOrtGetApiBaseFn)SDL_LoadFunction(lib, "OrtGetApiBase");
    if (!get_base) return false;
    const OrtApiBase *base = get_base();
    if (!base || !base->GetApi) return false;
    const OrtApi *api = base->GetApi(17);
    if (!api) api = base->GetApi(ORT_API_VERSION);
    if (!api) return false;

    const int S = 392;
    size_t in_count = (size_t)3 * (size_t)S * (size_t)S;
    float *input_buf = (float *)malloc(in_count * sizeof(float));
    if (!input_buf) return false;
    preprocess_imagenet(&app->work_img, input_buf, S);

    bool ok = false;
    OrtEnv *env = NULL;
    OrtSessionOptions *opts = NULL;
    OrtSession *session = NULL;
    OrtAllocator *alloc = NULL;
    char *in_name = NULL;
    char *out_name = NULL;
    OrtMemoryInfo *mem_info = NULL;
    OrtValue *in_val = NULL;
    OrtValue *out_val = NULL;
    OrtTensorTypeAndShapeInfo *shape_info = NULL;
    float *inv_depth = NULL;

    if (!ort_ok(api, api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "nebel", &env))) goto cleanup;
    if (!ort_ok(api, api->CreateSessionOptions(&opts))) goto cleanup;
    (void)ort_ok(api, api->SetIntraOpNumThreads(opts, 4));

#if defined(_WIN32)
    wchar_t wpath[512];
    if (mbstowcs(wpath, model_path, 511) == (size_t)-1) goto cleanup;
    wpath[511] = L'\0';
    if (!ort_ok(api, api->CreateSession(env, wpath, opts, &session))) goto cleanup;
#else
    if (!ort_ok(api, api->CreateSession(env, model_path, opts, &session))) goto cleanup;
#endif

    if (!ort_ok(api, api->GetAllocatorWithDefaultOptions(&alloc))) goto cleanup;
    if (!ort_ok(api, api->SessionGetInputName(session, 0, alloc, &in_name))) goto cleanup;
    if (!ort_ok(api, api->SessionGetOutputName(session, 0, alloc, &out_name))) goto cleanup;
    if (!ort_ok(api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem_info))) goto cleanup;

    int64_t in_shape[4] = { 1, 3, S, S };
    if (!ort_ok(api, api->CreateTensorWithDataAsOrtValue(
            mem_info, input_buf, in_count * sizeof(float), in_shape, 4,
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in_val))) {
        goto cleanup;
    }

    const char *in_names[1] = { in_name };
    const char *out_names[1] = { out_name };
    const OrtValue *in_vals[1] = { in_val };
    if (!ort_ok(api, api->Run(session, NULL, in_names, in_vals, 1, out_names, 1, &out_val))) {
        goto cleanup;
    }

    if (!ort_ok(api, api->GetTensorTypeAndShape(out_val, &shape_info))) goto cleanup;
    size_t ndims = 0;
    if (!ort_ok(api, api->GetDimensionsCount(shape_info, &ndims)) || ndims < 2 || ndims > 8) goto cleanup;
    int64_t dims[8] = {0};
    if (!ort_ok(api, api->GetDimensions(shape_info, dims, ndims))) goto cleanup;

    int oh = (int)dims[ndims - 2];
    int ow = (int)dims[ndims - 1];
    if (ow <= 0 || oh <= 0 || ow > 4096 || oh > 4096) goto cleanup;

    float *raw = NULL;
    if (!ort_ok(api, api->GetTensorMutableData(out_val, (void **)&raw)) || !raw) goto cleanup;

    size_t n = (size_t)ow * (size_t)oh;
    inv_depth = (float *)malloc(n * sizeof(float));
    if (!inv_depth) goto cleanup;

    float mn = raw[0], mx = raw[0];
    for (size_t i = 1; i < n; i++) {
        if (raw[i] < mn) mn = raw[i];
        if (raw[i] > mx) mx = raw[i];
    }
    float span = (mx - mn > 1e-6f) ? (mx - mn) : 1.0f;
    for (size_t i = 0; i < n; i++) {
        inv_depth[i] = 1.0f - (raw[i] - mn) / span;
    }

    ok = nebel_app_set_external_depth(app, inv_depth, ow, oh);

cleanup:
    free(inv_depth);
    if (shape_info) api->ReleaseTensorTypeAndShapeInfo(shape_info);
    if (out_val) api->ReleaseValue(out_val);
    if (in_val) api->ReleaseValue(in_val);
    if (mem_info) api->ReleaseMemoryInfo(mem_info);
    if (alloc && in_name) (void)ort_ok(api, api->AllocatorFree(alloc, in_name));
    if (alloc && out_name) (void)ort_ok(api, api->AllocatorFree(alloc, out_name));
    if (session) api->ReleaseSession(session);
    if (opts) api->ReleaseSessionOptions(opts);
    if (env) api->ReleaseEnv(env);
    free(input_buf);
    return ok;
}

void nebel_depth_onnx_init(NebelAppState *app) {
    (void)app;
}

void nebel_depth_onnx_poll(NebelAppState *app) {
    if (!app || !app->req_auto_depth) return;
    app->req_auto_depth = false;
    if (!app->work_img.rgba || app->work_img.width <= 0) return;

    char model_path[384];
    bool has_model = find_onnx_model(app->asset_root, model_path, sizeof(model_path));
    if (has_model) {
        SDL_SharedObject *lib = load_ort_library(app->asset_root);
        if (lib) {
            bool ok = run_desktop_onnx(app, lib, model_path);
            SDL_UnloadObject(lib);
            if (ok) return;
            nebel_app_compute_classical_depth(app);
            nebel_app_set_status(app, "ONNX depth failed - fell back to classical CV");
            return;
        }
        nebel_app_compute_classical_depth(app);
        nebel_app_set_status(app, "Classical CV depth (install libonnxruntime for Desktop AI)");
        return;
    }

    nebel_app_compute_classical_depth(app);
    nebel_app_set_status(app, "Classical CV depth (drop ONNX model in assets/models/ for AI)");
}
#endif

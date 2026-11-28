#include "platform/depth_onnx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

static NebelAppState *g_depth_app = NULL;

EM_JS(void, nebel_js_run_auto_depth, (const uint8_t *rgba, int w, int h, int target_edge, int tile_grid), {
    if (window.nebelWebRunAutoDepth) {
        window.nebelWebRunAutoDepth(rgba, w, h, target_edge, tile_grid);
    } else if (Module._nebel_web_on_depth_failed) {
        Module._nebel_web_on_depth_failed();
    }
});

EMSCRIPTEN_KEEPALIVE
void nebel_web_set_env(int is_mobile, const char *lang, const char *backend) {
    if (!g_depth_app) return;
    nebel_app_set_platform_tier(g_depth_app, is_mobile ? NEBEL_TIER_WEB_MOBILE : NEBEL_TIER_WEB_DESKTOP);
    if (lang && lang[0]) nebel_app_set_lang(g_depth_app, lang);
    if (backend && backend[0]) nebel_app_set_gpu_backend(g_depth_app, backend);
}

EMSCRIPTEN_KEEPALIVE
void nebel_web_on_depth_progress(float p) {
    if (!g_depth_app) return;
    if (g_depth_app->render_phase == NEBEL_PHASE_DEPTH) {
        if (p < 0.0f) p = 0.0f;
        if (p > 1.0f) p = 1.0f;
        g_depth_app->render_progress = 0.05f + 0.33f * p;
    }
}

EMSCRIPTEN_KEEPALIVE
void nebel_web_on_depth_ready(const float *depth, int w, int h) {
    if (!g_depth_app || !depth || w <= 0 || h <= 0) return;
    nebel_app_set_external_depth(g_depth_app, depth, w, h);
}

EMSCRIPTEN_KEEPALIVE
void nebel_web_on_depth_fused(const float *global_d, int gw, int gh,
                              const float *tiles_flat, const float *boxes,
                              int tw, int th, int tile_count) {
    if (!g_depth_app || !global_d || gw <= 0 || gh <= 0) return;
    if (!tiles_flat || !boxes || tile_count <= 0 || tile_count > 16 || tw <= 0 || th <= 0) {
        nebel_app_set_external_depth(g_depth_app, global_d, gw, gh);
        return;
    }
    NebelDepthTile tiles[16];
    size_t stride = (size_t)tw * (size_t)th;
    for (int i = 0; i < tile_count; i++) {
        tiles[i].data = tiles_flat + (size_t)i * stride;
        tiles[i].width = tw;
        tiles[i].height = th;
        tiles[i].u0 = boxes[i * 4 + 0];
        tiles[i].v0 = boxes[i * 4 + 1];
        tiles[i].u1 = boxes[i * 4 + 2];
        tiles[i].v1 = boxes[i * 4 + 3];
    }
    nebel_app_set_external_depth_fused(g_depth_app, global_d, gw, gh, tiles, tile_count);
}

EMSCRIPTEN_KEEPALIVE
void nebel_web_on_depth_failed(void) {
    if (!g_depth_app) return;
    g_depth_app->depth_job_running = false;
    nebel_app_compute_classical_depth(g_depth_app);
    if (g_depth_app->render_phase == NEBEL_PHASE_DEPTH) {
        g_depth_app->depth_is_hd = true;
        g_depth_app->render_phase = NEBEL_PHASE_FLUID;
        g_depth_app->render_progress = 0.35f;
    }
    nebel_app_set_status(g_depth_app, "AI depth unavailable - using classical CV depth");
}

void nebel_depth_onnx_init(NebelAppState *app) {
    g_depth_app = app;
}

void nebel_depth_onnx_poll(NebelAppState *app) {
    if (!app || (!app->req_auto_depth && !app->req_hd_depth)) return;
    bool hd = app->req_hd_depth;
    app->req_auto_depth = false;
    app->req_hd_depth = false;
    if (!app->work_img.rgba || app->work_img.width <= 0) return;

    int tile_grid = 1;
    if (hd) {
        nebel_app_get_render_budget(app, NULL, NULL, NULL, NULL, &tile_grid);
    }
    int target_edge = (app->platform_tier == NEBEL_TIER_WEB_MOBILE) ? 392 : (hd ? 518 : 420);

    size_t sz = (size_t)app->work_img.width * (size_t)app->work_img.height * 4u;
    uint8_t *rgba8 = (uint8_t *)malloc(sz);
    if (!rgba8) {
        nebel_web_on_depth_failed();
        return;
    }
    nebel_image_to_rgba8(&app->work_img, rgba8);
    app->depth_job_running = true;
    nebel_app_set_status(app, hd ? "Running HD PatchFusion Depth (ONNX)..."
                                 : "Analyzing 3D depth (ONNX)...");
    nebel_js_run_auto_depth(rgba8, app->work_img.width, app->work_img.height, target_edge, tile_grid);
    free(rgba8);
}

#else
#include "core/manifest.h"
#include "onnxruntime_c_api.h"

#include <SDL3/SDL.h>

typedef const OrtApiBase *(ORT_API_CALL *NebelOrtGetApiBaseFn)(void);
typedef OrtStatus *(ORT_API_CALL *NebelAppendDmlFn)(OrtSessionOptions *, int);
typedef OrtStatus *(ORT_API_CALL *NebelAppendCoremlFn)(OrtSessionOptions *, uint32_t);
typedef OrtStatus *(ORT_API_CALL *NebelAppendCudaFn)(OrtSessionOptions *, int);

static bool ort_ok(const OrtApi *api, OrtStatus *st) {
    if (!st) return true;
    api->ReleaseStatus(st);
    return false;
}

static bool find_onnx_model(const char *asset_root, char *out, size_t out_sz, bool *out_is_da3) {
    static const struct { const char *rel; bool is_da3; } candidates[] = {
        { "assets/models/da3_mono_large.onnx",        true  },
        { "assets/models/DA3Mono-Large.onnx",         true  },
        { "assets/models/depth_anything_v2_vits.onnx", false },
        { "assets/models/model_quantized.onnx",       false },
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (!nebel_path_join_asset(asset_root, candidates[i].rel, out, out_sz)) continue;
        FILE *f = fopen(out, "rb");
        if (f) {
            fclose(f);
            if (out_is_da3) *out_is_da3 = candidates[i].is_da3;
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

static int round_mult14(int v) {
    int r = ((v + 7) / 14) * 14;
    if (r < 224) r = 224;
    if (r > 1036) r = 1036;
    return r;
}

static void calc_aspect_dims(int src_w, int src_h, int target_long, int *out_w, int *out_h) {
    if (src_w >= src_h) {
        *out_w = round_mult14(target_long);
        *out_h = round_mult14((int)((float)(*out_w) * (float)src_h / (float)src_w + 0.5f));
    } else {
        *out_h = round_mult14(target_long);
        *out_w = round_mult14((int)((float)(*out_h) * (float)src_w / (float)src_h + 0.5f));
    }
}

static void preprocess_crop_imagenet(const NebelImage *img, float u0, float v0, float u1, float v1,
                                     float *dst, int dst_w, int dst_h) {
    static const float mean[3] = { 0.485f, 0.456f, 0.406f };
    static const float inv_std[3] = { 1.0f / 0.229f, 1.0f / 0.224f, 1.0f / 0.225f };
    int w = img->width;
    int h = img->height;
    size_t plane = (size_t)dst_w * (size_t)dst_h;

    for (int y = 0; y < dst_h; y++) {
        float tv = dst_h > 1 ? (float)y / (float)(dst_h - 1) : 0.0f;
        float sy = (v0 + (v1 - v0) * tv) * (float)(h - 1);
        int y0 = (int)floorf(sy);
        if (y0 < 0) y0 = 0;
        if (y0 >= h) y0 = h - 1;
        int y1 = y0 + 1 < h ? y0 + 1 : h - 1;
        float fy = sy - (float)y0;
        if (fy < 0.0f) fy = 0.0f;
        if (fy > 1.0f) fy = 1.0f;

        for (int x = 0; x < dst_w; x++) {
            float tu = dst_w > 1 ? (float)x / (float)(dst_w - 1) : 0.0f;
            float sx = (u0 + (u1 - u0) * tu) * (float)(w - 1);
            int x0 = (int)floorf(sx);
            if (x0 < 0) x0 = 0;
            if (x0 >= w) x0 = w - 1;
            int x1 = x0 + 1 < w ? x0 + 1 : w - 1;
            float fx = sx - (float)x0;
            if (fx < 0.0f) fx = 0.0f;
            if (fx > 1.0f) fx = 1.0f;

            const float *p00 = &img->rgba[((size_t)y0 * (size_t)w + (size_t)x0) * 4u];
            const float *p10 = &img->rgba[((size_t)y0 * (size_t)w + (size_t)x1) * 4u];
            const float *p01 = &img->rgba[((size_t)y1 * (size_t)w + (size_t)x0) * 4u];
            const float *p11 = &img->rgba[((size_t)y1 * (size_t)w + (size_t)x1) * 4u];

            size_t idx = (size_t)y * (size_t)dst_w + (size_t)x;
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

static bool run_single_inference(const OrtApi *api, OrtSession *session,
                                 const char *in_name, const char *out_name,
                                 OrtMemoryInfo *mem_info,
                                 const NebelImage *img,
                                 float u0, float v0, float u1, float v1,
                                 int in_w, int in_h, bool is_da3,
                                 float **out_buf, int *out_w, int *out_h) {
    size_t in_count = (size_t)3 * (size_t)in_w * (size_t)in_h;
    float *input_buf = (float *)malloc(in_count * sizeof(float));
    if (!input_buf) return false;
    preprocess_crop_imagenet(img, u0, v0, u1, v1, input_buf, in_w, in_h);

    OrtValue *in_val = NULL;
    OrtValue *out_val = NULL;
    OrtTensorTypeAndShapeInfo *shape_info = NULL;
    bool ok = false;

    int64_t in_shape[4] = { 1, 3, in_h, in_w };
    if (!ort_ok(api, api->CreateTensorWithDataAsOrtValue(
            mem_info, input_buf, in_count * sizeof(float), in_shape, 4,
            ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in_val))) {
        goto done;
    }

    const char *in_names[1] = { in_name };
    const char *out_names[1] = { out_name };
    const OrtValue *in_vals[1] = { in_val };
    if (!ort_ok(api, api->Run(session, NULL, in_names, in_vals, 1, out_names, 1, &out_val))) {
        goto done;
    }

    if (!ort_ok(api, api->GetTensorTypeAndShape(out_val, &shape_info))) goto done;
    size_t ndims = 0;
    if (!ort_ok(api, api->GetDimensionsCount(shape_info, &ndims)) || ndims < 2 || ndims > 8) goto done;
    int64_t dims[8] = {0};
    if (!ort_ok(api, api->GetDimensions(shape_info, dims, ndims))) goto done;

    int oh = (int)dims[ndims - 2];
    int ow = (int)dims[ndims - 1];
    if (ow <= 0 || oh <= 0 || ow > 4096 || oh > 4096) goto done;

    float *raw = NULL;
    if (!ort_ok(api, api->GetTensorMutableData(out_val, (void **)&raw)) || !raw) goto done;

    size_t n = (size_t)ow * (size_t)oh;
    float *res = (float *)malloc(n * sizeof(float));
    if (!res) goto done;

    float mn = raw[0], mx = raw[0];
    for (size_t i = 1; i < n; i++) {
        if (raw[i] < mn) mn = raw[i];
        if (raw[i] > mx) mx = raw[i];
    }
    float span = (mx - mn > 1e-6f) ? (mx - mn) : 1.0f;

    /* DA3Mono outputs direct depth (higher = farther); DA2 outputs disparity (higher = closer) */
    for (size_t i = 0; i < n; i++) {
        float norm = (raw[i] - mn) / span;
        res[i] = is_da3 ? norm : (1.0f - norm);
    }

    *out_buf = res;
    *out_w = ow;
    *out_h = oh;
    ok = true;

done:
    if (shape_info) api->ReleaseTensorTypeAndShapeInfo(shape_info);
    if (out_val) api->ReleaseValue(out_val);
    if (in_val) api->ReleaseValue(in_val);
    free(input_buf);
    return ok;
}

static SDL_SharedObject *g_ort_lib = NULL;
static const OrtApi *g_ort_api = NULL;
static OrtEnv *g_ort_env = NULL;

static bool ensure_ort_runtime(const char *asset_root) {
    if (g_ort_lib && g_ort_api && g_ort_env) return true;
    if (!g_ort_lib) {
        g_ort_lib = load_ort_library(asset_root);
        if (!g_ort_lib) return false;
    }
    if (!g_ort_api) {
        NebelOrtGetApiBaseFn get_base = (NebelOrtGetApiBaseFn)SDL_LoadFunction(g_ort_lib, "OrtGetApiBase");
        if (!get_base) return false;
        const OrtApiBase *base = get_base();
        if (!base || !base->GetApi) return false;
        g_ort_api = base->GetApi(17);
        if (!g_ort_api) g_ort_api = base->GetApi(ORT_API_VERSION);
        if (!g_ort_api) return false;
    }
    if (!g_ort_env) {
        if (!ort_ok(g_ort_api, g_ort_api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "nebel", &g_ort_env))) {
            g_ort_env = NULL;
            return false;
        }
    }
    return true;
}

static bool run_desktop_onnx(NebelAppState *app, const char *model_path, bool is_da3, bool hd) {
    const OrtApi *api = g_ort_api;
    OrtEnv *env = g_ort_env;
    SDL_SharedObject *lib = g_ort_lib;
    if (!api || !env || !lib) return false;

    bool ok = false;
    OrtSessionOptions *opts = NULL;
    OrtSession *session = NULL;
    OrtAllocator *alloc = NULL;
    char *in_name = NULL;
    char *out_name = NULL;
    OrtMemoryInfo *mem_info = NULL;

    if (!ort_ok(api, api->CreateSessionOptions(&opts))) goto cleanup;
#if defined(__APPLE__)
    (void)ort_ok(api, api->SetIntraOpNumThreads(opts, 6));
#else
    (void)ort_ok(api, api->SetIntraOpNumThreads(opts, 4));
#endif

    /* Probe native GPU Execution Providers dynamically (DirectML / CoreML / CUDA) */
    const char *ep_label = "ONNX CPU";
#if defined(_WIN32)
    {
        NebelAppendDmlFn add_dml = (NebelAppendDmlFn)SDL_LoadFunction(lib, "OrtSessionOptionsAppendExecutionProvider_DML");
        if (add_dml && ort_ok(api, add_dml(opts, 0))) ep_label = "DirectML GPU";
    }
#elif defined(__APPLE__)
    {
        /* CoreML fragments quantized INT8 ViTs into 130+ tiny CPU/ANE partitions;
           keep CoreML for FP32/FP16 models (e.g. DA3Mono-Large) and run INT8 cleanly on NEON CPU. */
        bool is_quant = (strstr(model_path, "quantized") != NULL);
        if (!is_quant) {
            NebelAppendCoremlFn add_cml = (NebelAppendCoremlFn)SDL_LoadFunction(lib, "OrtSessionOptionsAppendExecutionProvider_CoreML");
            if (add_cml && ort_ok(api, add_cml(opts, 0))) ep_label = "CoreML GPU";
        }
    }
#else
    {
        NebelAppendCudaFn add_cuda = (NebelAppendCudaFn)SDL_LoadFunction(lib, "OrtSessionOptionsAppendExecutionProvider_CUDA");
        if (add_cuda && ort_ok(api, add_cuda(opts, 0))) ep_label = "CUDA GPU";
    }
#endif
    {
        char badge[64];
        snprintf(badge, sizeof(badge), "GL3.3 + %s%s", ep_label, is_da3 ? " (DA3)" : "");
        nebel_app_set_gpu_backend(app, badge);
    }

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

    int tile_grid = 1;
    if (hd) {
        nebel_app_get_render_budget(app, NULL, NULL, NULL, NULL, &tile_grid);
    }
    int global_edge = hd ? ((app->quality == NEBEL_QUALITY_ULTRA) ? 728 : 616) : 518;
    int gw_in = 518, gh_in = 518;
    calc_aspect_dims(app->work_img.width, app->work_img.height, global_edge, &gw_in, &gh_in);

    float *global_d = NULL;
    int gw_out = 0, gh_out = 0;
    if (!run_single_inference(api, session, in_name, out_name, mem_info,
                              &app->work_img, 0.0f, 0.0f, 1.0f, 1.0f,
                              gw_in, gh_in, is_da3, &global_d, &gw_out, &gh_out)) {
        /* Retry square 392 if a static-shape ONNX export rejected dynamic aspect dimensions */
        if (!run_single_inference(api, session, in_name, out_name, mem_info,
                                  &app->work_img, 0.0f, 0.0f, 1.0f, 1.0f,
                                  392, 392, is_da3, &global_d, &gw_out, &gh_out)) {
            goto cleanup;
        }
        gw_in = 392; gh_in = 392;
    }

    if (tile_grid <= 1) {
        ok = nebel_app_set_external_depth(app, global_d, gw_out, gh_out);
        if (ok && hd) app->depth_is_hd = true;
    } else {
        /* Multi-scale PatchFusion: run overlapping tile_grid x tile_grid high-res crops */
        int max_tiles = tile_grid * tile_grid;
        if (max_tiles > 9) max_tiles = 9;
        NebelDepthTile tiles[9];
        float *tile_bufs[9] = {0};
        int got_tiles = 0;
        int tile_edge = (app->quality == NEBEL_QUALITY_ULTRA) ? 518 : 448;
        int tw_in = 448, th_in = 448;
        calc_aspect_dims(app->work_img.width, app->work_img.height, tile_edge, &tw_in, &th_in);
        if (gw_in == 392 && gh_in == 392) { tw_in = 392; th_in = 392; }

        float overlap = 0.16f;
        float span = (1.0f + overlap * (float)(tile_grid - 1)) / (float)tile_grid;
        float step = (tile_grid > 1) ? (1.0f - span) / (float)(tile_grid - 1) : 0.0f;

        for (int ty = 0; ty < tile_grid; ty++) {
            for (int tx = 0; tx < tile_grid; tx++) {
                float u0 = (float)tx * step;
                float v0 = (float)ty * step;
                float u1 = u0 + span; if (u1 > 1.0f) u1 = 1.0f;
                float v1 = v0 + span; if (v1 > 1.0f) v1 = 1.0f;
                int two = 0, tho = 0;
                if (run_single_inference(api, session, in_name, out_name, mem_info,
                                         &app->orig_img, u0, v0, u1, v1,
                                         tw_in, th_in, is_da3,
                                         &tile_bufs[got_tiles], &two, &tho)) {
                    tiles[got_tiles].data = tile_bufs[got_tiles];
                    tiles[got_tiles].width = two;
                    tiles[got_tiles].height = tho;
                    tiles[got_tiles].u0 = u0;
                    tiles[got_tiles].v0 = v0;
                    tiles[got_tiles].u1 = u1;
                    tiles[got_tiles].v1 = v1;
                    got_tiles++;
                }
            }
        }

        if (got_tiles > 0) {
            ok = nebel_app_set_external_depth_fused(app, global_d, gw_out, gh_out, tiles, got_tiles);
        } else {
            ok = nebel_app_set_external_depth(app, global_d, gw_out, gh_out);
        }
        for (int i = 0; i < got_tiles; i++) free(tile_bufs[i]);
    }
    free(global_d);

cleanup:
    if (mem_info) api->ReleaseMemoryInfo(mem_info);
    if (alloc && in_name) (void)ort_ok(api, api->AllocatorFree(alloc, in_name));
    if (alloc && out_name) (void)ort_ok(api, api->AllocatorFree(alloc, out_name));
    if (session) api->ReleaseSession(session);
    if (opts) api->ReleaseSessionOptions(opts);
    return ok;
}

void nebel_depth_onnx_init(NebelAppState *app) {
    (void)app;
}

void nebel_depth_onnx_poll(NebelAppState *app) {
    if (!app || (!app->req_auto_depth && !app->req_hd_depth)) return;
    bool hd = app->req_hd_depth;
    app->req_auto_depth = false;
    app->req_hd_depth = false;
    if (!app->work_img.rgba) return;

    char model_path[512];
    bool is_da3 = false;
    bool have_model = find_onnx_model(app->asset_root, model_path, sizeof(model_path), &is_da3);

    if (have_model && ensure_ort_runtime(app->asset_root)) {
        nebel_app_set_status(app, hd ? "Running Desktop HD PatchFusion Depth..."
                                     : "Running Desktop ONNX Depth...");
        bool ok = run_desktop_onnx(app, model_path, is_da3, hd);
        if (ok) {
            nebel_app_set_status(app, is_da3 ? "HD Depth ready (DA3Mono-Large + RGB Guided)"
                                             : "HD Depth ready (DA2-Small + RGB Guided)");
            return;
        }
    }

    nebel_app_compute_classical_depth(app);
    if (app->render_phase == NEBEL_PHASE_DEPTH) {
        app->depth_is_hd = true;
        app->render_phase = NEBEL_PHASE_FLUID;
        app->render_progress = 0.35f;
    }
    nebel_app_set_status(app, have_model
        ? "Classical CV depth active (install onnxruntime shared library for AI)"
        : "Classical CV depth active (place Apache-2.0 ONNX in assets/models/)");
}
#endif

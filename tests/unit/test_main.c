#include "core/app_state.h"
#include "core/brush.h"
#include "core/cv_depth.h"
#include "core/fog_layer.h"
#include "core/image.h"
#include "core/manifest.h"
#include "core/touch.h"
#include "core/ui_state.h"
#include "core/undo.h"
#include "script/lua_bridge.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) do { \
    if (cond) { g_pass++; } \
    else { \
        g_fail++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_NEAR(a, b, eps) CHECK(fabsf((a) - (b)) <= (eps))

static NebelImage make_synthetic_scene(int w, int h) {
    NebelImage img = {0};
    CHECK(nebel_image_alloc(&img, w, h));
    for (int y = 0; y < h; y++) {
        float v = (float)y / (float)(h - 1);
        for (int x = 0; x < w; x++) {
            float u = (float)x / (float)(w - 1);
            float *p = &img.rgba[((size_t)y * (size_t)w + (size_t)x) * 4u];
            /* Bright sky above horizon, darker valley below, sharp dark foreground block */
            if (v < 0.4f) {
                p[0] = 0.55f; p[1] = 0.70f; p[2] = 0.90f;
            } else {
                float grad = 0.25f * (1.0f - (v - 0.4f));
                p[0] = grad * 0.6f; p[1] = grad * 0.9f; p[2] = grad * 0.5f;
            }
            /* Bright sun highlight top-right */
            if (hypotf(u - 0.78f, v - 0.20f) < 0.08f) {
                p[0] = 0.98f; p[1] = 0.95f; p[2] = 0.85f;
            }
            /* Crisp dark foreground silhouette (like wildlife subject) */
            if (u > 0.40f && u < 0.60f && v > 0.45f && v < 0.85f) {
                p[0] = 0.03f; p[1] = 0.03f; p[2] = 0.03f;
            }
            /* Deterministic micro-noise so noise estimator measures non-zero grain */
            float n = ((float)(((x * 37 + y * 17) & 15) - 8)) * 0.0008f;
            p[0] = fmaxf(0.0f, fminf(1.0f, p[0] + n));
            p[1] = fmaxf(0.0f, fminf(1.0f, p[1] + n));
            p[2] = fmaxf(0.0f, fminf(1.0f, p[2] + n));
            p[3] = 1.0f;
        }
    }
    return img;
}

static void test_image_module(void) {
    /* sRGB <-> linear round-trip + clamping */
    CHECK_NEAR(nebel_srgb_to_linear(-0.5f), 0.0f, 1e-5f);
    CHECK_NEAR(nebel_srgb_to_linear(1.5f), 1.0f, 1e-5f);
    CHECK_NEAR(nebel_linear_to_srgb(-0.2f), 0.0f, 1e-5f);
    CHECK_NEAR(nebel_linear_to_srgb(2.0f), 1.0f, 1e-5f);
    for (int i = 0; i <= 20; i++) {
        float s = (float)i / 20.0f;
        float lin = nebel_srgb_to_linear(s);
        float back = nebel_linear_to_srgb(lin);
        CHECK_NEAR(back, s, 1e-4f);
    }

    /* Validation boundaries */
    NebelImage bad = {0};
    CHECK(!nebel_image_alloc(NULL, 10, 10));
    CHECK(!nebel_image_alloc(&bad, 0, 10));
    CHECK(!nebel_image_alloc(&bad, 10, -1));
    CHECK(!nebel_image_alloc(&bad, NEBEL_MAX_IMAGE_DIM + 1, 10));
    CHECK(!nebel_image_clone(NULL, &bad));
    CHECK(!nebel_image_from_rgba8(&bad, NULL, 4, 4));
    CHECK(!nebel_image_load_file(&bad, ""));
    CHECK(!nebel_image_load_file(&bad, "/nonexistent/file.png"));
    CHECK(!nebel_image_load_memory(&bad, NULL, 0));
    uint8_t junk[4] = {1, 2, 3, 4};
    CHECK(!nebel_image_load_memory(&bad, junk, sizeof(junk)));
    CHECK(!nebel_image_save_png(&bad, "x.png"));
    CHECK(!nebel_image_save_jpg(&bad, "x.jpg", 90));
    CHECK(!nebel_image_downsample(&bad, &bad, 16));
    CHECK_NEAR(nebel_image_estimate_noise(&bad), 0.0f, 1e-6f);

    /* RGBA8 conversion + PNG/JPEG memory & file round-trip + downsample + airlight + noise */
    NebelImage img = make_synthetic_scene(64, 48);
    uint8_t *rgba8 = (uint8_t *)malloc(64u * 48u * 4u);
    nebel_image_to_rgba8(&img, rgba8);
    NebelImage from8 = {0};
    CHECK(nebel_image_from_rgba8(&from8, rgba8, 64, 48));
    free(rgba8);
    CHECK(from8.width == 64 && from8.height == 48);
    nebel_image_free(&from8);

    uint8_t *png_buf = NULL, *jpg_buf = NULL;
    size_t png_len = 0, jpg_len = 0;
    CHECK(nebel_image_encode_png_mem(&img, &png_buf, &png_len));
    CHECK(png_buf && png_len > 64);
    CHECK(nebel_image_encode_jpg_mem(&img, 5, &jpg_buf, &jpg_len));
    CHECK(jpg_buf && jpg_len > 64);
    free(jpg_buf);
    CHECK(nebel_image_encode_jpg_mem(&img, 120, &jpg_buf, &jpg_len));
    CHECK(jpg_buf && jpg_len > 64);

    NebelImage dec = {0};
    CHECK(nebel_image_load_memory(&dec, png_buf, png_len));
    CHECK(dec.width == 64 && dec.height == 48);
    nebel_image_free(&dec);
    free(png_buf);
    free(jpg_buf);

    CHECK(nebel_image_save_png(&img, "/tmp/nebel_unit_test.png"));
    CHECK(nebel_image_save_jpg(&img, "/tmp/nebel_unit_test.jpg", 90));
    CHECK(nebel_image_load_file(&dec, "/tmp/nebel_unit_test.png"));
    CHECK(dec.width == 64 && dec.height == 48);
    nebel_image_free(&dec);
    CHECK(nebel_image_load_file(&dec, "/tmp/nebel_unit_test.jpg"));
    CHECK(dec.width == 64 && dec.height == 48);
    nebel_image_free(&dec);

    /* Downsample both no-op (larger max_dim) and shrinking */
    NebelImage ds1 = {0}, ds2 = {0};
    CHECK(nebel_image_downsample(&img, &ds1, 128));
    CHECK(ds1.width == 64 && ds1.height == 48);
    CHECK(nebel_image_downsample(&img, &ds2, 32));
    CHECK(ds2.width == 32 && ds2.height == 24);
    nebel_image_free(&ds1);
    nebel_image_free(&ds2);

    float sigma = nebel_image_estimate_noise(&img);
    CHECK(sigma > 0.0f && sigma <= 0.08f);

    float air_auto[3] = {0}, air_pt[3] = {0}, air_null[3] = {0};
    nebel_image_sample_airlight(NULL, 0.5f, 0.5f, air_null);
    CHECK(air_null[2] > air_null[0]);
    nebel_image_sample_airlight(&img, -1.0f, -1.0f, air_auto);
    CHECK(air_auto[2] > 0.5f);
    nebel_image_sample_airlight(&img, 0.5f, 0.2f, air_pt);
    CHECK(air_pt[2] > air_pt[0]);
    nebel_image_free(&img);
    nebel_image_free(NULL);
}

static void test_cv_depth_module(void) {
    NebelImage img = make_synthetic_scene(64, 48);
    size_t n = 64u * 48u;
    float *depth = (float *)calloc(n, sizeof(float));
    float *guide = (float *)calloc(n, sizeof(float));
    float *src_m = (float *)calloc(n, sizeof(float));
    float *dst_m = (float *)calloc(n, sizeof(float));

    CHECK(!nebel_guided_filter(NULL, src_m, dst_m, 64, 48, 3, 0.01f));
    CHECK(!nebel_guided_filter(guide, src_m, dst_m, 0, 48, 3, 0.01f));
    CHECK(!nebel_depth_estimate_classical(NULL, 0.4f, depth));
    CHECK(!nebel_depth_refine_external(&img, NULL, 8, 8, depth));

    /* Step edge test: guided filter preserves crisp boundary in guide */
    for (int y = 0; y < 48; y++) {
        for (int x = 0; x < 64; x++) {
            guide[y * 64 + x] = x < 32 ? 0.05f : 0.95f;
            src_m[y * 64 + x] = x < 34 ? 0.0f : 1.0f; /* Slightly bleeding mask */
        }
    }
    CHECK(nebel_guided_filter(guide, src_m, dst_m, 64, 48, 4, 1e-4f));
    CHECK(dst_m[24 * 64 + 10] < 0.05f);
    CHECK(dst_m[24 * 64 + 54] > 0.95f);
    CHECK(nebel_guided_filter(guide, src_m, dst_m, 64, 48, 0, 0.0f));

    /* Classical depth estimation: sky/horizon farther than bottom ground */
    CHECK(nebel_depth_estimate_classical(&img, 0.40f, depth));
    float top_d = depth[4 * 64 + 16];
    float bot_d = depth[44 * 64 + 16];
    CHECK(top_d > bot_d);

    /* External depth upsampling + Guided Filter refinement */
    float raw_d[16 * 12];
    for (int y = 0; y < 12; y++) {
        for (int x = 0; x < 16; x++) raw_d[y * 16 + x] = (float)(11 - y);
    }
    CHECK(nebel_depth_refine_external(&img, raw_d, 16, 12, depth));
    CHECK(depth[2 * 64 + 32] > depth[45 * 64 + 32]);

    free(depth); free(guide); free(src_m); free(dst_m);
    nebel_image_free(&img);
}

static void test_brush_and_fog_module(void) {
    NebelBrushParams bp;
    nebel_brush_defaults(&bp);
    bp.radius = -1.0f; bp.opacity = 5.0f; bp.softness = 2.0f;
    nebel_brush_clamp(&bp);
    CHECK_NEAR(bp.radius, 0.005f, 1e-5f);
    CHECK_NEAR(bp.opacity, 1.0f, 1e-5f);
    CHECK_NEAR(bp.softness, 1.0f, 1e-5f);

    uint8_t mask[64 * 48];
    memset(mask, 0, sizeof(mask));
    bp.radius = 0.15f; bp.opacity = 0.9f; bp.softness = 0.6f;
    nebel_brush_stroke(mask, 64, 48, 0.2f, 0.5f, 0.8f, 0.5f, &bp, false);
    CHECK(mask[24 * 64 + 32] > 100);
    /* Single-point stroke (zero length) */
    nebel_brush_stroke(mask, 64, 48, 0.5f, 0.5f, 0.5f, 0.5f, &bp, false);
    uint8_t before_erase = mask[24 * 64 + 32];
    nebel_brush_stroke(mask, 64, 48, 0.4f, 0.5f, 0.6f, 0.5f, &bp, true);
    CHECK(mask[24 * 64 + 32] < before_erase);
    nebel_brush_stamp(NULL, 64, 48, 10, 10, 5, 0.5f, 0.5f, false);

    /* Fog params clamp & math */
    NebelFogParams fp;
    nebel_fog_params_default(&fp);
    fp.density = 99.0f; fp.depth_min = 0.7f; fp.depth_max = 0.3f;
    fp.sun_anisotropy = 2.0f; fp.airlight[0] = -1.0f;
    nebel_fog_params_clamp(&fp);
    CHECK_NEAR(fp.density, 4.0f, 1e-5f);
    CHECK_NEAR(fp.depth_max, 0.7f, 1e-5f);
    CHECK_NEAR(fp.sun_anisotropy, 0.88f, 1e-5f);
    CHECK_NEAR(fp.airlight[0], 0.0f, 1e-5f);

    CHECK(nebel_henyey_greenstein(1.0f, 0.7f) > nebel_henyey_greenstein(-1.0f, 0.7f));
    float n1 = nebel_fbm2d(1.5f, 2.3f, 42);
    CHECK(n1 >= 0.0f && n1 <= 1.0f);

    /* Multi-layer linear-light rendering invariants */
    NebelImage img = make_synthetic_scene(64, 48);
    float depth[64 * 48];
    CHECK(nebel_depth_estimate_classical(&img, 0.40f, depth));

    NebelFogLayer layers[2];
    CHECK(nebel_fog_layer_init(&layers[0], "L1", 64, 48));
    CHECK(nebel_fog_layer_init(&layers[1], "L2", 64, 48));
    memset(layers[0].mask, 220, 64u * 48u);
    memset(layers[1].mask, 220, 64u * 48u);
    layers[0].params.depth_min = 0.0f; layers[0].params.depth_max = 1.0f;
    layers[1].params.depth_min = 0.0f; layers[1].params.depth_max = 0.8f;

    uint8_t prot[64 * 48];
    memset(prot, 0, sizeof(prot));
    for (int y = 22; y < 40; y++) {
        for (int x = 26; x < 38; x++) prot[y * 64 + x] = 255;
    }

    NebelImage out1 = {0}, out2 = {0}, out0 = {0};
    CHECK(!nebel_fog_render_cpu(NULL, depth, 64, 48, prot, 64, 48, layers, 1, 0.01f, &out1));
    CHECK(nebel_fog_render_cpu(&img, depth, 64, 48, prot, 64, 48, NULL, 0, 0.01f, &out0));
    CHECK(nebel_fog_render_cpu(&img, depth, 64, 48, prot, 64, 48, layers, 1, 0.01f, &out1));
    CHECK(nebel_fog_render_cpu(&img, depth, 64, 48, prot, 64, 48, layers, 2, 0.01f, &out2));

    /* Background dark valley lifts with 1 layer and lifts further with 2 layers */
    size_t bg_idx = (30u * 64u + 10u) * 4u;
    size_t fg_idx = (30u * 64u + 32u) * 4u;
    CHECK(out1.rgba[bg_idx] > img.rgba[bg_idx] + 0.10f);
    CHECK(out2.rgba[bg_idx] > out1.rgba[bg_idx] + 0.05f);
    /* Protected foreground silhouette remains substantially darker than fogged background */
    CHECK(out1.rgba[fg_idx] < out1.rgba[bg_idx] - 0.15f);

    nebel_fog_layer_clear_mask(&layers[0]);
    CHECK(layers[0].mask[0] == 0);
    nebel_fog_layer_free(&layers[0]);
    nebel_fog_layer_free(&layers[1]);
    nebel_image_free(&out0); nebel_image_free(&out1); nebel_image_free(&out2);
    nebel_image_free(&img);
}

static void test_undo_touch_ui_manifest(void) {
    /* Undo stack including wrap-around beyond NEBEL_MAX_UNDO */
    NebelUndoStack st;
    nebel_undo_init(&st);
    CHECK(!nebel_undo_can_undo(&st));
    CHECK(!nebel_undo_can_redo(&st));

    NebelFogLayer layers[2];
    CHECK(nebel_fog_layer_init(&layers[0], "Base", 8, 8));
    uint8_t prot[64] = {0};
    for (int i = 0; i < NEBEL_MAX_UNDO + 3; i++) {
        layers[0].mask[0] = (uint8_t)i;
        CHECK(nebel_undo_push(&st, layers, 1, 0, prot, 8, 8));
    }
    CHECK(nebel_undo_can_undo(&st));
    int lc = 1, act = 0;
    CHECK(nebel_undo_apply(&st, -1, layers, &lc, &act, prot, 8, 8));
    CHECK(layers[0].mask[0] == (uint8_t)(NEBEL_MAX_UNDO + 1));
    CHECK(nebel_undo_can_redo(&st));
    CHECK(nebel_undo_apply(&st, 1, layers, &lc, &act, prot, 8, 8));
    CHECK(layers[0].mask[0] == (uint8_t)(NEBEL_MAX_UNDO + 2));
    /* Pushing after undo truncates redo */
    CHECK(nebel_undo_apply(&st, -1, layers, &lc, &act, prot, 8, 8));
    layers[0].mask[0] = 99;
    CHECK(nebel_undo_push(&st, layers, 1, 0, prot, 8, 8));
    CHECK(!nebel_undo_can_redo(&st));
    CHECK(!nebel_undo_apply(&st, 1, layers, &lc, &act, prot, 8, 8));
    nebel_fog_layer_free(&layers[0]);
    nebel_undo_free(&st);

    /* Touch gesture recognizer: 1-finger stroke, 2-finger cancel+pinch, 2-tap undo, 3-tap redo */
    NebelTouchState ts;
    nebel_touch_init(&ts);
    nebel_touch_update_viewport(&ts, 1280, 760);
    CHECK(!ts.touch_mode);
    nebel_touch_update_viewport(&ts, 390, 844);
    CHECK(ts.touch_mode);

    NebelGestureAction a = nebel_touch_finger_down(&ts, 1, 100, 100, 10);
    CHECK(a.type == NEBEL_GESTURE_STROKE_BEGIN);
    a = nebel_touch_finger_move(&ts, 1, 120, 110, 20);
    CHECK(a.type == NEBEL_GESTURE_STROKE_MOVE);
    a = nebel_touch_finger_up(&ts, 1, 120, 110, 30);
    CHECK(a.type == NEBEL_GESTURE_STROKE_END);

    /* 2nd finger cancels tentative stroke and triggers pinch zoom */
    a = nebel_touch_finger_down(&ts, 1, 100, 100, 100);
    CHECK(a.type == NEBEL_GESTURE_STROKE_BEGIN);
    a = nebel_touch_finger_down(&ts, 2, 200, 100, 115);
    CHECK(a.type == NEBEL_GESTURE_STROKE_CANCEL);
    a = nebel_touch_finger_move(&ts, 2, 300, 100, 150);
    CHECK(a.type == NEBEL_GESTURE_ZOOM_PAN && a.zoom_scale > 1.5f);
    nebel_touch_finger_up(&ts, 1, 100, 100, 600);
    a = nebel_touch_finger_up(&ts, 2, 300, 100, 610);
    CHECK(a.type == NEBEL_GESTURE_NONE);

    /* 2-finger quick tap -> undo */
    nebel_touch_finger_down(&ts, 1, 100, 100, 1000);
    nebel_touch_finger_down(&ts, 2, 160, 100, 1010);
    nebel_touch_finger_up(&ts, 1, 102, 100, 1090);
    a = nebel_touch_finger_up(&ts, 2, 162, 100, 1100);
    CHECK(a.type == NEBEL_GESTURE_TAP_UNDO);

    /* 3-finger quick tap -> redo */
    nebel_touch_finger_down(&ts, 1, 100, 100, 2000);
    nebel_touch_finger_down(&ts, 2, 140, 100, 2005);
    nebel_touch_finger_down(&ts, 3, 180, 100, 2010);
    nebel_touch_finger_up(&ts, 1, 100, 100, 2090);
    nebel_touch_finger_up(&ts, 2, 140, 100, 2095);
    a = nebel_touch_finger_up(&ts, 3, 180, 100, 2100);
    CHECK(a.type == NEBEL_GESTURE_TAP_REDO);

    /* UI immediate-mode hit testing */
    NebelUiState ui;
    nebel_ui_init(&ui);
    nebel_ui_begin_frame(&ui, 800, 600, 50, 20, true, true, false);
    nebel_ui_panel(&ui, 0, 0, 800, 44, 0.1f, 0.1f, 0.1f, 1.0f);
    CHECK(!nebel_ui_button(&ui, 1, 10, 5, 80, 30, "Open", false, 4));
    float sval = 1.0f;
    CHECK(nebel_ui_slider(&ui, 2, 10, 10, 100, 30, "Density", &sval, 0.0f, 4.0f));
    nebel_ui_ring(&ui, 400, 300, 30, 1, 1, 1, 0.8f);
    nebel_ui_end_frame(&ui);
    CHECK(ui.pointer_over_ui && ui.cmd_count > 4);

    /* Release on button triggers click */
    nebel_ui_begin_frame(&ui, 800, 600, 50, 20, true, true, false);
    nebel_ui_button(&ui, 7, 10, 5, 80, 30, "Go", true, -1);
    nebel_ui_end_frame(&ui);
    nebel_ui_begin_frame(&ui, 800, 600, 50, 20, false, false, true);
    CHECK(nebel_ui_button(&ui, 7, 10, 5, 80, 30, "Go", false, -1));
    nebel_ui_end_frame(&ui);

    /* Manifest & path traversal safety */
    CHECK(nebel_path_is_safe("assets/shaders/fog_layer.frag"));
    CHECK(!nebel_path_is_safe(NULL));
    CHECK(!nebel_path_is_safe(""));
    CHECK(!nebel_path_is_safe("/etc/passwd"));
    CHECK(!nebel_path_is_safe("../secret.lua"));
    CHECK(!nebel_path_is_safe("assets/../secret"));
    CHECK(!nebel_path_is_safe("C:\\windows\\win.ini"));
    char joined[128];
    CHECK(nebel_path_join_asset(".", "scripts/main.lua", joined, sizeof(joined)));
    CHECK(strcmp(joined, "./scripts/main.lua") == 0);
    CHECK(nebel_path_join_asset("", "scripts/main.lua", joined, sizeof(joined)));
    CHECK(!nebel_path_join_asset(".", "../bad", joined, sizeof(joined)));

    const char *mjson = "{\"files\":[\"scripts/main.lua\",\"../evil\",\"assets/a.png\"]}";
    char parsed[8][NEBEL_PATH_MAX];
    int cnt = nebel_manifest_parse(mjson, parsed, 8);
    CHECK(cnt == 2);
    CHECK(strcmp(parsed[0], "scripts/main.lua") == 0);
    CHECK(strcmp(parsed[1], "assets/a.png") == 0);
    CHECK(nebel_manifest_parse("{}", parsed, 8) == 0);
}

static void test_app_and_lua_bridge(void) {
    NebelAppState app;
    nebel_app_init(&app, NEBEL_SOURCE_DIR);
    NebelImage img = make_synthetic_scene(64, 48);
    CHECK(nebel_app_load_image(&app, &img));
    nebel_image_free(&img);

    /* Layer param get/set + unknown key */
    const char *keys[] = {
        "density", "height_falloff", "horizon_y", "depth_min", "depth_max",
        "airlight_r", "airlight_g", "airlight_b", "sun_x", "sun_y",
        "sun_strength", "sun_anisotropy", "noise_scale", "noise_amount",
        "bloom_amount", "grain_match", "edge_refine", "visible"
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        CHECK(nebel_app_set_layer_param(&app, 0, keys[i], 0.5f));
        float out = -1.0f;
        CHECK(nebel_app_get_layer_param(&app, 0, keys[i], &out));
    }
    float dummy = 0.0f;
    CHECK(!nebel_app_set_layer_param(&app, 0, "nope", 1.0f));
    CHECK(!nebel_app_get_layer_param(&app, 0, "nope", &dummy));

    /* Add/select/remove layer + sun tool + stroke cancel */
    int l2 = nebel_app_add_layer(&app, "Ridge Haze");
    CHECK(l2 == 1 && app.layer_count == 2);
    CHECK(nebel_app_select_layer(&app, 0));
    app.active_tool = NEBEL_TOOL_SUN;
    nebel_app_stroke_begin(&app, 0.3f, 0.2f);
    nebel_app_stroke_move(&app, 0.4f, 0.25f);
    nebel_app_stroke_end(&app);
    CHECK_NEAR(app.layers[0].params.sun_x, 0.4f, 1e-4f);

    app.active_tool = NEBEL_TOOL_FOG;
    nebel_app_stroke_begin(&app, 0.5f, 0.5f);
    nebel_app_stroke_cancel(&app);
    CHECK(app.layers[0].mask[24 * 64 + 32] == 0);

    CHECK(nebel_app_remove_layer(&app, 1));
    CHECK(app.layer_count == 1);
    CHECK(!nebel_app_remove_layer(&app, 0)); /* Cannot remove last layer */

    /* Lua bridge full coverage */
    NebelLuaBridge lua;
    CHECK(nebel_lua_init(&lua, &app));
    CHECK(nebel_lua_reload(&lua));
    CHECK(nebel_lua_apply_preset(&lua, "Morning Valley"));
    CHECK(!nebel_lua_apply_preset(&lua, "Nonexistent Preset"));

    /* Exercise Lua API surface directly */
    const char *script =
        "assert(nebel.layers.count() >= 1)\n"
        "local idx = nebel.layers.add('Extra')\n"
        "assert(idx == 2)\n"
        "assert(nebel.layers.name(idx) == 'Extra')\n"
        "assert(nebel.layers.name(99) == nil)\n"
        "assert(nebel.layers.select(idx))\n"
        "assert(nebel.layers.set(idx, 'density', 1.7))\n"
        "assert(math.abs(nebel.layers.get(idx, 'density') - 1.7) < 0.01)\n"
        "assert(nebel.layers.get(idx, 'bad_key') == nil)\n"
        "assert(nebel.layers.clear(idx))\n"
        "assert(nebel.layers.remove(idx))\n"
        "assert(nebel.brush.set_tool('protect'))\n"
        "assert(nebel.brush.get_tool() == 'protect')\n"
        "assert(not nebel.brush.set_tool('invalid'))\n"
        "nebel.brush.set_radius(0.12); assert(math.abs(nebel.brush.get_radius()-0.12)<0.01)\n"
        "nebel.brush.set_opacity(0.7); assert(math.abs(nebel.brush.get_opacity()-0.7)<0.01)\n"
        "nebel.brush.set_softness(0.5); assert(math.abs(nebel.brush.get_softness()-0.5)<0.01)\n"
        "nebel.brush.stroke_begin(0.2, 0.2); nebel.brush.stroke_move(0.3, 0.3); nebel.brush.stroke_end()\n"
        "nebel.view.set_zoom(2.0); assert(nebel.view.zoom() == 2.0)\n"
        "nebel.view.pan(10, -5); nebel.view.reset(); assert(nebel.view.zoom() == 1.0)\n"
        "nebel.view.set_split(true, 0.4); assert(nebel.view.split_enabled())\n"
        "assert(math.abs(nebel.view.split_pos() - 0.4) < 0.01)\n"
        "assert(nebel.view.can_undo()); assert(nebel.view.undo()); assert(nebel.view.redo())\n"
        "nebel.view.trigger_auto_depth(); nebel.view.sample_airlight(-1, -1)\n"
        "nebel.view.set_show_depth(true); assert(nebel.view.show_depth())\n"
        "nebel.view.set_show_depth(false); assert(not nebel.view.show_depth())\n"
        "nebel.view.set_status('ok'); assert(nebel.view.status() == 'ok')\n"
        "local cx,cy,cw,ch = nebel.view.canvas_rect(); assert(cw > 0 and ch > 0)\n"
        "nebel.io.open_photo(); nebel.io.export_png(); nebel.io.export_jpg(90); nebel.io.reload_scripts()\n"
        "assert(not nebel.io.load_sample('../bad.png'))\n"
        "nebel.audio.set_muted(true); assert(nebel.audio.is_muted())\n"
        "nebel.audio.set_muted(false); nebel.audio.play('assets/sounds/ui_click.wav', 0.5)\n"
        "local vw, vh = nebel.ui.viewport_size(); local mx, my = nebel.ui.pointer_pos()\n";
    CHECK(nebel_lua_eval(&lua, script));

    /* Memory load (valid & invalid), bad file load, classical depth recompute, layer-count undo */
    CHECK(!nebel_app_load_image_file(&app, "/nonexistent/nebel_missing.png"));
    uint8_t bad_bytes[4] = {0, 1, 2, 3};
    CHECK(!nebel_app_load_image_mem(&app, bad_bytes, sizeof(bad_bytes)));
    uint8_t *enc_png = NULL;
    size_t enc_len = 0;
    CHECK(nebel_image_encode_png_mem(&app.orig_img, &enc_png, &enc_len));
    CHECK(nebel_app_load_image_mem(&app, enc_png, enc_len));
    free(enc_png);
    CHECK(nebel_app_compute_classical_depth(&app));

    /* Undo/redo across adding and removing a second fog layer */
    CHECK(nebel_app_add_layer(&app, "Upper Haze") == 1);
    CHECK(app.layer_count == 2);
    CHECK(nebel_app_undo(&app));
    CHECK(app.layer_count == 1);
    CHECK(nebel_app_redo(&app));
    CHECK(app.layer_count == 2);

    /* Safe error isolation: bad Lua never crashes C (including inside callbacks) */
    CHECK(!nebel_lua_eval(&lua, "error('intentional boom')"));
    CHECK(lua.last_error[0] != '\0');
    CHECK(!nebel_lua_load_file(&lua, "../escape.lua"));
    CHECK(!nebel_lua_load_file(&lua, "scripts/does_not_exist.lua"));
    CHECK(nebel_lua_eval(&lua, "function on_ui() error('ui boom') end"));
    CHECK(!nebel_lua_on_ui(&lua, 100, 100));
    CHECK(nebel_lua_eval(&lua, "on_ui = nil; on_key = nil; apply_preset = nil"));
    CHECK(nebel_lua_on_ui(&lua, 100, 100));
    CHECK(nebel_lua_on_key(&lua, "B", false, false));
    CHECK(!nebel_lua_apply_preset(&lua, "Morning Valley"));
    CHECK(nebel_lua_eval(&lua, "function on_key() error('k') end; function apply_preset() error('p') end"));
    CHECK(!nebel_lua_on_key(&lua, "B", false, false));
    CHECK(!nebel_lua_apply_preset(&lua, "Morning Valley"));
    CHECK(nebel_lua_reload(&lua));

    /* UI + Keyboard hooks */
    nebel_ui_begin_frame(&app.ui, 1280, 760, 600, 400, false, false, false);
    CHECK(nebel_lua_on_ui(&lua, 1280, 760));
    nebel_ui_end_frame(&app.ui);
    CHECK(nebel_lua_on_key(&lua, "B", false, false));
    CHECK(nebel_lua_on_key(&lua, "E", false, false));
    CHECK(nebel_lua_on_key(&lua, "P", false, false));
    CHECK(nebel_lua_on_key(&lua, "S", false, false));
    CHECK(nebel_lua_on_key(&lua, "[", false, false));
    CHECK(nebel_lua_on_key(&lua, "]", false, false));
    CHECK(nebel_lua_on_key(&lua, "SPACE", false, false));
    CHECK(nebel_lua_on_key(&lua, "Z", true, false));
    CHECK(nebel_lua_on_key(&lua, "Z", true, true));
    CHECK(nebel_lua_on_key(&lua, "Y", true, false));
    CHECK(nebel_lua_on_key(&lua, "0", false, false));
    CHECK(nebel_lua_on_key(&lua, "F5", false, false));

    nebel_lua_free(&lua);
    nebel_app_free(&app);
}

int main(void) {
    test_image_module();
    test_cv_depth_module();
    test_brush_and_fog_module();
    test_undo_touch_ui_manifest();
    test_app_and_lua_bridge();

    printf("Unit tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

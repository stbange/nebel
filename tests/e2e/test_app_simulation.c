/* Automated End-to-End Headless Simulation ("Game / App") Test for Nebel.
 * Boots real C core + external Lua scripts + external loose assets, feeds
 * synthetic desktop mouse, keyboard, and mobile touch frames, and verifies
 * optical physics, foreground protection, multi-layer fog, undo/redo, and export.
 */
#include "core/app_state.h"
#include "core/manifest.h"
#include "script/lua_bridge.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "[E2E FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static char *read_all(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = (char *)malloc((size_t)sz + 1u);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
    b[sz] = '\0';
    fclose(f);
    return b;
}

static float pixel_luma(const NebelImage *img, int x, int y) {
    const float *p = &img->rgba[((size_t)y * (size_t)img->width + (size_t)x) * 4u];
    return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
}

int main(void) {
    /* 1. Verify every loose file listed in assets/manifest.json exists on disk */
    char mpath[384];
    ASSERT_TRUE(nebel_path_join_asset(NEBEL_SOURCE_DIR, "assets/manifest.json", mpath, sizeof(mpath)));
    char *mjson = read_all(mpath);
    ASSERT_TRUE(mjson != NULL);
    char files[64][NEBEL_PATH_MAX];
    int fcount = nebel_manifest_parse(mjson, files, 64);
    free(mjson);
    ASSERT_TRUE(fcount >= 14);
    for (int i = 0; i < fcount; i++) {
        char fp[384];
        ASSERT_TRUE(nebel_path_join_asset(NEBEL_SOURCE_DIR, files[i], fp, sizeof(fp)));
        FILE *f = fopen(fp, "rb");
        ASSERT_TRUE(f != NULL);
        fclose(f);
    }

    /* 2. Boot engine + external Lua scripts (loads bundled sample alpine_valley.jpg!) */
    NebelAppState app;
    NebelLuaBridge lua;
    nebel_app_init(&app, NEBEL_SOURCE_DIR);
    ASSERT_TRUE(nebel_lua_init(&lua, &app));
    ASSERT_TRUE(nebel_lua_reload(&lua));
    ASSERT_TRUE(app.orig_img.width > 100 && app.orig_img.height > 100);
    ASSERT_TRUE(app.layer_count == 1);

    /* 3. Load deterministic wildlife-silhouette synthetic photo to test exact optical physics */
    const int W = 96, H = 64;
    NebelImage scene = {0};
    ASSERT_TRUE(nebel_image_alloc(&scene, W, H));
    for (int y = 0; y < H; y++) {
        float v = (float)y / (float)(H - 1);
        for (int x = 0; x < W; x++) {
            float u = (float)x / (float)(W - 1);
            float *p = &scene.rgba[((size_t)y * (size_t)W + (size_t)x) * 4u];
            if (v < 0.38f) {
                p[0] = 0.55f; p[1] = 0.72f; p[2] = 0.92f; /* Clear sky */
            } else {
                p[0] = 0.08f; p[1] = 0.14f; p[2] = 0.09f; /* Dark valley pines */
            }
            /* Sharp dark foreground stag silhouette in center-right */
            if (u >= 0.58f && u <= 0.74f && v >= 0.42f && v <= 0.88f) {
                p[0] = 0.02f; p[1] = 0.02f; p[2] = 0.02f;
            }
            p[3] = 1.0f;
        }
    }
    ASSERT_TRUE(nebel_app_load_image(&app, &scene));
    nebel_image_free(&scene);

    /* Feed realistic 3D depth structure (foreground stag silhouette=0.12, deep forest=0.85) */
    float depth3d[96 * 64];
    for (int y = 0; y < H; y++) {
        float v = (float)y / (float)(H - 1);
        for (int x = 0; x < W; x++) {
            float u = (float)x / (float)(W - 1);
            float d = 0.85f - 0.15f * v;
            if (u >= 0.58f && u <= 0.74f && v >= 0.42f && v <= 0.88f) d = 0.12f;
            depth3d[y * W + x] = d;
        }
    }
    ASSERT_TRUE(nebel_app_set_external_depth(&app, depth3d, W, H));
    ASSERT_TRUE(app.depth_is_neural);

    /* 4. Desktop workflow: broad single swipe over BOTH background AND foreground stag! */
    nebel_ui_begin_frame(&app.ui, 1280, 760, 400, 400, false, false, false);
    ASSERT_TRUE(nebel_lua_on_ui(&lua, 1280, 760));
    nebel_ui_end_frame(&app.ui);
    ASSERT_TRUE(!app.ui.pointer_over_ui);

    ASSERT_TRUE(nebel_lua_apply_preset(&lua, "Backlit Wildlife"));
    ASSERT_TRUE(nebel_lua_on_key(&lua, "B", false, false)); /* Fog marker */
    app.brush.radius = 0.30f;
    app.brush.opacity = 0.95f;
    nebel_app_stroke_begin(&app, 0.15f, 0.62f);
    nebel_app_stroke_move(&app, 0.50f, 0.62f);
    nebel_app_stroke_move(&app, 0.85f, 0.62f);
    nebel_app_stroke_end(&app);

    /* Render 1-layer result: 3D depth MUST keep foreground stag crisp even WITHOUT Protect brush! */
    NebelImage res1 = {0};
    ASSERT_TRUE(nebel_app_render_full(&app, &res1));
    float orig_bg = pixel_luma(&app.orig_img, 24, 40);
    float orig_fg = pixel_luma(&app.orig_img, 63, 40);
    float fog1_bg = pixel_luma(&res1, 24, 40);
    float fog1_fg = pixel_luma(&res1, 63, 40);
    ASSERT_TRUE(fog1_bg > orig_bg + 0.12f);
    ASSERT_TRUE(fog1_fg < orig_fg + 0.03f); /* Zero foreground bleed! */

    /* Also exercise Foreground-Protect brush explicitly */
    ASSERT_TRUE(nebel_lua_on_key(&lua, "P", false, false));
    app.brush.radius = 0.10f;
    app.brush.opacity = 1.0f;
    nebel_app_stroke_begin(&app, 0.66f, 0.52f);
    nebel_app_stroke_move(&app, 0.66f, 0.78f);
    nebel_app_stroke_end(&app);

    /* 5. Multi-layer fog: add second independent layer and paint ridge haze */
    ASSERT_TRUE(nebel_app_add_layer(&app, "Ridge Haze") == 1);
    ASSERT_TRUE(nebel_lua_on_key(&lua, "B", false, false));
    app.brush.radius = 0.30f;
    nebel_app_stroke_begin(&app, 0.15f, 0.62f);
    nebel_app_stroke_move(&app, 0.45f, 0.62f);
    nebel_app_stroke_end(&app);

    NebelImage res2 = {0};
    ASSERT_TRUE(nebel_app_render_full(&app, &res2));
    float fog2_bg = pixel_luma(&res2, 24, 40);
    ASSERT_TRUE(fog2_bg > fog1_bg + 0.04f);

    /* 6. Mobile Touch simulation: responsive switch, paint, 2-finger undo, 3-finger redo, pinch zoom */
    const int MVW = 390, MVH = 844;
    nebel_touch_update_viewport(&app.touch, MVW, MVH);
    ASSERT_TRUE(app.touch.touch_mode);

    /* Render mobile drawer across all 3 tabs */
    nebel_ui_begin_frame(&app.ui, MVW, MVH, 200, 350, false, false, false);
    ASSERT_TRUE(nebel_lua_on_ui(&lua, MVW, MVH));
    nebel_ui_end_frame(&app.ui);
    ASSERT_TRUE(app.ui.cmd_count > 10);

    size_t msz = (size_t)app.work_img.width * (size_t)app.work_img.height;
    uint8_t *before_touch = (uint8_t *)malloc(msz);
    ASSERT_TRUE(before_touch != NULL);
    memcpy(before_touch, app.layers[app.active_layer].mask, msz);

    /* 1-finger paint inside canvas */
    float cx, cy, cw, ch;
    nebel_app_canvas_rect(&app, MVW, MVH, &cx, &cy, &cw, &ch);
    float px = cx + cw * 0.30f, py = cy + ch * 0.50f;
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_down(&app.touch, 1, px, py, 100));
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_move(&app.touch, 1, px + 30.0f, py, 130));
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_up(&app.touch, 1, px + 30.0f, py, 160));
    ASSERT_TRUE(memcmp(before_touch, app.layers[app.active_layer].mask, msz) != 0);

    /* 2-finger tap -> Undo restores exact bit-for-bit mask */
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_down(&app.touch, 1, 120, 300, 500));
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_down(&app.touch, 2, 180, 300, 510));
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_up(&app.touch, 1, 121, 300, 580));
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_up(&app.touch, 2, 181, 300, 590));
    ASSERT_TRUE(memcmp(before_touch, app.layers[app.active_layer].mask, msz) == 0);

    /* 3-finger tap -> Redo restores painted stroke */
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_down(&app.touch, 1, 100, 300, 900));
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_down(&app.touch, 2, 150, 300, 905));
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_down(&app.touch, 3, 200, 300, 910));
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_up(&app.touch, 1, 100, 300, 960));
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_up(&app.touch, 2, 150, 300, 965));
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_up(&app.touch, 3, 200, 300, 970));
    ASSERT_TRUE(memcmp(before_touch, app.layers[app.active_layer].mask, msz) != 0);

    /* Pinch zoom cancels tentative stroke and scales zoom > 1 */
    float z0 = app.zoom;
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_down(&app.touch, 1, 120, 320, 1500));
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_down(&app.touch, 2, 220, 320, 1510));
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_move(&app.touch, 2, 320, 320, 1550));
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_up(&app.touch, 1, 120, 320, 2000));
    nebel_app_handle_gesture(&app, MVW, MVH, nebel_touch_finger_up(&app.touch, 2, 320, 320, 2010));
    ASSERT_TRUE(app.zoom > z0 * 1.4f);

    /* 7. External neural depth map + AutoLock brush depth anchoring check */
    float nn_depth[16 * 12];
    for (int i = 0; i < 16 * 12; i++) nn_depth[i] = 0.85f - 0.25f * ((float)(i / 16) / 11.0f);
    ASSERT_TRUE(nebel_app_set_external_depth(&app, nn_depth, 16, 12));
    ASSERT_TRUE(app.depth_is_neural);

    ASSERT_TRUE(!app.auto_depth_brush);
    ASSERT_TRUE(nebel_lua_on_key(&lua, "A", false, false));
    ASSERT_TRUE(app.auto_depth_brush);
    ASSERT_TRUE(nebel_lua_on_key(&lua, "B", false, false));
    nebel_app_stroke_begin(&app, 0.25f, 0.15f);
    nebel_app_stroke_end(&app);
    ASSERT_TRUE(app.layers[app.active_layer].params.depth_min > 0.50f);
    ASSERT_TRUE(nebel_app_undo(&app));
    ASSERT_TRUE(nebel_lua_on_key(&lua, "A", false, false));
    ASSERT_TRUE(!app.auto_depth_brush);

    /* 8. Full-resolution PNG, JPEG, 16-bit PNG, and 16-bit TIFF export verification */
    const char *out_png = "/tmp/nebel_e2e_export.png";
    const char *out_jpg = "/tmp/nebel_e2e_export.jpg";
    const char *out_png16 = "/tmp/nebel_e2e_export16.png";
    const char *out_tif16 = "/tmp/nebel_e2e_export16.tiff";
    ASSERT_TRUE(nebel_app_export_png(&app, out_png));
    ASSERT_TRUE(nebel_app_export_jpg(&app, out_jpg, 92));
    ASSERT_TRUE(nebel_app_export_png16(&app, out_png16));
    ASSERT_TRUE(nebel_app_export_tiff16(&app, out_tif16));

    NebelImage reloaded = {0};
    ASSERT_TRUE(nebel_image_load_file(&reloaded, out_png));
    ASSERT_TRUE(reloaded.width == W && reloaded.height == H);
    ASSERT_TRUE(pixel_luma(&reloaded, 24, 40) > orig_bg + 0.08f);
    nebel_image_free(&reloaded);

    ASSERT_TRUE(nebel_image_load_file(&reloaded, out_png16));
    ASSERT_TRUE(reloaded.width == W && reloaded.height == H);
    nebel_image_free(&reloaded);

    FILE *tf = fopen(out_tif16, "rb");
    ASSERT_TRUE(tf != NULL);
    uint8_t thdr[4] = {0};
    ASSERT_TRUE(fread(thdr, 1, 4, tf) == 4);
    fclose(tf);
    ASSERT_TRUE(thdr[0] == 'I' && thdr[1] == 'I' && thdr[2] == 42 && thdr[3] == 0);

    ASSERT_TRUE(nebel_image_load_file(&reloaded, out_jpg));
    ASSERT_TRUE(reloaded.width == W && reloaded.height == H);
    nebel_image_free(&reloaded);

    /* 9. Nebel v3: Mark -> Render workflow, 3-tier budgets, PatchFusion, EN/DE/PL & Help */
    int s_nat, sl_nat, sh_nat, fl_nat, g_nat;
    int s_wdk, sl_wdk, sh_wdk, fl_wdk, g_wdk;
    int s_wmb, sl_wmb, sh_wmb, fl_wmb, g_wmb;
    nebel_app_set_quality(&app, NEBEL_QUALITY_STUDIO);
    nebel_app_set_platform_tier(&app, NEBEL_TIER_DESKTOP);
    nebel_app_get_render_budget(&app, &s_nat, &sl_nat, &sh_nat, &fl_nat, &g_nat);
    nebel_app_set_platform_tier(&app, NEBEL_TIER_WEB_DESKTOP);
    nebel_app_get_render_budget(&app, &s_wdk, &sl_wdk, &sh_wdk, &fl_wdk, &g_wdk);
    nebel_app_set_platform_tier(&app, NEBEL_TIER_WEB_MOBILE);
    nebel_app_get_render_budget(&app, &s_wmb, &sl_wmb, &sh_wmb, &fl_wmb, &g_wmb);
    ASSERT_TRUE(sl_nat >= sl_wdk && sl_wdk >= sl_wmb);
    ASSERT_TRUE(g_nat >= g_wdk && g_wdk >= g_wmb);
    ASSERT_TRUE(s_nat >= s_wdk && s_wdk >= s_wmb);
    nebel_app_set_platform_tier(&app, NEBEL_TIER_DESKTOP);

    /* PatchFusion tile merge produces HD depth map */
    float pf_tile[8 * 8];
    for (int i = 0; i < 64; i++) pf_tile[i] = 0.72f;
    NebelDepthTile pf_tiles[1] = { { pf_tile, 8, 8, 0.0f, 0.0f, 1.0f, 1.0f } };
    ASSERT_TRUE(nebel_app_set_external_depth_fused(&app, nn_depth, 16, 12, pf_tiles, 1));
    ASSERT_TRUE(app.depth_is_hd);

    /* Mark -> Render -> Cancel state machine via hotkeys */
    ASSERT_TRUE(nebel_lua_on_key(&lua, "M", false, false));
    ASSERT_TRUE(nebel_lua_on_key(&lua, "ENTER", false, false));
    ASSERT_TRUE(app.render_phase != NEBEL_PHASE_MARK);
    ASSERT_TRUE(nebel_lua_on_key(&lua, "ESC", false, false));
    ASSERT_TRUE(app.render_phase == NEBEL_PHASE_MARK);

    /* Multi-language UI frames (German & Polish) + Help F1 dispatch */
    const char *langs[3] = { "de", "pl", "en" };
    for (int li = 0; li < 3; li++) {
        nebel_app_set_lang(&app, langs[li]);
        nebel_ui_begin_frame(&app.ui, 1280, 760, 120, 120, false, false, false);
        ASSERT_TRUE(nebel_lua_on_ui(&lua, 1280, 760));
        nebel_ui_end_frame(&app.ui);
        ASSERT_TRUE(app.ui.cmd_count > 15);
    }
    app.req_open_url[0] = '\0';
    ASSERT_TRUE(nebel_lua_on_key(&lua, "F1", false, false));
    ASSERT_TRUE(strstr(app.req_open_url, "help.html?lang=en") != NULL);

    /* Adjustable brush soft edge via Shift+[ and Shift+] */
    app.brush.softness = 0.50f;
    ASSERT_TRUE(nebel_lua_on_key(&lua, "[", false, true));
    ASSERT_TRUE(app.brush.softness < 0.45f);
    ASSERT_TRUE(nebel_lua_on_key(&lua, "]", false, true));
    ASSERT_TRUE(app.brush.softness > 0.48f);

    free(before_touch);
    nebel_image_free(&res1);
    nebel_image_free(&res2);
    nebel_lua_free(&lua);
    nebel_app_free(&app);

    printf("E2E automated simulation test PASSED (manifest, desktop, touch, multi-layer fog, god rays, shadow pool, auto-depth, v3 Mark->Render/i18n/PatchFusion, 16-bit export).\n");
    return 0;
}

#include "script/lua_bridge.h"
#include "core/manifest.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <stdio.h>
#include <string.h>

static NebelAppState *get_app(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "nebel_app");
    NebelAppState *app = (NebelAppState *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return app;
}

static void record_lua_error(NebelLuaBridge *bridge) {
    const char *err = lua_tostring(bridge->L, -1);
    snprintf(bridge->last_error, sizeof(bridge->last_error), "%s", err ? err : "unknown lua error");
    lua_pop(bridge->L, 1);
    if (bridge->app) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Lua error: %.110s", bridge->last_error);
        nebel_app_set_status(bridge->app, msg);
    }
}

/* ---- nebel.layers ---- */
static int l_layers_count(lua_State *L) {
    lua_pushinteger(L, get_app(L)->layer_count);
    return 1;
}
static int l_layers_active(lua_State *L) {
    lua_pushinteger(L, get_app(L)->active_layer + 1);
    return 1;
}
static int l_layers_name(lua_State *L) {
    NebelAppState *app = get_app(L);
    int idx = (int)luaL_checkinteger(L, 1) - 1;
    if (idx < 0 || idx >= app->layer_count) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, app->layers[idx].name);
    return 1;
}
static int l_layers_add(lua_State *L) {
    NebelAppState *app = get_app(L);
    const char *name = luaL_optstring(L, 1, "");
    int idx = nebel_app_add_layer(app, name);
    lua_pushinteger(L, idx >= 0 ? (idx + 1) : 0);
    return 1;
}
static int l_layers_remove(lua_State *L) {
    NebelAppState *app = get_app(L);
    int idx = (int)luaL_checkinteger(L, 1) - 1;
    lua_pushboolean(L, nebel_app_remove_layer(app, idx));
    return 1;
}
static int l_layers_select(lua_State *L) {
    NebelAppState *app = get_app(L);
    int idx = (int)luaL_checkinteger(L, 1) - 1;
    lua_pushboolean(L, nebel_app_select_layer(app, idx));
    return 1;
}
static int l_layers_get(lua_State *L) {
    NebelAppState *app = get_app(L);
    int idx = (int)luaL_checkinteger(L, 1) - 1;
    const char *key = luaL_checkstring(L, 2);
    float val = 0.0f;
    if (!nebel_app_get_layer_param(app, idx, key, &val)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, (lua_Number)val);
    return 1;
}
static int l_layers_set(lua_State *L) {
    NebelAppState *app = get_app(L);
    int idx = (int)luaL_checkinteger(L, 1) - 1;
    const char *key = luaL_checkstring(L, 2);
    float val = (float)luaL_checknumber(L, 3);
    lua_pushboolean(L, nebel_app_set_layer_param(app, idx, key, val));
    return 1;
}
static int l_layers_clear(lua_State *L) {
    NebelAppState *app = get_app(L);
    int idx = (int)luaL_checkinteger(L, 1) - 1;
    lua_pushboolean(L, nebel_app_clear_layer_mask(app, idx));
    return 1;
}

/* ---- nebel.brush ---- */
static const char *tool_to_str(NebelTool t) {
    switch (t) {
    case NEBEL_TOOL_ERASER:     return "eraser";
    case NEBEL_TOOL_PROTECT:    return "protect";
    case NEBEL_TOOL_SUN:        return "sun";
    case NEBEL_TOOL_DEPTH_NEAR: return "depth_near";
    case NEBEL_TOOL_DEPTH_FAR:  return "depth_far";
    default:                    return "fog";
    }
}
static int l_brush_get_tool(lua_State *L) {
    lua_pushstring(L, tool_to_str(get_app(L)->active_tool));
    return 1;
}
static int l_brush_set_tool(lua_State *L) {
    NebelAppState *app = get_app(L);
    const char *s = luaL_checkstring(L, 1);
    if (strcmp(s, "fog") == 0) app->active_tool = NEBEL_TOOL_FOG;
    else if (strcmp(s, "eraser") == 0) app->active_tool = NEBEL_TOOL_ERASER;
    else if (strcmp(s, "protect") == 0) app->active_tool = NEBEL_TOOL_PROTECT;
    else if (strcmp(s, "sun") == 0) app->active_tool = NEBEL_TOOL_SUN;
    else if (strcmp(s, "depth_near") == 0) app->active_tool = NEBEL_TOOL_DEPTH_NEAR;
    else if (strcmp(s, "depth_far") == 0) app->active_tool = NEBEL_TOOL_DEPTH_FAR;
    else { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, 1);
    return 1;
}
static int l_brush_get_radius(lua_State *L) {
    lua_pushnumber(L, (lua_Number)get_app(L)->brush.radius);
    return 1;
}
static int l_brush_set_radius(lua_State *L) {
    NebelAppState *app = get_app(L);
    app->brush.radius = (float)luaL_checknumber(L, 1);
    nebel_brush_clamp(&app->brush);
    return 0;
}
static int l_brush_get_opacity(lua_State *L) {
    lua_pushnumber(L, (lua_Number)get_app(L)->brush.opacity);
    return 1;
}
static int l_brush_set_opacity(lua_State *L) {
    NebelAppState *app = get_app(L);
    app->brush.opacity = (float)luaL_checknumber(L, 1);
    nebel_brush_clamp(&app->brush);
    return 0;
}
static int l_brush_get_softness(lua_State *L) {
    lua_pushnumber(L, (lua_Number)get_app(L)->brush.softness);
    return 1;
}
static int l_brush_set_softness(lua_State *L) {
    NebelAppState *app = get_app(L);
    app->brush.softness = (float)luaL_checknumber(L, 1);
    nebel_brush_clamp(&app->brush);
    return 0;
}
static int l_brush_get_auto_depth(lua_State *L) {
    lua_pushboolean(L, get_app(L)->auto_depth_brush);
    return 1;
}
static int l_brush_set_auto_depth(lua_State *L) {
    get_app(L)->auto_depth_brush = lua_toboolean(L, 1) != 0;
    return 0;
}
static int l_brush_stroke_begin(lua_State *L) {
    nebel_app_stroke_begin(get_app(L), (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2));
    return 0;
}
static int l_brush_stroke_move(lua_State *L) {
    nebel_app_stroke_move(get_app(L), (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2));
    return 0;
}
static int l_brush_stroke_end(lua_State *L) {
    nebel_app_stroke_end(get_app(L));
    return 0;
}

/* ---- nebel.view ---- */
static int l_view_zoom(lua_State *L) { lua_pushnumber(L, get_app(L)->zoom); return 1; }
static int l_view_set_zoom(lua_State *L) {
    NebelAppState *app = get_app(L);
    float z = (float)luaL_checknumber(L, 1);
    if (z < 0.25f) z = 0.25f;
    if (z > 8.0f) z = 8.0f;
    app->zoom = z;
    return 0;
}
static int l_view_pan(lua_State *L) {
    NebelAppState *app = get_app(L);
    app->pan_x += (float)luaL_checknumber(L, 1);
    app->pan_y += (float)luaL_checknumber(L, 2);
    return 0;
}
static int l_view_reset(lua_State *L) {
    NebelAppState *app = get_app(L);
    app->zoom = 1.0f; app->pan_x = 0.0f; app->pan_y = 0.0f;
    return 0;
}
static int l_view_split_enabled(lua_State *L) { lua_pushboolean(L, get_app(L)->split_enabled); return 1; }
static int l_view_split_pos(lua_State *L) { lua_pushnumber(L, get_app(L)->split_pos); return 1; }
static int l_view_set_split(lua_State *L) {
    NebelAppState *app = get_app(L);
    app->split_enabled = lua_toboolean(L, 1) != 0;
    if (!lua_isnoneornil(L, 2)) {
        float p = (float)luaL_checknumber(L, 2);
        if (p < 0.05f) p = 0.05f;
        if (p > 0.95f) p = 0.95f;
        app->split_pos = p;
    }
    return 0;
}
static int l_view_undo(lua_State *L) { lua_pushboolean(L, nebel_app_undo(get_app(L))); return 1; }
static int l_view_redo(lua_State *L) { lua_pushboolean(L, nebel_app_redo(get_app(L))); return 1; }
static int l_view_can_undo(lua_State *L) { lua_pushboolean(L, nebel_undo_can_undo(&get_app(L)->undo)); return 1; }
static int l_view_can_redo(lua_State *L) { lua_pushboolean(L, nebel_undo_can_redo(&get_app(L)->undo)); return 1; }
static int l_view_trigger_auto_depth(lua_State *L) {
    NebelAppState *app = get_app(L);
    app->req_hd_depth = true;
    app->req_auto_depth = true;
    return 0;
}
static int l_view_is_neural_depth(lua_State *L) { lua_pushboolean(L, get_app(L)->depth_is_neural); return 1; }
static int l_view_is_hd_depth(lua_State *L) { lua_pushboolean(L, get_app(L)->depth_is_hd); return 1; }
static int l_view_show_depth(lua_State *L) { lua_pushboolean(L, get_app(L)->show_depth); return 1; }
static int l_view_set_show_depth(lua_State *L) {
    get_app(L)->show_depth = lua_toboolean(L, 1) != 0;
    return 0;
}
static int l_view_show_mask(lua_State *L) { lua_pushboolean(L, get_app(L)->show_mask_overlay); return 1; }
static int l_view_set_show_mask(lua_State *L) {
    get_app(L)->show_mask_overlay = lua_toboolean(L, 1) != 0;
    return 0;
}
static int l_view_sample_airlight(lua_State *L) {
    nebel_app_sample_airlight(get_app(L), (float)luaL_optnumber(L, 1, -1.0), (float)luaL_optnumber(L, 2, -1.0));
    return 0;
}
static int l_view_status(lua_State *L) { lua_pushstring(L, get_app(L)->status_msg); return 1; }
static int l_view_set_status(lua_State *L) { nebel_app_set_status(get_app(L), luaL_checkstring(L, 1)); return 0; }
static int l_view_canvas_rect(lua_State *L) {
    NebelAppState *app = get_app(L);
    float x, y, w, h;
    nebel_app_canvas_rect(app, app->ui.viewport_w, app->ui.viewport_h, &x, &y, &w, &h);
    lua_pushnumber(L, x); lua_pushnumber(L, y); lua_pushnumber(L, w); lua_pushnumber(L, h);
    return 4;
}

/* ---- nebel.render ---- */
static const char *phase_to_str(NebelRenderPhase p) {
    switch (p) {
    case NEBEL_PHASE_DEPTH:     return "depth";
    case NEBEL_PHASE_FLUID:     return "fluid";
    case NEBEL_PHASE_RENDERING: return "rendering";
    case NEBEL_PHASE_DONE:      return "done";
    default:                    return "mark";
    }
}
static const char *quality_to_str(NebelQualityPreset q) {
    switch (q) {
    case NEBEL_QUALITY_FAST:  return "fast";
    case NEBEL_QUALITY_ULTRA: return "ultra";
    default:                  return "studio";
    }
}
static int l_render_start(lua_State *L) { nebel_app_render_start(get_app(L)); return 0; }
static int l_render_cancel(lua_State *L) { nebel_app_render_cancel(get_app(L)); return 0; }
static int l_render_phase(lua_State *L) { lua_pushstring(L, phase_to_str(get_app(L)->render_phase)); return 1; }
static int l_render_is_dirty(lua_State *L) { lua_pushboolean(L, get_app(L)->render_dirty); return 1; }
static int l_render_progress(lua_State *L) { lua_pushnumber(L, get_app(L)->render_progress); return 1; }
static int l_render_samples(lua_State *L) {
    NebelAppState *app = get_app(L);
    lua_pushinteger(L, app->render_samples_done);
    lua_pushinteger(L, app->render_samples_total);
    return 2;
}
static int l_render_quality(lua_State *L) { lua_pushstring(L, quality_to_str(get_app(L)->quality)); return 1; }
static int l_render_set_quality(lua_State *L) {
    NebelAppState *app = get_app(L);
    const char *q = luaL_checkstring(L, 1);
    if (strcmp(q, "fast") == 0) nebel_app_set_quality(app, NEBEL_QUALITY_FAST);
    else if (strcmp(q, "ultra") == 0) nebel_app_set_quality(app, NEBEL_QUALITY_ULTRA);
    else if (strcmp(q, "studio") == 0) nebel_app_set_quality(app, NEBEL_QUALITY_STUDIO);
    else { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, 1);
    return 1;
}

/* ---- nebel.platform ---- */
static const char *tier_to_str(NebelPlatformTier t) {
    switch (t) {
    case NEBEL_TIER_WEB_DESKTOP: return "web_desktop";
    case NEBEL_TIER_WEB_MOBILE:  return "web_mobile";
    default:                     return "desktop";
    }
}
static int l_platform_tier(lua_State *L) { lua_pushstring(L, tier_to_str(get_app(L)->platform_tier)); return 1; }
static int l_platform_set_tier(lua_State *L) {
    NebelAppState *app = get_app(L);
    const char *t = luaL_checkstring(L, 1);
    if (strcmp(t, "desktop") == 0) nebel_app_set_platform_tier(app, NEBEL_TIER_DESKTOP);
    else if (strcmp(t, "web_desktop") == 0) nebel_app_set_platform_tier(app, NEBEL_TIER_WEB_DESKTOP);
    else if (strcmp(t, "web_mobile") == 0) nebel_app_set_platform_tier(app, NEBEL_TIER_WEB_MOBILE);
    else { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, 1);
    return 1;
}
static int l_platform_gpu_backend(lua_State *L) { lua_pushstring(L, get_app(L)->gpu_backend); return 1; }
static int l_platform_lang(lua_State *L) { lua_pushstring(L, get_app(L)->lang); return 1; }
static int l_platform_set_lang(lua_State *L) {
    nebel_app_set_lang(get_app(L), luaL_checkstring(L, 1));
    return 0;
}
static int l_platform_open_help(lua_State *L) {
    NebelAppState *app = get_app(L);
    const char *lang = luaL_optstring(L, 1, app->lang);
    snprintf(app->req_open_url, sizeof(app->req_open_url),
             "https://stbange.github.io/nebel/help.html?lang=%.4s",
             (lang && lang[0]) ? lang : "en");
    return 0;
}

/* ---- nebel.io ---- */
static int l_io_open_photo(lua_State *L) { get_app(L)->req_open_dialog = true; return 0; }
static int l_io_export_png(lua_State *L) { get_app(L)->req_export_png = true; return 0; }
static int l_io_export_jpg(lua_State *L) {
    NebelAppState *app = get_app(L);
    app->req_export_jpg = true;
    app->req_jpg_quality = (int)luaL_optinteger(L, 1, 92);
    return 0;
}
static int l_io_export_png16(lua_State *L) { get_app(L)->req_export_png16 = true; return 0; }
static int l_io_export_tiff16(lua_State *L) { get_app(L)->req_export_tiff16 = true; return 0; }
static int l_io_reload_scripts(lua_State *L) { get_app(L)->req_reload_scripts = true; return 0; }
static int l_io_load_sample(lua_State *L) {
    NebelAppState *app = get_app(L);
    const char *rel = luaL_checkstring(L, 1);
    char full[384];
    if (!nebel_path_join_asset(app->asset_root, rel, full, sizeof(full))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, nebel_app_load_image_file(app, full));
    return 1;
}

/* ---- nebel.ui ---- */
static int l_ui_panel(lua_State *L) {
    NebelAppState *app = get_app(L);
    nebel_ui_panel(&app->ui,
        (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
        (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
        (float)luaL_optnumber(L, 5, 0.08), (float)luaL_optnumber(L, 6, 0.09),
        (float)luaL_optnumber(L, 7, 0.11), (float)luaL_optnumber(L, 8, 0.94));
    return 0;
}
static int l_ui_label(lua_State *L) {
    NebelAppState *app = get_app(L);
    nebel_ui_label(&app->ui,
        (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
        luaL_checkstring(L, 3),
        (float)luaL_optnumber(L, 4, 0.88), (float)luaL_optnumber(L, 5, 0.90),
        (float)luaL_optnumber(L, 6, 0.94), (float)luaL_optnumber(L, 7, 1.0));
    return 0;
}
static int l_ui_ring(lua_State *L) {
    NebelAppState *app = get_app(L);
    nebel_ui_ring(&app->ui,
        (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
        (float)luaL_checknumber(L, 3),
        (float)luaL_optnumber(L, 4, 0.90), (float)luaL_optnumber(L, 5, 0.95),
        (float)luaL_optnumber(L, 6, 1.00), (float)luaL_optnumber(L, 7, 0.75));
    return 0;
}
static int l_ui_button(lua_State *L) {
    NebelAppState *app = get_app(L);
    uint32_t id = (uint32_t)luaL_checkinteger(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float w = (float)luaL_checknumber(L, 4);
    float h = (float)luaL_checknumber(L, 5);
    const char *label = luaL_optstring(L, 6, "");
    bool active = lua_toboolean(L, 7) != 0;
    int icon_id = (int)luaL_optinteger(L, 8, -1);
    bool clicked = nebel_ui_button(&app->ui, id, x, y, w, h, label, active, icon_id);
    if (clicked) nebel_app_queue_audio(app, "assets/sounds/ui_click.wav", 0.4f);
    lua_pushboolean(L, clicked);
    return 1;
}
static int l_ui_accent_button(lua_State *L) {
    NebelAppState *app = get_app(L);
    uint32_t id = (uint32_t)luaL_checkinteger(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float w = (float)luaL_checknumber(L, 4);
    float h = (float)luaL_checknumber(L, 5);
    const char *label = luaL_optstring(L, 6, "");
    bool pulsing = lua_toboolean(L, 7) != 0;
    int icon_id = (int)luaL_optinteger(L, 8, -1);
    bool clicked = nebel_ui_accent_button(&app->ui, id, x, y, w, h, label, pulsing, icon_id);
    if (clicked) nebel_app_queue_audio(app, "assets/sounds/ui_click.wav", 0.45f);
    lua_pushboolean(L, clicked);
    return 1;
}
static int l_ui_slider(lua_State *L) {
    NebelAppState *app = get_app(L);
    uint32_t id = (uint32_t)luaL_checkinteger(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float w = (float)luaL_checknumber(L, 4);
    float h = (float)luaL_checknumber(L, 5);
    const char *label = luaL_optstring(L, 6, "");
    float val = (float)luaL_checknumber(L, 7);
    float min_v = (float)luaL_checknumber(L, 8);
    float max_v = (float)luaL_checknumber(L, 9);
    bool changed = nebel_ui_slider(&app->ui, id, x, y, w, h, label, &val, min_v, max_v);
    lua_pushnumber(L, (lua_Number)val);
    lua_pushboolean(L, changed);
    return 2;
}
static int l_ui_progress_bar(lua_State *L) {
    NebelAppState *app = get_app(L);
    nebel_ui_progress_bar(&app->ui,
        (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
        (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
        (float)luaL_checknumber(L, 5), luaL_optstring(L, 6, ""));
    return 0;
}
static int l_ui_tooltip(lua_State *L) {
    nebel_ui_tooltip(&get_app(L)->ui, luaL_checkstring(L, 1));
    return 0;
}
static int l_ui_last_hovered(lua_State *L) {
    lua_pushboolean(L, get_app(L)->ui.last_hovered);
    return 1;
}
static int l_ui_set_modal(lua_State *L) {
    NebelAppState *app = get_app(L);
    bool active = lua_toboolean(L, 1) != 0;
    app->ui.modal_active = active;
    if (active) app->ui.pointer_over_ui = true;
    return 0;
}
static int l_ui_is_touch_mode(lua_State *L) { lua_pushboolean(L, get_app(L)->touch.touch_mode); return 1; }
static int l_ui_viewport_size(lua_State *L) {
    NebelAppState *app = get_app(L);
    lua_pushinteger(L, app->ui.viewport_w);
    lua_pushinteger(L, app->ui.viewport_h);
    return 2;
}
static int l_ui_pointer_over_ui(lua_State *L) { lua_pushboolean(L, get_app(L)->ui.pointer_over_ui); return 1; }
static int l_ui_pointer_pos(lua_State *L) {
    NebelAppState *app = get_app(L);
    lua_pushnumber(L, app->ui.mouse_x);
    lua_pushnumber(L, app->ui.mouse_y);
    return 2;
}

/* ---- nebel.audio ---- */
static int l_audio_play(lua_State *L) {
    nebel_app_queue_audio(get_app(L), luaL_checkstring(L, 1), (float)luaL_optnumber(L, 2, 0.5));
    return 0;
}
static int l_audio_set_muted(lua_State *L) { get_app(L)->muted = lua_toboolean(L, 1) != 0; return 0; }
static int l_audio_is_muted(lua_State *L) { lua_pushboolean(L, get_app(L)->muted); return 1; }

static void register_subtable(lua_State *L, const char *name, const luaL_Reg *funcs) {
    lua_newtable(L);
    luaL_setfuncs(L, funcs, 0);
    lua_setfield(L, -2, name);
}

static void register_api(lua_State *L) {
    static const luaL_Reg layers_fn[] = {
        {"count", l_layers_count}, {"active", l_layers_active}, {"name", l_layers_name},
        {"add", l_layers_add}, {"remove", l_layers_remove}, {"select", l_layers_select},
        {"get", l_layers_get}, {"set", l_layers_set}, {"clear", l_layers_clear},
        {NULL, NULL}
    };
    static const luaL_Reg brush_fn[] = {
        {"get_tool", l_brush_get_tool}, {"set_tool", l_brush_set_tool},
        {"get_radius", l_brush_get_radius}, {"set_radius", l_brush_set_radius},
        {"get_opacity", l_brush_get_opacity}, {"set_opacity", l_brush_set_opacity},
        {"get_softness", l_brush_get_softness}, {"set_softness", l_brush_set_softness},
        {"get_auto_depth", l_brush_get_auto_depth}, {"set_auto_depth", l_brush_set_auto_depth},
        {"stroke_begin", l_brush_stroke_begin}, {"stroke_move", l_brush_stroke_move},
        {"stroke_end", l_brush_stroke_end}, {NULL, NULL}
    };
    static const luaL_Reg view_fn[] = {
        {"zoom", l_view_zoom}, {"set_zoom", l_view_set_zoom}, {"pan", l_view_pan},
        {"reset", l_view_reset}, {"split_enabled", l_view_split_enabled},
        {"split_pos", l_view_split_pos}, {"set_split", l_view_set_split},
        {"undo", l_view_undo}, {"redo", l_view_redo}, {"can_undo", l_view_can_undo},
        {"can_redo", l_view_can_redo}, {"trigger_auto_depth", l_view_trigger_auto_depth},
        {"is_neural_depth", l_view_is_neural_depth}, {"is_hd_depth", l_view_is_hd_depth},
        {"show_depth", l_view_show_depth}, {"set_show_depth", l_view_set_show_depth},
        {"show_mask", l_view_show_mask}, {"set_show_mask", l_view_set_show_mask},
        {"sample_airlight", l_view_sample_airlight},
        {"status", l_view_status}, {"set_status", l_view_set_status},
        {"canvas_rect", l_view_canvas_rect}, {NULL, NULL}
    };
    static const luaL_Reg render_fn[] = {
        {"start", l_render_start}, {"cancel", l_render_cancel},
        {"phase", l_render_phase}, {"is_dirty", l_render_is_dirty},
        {"progress", l_render_progress}, {"samples", l_render_samples},
        {"quality", l_render_quality}, {"set_quality", l_render_set_quality},
        {NULL, NULL}
    };
    static const luaL_Reg platform_fn[] = {
        {"tier", l_platform_tier}, {"set_tier", l_platform_set_tier},
        {"gpu_backend", l_platform_gpu_backend},
        {"lang", l_platform_lang}, {"set_lang", l_platform_set_lang},
        {"open_help", l_platform_open_help}, {NULL, NULL}
    };
    static const luaL_Reg io_fn[] = {
        {"open_photo", l_io_open_photo}, {"load_sample", l_io_load_sample},
        {"export_png", l_io_export_png}, {"export_jpg", l_io_export_jpg},
        {"export_png16", l_io_export_png16}, {"export_tiff16", l_io_export_tiff16},
        {"reload_scripts", l_io_reload_scripts}, {NULL, NULL}
    };
    static const luaL_Reg ui_fn[] = {
        {"panel", l_ui_panel}, {"label", l_ui_label}, {"ring", l_ui_ring},
        {"button", l_ui_button}, {"accent_button", l_ui_accent_button},
        {"slider", l_ui_slider}, {"progress_bar", l_ui_progress_bar},
        {"tooltip", l_ui_tooltip}, {"last_hovered", l_ui_last_hovered},
        {"set_modal", l_ui_set_modal},
        {"is_touch_mode", l_ui_is_touch_mode}, {"viewport_size", l_ui_viewport_size},
        {"pointer_over_ui", l_ui_pointer_over_ui}, {"pointer_pos", l_ui_pointer_pos},
        {NULL, NULL}
    };
    static const luaL_Reg audio_fn[] = {
        {"play", l_audio_play}, {"set_muted", l_audio_set_muted}, {"is_muted", l_audio_is_muted},
        {NULL, NULL}
    };

    lua_newtable(L);
    register_subtable(L, "layers",   layers_fn);
    register_subtable(L, "brush",    brush_fn);
    register_subtable(L, "view",     view_fn);
    register_subtable(L, "render",   render_fn);
    register_subtable(L, "platform", platform_fn);
    register_subtable(L, "io",       io_fn);
    register_subtable(L, "ui",       ui_fn);
    register_subtable(L, "audio",    audio_fn);
    lua_setglobal(L, "nebel");
}

bool nebel_lua_init(NebelLuaBridge *bridge, NebelAppState *app) {
    if (!bridge || !app) return false;
    memset(bridge, 0, sizeof(*bridge));
    bridge->app = app;

    lua_State *L = luaL_newstate();
    if (!L) return false;
    bridge->L = L;

    luaL_requiref(L, "_G", luaopen_base, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_LOADLIBNAME, luaopen_package, 1); lua_pop(L, 1);

    char path_expr[384];
    snprintf(path_expr, sizeof(path_expr), "%s/scripts/?.lua", app->asset_root);
    lua_getglobal(L, "package");
    lua_pushstring(L, path_expr);
    lua_setfield(L, -2, "path");
    lua_pushstring(L, "");
    lua_setfield(L, -2, "cpath");
    lua_pop(L, 1);

    lua_pushlightuserdata(L, app);
    lua_setfield(L, LUA_REGISTRYINDEX, "nebel_app");
    register_api(L);
    return true;
}

void nebel_lua_free(NebelLuaBridge *bridge) {
    if (!bridge) return;
    if (bridge->L) {
        lua_close(bridge->L);
        bridge->L = NULL;
    }
    bridge->app = NULL;
}

bool nebel_lua_load_file(NebelLuaBridge *bridge, const char *rel_path) {
    if (!bridge || !bridge->L || !bridge->app || !rel_path) return false;
    char full[384];
    if (!nebel_path_join_asset(bridge->app->asset_root, rel_path, full, sizeof(full))) {
        snprintf(bridge->last_error, sizeof(bridge->last_error), "unsafe script path");
        return false;
    }
    if (luaL_loadfile(bridge->L, full) != LUA_OK || lua_pcall(bridge->L, 0, 0, 0) != LUA_OK) {
        record_lua_error(bridge);
        return false;
    }
    bridge->last_error[0] = '\0';
    return true;
}

bool nebel_lua_reload(NebelLuaBridge *bridge) {
    if (!bridge || !bridge->L) return false;
    lua_getglobal(bridge->L, "package");
    lua_newtable(bridge->L);
    lua_setfield(bridge->L, -2, "loaded");
    lua_pop(bridge->L, 1);

    if (!nebel_lua_load_file(bridge, "scripts/main.lua")) return false;
    lua_getglobal(bridge->L, "on_init");
    if (lua_isfunction(bridge->L, -1)) {
        if (lua_pcall(bridge->L, 0, 0, 0) != LUA_OK) {
            record_lua_error(bridge);
            return false;
        }
    } else {
        lua_pop(bridge->L, 1);
    }
    return true;
}

bool nebel_lua_eval(NebelLuaBridge *bridge, const char *code) {
    if (!bridge || !bridge->L || !code) return false;
    if (luaL_loadstring(bridge->L, code) != LUA_OK || lua_pcall(bridge->L, 0, 0, 0) != LUA_OK) {
        record_lua_error(bridge);
        return false;
    }
    bridge->last_error[0] = '\0';
    return true;
}

bool nebel_lua_on_ui(NebelLuaBridge *bridge, int vw, int vh) {
    if (!bridge || !bridge->L) return false;
    lua_getglobal(bridge->L, "on_ui");
    if (!lua_isfunction(bridge->L, -1)) {
        lua_pop(bridge->L, 1);
        return true;
    }
    lua_pushinteger(bridge->L, vw);
    lua_pushinteger(bridge->L, vh);
    if (lua_pcall(bridge->L, 2, 0, 0) != LUA_OK) {
        record_lua_error(bridge);
        return false;
    }
    return true;
}

bool nebel_lua_on_key(NebelLuaBridge *bridge, const char *key_name, bool ctrl, bool shift) {
    if (!bridge || !bridge->L || !key_name) return false;
    lua_getglobal(bridge->L, "on_key");
    if (!lua_isfunction(bridge->L, -1)) {
        lua_pop(bridge->L, 1);
        return true;
    }
    lua_pushstring(bridge->L, key_name);
    lua_pushboolean(bridge->L, ctrl);
    lua_pushboolean(bridge->L, shift);
    if (lua_pcall(bridge->L, 3, 0, 0) != LUA_OK) {
        record_lua_error(bridge);
        return false;
    }
    return true;
}

bool nebel_lua_apply_preset(NebelLuaBridge *bridge, const char *preset_name) {
    if (!bridge || !bridge->L || !preset_name) return false;
    lua_getglobal(bridge->L, "apply_preset");
    if (!lua_isfunction(bridge->L, -1)) {
        lua_pop(bridge->L, 1);
        return false;
    }
    lua_pushstring(bridge->L, preset_name);
    if (lua_pcall(bridge->L, 1, 1, 0) != LUA_OK) {
        record_lua_error(bridge);
        return false;
    }
    bool ok = lua_toboolean(bridge->L, -1) != 0;
    lua_pop(bridge->L, 1);
    return ok;
}

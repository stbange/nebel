#include "core/app_state.h"
#include "platform/audio.h"
#include "platform/depth_onnx.h"
#include "platform/file_dialog.h"
#include "render/renderer.h"
#include "script/lua_bridge.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

typedef struct {
    SDL_Window *window;
    SDL_GLContext gl_ctx;
    NebelAppState app;
    NebelLuaBridge lua;
    NebelRenderer renderer;
    NebelAudio audio;

    bool running;
    float mouse_x, mouse_y;
    bool mouse_down, mouse_pressed, mouse_released;
    bool pan_dragging;
    bool split_dragging;
} NebelRuntime;

static const char *map_key_name(SDL_Keycode key) {
    switch (key) {
    case SDLK_B: return "B";
    case SDLK_D: return "D";
    case SDLK_E: return "E";
    case SDLK_P: return "P";
    case SDLK_R: return "R";
    case SDLK_S: return "S";
    case SDLK_Z: return "Z";
    case SDLK_Y: return "Y";
    case SDLK_0: return "0";
    case SDLK_LEFTBRACKET:  return "[";
    case SDLK_RIGHTBRACKET: return "]";
    case SDLK_SPACE: return "SPACE";
    case SDLK_F5:    return "F5";
    default: return NULL;
    }
}

static void handle_events(NebelRuntime *rt, int vw, int vh) {
    rt->mouse_pressed = false;
    rt->mouse_released = false;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            rt->running = false;
            break;

        case SDL_EVENT_KEY_DOWN: {
            const char *k = map_key_name(ev.key.key);
            if (k) {
                bool ctrl = (ev.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
                bool shift = (ev.key.mod & SDL_KMOD_SHIFT) != 0;
                nebel_lua_on_key(&rt->lua, k, ctrl, shift);
            }
            break;
        }

        case SDL_EVENT_MOUSE_MOTION: {
            float dx = ev.motion.x - rt->mouse_x;
            float dy = ev.motion.y - rt->mouse_y;
            rt->mouse_x = ev.motion.x;
            rt->mouse_y = ev.motion.y;
            if (rt->pan_dragging) {
                rt->app.pan_x += dx;
                rt->app.pan_y += dy;
            } else if (rt->split_dragging) {
                float u = 0.5f, v = 0.5f;
                nebel_app_screen_to_photo(&rt->app, vw, vh, rt->mouse_x, rt->mouse_y, &u, &v);
                if (u < 0.05f) u = 0.05f;
                if (u > 0.95f) u = 0.95f;
                rt->app.split_pos = u;
            } else if (rt->app.stroke_in_progress && !rt->app.touch.touch_mode) {
                float u = 0.0f, v = 0.0f;
                nebel_app_screen_to_photo(&rt->app, vw, vh, rt->mouse_x, rt->mouse_y, &u, &v);
                nebel_app_stroke_move(&rt->app, u, v);
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            rt->mouse_x = ev.button.x;
            rt->mouse_y = ev.button.y;
            if (ev.button.button == SDL_BUTTON_MIDDLE || ev.button.button == SDL_BUTTON_RIGHT) {
                rt->pan_dragging = true;
            } else if (ev.button.button == SDL_BUTTON_LEFT) {
                rt->mouse_down = true;
                rt->mouse_pressed = true;
                /* Check split divider drag before starting a paint stroke */
                float u = 0.0f, v = 0.0f;
                if (!rt->app.ui.pointer_over_ui &&
                    nebel_app_screen_to_photo(&rt->app, vw, vh, rt->mouse_x, rt->mouse_y, &u, &v)) {
                    if (rt->app.split_enabled && fabsf(u - rt->app.split_pos) < 0.03f) {
                        rt->split_dragging = true;
                    } else if (!rt->app.touch.touch_mode) {
                        nebel_app_stroke_begin(&rt->app, u, v);
                    }
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            rt->mouse_x = ev.button.x;
            rt->mouse_y = ev.button.y;
            if (ev.button.button == SDL_BUTTON_MIDDLE || ev.button.button == SDL_BUTTON_RIGHT) {
                rt->pan_dragging = false;
            } else if (ev.button.button == SDL_BUTTON_LEFT) {
                rt->mouse_down = false;
                rt->mouse_released = true;
                rt->split_dragging = false;
                if (rt->app.stroke_in_progress && !rt->app.touch.touch_mode) {
                    nebel_app_stroke_end(&rt->app);
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL: {
            float factor = ev.wheel.y > 0 ? 1.12f : (ev.wheel.y < 0 ? 0.89f : 1.0f);
            float z = rt->app.zoom * factor;
            if (z < 0.25f) z = 0.25f;
            if (z > 8.0f) z = 8.0f;
            rt->app.zoom = z;
            break;
        }

        case SDL_EVENT_FINGER_DOWN: {
            float fx = ev.tfinger.x * (float)vw;
            float fy = ev.tfinger.y * (float)vh;
            rt->mouse_x = fx; rt->mouse_y = fy;
            rt->mouse_down = true; rt->mouse_pressed = true;
            float u = 0.0f, v = 0.0f;
            if (rt->app.split_enabled && !rt->app.ui.pointer_over_ui &&
                nebel_app_screen_to_photo(&rt->app, vw, vh, fx, fy, &u, &v) &&
                fabsf(u - rt->app.split_pos) < 0.055f) {
                rt->split_dragging = true;
                break;
            }
            NebelGestureAction act = nebel_touch_finger_down(
                &rt->app.touch, (int64_t)ev.tfinger.fingerID, fx, fy, SDL_GetTicks());
            nebel_app_handle_gesture(&rt->app, vw, vh, act);
            break;
        }

        case SDL_EVENT_FINGER_MOTION: {
            float fx = ev.tfinger.x * (float)vw;
            float fy = ev.tfinger.y * (float)vh;
            rt->mouse_x = fx; rt->mouse_y = fy;
            if (rt->split_dragging) {
                float u = 0.5f, v = 0.5f;
                nebel_app_screen_to_photo(&rt->app, vw, vh, fx, fy, &u, &v);
                if (u < 0.05f) u = 0.05f;
                if (u > 0.95f) u = 0.95f;
                rt->app.split_pos = u;
                break;
            }
            NebelGestureAction act = nebel_touch_finger_move(
                &rt->app.touch, (int64_t)ev.tfinger.fingerID, fx, fy, SDL_GetTicks());
            nebel_app_handle_gesture(&rt->app, vw, vh, act);
            break;
        }

        case SDL_EVENT_FINGER_UP: {
            float fx = ev.tfinger.x * (float)vw;
            float fy = ev.tfinger.y * (float)vh;
            rt->mouse_x = fx; rt->mouse_y = fy;
            rt->mouse_down = false; rt->mouse_released = true;
            if (rt->split_dragging) {
                rt->split_dragging = false;
                break;
            }
            NebelGestureAction act = nebel_touch_finger_up(
                &rt->app.touch, (int64_t)ev.tfinger.fingerID, fx, fy, SDL_GetTicks());
            nebel_app_handle_gesture(&rt->app, vw, vh, act);
            break;
        }

        case SDL_EVENT_DROP_FILE:
            if (ev.drop.data) {
                nebel_app_load_image_file(&rt->app, ev.drop.data);
            }
            break;

        default:
            break;
        }
    }
}

static void frame_tick(void *userdata) {
    NebelRuntime *rt = (NebelRuntime *)userdata;
    int vw = 1280, vh = 760;
    SDL_GetWindowSize(rt->window, &vw, &vh);
#if defined(__EMSCRIPTEN__)
    double css_w = 0.0, css_h = 0.0;
    if (emscripten_get_element_css_size("#canvas", &css_w, &css_h) == EMSCRIPTEN_RESULT_SUCCESS) {
        int iw = (int)css_w, ih = (int)css_h;
        if (iw > 64 && ih > 64 && (iw != vw || ih != vh)) {
            SDL_SetWindowSize(rt->window, iw, ih);
            vw = iw; vh = ih;
        }
    }
#endif
    nebel_touch_update_viewport(&rt->app.touch, vw, vh);

    handle_events(rt, vw, vh);

    /* Run Lua darkroom UI layout and collect immediate draw commands */
    nebel_ui_begin_frame(&rt->app.ui, vw, vh, rt->mouse_x, rt->mouse_y,
                         rt->mouse_down, rt->mouse_pressed, rt->mouse_released);
    nebel_lua_on_ui(&rt->lua, vw, vh);
    nebel_ui_end_frame(&rt->app.ui);

    /* If user clicked inside a UI widget on press, cancel any accidental canvas stroke */
    if (rt->app.ui.pointer_over_ui && rt->mouse_pressed && rt->app.stroke_in_progress) {
        nebel_app_stroke_cancel(&rt->app);
    }

    if (rt->app.req_reload_scripts) {
        rt->app.req_reload_scripts = false;
        bool ok_lua = nebel_lua_reload(&rt->lua);
        bool ok_gl = nebel_renderer_reload_shaders(&rt->renderer);
        nebel_app_set_status(&rt->app, (ok_lua && ok_gl) ? "Reloaded Lua scripts & shaders"
                                                         : "Reload completed with errors");
    }

    if (rt->app.dirty_depth_gpu && !rt->app.depth_is_neural) {
        nebel_app_compute_classical_depth(&rt->app);
    }

    nebel_file_dialog_poll(&rt->app, rt->window);
    nebel_depth_onnx_poll(&rt->app);
    nebel_audio_flush(&rt->audio, &rt->app);

    int rw = vw, rh = vh;
    SDL_GetWindowSizeInPixels(rt->window, &rw, &rh);
    nebel_renderer_draw(&rt->renderer, &rt->app, vw, vh, rw, rh);
    SDL_GL_SwapWindow(rt->window);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "[nebel] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

#if defined(__EMSCRIPTEN__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  #if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  #endif
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    NebelRuntime *rt = (NebelRuntime *)calloc(1, sizeof(NebelRuntime));
    if (!rt) return 1;

    rt->window = SDL_CreateWindow("Nebel — Atmospheric Fog Studio", 1280, 760,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!rt->window) {
        fprintf(stderr, "[nebel] SDL_CreateWindow failed: %s\n", SDL_GetError());
        free(rt);
        SDL_Quit();
        return 1;
    }

    rt->gl_ctx = SDL_GL_CreateContext(rt->window);
    if (!rt->gl_ctx) {
        fprintf(stderr, "[nebel] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(rt->window);
        free(rt);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

    /* Resolve loose external assets/ and scripts/ directory beside binary */
    const char *base_path = SDL_GetBasePath();
    const char *asset_root = ".";
#if !defined(__EMSCRIPTEN__)
    if (base_path && base_path[0]) {
        char probe[512];
        snprintf(probe, sizeof(probe), "%sscripts/main.lua", base_path);
        FILE *pf = fopen(probe, "rb");
        if (pf) {
            fclose(pf);
            asset_root = base_path;
        }
    }
#else
    (void)base_path;
#endif

    nebel_app_init(&rt->app, asset_root);
    nebel_file_dialog_init(&rt->app);
    nebel_depth_onnx_init(&rt->app);
    nebel_audio_init(&rt->audio);

    if (!nebel_renderer_init(&rt->renderer, rt->app.asset_root)) {
        fprintf(stderr, "[nebel] renderer init failed\n");
        nebel_app_free(&rt->app);
        SDL_GL_DestroyContext(rt->gl_ctx);
        SDL_DestroyWindow(rt->window);
        free(rt);
        SDL_Quit();
        return 1;
    }

    nebel_lua_init(&rt->lua, &rt->app);
    nebel_lua_reload(&rt->lua);
    rt->running = true;

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop_arg(frame_tick, rt, 0, 1);
#else
    while (rt->running) {
        frame_tick(rt);
    }
    nebel_lua_free(&rt->lua);
    nebel_renderer_free(&rt->renderer);
    nebel_audio_free(&rt->audio);
    nebel_app_free(&rt->app);
    SDL_GL_DestroyContext(rt->gl_ctx);
    SDL_DestroyWindow(rt->window);
    free(rt);
    SDL_Quit();
#endif
    return 0;
}

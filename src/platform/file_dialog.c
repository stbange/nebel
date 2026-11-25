#include "platform/file_dialog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

static NebelAppState *g_web_app = NULL;

EM_JS(void, nebel_js_open_file, (void), {
    if (window.nebelWebOpenFile) window.nebelWebOpenFile();
});

EM_JS(void, nebel_js_download_file, (const uint8_t *ptr, int len, const char *name, const char *mime), {
    if (window.nebelWebDownloadFile) {
        window.nebelWebDownloadFile(ptr, len, UTF8ToString(name), UTF8ToString(mime));
    }
});

EMSCRIPTEN_KEEPALIVE
void nebel_web_on_file_loaded(const uint8_t *bytes, int len) {
    if (!g_web_app || !bytes || len <= 0) return;
    nebel_app_load_image_mem(g_web_app, bytes, (size_t)len);
}

void nebel_file_dialog_init(NebelAppState *app) {
    g_web_app = app;
}

void nebel_file_dialog_poll(NebelAppState *app, SDL_Window *window) {
    (void)window;
    if (!app) return;
    if (app->req_open_dialog) {
        app->req_open_dialog = false;
        nebel_js_open_file();
    }
    if (app->req_export_png) {
        app->req_export_png = false;
        NebelImage out = {0};
        uint8_t *buf = NULL;
        size_t len = 0;
        if (nebel_app_render_full(app, &out) && nebel_image_encode_png_mem(&out, &buf, &len)) {
            nebel_js_download_file(buf, (int)len, "nebel_export.png", "image/png");
            nebel_app_queue_audio(app, "assets/sounds/export_shutter.wav", 0.55f);
            nebel_app_set_status(app, "Exported full-resolution PNG");
        } else {
            nebel_app_set_status(app, "PNG export failed");
        }
        free(buf);
        nebel_image_free(&out);
    }
    if (app->req_export_jpg) {
        app->req_export_jpg = false;
        NebelImage out = {0};
        uint8_t *buf = NULL;
        size_t len = 0;
        if (nebel_app_render_full(app, &out) &&
            nebel_image_encode_jpg_mem(&out, app->req_jpg_quality, &buf, &len)) {
            nebel_js_download_file(buf, (int)len, "nebel_export.jpg", "image/jpeg");
            nebel_app_queue_audio(app, "assets/sounds/export_shutter.wav", 0.55f);
            nebel_app_set_status(app, "Exported full-resolution JPEG");
        } else {
            nebel_app_set_status(app, "JPEG export failed");
        }
        free(buf);
        nebel_image_free(&out);
    }
    if (app->req_export_png16) {
        app->req_export_png16 = false;
        NebelImage out = {0};
        uint8_t *buf = NULL;
        size_t len = 0;
        if (nebel_app_render_full(app, &out) && nebel_image_encode_png16_mem(&out, &buf, &len)) {
            nebel_js_download_file(buf, (int)len, "nebel_export_16bit.png", "image/png");
            nebel_app_queue_audio(app, "assets/sounds/export_shutter.wav", 0.55f);
            nebel_app_set_status(app, "Exported 16-bit lossless PNG");
        } else {
            nebel_app_set_status(app, "16-bit PNG export failed");
        }
        free(buf);
        nebel_image_free(&out);
    }
    if (app->req_export_tiff16) {
        app->req_export_tiff16 = false;
        NebelImage out = {0};
        uint8_t *buf = NULL;
        size_t len = 0;
        if (nebel_app_render_full(app, &out) && nebel_image_encode_tiff16_mem(&out, &buf, &len)) {
            nebel_js_download_file(buf, (int)len, "nebel_export_16bit.tiff", "image/tiff");
            nebel_app_queue_audio(app, "assets/sounds/export_shutter.wav", 0.55f);
            nebel_app_set_status(app, "Exported 16-bit print TIFF");
        } else {
            nebel_app_set_status(app, "16-bit TIFF export failed");
        }
        free(buf);
        nebel_image_free(&out);
    }
}

#else

static SDL_Mutex *g_dlg_mutex = NULL;
static char g_pending_open[512] = {0};
static char g_pending_save_png[512] = {0};
static char g_pending_save_jpg[512] = {0};
static char g_pending_save_png16[512] = {0};
static char g_pending_save_tiff16[512] = {0};

static void store_pending(char dst[512], const char *const *filelist) {
    if (!filelist || !filelist[0]) return;
    SDL_LockMutex(g_dlg_mutex);
    snprintf(dst, 512, "%s", filelist[0]);
    SDL_UnlockMutex(g_dlg_mutex);
}

static void SDLCALL on_open_cb(void *u, const char *const *fl, int f) { (void)u; (void)f; store_pending(g_pending_open, fl); }
static void SDLCALL on_save_png_cb(void *u, const char *const *fl, int f) { (void)u; (void)f; store_pending(g_pending_save_png, fl); }
static void SDLCALL on_save_jpg_cb(void *u, const char *const *fl, int f) { (void)u; (void)f; store_pending(g_pending_save_jpg, fl); }
static void SDLCALL on_save_png16_cb(void *u, const char *const *fl, int f) { (void)u; (void)f; store_pending(g_pending_save_png16, fl); }
static void SDLCALL on_save_tiff16_cb(void *u, const char *const *fl, int f) { (void)u; (void)f; store_pending(g_pending_save_tiff16, fl); }

static bool take_pending(char src[512], char out[512]) {
    out[0] = '\0';
    SDL_LockMutex(g_dlg_mutex);
    if (src[0]) {
        snprintf(out, 512, "%s", src);
        src[0] = '\0';
    }
    SDL_UnlockMutex(g_dlg_mutex);
    return out[0] != '\0';
}

void nebel_file_dialog_init(NebelAppState *app) {
    (void)app;
    if (!g_dlg_mutex) g_dlg_mutex = SDL_CreateMutex();
}

void nebel_file_dialog_poll(NebelAppState *app, SDL_Window *window) {
    if (!app || !g_dlg_mutex) return;

    if (app->req_open_dialog) {
        app->req_open_dialog = false;
        static const SDL_DialogFileFilter filters[] = {
            { "Photos (PNG, JPEG)", "png;jpg;jpeg" }
        };
        SDL_ShowOpenFileDialog(on_open_cb, NULL, window, filters, 1, NULL, false);
    }
    if (app->req_export_png) {
        app->req_export_png = false;
        static const SDL_DialogFileFilter filters[] = { { "PNG Image", "png" } };
        SDL_ShowSaveFileDialog(on_save_png_cb, NULL, window, filters, 1, "nebel_export.png");
    }
    if (app->req_export_jpg) {
        app->req_export_jpg = false;
        static const SDL_DialogFileFilter filters[] = { { "JPEG Image", "jpg;jpeg" } };
        SDL_ShowSaveFileDialog(on_save_jpg_cb, NULL, window, filters, 1, "nebel_export.jpg");
    }
    if (app->req_export_png16) {
        app->req_export_png16 = false;
        static const SDL_DialogFileFilter filters[] = { { "16-bit PNG Image", "png" } };
        SDL_ShowSaveFileDialog(on_save_png16_cb, NULL, window, filters, 1, "nebel_export_16bit.png");
    }
    if (app->req_export_tiff16) {
        app->req_export_tiff16 = false;
        static const SDL_DialogFileFilter filters[] = { { "16-bit TIFF Image", "tiff;tif" } };
        SDL_ShowSaveFileDialog(on_save_tiff16_cb, NULL, window, filters, 1, "nebel_export_16bit.tiff");
    }

    char path[512];
    if (take_pending(g_pending_open, path)) {
        nebel_app_load_image_file(app, path);
    }
    if (take_pending(g_pending_save_png, path)) {
        if (nebel_app_export_png(app, path)) {
            nebel_app_queue_audio(app, "assets/sounds/export_shutter.wav", 0.55f);
            nebel_app_set_status(app, "Exported full-resolution PNG");
        } else {
            nebel_app_set_status(app, "PNG export failed");
        }
    }
    if (take_pending(g_pending_save_jpg, path)) {
        if (nebel_app_export_jpg(app, path, app->req_jpg_quality)) {
            nebel_app_queue_audio(app, "assets/sounds/export_shutter.wav", 0.55f);
            nebel_app_set_status(app, "Exported full-resolution JPEG");
        } else {
            nebel_app_set_status(app, "JPEG export failed");
        }
    }
    if (take_pending(g_pending_save_png16, path)) {
        if (nebel_app_export_png16(app, path)) {
            nebel_app_queue_audio(app, "assets/sounds/export_shutter.wav", 0.55f);
            nebel_app_set_status(app, "Exported 16-bit lossless PNG");
        } else {
            nebel_app_set_status(app, "16-bit PNG export failed");
        }
    }
    if (take_pending(g_pending_save_tiff16, path)) {
        if (nebel_app_export_tiff16(app, path)) {
            nebel_app_queue_audio(app, "assets/sounds/export_shutter.wav", 0.55f);
            nebel_app_set_status(app, "Exported 16-bit print TIFF");
        } else {
            nebel_app_set_status(app, "16-bit TIFF export failed");
        }
    }
}
#endif

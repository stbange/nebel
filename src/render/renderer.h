#ifndef NEBEL_RENDER_RENDERER_H
#define NEBEL_RENDER_RENDERER_H

#include "core/app_state.h"
#include "render/font.h"
#include "render/gl_api.h"
#include <stdbool.h>

typedef struct {
    GLuint vao;
    GLuint vbo;

    GLuint prog_fog;
    GLuint prog_fluid;
    GLuint prog_accum;
    GLuint prog_bloom;
    GLuint prog_comp;
    GLuint prog_ui;

    GLuint tex_photo;
    GLuint tex_depth;
    GLuint tex_protect;
    GLuint tex_masks[NEBEL_MAX_LAYERS];
    GLuint tex_icons;
    int icon_count;

    GLuint fbo_ping[2];
    GLuint tex_ping[2];
    GLuint fbo_accum[2];
    GLuint tex_accum[2];
    GLuint fbo_flow[2];
    GLuint tex_flow[2];
    GLuint fbo_bloom[2];
    GLuint tex_bloom[2];
    GLuint fbo_amb[2];
    GLuint tex_amb[2];
    int fbo_w;
    int fbo_h;
    bool use_half_float;
    bool ambient_dirty;
    bool bloom_dirty;
    bool has_valid_render;
    int accum_active;
    int flow_active;

    NebelFont font;
    char asset_root[256];
} NebelRenderer;

bool nebel_renderer_init(NebelRenderer *r, const char *asset_root);
void nebel_renderer_free(NebelRenderer *r);
bool nebel_renderer_reload_shaders(NebelRenderer *r);
void nebel_renderer_draw(NebelRenderer *r, NebelAppState *app,
                         int vw, int vh, int fb_w, int fb_h);

/* Desktop GPU-accelerated tiled full-resolution fog pass (falls back to CPU if unavailable). */
bool nebel_renderer_render_full(NebelRenderer *r, NebelAppState *app, NebelImage *out);

#endif /* NEBEL_RENDER_RENDERER_H */

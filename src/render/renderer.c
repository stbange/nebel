#include "render/renderer.h"
#include "core/cv_depth.h"
#include "core/manifest.h"

#include "stb_image.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 512 * 1024) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return NULL;
    }
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

static GLuint compile_shader(GLenum type, const char *body, const char *label) {
#if defined(__EMSCRIPTEN__)
    const char *preamble = "#version 300 es\nprecision highp float;\n";
#else
    const char *preamble = "#version 330 core\n";
#endif
    const char *srcs[2] = { preamble, body };
    GLuint s = glCreateShader(type);
    glShaderSource(s, 2, srcs, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "[nebel] shader compile failed (%s): %s\n", label, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint load_program(const char *asset_root, const char *vert_rel, const char *frag_rel) {
    char vp[384], fp[384];
    if (!nebel_path_join_asset(asset_root, vert_rel, vp, sizeof(vp)) ||
        !nebel_path_join_asset(asset_root, frag_rel, fp, sizeof(fp))) {
        return 0;
    }
    char *vsrc = read_text_file(vp);
    char *fsrc = read_text_file(fp);
    if (!vsrc || !fsrc) {
        fprintf(stderr, "[nebel] missing shader file: %s or %s\n", vp, fp);
        free(vsrc); free(fsrc);
        return 0;
    }
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vsrc, vert_rel);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fsrc, frag_rel);
    free(vsrc); free(fsrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "[nebel] program link failed (%s): %s\n", frag_rel, log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

static GLuint make_tex(GLint filter) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static void destroy_fbos(NebelRenderer *r) {
    for (int i = 0; i < 2; i++) {
        if (r->fbo_ping[i])  { glDeleteFramebuffers(1, &r->fbo_ping[i]);  r->fbo_ping[i] = 0; }
        if (r->tex_ping[i])  { glDeleteTextures(1, &r->tex_ping[i]);      r->tex_ping[i] = 0; }
        if (r->fbo_bloom[i]) { glDeleteFramebuffers(1, &r->fbo_bloom[i]); r->fbo_bloom[i] = 0; }
        if (r->tex_bloom[i]) { glDeleteTextures(1, &r->tex_bloom[i]);     r->tex_bloom[i] = 0; }
        if (r->fbo_amb[i])   { glDeleteFramebuffers(1, &r->fbo_amb[i]);   r->fbo_amb[i] = 0; }
        if (r->tex_amb[i])   { glDeleteTextures(1, &r->tex_amb[i]);       r->tex_amb[i] = 0; }
    }
    r->fbo_w = 0;
    r->fbo_h = 0;
}

static bool alloc_fbo_pair(GLuint fbos[2], GLuint texs[2], int w, int h, bool half_float) {
    GLint internal = half_float ? GL_RGBA16F : GL_RGBA8;
    GLenum type = half_float ? GL_FLOAT : GL_UNSIGNED_BYTE;
    for (int i = 0; i < 2; i++) {
        texs[i] = make_tex(GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, GL_RGBA, type, NULL);
        glGenFramebuffers(1, &fbos[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texs[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

static void ensure_fbos(NebelRenderer *r, int w, int h) {
    if (r->fbo_w == w && r->fbo_h == h && r->fbo_ping[0]) return;
    destroy_fbos(r);
    int bw = w / 2 > 1 ? w / 2 : 1;
    int bh = h / 2 > 1 ? h / 2 : 1;
    int aw = w / 4 > 1 ? w / 4 : 1;
    int ah = h / 4 > 1 ? h / 4 : 1;

    r->use_half_float = true;
    if (!alloc_fbo_pair(r->fbo_ping, r->tex_ping, w, h, true) ||
        !alloc_fbo_pair(r->fbo_bloom, r->tex_bloom, bw, bh, true) ||
        !alloc_fbo_pair(r->fbo_amb, r->tex_amb, aw, ah, true)) {
        destroy_fbos(r);
        r->use_half_float = false;
        alloc_fbo_pair(r->fbo_ping, r->tex_ping, w, h, false);
        alloc_fbo_pair(r->fbo_bloom, r->tex_bloom, bw, bh, false);
        alloc_fbo_pair(r->fbo_amb, r->tex_amb, aw, ah, false);
    }
    r->fbo_w = w;
    r->fbo_h = h;
    r->ambient_dirty = true;
}

bool nebel_renderer_reload_shaders(NebelRenderer *r) {
    if (!r) return false;
    GLuint pf = load_program(r->asset_root, "assets/shaders/quad.vert", "assets/shaders/fog_layer.frag");
    GLuint pb = load_program(r->asset_root, "assets/shaders/quad.vert", "assets/shaders/bloom.frag");
    GLuint pc = load_program(r->asset_root, "assets/shaders/quad.vert", "assets/shaders/composite.frag");
    GLuint pu = load_program(r->asset_root, "assets/shaders/quad.vert", "assets/shaders/ui.frag");
    if (!pf || !pb || !pc || !pu) {
        if (pf) glDeleteProgram(pf);
        if (pb) glDeleteProgram(pb);
        if (pc) glDeleteProgram(pc);
        if (pu) glDeleteProgram(pu);
        return false;
    }
    if (r->prog_fog)   glDeleteProgram(r->prog_fog);
    if (r->prog_bloom) glDeleteProgram(r->prog_bloom);
    if (r->prog_comp)  glDeleteProgram(r->prog_comp);
    if (r->prog_ui)    glDeleteProgram(r->prog_ui);
    r->prog_fog = pf;
    r->prog_bloom = pb;
    r->prog_comp = pc;
    r->prog_ui = pu;
    return true;
}

static void load_icons(NebelRenderer *r) {
    char path[384];
    if (!nebel_path_join_asset(r->asset_root, "assets/icons/icons_atlas.png", path, sizeof(path))) return;
    int w = 0, h = 0, c = 0;
    uint8_t *px = stbi_load(path, &w, &h, &c, 4);
    if (!px) return;
    r->tex_icons = make_tex(GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    r->icon_count = h > 0 ? (w / h) : 6;
    if (r->icon_count < 1) r->icon_count = 1;
}

bool nebel_renderer_init(NebelRenderer *r, const char *asset_root) {
    if (!r) return false;
    memset(r, 0, sizeof(*r));
    snprintf(r->asset_root, sizeof(r->asset_root), "%s",
             (asset_root && asset_root[0]) ? asset_root : ".");

    if (!nebel_gl_load()) return false;
    if (!nebel_renderer_reload_shaders(r)) return false;

    static const float quad_verts[24] = {
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
    };
    glGenVertexArrays(1, &r->vao);
    glBindVertexArray(r->vao);
    glGenBuffers(1, &r->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glBindVertexArray(0);

    r->tex_photo = make_tex(GL_LINEAR);
    r->tex_depth = make_tex(GL_LINEAR);
    for (int i = 0; i < NEBEL_MAX_LAYERS; i++) {
        r->tex_masks[i] = make_tex(GL_LINEAR);
    }

    char font_path[384];
    if (nebel_path_join_asset(r->asset_root, "assets/fonts/DejaVuSans.ttf", font_path, sizeof(font_path))) {
        nebel_font_load(&r->font, font_path, 15.0f);
    }
    load_icons(r);
    return true;
}

void nebel_renderer_free(NebelRenderer *r) {
    if (!r) return;
    destroy_fbos(r);
    nebel_font_free(&r->font);
    if (r->tex_photo) glDeleteTextures(1, &r->tex_photo);
    if (r->tex_depth) glDeleteTextures(1, &r->tex_depth);
    if (r->tex_icons) glDeleteTextures(1, &r->tex_icons);
    for (int i = 0; i < NEBEL_MAX_LAYERS; i++) {
        if (r->tex_masks[i]) glDeleteTextures(1, &r->tex_masks[i]);
    }
    if (r->prog_fog)   glDeleteProgram(r->prog_fog);
    if (r->prog_bloom) glDeleteProgram(r->prog_bloom);
    if (r->prog_comp)  glDeleteProgram(r->prog_comp);
    if (r->prog_ui)    glDeleteProgram(r->prog_ui);
    if (r->vbo) glDeleteBuffers(1, &r->vbo);
    if (r->vao) glDeleteVertexArrays(1, &r->vao);
    memset(r, 0, sizeof(*r));
}

static void draw_quad(GLuint prog, float x, float y, float w, float h,
                      float vw, float vh, float u0, float v0, float u1, float v1) {
    glUniform4f(glGetUniformLocation(prog, "u_rect"), x, y, w, h);
    glUniform2f(glGetUniformLocation(prog, "u_viewport"), vw, vh);
    glUniform4f(glGetUniformLocation(prog, "u_uv_rect"), u0, v0, u1, v1);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void sync_gpu_textures(NebelRenderer *r, NebelAppState *app) {
    int w = app->work_img.width;
    int h = app->work_img.height;
    if (w <= 0 || h <= 0) return;
    size_t n = (size_t)w * (size_t)h;

    if (app->dirty_photo_gpu) {
        ensure_fbos(r, w, h);
        glBindTexture(GL_TEXTURE_2D, r->tex_photo);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (r->use_half_float) {
            /* Set alpha to 0 so ping-pong initial fog opacity starts at 0 */
            float *tmp = (float *)malloc(n * 4u * sizeof(float));
            if (tmp) {
                memcpy(tmp, app->work_img.rgba, n * 4u * sizeof(float));
                for (size_t i = 0; i < n; i++) tmp[i * 4u + 3u] = 0.0f;
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, tmp);
                free(tmp);
            }
        } else {
            uint8_t *tmp = (uint8_t *)malloc(n * 4u);
            if (tmp) {
                for (size_t i = 0; i < n; i++) {
                    for (int c = 0; c < 3; c++) {
                        float v = app->work_img.rgba[i * 4u + c];
                        if (v < 0.0f) v = 0.0f;
                        if (v > 1.0f) v = 1.0f;
                        tmp[i * 4u + c] = (uint8_t)(v * 255.0f + 0.5f);
                    }
                    tmp[i * 4u + 3u] = 0;
                }
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
                free(tmp);
            }
        }
        app->dirty_photo_gpu = false;
        r->ambient_dirty = true;
    }

    if (app->dirty_depth_gpu && app->depth_map) {
        uint8_t *d8 = (uint8_t *)malloc(n);
        if (d8) {
            for (size_t i = 0; i < n; i++) {
                float d = app->depth_map[i];
                if (d < 0.0f) d = 0.0f;
                if (d > 1.0f) d = 1.0f;
                d8[i] = (uint8_t)(d * 255.0f + 0.5f);
            }
            glBindTexture(GL_TEXTURE_2D, r->tex_depth);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, d8);
            free(d8);
        }
        app->dirty_depth_gpu = false;
    }

    if (app->dirty_masks_gpu) {
        float *luma = (float *)malloc(n * 3u * sizeof(float));
        uint8_t *m8 = (uint8_t *)malloc(n);
        if (luma && m8) {
            float *raw = luma + n;
            float *ref = luma + 2u * n;
            for (size_t i = 0; i < n; i++) {
                const float *p = &app->work_img.rgba[i * 4u];
                luma[i] = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
            }
            int rad = (w < h ? w : h) / 64;
            if (rad < 2) rad = 2;
            if (rad > 10) rad = 10;

            for (int li = 0; li < app->layer_count; li++) {
                const NebelFogLayer *layer = &app->layers[li];
                if (!layer->mask) continue;
                for (size_t i = 0; i < n; i++) {
                    float m = (float)layer->mask[i] / 255.0f;
                    float pr = app->protect_mask ? ((float)app->protect_mask[i] / 255.0f) : 0.0f;
                    raw[i] = m * (1.0f - pr);
                }
                float er = layer->params.edge_refine;
                bool gf_ok = (er > 0.01f) && nebel_guided_filter(luma, raw, ref, w, h, rad, 0.0015f);
                for (size_t i = 0; i < n; i++) {
                    float v = gf_ok ? (raw[i] * (1.0f - er) + ref[i] * er) : raw[i];
                    if (v < 0.0f) v = 0.0f;
                    if (v > 1.0f) v = 1.0f;
                    m8[i] = (uint8_t)(v * 255.0f + 0.5f);
                }
                glBindTexture(GL_TEXTURE_2D, r->tex_masks[li]);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, m8);
            }
        }
        free(luma);
        free(m8);
        app->dirty_masks_gpu = false;
    }
}

static void draw_ui_commands(NebelRenderer *r, const NebelUiState *ui, int vw, int vh) {
    glUseProgram(r->prog_ui);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(r->prog_ui, "u_tex"), 0);

    for (int i = 0; i < ui->cmd_count; i++) {
        const NebelUiDrawCmd *c = &ui->cmds[i];
        glUniform4f(glGetUniformLocation(r->prog_ui, "u_color"),
                    c->color[0], c->color[1], c->color[2], c->color[3]);

        if (c->type == NEBEL_UI_CMD_RECT) {
            glUniform1i(glGetUniformLocation(r->prog_ui, "u_mode"), 0);
            glUniform2f(glGetUniformLocation(r->prog_ui, "u_rect_size"), c->w, c->h);
            glUniform1f(glGetUniformLocation(r->prog_ui, "u_radius"), c->radius);
            draw_quad(r->prog_ui, c->x, c->y, c->w, c->h, (float)vw, (float)vh, 0, 0, 1, 1);
        } else if (c->type == NEBEL_UI_CMD_RING) {
            float d = c->radius * 2.0f + 4.0f;
            glUniform1i(glGetUniformLocation(r->prog_ui, "u_mode"), 3);
            glUniform2f(glGetUniformLocation(r->prog_ui, "u_rect_size"), d, d);
            glUniform1f(glGetUniformLocation(r->prog_ui, "u_radius"), c->radius);
            draw_quad(r->prog_ui, c->x - d * 0.5f, c->y - d * 0.5f, d, d, (float)vw, (float)vh, 0, 0, 1, 1);
        } else if (c->type == NEBEL_UI_CMD_ICON && r->tex_icons) {
            glBindTexture(GL_TEXTURE_2D, r->tex_icons);
            glUniform1i(glGetUniformLocation(r->prog_ui, "u_mode"), 2);
            int idx = c->icon_id % r->icon_count;
            if (idx < 0) idx = 0;
            float u0 = (float)idx / (float)r->icon_count;
            float u1 = (float)(idx + 1) / (float)r->icon_count;
            draw_quad(r->prog_ui, c->x, c->y, c->w, c->h, (float)vw, (float)vh, u0, 0.0f, u1, 1.0f);
        } else if (c->type == NEBEL_UI_CMD_TEXT && r->font.loaded) {
            glBindTexture(GL_TEXTURE_2D, r->font.tex);
            glUniform1i(glGetUniformLocation(r->prog_ui, "u_mode"), 1);
            float pen_x = c->x;
            float pen_y = c->y;
            for (const char *p = c->text; *p; p++) {
                unsigned char ch = (unsigned char)*p;
                if (ch < 32 || ch >= 128) continue;
                const NebelGlyph *g = &r->font.glyphs[ch - 32];
                float gw = g->x1 - g->x0;
                float gh = g->y1 - g->y0;
                if (gw > 0.0f && gh > 0.0f) {
                    draw_quad(r->prog_ui, pen_x + g->x0, pen_y + g->y0, gw, gh,
                              (float)vw, (float)vh, g->u0, g->v0, g->u1, g->v1);
                }
                pen_x += g->xadvance;
            }
        }
    }
    glDisable(GL_BLEND);
}

void nebel_renderer_draw(NebelRenderer *r, NebelAppState *app,
                         int vw, int vh, int fb_w, int fb_h) {
    if (!r || !app || vw <= 0 || vh <= 0 || fb_w <= 0 || fb_h <= 0) return;

    glBindVertexArray(r->vao);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    if (app->work_img.width > 0 && app->work_img.height > 0) {
        sync_gpu_textures(r, app);
        int w = r->fbo_w;
        int h = r->fbo_h;

        /* 0. Wide low-frequency scene ambient light map (recomputed only when photo changes) */
        if (r->ambient_dirty) {
            int aw = w / 4 > 1 ? w / 4 : 1;
            int ah = h / 4 > 1 ? h / 4 : 1;
            glUseProgram(r->prog_bloom);
            glViewport(0, 0, aw, ah);
            glUniform1i(glGetUniformLocation(r->prog_bloom, "u_tex"), 0);
            glUniform1i(glGetUniformLocation(r->prog_bloom, "u_extract"), 0);
            glUniform1f(glGetUniformLocation(r->prog_bloom, "u_spread"), 5.5f);

            glBindFramebuffer(GL_FRAMEBUFFER, r->fbo_amb[0]);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, r->tex_photo);
            glUniform2f(glGetUniformLocation(r->prog_bloom, "u_dir"), 1.0f / (float)aw, 0.0f);
            draw_quad(r->prog_bloom, 0, 0, (float)aw, (float)ah, (float)aw, (float)ah, 0, 1, 1, 0);

            glBindFramebuffer(GL_FRAMEBUFFER, r->fbo_amb[1]);
            glBindTexture(GL_TEXTURE_2D, r->tex_amb[0]);
            glUniform2f(glGetUniformLocation(r->prog_bloom, "u_dir"), 0.0f, 1.0f / (float)ah);
            draw_quad(r->prog_bloom, 0, 0, (float)aw, (float)ah, (float)aw, (float)ah, 0, 1, 1, 0);
            r->ambient_dirty = false;
        }

        /* 1. Multi-layer ping-pong linear fog pass */
        GLuint cur_tex = r->tex_photo;
        int ping = 0;
        float max_bloom = 0.0f;
        float max_grain = 0.0f;
        float max_god_rays = 0.0f;
        float rays_sun_x = 0.72f, rays_sun_y = 0.22f;

        glUseProgram(r->prog_fog);
        glViewport(0, 0, w, h);
        glUniform1i(glGetUniformLocation(r->prog_fog, "u_prev"), 0);
        glUniform1i(glGetUniformLocation(r->prog_fog, "u_depth"), 1);
        glUniform1i(glGetUniformLocation(r->prog_fog, "u_mask"), 2);
        glUniform1i(glGetUniformLocation(r->prog_fog, "u_ambient"), 3);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, r->tex_depth);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, r->tex_amb[1]);

        for (int li = 0; li < app->layer_count; li++) {
            const NebelFogLayer *layer = &app->layers[li];
            if (!layer->params.visible) continue;
            const NebelFogParams *p = &layer->params;
            if (p->bloom_amount > max_bloom) max_bloom = p->bloom_amount;
            if (p->grain_match > max_grain) max_grain = p->grain_match;
            if (p->god_rays > max_god_rays) {
                max_god_rays = p->god_rays;
                rays_sun_x = p->sun_x;
                rays_sun_y = p->sun_y;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, r->fbo_ping[ping]);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, cur_tex);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, r->tex_masks[li]);

            glUniform1f(glGetUniformLocation(r->prog_fog, "u_density"), p->density);
            glUniform1f(glGetUniformLocation(r->prog_fog, "u_height_falloff"), p->height_falloff);
            glUniform1f(glGetUniformLocation(r->prog_fog, "u_horizon_y"), p->horizon_y);
            glUniform1f(glGetUniformLocation(r->prog_fog, "u_depth_min"), p->depth_min);
            glUniform1f(glGetUniformLocation(r->prog_fog, "u_depth_max"), p->depth_max);
            glUniform3f(glGetUniformLocation(r->prog_fog, "u_airlight"),
                        p->airlight[0], p->airlight[1], p->airlight[2]);
            glUniform2f(glGetUniformLocation(r->prog_fog, "u_sun_pos"), p->sun_x, p->sun_y);
            glUniform1f(glGetUniformLocation(r->prog_fog, "u_sun_strength"), p->sun_strength);
            glUniform1f(glGetUniformLocation(r->prog_fog, "u_sun_g"), p->sun_anisotropy);
            glUniform1f(glGetUniformLocation(r->prog_fog, "u_noise_scale"), p->noise_scale);
            glUniform1f(glGetUniformLocation(r->prog_fog, "u_noise_amount"), p->noise_amount);
            glUniform1f(glGetUniformLocation(r->prog_fog, "u_shadow_pool"), p->shadow_pool);
            glUniform1f(glGetUniformLocation(r->prog_fog, "u_seed"), (float)(p->noise_seed % 10000u));

            /* Render into FBO preserving row-0 = top-of-image UV orientation */
            draw_quad(r->prog_fog, 0, 0, (float)w, (float)h, (float)w, (float)h, 0, 1, 1, 0);
            cur_tex = r->tex_ping[ping];
            ping ^= 1;
        }

        /* 2. Two-pass separable wide bloom (veiling glare) */
        int bw = w / 2 > 1 ? w / 2 : 1;
        int bh = h / 2 > 1 ? h / 2 : 1;
        glUseProgram(r->prog_bloom);
        glViewport(0, 0, bw, bh);
        glUniform1i(glGetUniformLocation(r->prog_bloom, "u_tex"), 0);
        glUniform1f(glGetUniformLocation(r->prog_bloom, "u_spread"), 4.2f);

        glBindFramebuffer(GL_FRAMEBUFFER, r->fbo_bloom[0]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, r->tex_photo);
        glUniform2f(glGetUniformLocation(r->prog_bloom, "u_dir"), 1.0f / (float)bw, 0.0f);
        glUniform1i(glGetUniformLocation(r->prog_bloom, "u_extract"), 1);
        draw_quad(r->prog_bloom, 0, 0, (float)bw, (float)bh, (float)bw, (float)bh, 0, 1, 1, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, r->fbo_bloom[1]);
        glBindTexture(GL_TEXTURE_2D, r->tex_bloom[0]);
        glUniform2f(glGetUniformLocation(r->prog_bloom, "u_dir"), 0.0f, 1.0f / (float)bh);
        glUniform1i(glGetUniformLocation(r->prog_bloom, "u_extract"), 0);
        draw_quad(r->prog_bloom, 0, 0, (float)bw, (float)bh, (float)bw, (float)bh, 0, 1, 1, 0);

        /* 3. Final composite to default framebuffer (linear -> sRGB + god rays + split + depth) */
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.055f, 0.062f, 0.074f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        float cx, cy, cw, ch;
        nebel_app_canvas_rect(app, vw, vh, &cx, &cy, &cw, &ch);
        glUseProgram(r->prog_comp);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->tex_photo);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, cur_tex);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, r->tex_bloom[1]);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, r->tex_depth);
        glUniform1i(glGetUniformLocation(r->prog_comp, "u_orig"), 0);
        glUniform1i(glGetUniformLocation(r->prog_comp, "u_fog"), 1);
        glUniform1i(glGetUniformLocation(r->prog_comp, "u_bloom"), 2);
        glUniform1i(glGetUniformLocation(r->prog_comp, "u_depth"), 3);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_bloom_amount"), max_bloom);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_grain_sigma"), app->sensor_noise * max_grain);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_split_enabled"), app->split_enabled ? 1.0f : 0.0f);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_split_pos"), app->split_pos);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_show_depth"), app->show_depth ? 1.0f : 0.0f);
        glUniform2f(glGetUniformLocation(r->prog_comp, "u_sun_pos"), rays_sun_x, rays_sun_y);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_god_rays"), max_god_rays);
        draw_quad(r->prog_comp, cx, cy, cw, ch, (float)vw, (float)vh, 0, 0, 1, 1);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.055f, 0.062f, 0.074f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    draw_ui_commands(r, &app->ui, vw, vh);
    glBindVertexArray(0);
}

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
    const char *preamble = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
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

static void destroy_fbo_pair(GLuint fbos[2], GLuint texs[2]) {
    for (int i = 0; i < 2; i++) {
        if (fbos[i]) { glDeleteFramebuffers(1, &fbos[i]); fbos[i] = 0; }
        if (texs[i]) { glDeleteTextures(1, &texs[i]);     texs[i] = 0; }
    }
}

static void destroy_fbos(NebelRenderer *r) {
    destroy_fbo_pair(r->fbo_ping,  r->tex_ping);
    destroy_fbo_pair(r->fbo_accum, r->tex_accum);
    destroy_fbo_pair(r->fbo_flow,  r->tex_flow);
    destroy_fbo_pair(r->fbo_bloom, r->tex_bloom);
    destroy_fbo_pair(r->fbo_amb,   r->tex_amb);
    r->fbo_w = 0;
    r->fbo_h = 0;
    r->has_valid_render = false;
}

static bool alloc_fbo_pair(GLuint fbos[2], GLuint texs[2], int w, int h, GLint internal, GLenum type) {
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

#if defined(__EMSCRIPTEN__)
    GLint hi_fmt = GL_RGBA16F;
#else
    GLint hi_fmt = GL_RGBA32F;
#endif
    r->use_half_float = true;
    if (!alloc_fbo_pair(r->fbo_ping,  r->tex_ping,  w,  h,  hi_fmt,     GL_FLOAT) ||
        !alloc_fbo_pair(r->fbo_accum, r->tex_accum, w,  h,  hi_fmt,     GL_FLOAT) ||
        !alloc_fbo_pair(r->fbo_flow,  r->tex_flow,  aw, ah, GL_RGBA16F, GL_FLOAT) ||
        !alloc_fbo_pair(r->fbo_bloom, r->tex_bloom, bw, bh, GL_RGBA16F, GL_FLOAT) ||
        !alloc_fbo_pair(r->fbo_amb,   r->tex_amb,   aw, ah, GL_RGBA16F, GL_FLOAT)) {
        destroy_fbos(r);
        if (!alloc_fbo_pair(r->fbo_ping,  r->tex_ping,  w,  h,  GL_RGBA16F, GL_FLOAT) ||
            !alloc_fbo_pair(r->fbo_accum, r->tex_accum, w,  h,  GL_RGBA16F, GL_FLOAT) ||
            !alloc_fbo_pair(r->fbo_flow,  r->tex_flow,  aw, ah, GL_RGBA16F, GL_FLOAT) ||
            !alloc_fbo_pair(r->fbo_bloom, r->tex_bloom, bw, bh, GL_RGBA16F, GL_FLOAT) ||
            !alloc_fbo_pair(r->fbo_amb,   r->tex_amb,   aw, ah, GL_RGBA16F, GL_FLOAT)) {
            destroy_fbos(r);
            r->use_half_float = false;
            alloc_fbo_pair(r->fbo_ping,  r->tex_ping,  w,  h,  GL_RGBA8, GL_UNSIGNED_BYTE);
            alloc_fbo_pair(r->fbo_accum, r->tex_accum, w,  h,  GL_RGBA8, GL_UNSIGNED_BYTE);
            alloc_fbo_pair(r->fbo_flow,  r->tex_flow,  aw, ah, GL_RGBA8, GL_UNSIGNED_BYTE);
            alloc_fbo_pair(r->fbo_bloom, r->tex_bloom, bw, bh, GL_RGBA8, GL_UNSIGNED_BYTE);
            alloc_fbo_pair(r->fbo_amb,   r->tex_amb,   aw, ah, GL_RGBA8, GL_UNSIGNED_BYTE);
        }
    }

    /* Clear flow FBOs to zero so initial fluid offset is (0, 0) */
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, r->fbo_flow[i]);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    r->fbo_w = w;
    r->fbo_h = h;
    r->ambient_dirty = true;
    r->bloom_dirty = true;
    r->has_valid_render = false;
    r->accum_active = 0;
    r->flow_active = 0;
}

bool nebel_renderer_reload_shaders(NebelRenderer *r) {
    if (!r) return false;
    GLuint pf  = load_program(r->asset_root, "assets/shaders/quad.vert", "assets/shaders/fog_layer.frag");
    GLuint pfl = load_program(r->asset_root, "assets/shaders/quad.vert", "assets/shaders/fluid_advect.frag");
    GLuint pa  = load_program(r->asset_root, "assets/shaders/quad.vert", "assets/shaders/accum.frag");
    GLuint pb  = load_program(r->asset_root, "assets/shaders/quad.vert", "assets/shaders/bloom.frag");
    GLuint pc  = load_program(r->asset_root, "assets/shaders/quad.vert", "assets/shaders/composite.frag");
    GLuint pu  = load_program(r->asset_root, "assets/shaders/quad.vert", "assets/shaders/ui.frag");
    if (!pf || !pfl || !pa || !pb || !pc || !pu) {
        if (pf)  glDeleteProgram(pf);
        if (pfl) glDeleteProgram(pfl);
        if (pa)  glDeleteProgram(pa);
        if (pb)  glDeleteProgram(pb);
        if (pc)  glDeleteProgram(pc);
        if (pu)  glDeleteProgram(pu);
        return false;
    }
    if (r->prog_fog)   glDeleteProgram(r->prog_fog);
    if (r->prog_fluid) glDeleteProgram(r->prog_fluid);
    if (r->prog_accum) glDeleteProgram(r->prog_accum);
    if (r->prog_bloom) glDeleteProgram(r->prog_bloom);
    if (r->prog_comp)  glDeleteProgram(r->prog_comp);
    if (r->prog_ui)    glDeleteProgram(r->prog_ui);
    r->prog_fog   = pf;
    r->prog_fluid = pfl;
    r->prog_accum = pa;
    r->prog_bloom = pb;
    r->prog_comp  = pc;
    r->prog_ui    = pu;
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

static void load_fog_struct_tex(NebelRenderer *r) {
    char path[384];
    if (!nebel_path_join_asset(r->asset_root, "assets/textures/fog_structure.png", path, sizeof(path))) return;
    int w = 0, h = 0, c = 0;
    uint8_t *px = stbi_load(path, &w, &h, &c, 4);
    if (!px) return;
    r->tex_fog_struct = make_tex(GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
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
    r->tex_protect = make_tex(GL_LINEAR);
    for (int i = 0; i < NEBEL_MAX_LAYERS; i++) {
        r->tex_masks[i] = make_tex(GL_LINEAR);
    }

    char font_path[384];
    if (nebel_path_join_asset(r->asset_root, "assets/fonts/DejaVuSans.ttf", font_path, sizeof(font_path))) {
        nebel_font_load(&r->font, font_path, 15.0f);
    }
    load_icons(r);
    load_fog_struct_tex(r);
    return true;
}

void nebel_renderer_free(NebelRenderer *r) {
    if (!r) return;
    destroy_fbos(r);
    nebel_font_free(&r->font);
    if (r->tex_photo)      glDeleteTextures(1, &r->tex_photo);
    if (r->tex_depth)      glDeleteTextures(1, &r->tex_depth);
    if (r->tex_protect)    glDeleteTextures(1, &r->tex_protect);
    if (r->tex_fog_struct) glDeleteTextures(1, &r->tex_fog_struct);
    if (r->tex_icons)      glDeleteTextures(1, &r->tex_icons);
    for (int i = 0; i < NEBEL_MAX_LAYERS; i++) {
        if (r->tex_masks[i]) glDeleteTextures(1, &r->tex_masks[i]);
    }
    if (r->prog_fog)   glDeleteProgram(r->prog_fog);
    if (r->prog_fluid) glDeleteProgram(r->prog_fluid);
    if (r->prog_accum) glDeleteProgram(r->prog_accum);
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
        r->bloom_dirty = true;
        r->has_valid_render = false;
    }

    if (app->dirty_depth_gpu && app->depth_map) {
        glBindTexture(GL_TEXTURE_2D, r->tex_depth);
        if (r->use_half_float) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#if defined(__EMSCRIPTEN__)
            /* WebGL 2.0 guarantees GL_LINEAR filtering on GL_R16F without extra extensions */
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, w, h, 0, GL_RED, GL_FLOAT, app->depth_map);
#else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, app->depth_map);
#endif
        } else {
            uint8_t *d8 = (uint8_t *)malloc(n);
            if (d8) {
                for (size_t i = 0; i < n; i++) {
                    float d = app->depth_map[i];
                    if (d < 0.0f) d = 0.0f;
                    if (d > 1.0f) d = 1.0f;
                    d8[i] = (uint8_t)(d * 255.0f + 0.5f);
                }
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, d8);
                free(d8);
            }
        }
        app->dirty_depth_gpu = false;
    }

    if (app->dirty_masks_gpu) {
        if (app->protect_mask) {
            glBindTexture(GL_TEXTURE_2D, r->tex_protect);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, app->protect_mask);
        }

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

static void run_ambient_and_bloom_passes(NebelRenderer *r) {
    int w = r->fbo_w;
    int h = r->fbo_h;

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

    if (r->bloom_dirty) {
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
        r->bloom_dirty = false;
    }
}

static void run_fluid_advection_gpu(NebelRenderer *r, int fluid_steps) {
    int aw = r->fbo_w / 4 > 1 ? r->fbo_w / 4 : 1;
    int ah = r->fbo_h / 4 > 1 ? r->fbo_h / 4 : 1;

    glBindFramebuffer(GL_FRAMEBUFFER, r->fbo_flow[0]);
    glViewport(0, 0, aw, ah);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    r->flow_active = 0;

    if (fluid_steps <= 0) return;

    glUseProgram(r->prog_fluid);
    glUniform1i(glGetUniformLocation(r->prog_fluid, "u_flow"), 0);
    glUniform1i(glGetUniformLocation(r->prog_fluid, "u_depth"), 1);
    glUniform2f(glGetUniformLocation(r->prog_fluid, "u_texel"), 2.0f / (float)aw, 2.0f / (float)ah);
    float dt = 0.045f / (float)(fluid_steps > 4 ? fluid_steps : 4);
    glUniform1f(glGetUniformLocation(r->prog_fluid, "u_dt"), dt);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, r->tex_depth);

    for (int s = 0; s < fluid_steps; s++) {
        int dst = r->flow_active ^ 1;
        glBindFramebuffer(GL_FRAMEBUFFER, r->fbo_flow[dst]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, r->tex_flow[r->flow_active]);
        draw_quad(r->prog_fluid, 0, 0, (float)aw, (float)ah, (float)aw, (float)ah, 0, 1, 1, 0);
        r->flow_active = dst;
    }
}

static GLuint run_fog_layers_sample(NebelRenderer *r, const NebelAppState *app,
                                    int ray_slices, int shadow_steps, int sample_idx) {
    int w = r->fbo_w;
    int h = r->fbo_h;
    GLuint cur_tex = r->tex_photo;
    int ping = 0;

    glUseProgram(r->prog_fog);
    glViewport(0, 0, w, h);
    glUniform1i(glGetUniformLocation(r->prog_fog, "u_prev"), 0);
    glUniform1i(glGetUniformLocation(r->prog_fog, "u_depth"), 1);
    glUniform1i(glGetUniformLocation(r->prog_fog, "u_mask"), 2);
    glUniform1i(glGetUniformLocation(r->prog_fog, "u_ambient"), 3);
    glUniform1i(glGetUniformLocation(r->prog_fog, "u_flow"), 4);
    glUniform1i(glGetUniformLocation(r->prog_fog, "u_fog_tex"), 5);
    glUniform1i(glGetUniformLocation(r->prog_fog, "u_has_fog_tex"), r->tex_fog_struct ? 1 : 0);
    glUniform2f(glGetUniformLocation(r->prog_fog, "u_tile_size"), 0.0f, 0.0f);
    glUniform1i(glGetUniformLocation(r->prog_fog, "u_ray_slices"), ray_slices);
    glUniform1i(glGetUniformLocation(r->prog_fog, "u_shadow_steps"), shadow_steps);
    glUniform1i(glGetUniformLocation(r->prog_fog, "u_sample_idx"), sample_idx);

    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, r->tex_depth);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, r->tex_amb[1]);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, r->tex_flow[r->flow_active]);
    glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, r->tex_fog_struct ? r->tex_fog_struct : r->tex_photo);

    for (int li = 0; li < app->layer_count; li++) {
        const NebelFogLayer *layer = &app->layers[li];
        if (!layer->params.visible) continue;
        const NebelFogParams *p = &layer->params;

        glBindFramebuffer(GL_FRAMEBUFFER, r->fbo_ping[ping]);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, cur_tex);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, r->tex_masks[li]);

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
        glUniform1f(glGetUniformLocation(r->prog_fog, "u_fluid_flow"), p->fluid_flow);
        glUniform1f(glGetUniformLocation(r->prog_fog, "u_delight_strength"), p->delight_strength);
        glUniform1f(glGetUniformLocation(r->prog_fog, "u_god_rays_hint"), p->god_rays);
        glUniform1i(glGetUniformLocation(r->prog_fog, "u_seed"), (int)(p->noise_seed % 10000u));
        glUniform1i(glGetUniformLocation(r->prog_fog, "u_use_proc"), p->struct_procedural ? 1 : 0);
        glUniform1i(glGetUniformLocation(r->prog_fog, "u_use_tex"), p->struct_texture ? 1 : 0);

        draw_quad(r->prog_fog, 0, 0, (float)w, (float)h, (float)w, (float)h, 0, 1, 1, 0);
        cur_tex = r->tex_ping[ping];
        ping ^= 1;
    }
    return cur_tex;
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
            const char *p = c->text;
            while (*p) {
                uint32_t cp = nebel_utf8_next(&p);
                if (cp < NEBEL_FONT_FIRST_CP || cp >= NEBEL_FONT_FIRST_CP + NEBEL_FONT_GLYPH_COUNT) {
                    if (cp == 0x2013u || cp == 0x2014u) cp = '-';
                    else if (cp == 0x2022u || cp == 0x00B7u) cp = '*';
                    else continue;
                }
                const NebelGlyph *g = &r->font.glyphs[cp - NEBEL_FONT_FIRST_CP];
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
        run_ambient_and_bloom_passes(r);

        int w = r->fbo_w;
        int h = r->fbo_h;

        int budget_samples = 16, budget_slices = 12, budget_shadows = 8, budget_fluid = 10;
        nebel_app_get_render_budget(app, &budget_samples, &budget_slices, &budget_shadows, &budget_fluid, NULL);

        /* Stage 2: Fluid Advection */
        if (app->render_phase == NEBEL_PHASE_FLUID) {
            run_fluid_advection_gpu(r, budget_fluid);
            app->dirty_fluid_gpu = false;
            app->render_phase = NEBEL_PHASE_RENDERING;
            app->render_samples_total = budget_samples;
            app->render_samples_done = 0;
            app->render_progress = 0.45f;
        }

        /* Stage 3: Progressive Multi-Sample Volumetric Lighting */
        if (app->render_phase == NEBEL_PHASE_RENDERING) {
            int batch = (app->platform_tier == NEBEL_TIER_DESKTOP) ? 2 : 1;
            for (int b = 0; b < batch && app->render_samples_done < app->render_samples_total; b++) {
                int sidx = app->render_samples_done;
                GLuint sample_tex = run_fog_layers_sample(r, app, budget_slices, budget_shadows, sidx);

                int dst_acc = r->accum_active ^ 1;
                glBindFramebuffer(GL_FRAMEBUFFER, r->fbo_accum[dst_acc]);
                glViewport(0, 0, w, h);
                glUseProgram(r->prog_accum);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, sidx == 0 ? sample_tex : r->tex_accum[r->accum_active]);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, sample_tex);
                glUniform1i(glGetUniformLocation(r->prog_accum, "u_accum"), 0);
                glUniform1i(glGetUniformLocation(r->prog_accum, "u_sample"), 1);
                glUniform1f(glGetUniformLocation(r->prog_accum, "u_weight"),
                            sidx == 0 ? 1.0f : (1.0f / (float)(sidx + 1)));
                draw_quad(r->prog_accum, 0, 0, (float)w, (float)h, (float)w, (float)h, 0, 1, 1, 0);

                r->accum_active = dst_acc;
                r->has_valid_render = true;
                app->render_samples_done++;
            }

            float frac = app->render_samples_total > 0
                ? (float)app->render_samples_done / (float)app->render_samples_total : 1.0f;
            app->render_progress = 0.45f + 0.55f * frac;

            if (app->render_samples_done >= app->render_samples_total) {
                app->render_phase = NEBEL_PHASE_DONE;
                app->render_dirty = false;
                app->render_progress = 1.0f;
                app->show_mask_overlay = false;
                nebel_app_queue_audio(app, "assets/sounds/ui_click.wav", 0.35f);
            }
        }

        /* Aggregate active post-process parameters */
        float max_bloom = 0.0f, max_grain = 0.0f, max_god_rays = 0.0f;
        float rays_sun_x = 0.72f, rays_sun_y = 0.22f;
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
        }

        GLuint display_fog_tex = r->has_valid_render ? r->tex_accum[r->accum_active] : r->tex_photo;
        int active_li = (app->active_layer >= 0 && app->active_layer < app->layer_count) ? app->active_layer : 0;
        float mask_dmin = app->layer_count > 0 ? app->layers[active_li].params.depth_min : 0.35f;
        float mask_dmax = app->layer_count > 0 ? app->layers[active_li].params.depth_max : 1.00f;

        /* Final composite to default framebuffer */
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.055f, 0.062f, 0.074f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        float cx, cy, cw, ch;
        nebel_app_canvas_rect(app, vw, vh, &cx, &cy, &cw, &ch);
        glUseProgram(r->prog_comp);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, r->tex_photo);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, display_fog_tex);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, r->tex_bloom[1]);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, r->tex_depth);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, r->tex_masks[active_li]);
        glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, r->tex_protect);

        glUniform1i(glGetUniformLocation(r->prog_comp, "u_orig"), 0);
        glUniform1i(glGetUniformLocation(r->prog_comp, "u_fog"), 1);
        glUniform1i(glGetUniformLocation(r->prog_comp, "u_bloom"), 2);
        glUniform1i(glGetUniformLocation(r->prog_comp, "u_depth"), 3);
        glUniform1i(glGetUniformLocation(r->prog_comp, "u_mask_vis"), 4);
        glUniform1i(glGetUniformLocation(r->prog_comp, "u_prot_vis"), 5);

        glUniform1f(glGetUniformLocation(r->prog_comp, "u_bloom_amount"), r->has_valid_render ? max_bloom : 0.0f);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_grain_sigma"), r->has_valid_render ? (app->sensor_noise * max_grain) : 0.0f);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_split_enabled"), app->split_enabled ? 1.0f : 0.0f);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_split_pos"), app->split_pos);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_show_depth"), app->show_depth ? 1.0f : 0.0f);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_show_mask"), app->show_mask_overlay ? 1.0f : 0.0f);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_show_zone"), app->show_paintable_zone ? 1.0f : 0.0f);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_mask_depth_min"), mask_dmin);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_mask_depth_max"), mask_dmax);
        glUniform2f(glGetUniformLocation(r->prog_comp, "u_sun_pos"), rays_sun_x, rays_sun_y);
        glUniform1f(glGetUniformLocation(r->prog_comp, "u_god_rays"), r->has_valid_render ? max_god_rays : 0.0f);
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

#if !defined(__EMSCRIPTEN__)
static float clampf_r(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
#endif

bool nebel_renderer_render_full(NebelRenderer *r, NebelAppState *app, NebelImage *out) {
#if defined(__EMSCRIPTEN__)
    (void)r;
    return nebel_app_render_full(app, out);
#else
    /* Desktop GPU tiled full-resolution fog pass (1024x1024 GL_RGBA32F tiles -> 100% GPU speed) */
    if (!r || !r->prog_fog || !r->use_half_float || !app || !app->orig_img.rgba || !out) {
        return nebel_app_render_full(app, out);
    }

    int fw = app->orig_img.width;
    int fh = app->orig_img.height;
    if (fw <= 0 || fh <= 0) return false;

    /* For photos <= work_img size where bloom/godrays/grain are fast on CPU, CPU takes <0.3s;
       use GPU tiled rendering for larger DSLR photos (> work_img) OR Ultra quality so Desktop GPU
       handles the heavy multi-slice shadow-ray math at 50x CPU speed! */
    int samples = 16, slices = 16, sh_steps = 12, fluid_steps = 20;
    nebel_app_get_render_budget(app, &samples, &slices, &sh_steps, &fluid_steps, NULL);

    sync_gpu_textures(r, app);
    run_ambient_and_bloom_passes(r);
    if (app->dirty_fluid_gpu) {
        run_fluid_advection_gpu(r, fluid_steps);
        app->dirty_fluid_gpu = false;
    }

    if (!nebel_image_alloc(out, fw, fh)) return false;

    const int TILE = 1024;
    GLuint t_orig = make_tex(GL_LINEAR);
    GLuint t_fbos[2] = {0, 0}, t_texs[2] = {0, 0};
    if (!alloc_fbo_pair(t_fbos, t_texs, TILE, TILE, GL_RGBA32F, GL_FLOAT)) {
        glDeleteTextures(1, &t_orig);
        destroy_fbo_pair(t_fbos, t_texs);
        nebel_image_free(out);
        return nebel_app_render_full(app, out);
    }

    float *tile_in  = (float *)malloc((size_t)TILE * (size_t)TILE * 4u * sizeof(float));
    float *tile_out = (float *)malloc((size_t)TILE * (size_t)TILE * 4u * sizeof(float));
    float *fog_a_full = (float *)calloc((size_t)fw * (size_t)fh, sizeof(float));
    if (!tile_in || !tile_out || !fog_a_full) {
        free(tile_in); free(tile_out); free(fog_a_full);
        glDeleteTextures(1, &t_orig);
        destroy_fbo_pair(t_fbos, t_texs);
        nebel_image_free(out);
        return nebel_app_render_full(app, out);
    }

    glBindVertexArray(r->vao);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    for (int ty = 0; ty < fh; ty += TILE) {
        int th = (ty + TILE <= fh) ? TILE : (fh - ty);
        for (int tx = 0; tx < fw; tx += TILE) {
            int tw = (tx + TILE <= fw) ? TILE : (fw - tx);

            /* Pack tile pixels with alpha=0 */
            for (int y = 0; y < th; y++) {
                const float *src_row = &app->orig_img.rgba[((size_t)(ty + y) * (size_t)fw + (size_t)tx) * 4u];
                float *dst_row = &tile_in[(size_t)y * (size_t)tw * 4u];
                for (int x = 0; x < tw; x++) {
                    dst_row[x * 4 + 0] = src_row[x * 4 + 0];
                    dst_row[x * 4 + 1] = src_row[x * 4 + 1];
                    dst_row[x * 4 + 2] = src_row[x * 4 + 2];
                    dst_row[x * 4 + 3] = 0.0f;
                }
            }
            glBindTexture(GL_TEXTURE_2D, t_orig);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, tw, th, 0, GL_RGBA, GL_FLOAT, tile_in);

            float u0 = (float)tx / (float)fw;
            float v0 = (float)ty / (float)fh;
            float u1 = (float)(tx + tw) / (float)fw;
            float v1 = (float)(ty + th) / (float)fh;

            GLuint cur_tex = t_orig;
            int ping = 0;

            glUseProgram(r->prog_fog);
            glViewport(0, 0, tw, th);
            glUniform1i(glGetUniformLocation(r->prog_fog, "u_prev"), 0);
            glUniform1i(glGetUniformLocation(r->prog_fog, "u_depth"), 1);
            glUniform1i(glGetUniformLocation(r->prog_fog, "u_mask"), 2);
            glUniform1i(glGetUniformLocation(r->prog_fog, "u_ambient"), 3);
            glUniform1i(glGetUniformLocation(r->prog_fog, "u_flow"), 4);
            glUniform1i(glGetUniformLocation(r->prog_fog, "u_fog_tex"), 5);
            glUniform1i(glGetUniformLocation(r->prog_fog, "u_has_fog_tex"), r->tex_fog_struct ? 1 : 0);
            glUniform2f(glGetUniformLocation(r->prog_fog, "u_tile_size"), (float)tw, (float)th);
            glUniform1i(glGetUniformLocation(r->prog_fog, "u_ray_slices"), slices);
            glUniform1i(glGetUniformLocation(r->prog_fog, "u_shadow_steps"), sh_steps);
            glUniform1i(glGetUniformLocation(r->prog_fog, "u_sample_idx"), 0);

            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, r->tex_depth);
            glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, r->tex_amb[1]);
            glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, r->tex_flow[r->flow_active]);
            glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, r->tex_fog_struct ? r->tex_fog_struct : r->tex_photo);

            for (int li = 0; li < app->layer_count; li++) {
                const NebelFogLayer *layer = &app->layers[li];
                if (!layer->params.visible) continue;
                const NebelFogParams *p = &layer->params;

                glBindFramebuffer(GL_FRAMEBUFFER, t_fbos[ping]);
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, cur_tex);
                glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, r->tex_masks[li]);

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
                glUniform1f(glGetUniformLocation(r->prog_fog, "u_fluid_flow"), p->fluid_flow);
                glUniform1f(glGetUniformLocation(r->prog_fog, "u_delight_strength"), p->delight_strength);
                glUniform1f(glGetUniformLocation(r->prog_fog, "u_god_rays_hint"), p->god_rays);
                glUniform1i(glGetUniformLocation(r->prog_fog, "u_seed"), (int)(p->noise_seed % 10000u));
                glUniform1i(glGetUniformLocation(r->prog_fog, "u_use_proc"), p->struct_procedural ? 1 : 0);
                glUniform1i(glGetUniformLocation(r->prog_fog, "u_use_tex"), p->struct_texture ? 1 : 0);

                draw_quad(r->prog_fog, 0, 0, (float)tw, (float)th, (float)tw, (float)th, u0, v1, u1, v0);
                cur_tex = t_texs[ping];
                ping ^= 1;
            }

            if (cur_tex == t_orig) {
                /* No visible layers: copy orig pixels */
                for (int y = 0; y < th; y++) {
                    memcpy(&out->rgba[((size_t)(ty + y) * (size_t)fw + (size_t)tx) * 4u],
                           &app->orig_img.rgba[((size_t)(ty + y) * (size_t)fw + (size_t)tx) * 4u],
                           (size_t)tw * 4u * sizeof(float));
                }
            } else {
                glBindFramebuffer(GL_FRAMEBUFFER, t_fbos[ping ^ 1]);
                glPixelStorei(GL_PACK_ALIGNMENT, 4);
                glReadPixels(0, 0, tw, th, GL_RGBA, GL_FLOAT, tile_out);
                for (int y = 0; y < th; y++) {
                    const float *srow = &tile_out[(size_t)y * (size_t)tw * 4u];
                    float *drow = &out->rgba[((size_t)(ty + y) * (size_t)fw + (size_t)tx) * 4u];
                    float *arow = &fog_a_full[(size_t)(ty + y) * (size_t)fw + (size_t)tx];
                    for (int x = 0; x < tw; x++) {
                        drow[x * 4 + 0] = srow[x * 4 + 0];
                        drow[x * 4 + 1] = srow[x * 4 + 1];
                        drow[x * 4 + 2] = srow[x * 4 + 2];
                        drow[x * 4 + 3] = 1.0f;
                        arow[x] = clampf_r(srow[x * 4 + 3], 0.0f, 1.0f);
                    }
                }
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glDeleteTextures(1, &t_orig);
    destroy_fbo_pair(t_fbos, t_texs);
    free(tile_in);
    free(tile_out);

    /* Apply lightweight GPU-matched bloom + god rays + sensor grain over full-res linear result */
    float max_bloom = 0.0f, max_grain = 0.0f, max_god_rays = 0.0f;
    float rays_sun_x = 0.72f, rays_sun_y = 0.22f;
    for (int li = 0; li < app->layer_count; li++) {
        if (!app->layers[li].params.visible) continue;
        const NebelFogParams *p = &app->layers[li].params;
        if (p->bloom_amount > max_bloom) max_bloom = p->bloom_amount;
        if (p->grain_match > max_grain) max_grain = p->grain_match;
        if (p->god_rays > max_god_rays) {
            max_god_rays = p->god_rays;
            rays_sun_x = p->sun_x;
            rays_sun_y = p->sun_y;
        }
    }

    /* Read half-res GPU bloom texture once for exact screen-matched depth-weighted optical PSF */
    int bw = r->fbo_w / 2 > 1 ? r->fbo_w / 2 : 1;
    int bh = r->fbo_h / 2 > 1 ? r->fbo_h / 2 : 1;
    float *bloom_px = NULL;
    if (max_bloom > 1e-3f) {
        bloom_px = (float *)malloc((size_t)bw * (size_t)bh * 4u * sizeof(float));
        if (bloom_px) {
            glBindFramebuffer(GL_FRAMEBUFFER, r->fbo_bloom[1]);
            glReadPixels(0, 0, bw, bh, GL_RGBA, GL_FLOAT, bloom_px);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

    int dw = app->work_img.width, dh = app->work_img.height;
    float sigma = app->sensor_noise * max_grain;
    for (int y = 0; y < fh; y++) {
        float v = fh > 1 ? (float)y / (float)(fh - 1) : 0.0f;
        int dy_i = (int)(v * (float)(dh - 1) + 0.5f);
        int by_i = (int)(v * (float)(bh - 1) + 0.5f);
        for (int x = 0; x < fw; x++) {
            float u = fw > 1 ? (float)x / (float)(fw - 1) : 0.0f;
            size_t idx = (size_t)y * (size_t)fw + (size_t)x;
            float fa = fog_a_full[idx];
            float *px = &out->rgba[idx * 4u];
            int dx_i = (int)(u * (float)(dw - 1) + 0.5f);
            float d_here = app->depth_map ? app->depth_map[(size_t)dy_i * (size_t)dw + (size_t)dx_i] : 0.7f;

            if (bloom_px && fa > 1e-4f) {
                int bx_i = (int)(u * (float)(bw - 1) + 0.5f);
                const float *bl = &bloom_px[((size_t)by_i * (size_t)bw + (size_t)bx_i) * 4u];
                float t_d = clampf_r((d_here - 0.20f) / 0.40f, 0.0f, 1.0f);
                float d_psf = 0.68f + 0.32f * (t_d * t_d * (3.0f - 2.0f * t_d));
                float gain = fa * max_bloom * d_psf * 0.75f;
                px[0] += bl[0] * gain;
                px[1] += bl[1] * gain;
                px[2] += bl[2] * gain;
            }

            if (max_god_rays > 1e-3f && fa > 1e-3f) {
                float t_fg = clampf_r((d_here - 0.18f) / 0.28f, 0.0f, 1.0f);
                float fg_vis = t_fg * t_fg * (3.0f - 2.0f * t_fg);
                if (fg_vis > 1e-3f) {
                    uint32_t h1 = (uint32_t)idx * 2654435761u;
                    h1 ^= h1 >> 16; h1 *= 0x7feb352du; h1 ^= h1 >> 15;
                    float jitter = (float)(h1 & 0xFFu) / 255.0f;
                    float du = (rays_sun_x - u) * 0.85f / 16.0f;
                    float dv = (rays_sun_y - v) * 0.85f / 16.0f;
                    float su = u + du * jitter, sv = v + dv * jitter;
                    float decay = 1.0f, shaft = 0.0f;
                    for (int s = 0; s < 16; s++) {
                        su += du; sv += dv;
                        if (su < 0.0f || su > 1.0f || sv < 0.0f || sv > 1.0f) break;
                        int sx = (int)(su * (float)(dw - 1) + 0.5f);
                        int sy = (int)(sv * (float)(dh - 1) + 0.5f);
                        float sd = app->depth_map ? app->depth_map[(size_t)sy * (size_t)dw + (size_t)sx] : 0.7f;
                        const float *wp = &app->work_img.rgba[((size_t)sy * (size_t)dw + (size_t)sx) * 4u];
                        float wl = clampf_r(0.2126f * wp[0] + 0.7152f * wp[1] + 0.0722f * wp[2], 0.0f, 1.0f);
                        float to = clampf_r((sd - 0.38f) / 0.30f, 0.0f, 1.0f);
                        float tb = clampf_r((sqrtf(wl) - 0.28f) / 0.60f, 0.0f, 1.0f);
                        shaft += (to * to * (3.0f - 2.0f * to)) * (tb * tb * (3.0f - 2.0f * tb)) * decay;
                        decay *= 0.90f;
                    }
                    float tf = clampf_r(fa / 0.25f, 0.0f, 1.0f);
                    float fog_w = (tf * tf * (3.0f - 2.0f * tf)) * (0.30f + 0.70f * fa);
                    float g_ray = (shaft / 16.0f) * max_god_rays * fog_w * fg_vis * 0.65f;
                    px[0] += g_ray * 1.18f;
                    px[1] += g_ray * 0.98f;
                    px[2] += g_ray * 0.70f;
                }
            }

            if (sigma > 1e-5f && fa > 1e-3f) {
                uint32_t ha = (uint32_t)idx * 747796405u + 2891336453u;
                ha ^= ha >> 16; ha *= 0x7feb352du; ha ^= ha >> 15;
                uint32_t hb = (uint32_t)idx * 277803737u + 1013904223u;
                hb ^= hb >> 16; hb *= 0x7feb352du; hb ^= hb >> 15;
                float g = ((float)(ha & 0xFFFFu) / 65535.0f + (float)(hb & 0xFFFFu) / 65535.0f - 1.0f) * sigma * fa;
                px[0] += g; px[1] += g; px[2] += g;
            }
            px[0] = clampf_r(px[0], 0.0f, 1.0f);
            px[1] = clampf_r(px[1], 0.0f, 1.0f);
            px[2] = clampf_r(px[2], 0.0f, 1.0f);
            px[3] = 1.0f;
        }
    }

    free(bloom_px);
    free(fog_a_full);
    return true;
#endif
}

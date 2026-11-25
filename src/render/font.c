#include "render/font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

bool nebel_font_load(NebelFont *font, const char *ttf_path, float pixel_height) {
    if (!font || !ttf_path) return false;
    memset(font, 0, sizeof(*font));

    FILE *f = fopen(ttf_path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 16 * 1024 * 1024) {
        fclose(f);
        return false;
    }
    unsigned char *ttf = (unsigned char *)malloc((size_t)sz);
    if (!ttf) { fclose(f); return false; }
    if (fread(ttf, 1, (size_t)sz, f) != (size_t)sz) {
        free(ttf); fclose(f); return false;
    }
    fclose(f);

    const int aw = 512, ah = 512;
    unsigned char *bitmap = (unsigned char *)calloc((size_t)aw * (size_t)ah, 1u);
    stbtt_bakedchar baked[96];
    if (!bitmap) { free(ttf); return false; }

    int res = stbtt_BakeFontBitmap(ttf, 0, pixel_height, bitmap, aw, ah, 32, 96, baked);
    free(ttf);
    if (res <= 0) {
        free(bitmap);
        return false;
    }

    for (int i = 0; i < 96; i++) {
        NebelGlyph *g = &font->glyphs[i];
        g->u0 = (float)baked[i].x0 / (float)aw;
        g->v0 = (float)baked[i].y0 / (float)ah;
        g->u1 = (float)baked[i].x1 / (float)aw;
        g->v1 = (float)baked[i].y1 / (float)ah;
        g->x0 = baked[i].xoff;
        g->y0 = baked[i].yoff + pixel_height * 0.8f;
        g->x1 = g->x0 + (float)(baked[i].x1 - baked[i].x0);
        g->y1 = g->y0 + (float)(baked[i].y1 - baked[i].y0);
        g->xadvance = baked[i].xadvance;
    }

    glGenTextures(1, &font->tex);
    glBindTexture(GL_TEXTURE_2D, font->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, aw, ah, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap);
    free(bitmap);

    font->atlas_w = aw;
    font->atlas_h = ah;
    font->pixel_height = pixel_height;
    font->loaded = true;
    return true;
}

void nebel_font_free(NebelFont *font) {
    if (!font) return;
    if (font->tex) {
        glDeleteTextures(1, &font->tex);
        font->tex = 0;
    }
    font->loaded = false;
}

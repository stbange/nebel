#include "render/font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

uint32_t nebel_utf8_next(const char **p) {
    if (!p || !*p || !**p) return 0u;
    const unsigned char *s = (const unsigned char *)(*p);
    uint32_t cp = 0u;
    if ((s[0] & 0x80u) == 0u) {
        cp = s[0];
        *p += 1;
    } else if ((s[0] & 0xE0u) == 0xC0u && (s[1] & 0xC0u) == 0x80u) {
        cp = ((uint32_t)(s[0] & 0x1Fu) << 6) | (uint32_t)(s[1] & 0x3Fu);
        *p += 2;
    } else if ((s[0] & 0xF0u) == 0xE0u && (s[1] & 0xC0u) == 0x80u && (s[2] & 0xC0u) == 0x80u) {
        cp = ((uint32_t)(s[0] & 0x0Fu) << 12) | ((uint32_t)(s[1] & 0x3Fu) << 6) | (uint32_t)(s[2] & 0x3Fu);
        *p += 3;
    } else if ((s[0] & 0xF8u) == 0xF0u && (s[1] & 0xC0u) == 0x80u &&
               (s[2] & 0xC0u) == 0x80u && (s[3] & 0xC0u) == 0x80u) {
        cp = ((uint32_t)(s[0] & 0x07u) << 18) | ((uint32_t)(s[1] & 0x3Fu) << 12) |
             ((uint32_t)(s[2] & 0x3Fu) << 6)  | (uint32_t)(s[3] & 0x3Fu);
        *p += 4;
    } else {
        cp = '?';
        *p += 1;
    }
    return cp;
}

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
    stbtt_bakedchar *baked = (stbtt_bakedchar *)calloc(NEBEL_FONT_GLYPH_COUNT, sizeof(stbtt_bakedchar));
    if (!bitmap || !baked) {
        free(ttf); free(bitmap); free(baked);
        return false;
    }

    int res = stbtt_BakeFontBitmap(ttf, 0, pixel_height, bitmap, aw, ah,
                                   (int)NEBEL_FONT_FIRST_CP, (int)NEBEL_FONT_GLYPH_COUNT, baked);
    free(ttf);
    if (res <= 0) {
        free(bitmap); free(baked);
        return false;
    }

    for (uint32_t i = 0; i < NEBEL_FONT_GLYPH_COUNT; i++) {
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
    free(baked);

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

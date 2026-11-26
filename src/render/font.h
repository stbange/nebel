#ifndef NEBEL_RENDER_FONT_H
#define NEBEL_RENDER_FONT_H

#include "render/gl_api.h"
#include <stdbool.h>
#include <stdint.h>

/* Contiguous U+0020..U+017F covers ASCII + Latin-1 (German äöüß) + Latin Ext-A (Polish ąćęłńóśźż) */
#define NEBEL_FONT_FIRST_CP    32u
#define NEBEL_FONT_GLYPH_COUNT 352u

typedef struct {
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
    float xadvance;
} NebelGlyph;

typedef struct {
    GLuint tex;
    int atlas_w;
    int atlas_h;
    float pixel_height;
    NebelGlyph glyphs[NEBEL_FONT_GLYPH_COUNT];
    bool loaded;
} NebelFont;

bool nebel_font_load(NebelFont *font, const char *ttf_path, float pixel_height);
void nebel_font_free(NebelFont *font);
uint32_t nebel_utf8_next(const char **p);

#endif /* NEBEL_RENDER_FONT_H */

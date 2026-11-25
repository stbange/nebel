#ifndef NEBEL_RENDER_FONT_H
#define NEBEL_RENDER_FONT_H

#include "render/gl_api.h"
#include <stdbool.h>

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
    NebelGlyph glyphs[96]; /* ASCII 32..127 */
    bool loaded;
} NebelFont;

bool nebel_font_load(NebelFont *font, const char *ttf_path, float pixel_height);
void nebel_font_free(NebelFont *font);

#endif /* NEBEL_RENDER_FONT_H */

#include "render/gl_api.h"

#if !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
#include <SDL3/SDL.h>

#define NEBEL_GL_DEF(type, name) type nebel_##name = NULL;
NEBEL_GL_FUNCS(NEBEL_GL_DEF)
#undef NEBEL_GL_DEF

bool nebel_gl_load(void) {
    bool ok = true;
#define NEBEL_GL_LOAD(type, name) \
    nebel_##name = (type)SDL_GL_GetProcAddress(#name); \
    if (!nebel_##name) ok = false;
    NEBEL_GL_FUNCS(NEBEL_GL_LOAD)
#undef NEBEL_GL_LOAD
    return ok;
}
#else
bool nebel_gl_load(void) {
    return true;
}
#endif

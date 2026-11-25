#ifndef NEBEL_SCRIPT_LUA_BRIDGE_H
#define NEBEL_SCRIPT_LUA_BRIDGE_H

#include "core/app_state.h"
#include <stdbool.h>

typedef struct lua_State lua_State;

typedef struct {
    lua_State *L;
    NebelAppState *app;
    char last_error[256];
} NebelLuaBridge;

bool nebel_lua_init(NebelLuaBridge *bridge, NebelAppState *app);
void nebel_lua_free(NebelLuaBridge *bridge);

bool nebel_lua_load_file(NebelLuaBridge *bridge, const char *rel_path);
bool nebel_lua_reload(NebelLuaBridge *bridge);
bool nebel_lua_eval(NebelLuaBridge *bridge, const char *code);

bool nebel_lua_on_ui(NebelLuaBridge *bridge, int vw, int vh);
bool nebel_lua_on_key(NebelLuaBridge *bridge, const char *key_name, bool ctrl, bool shift);
bool nebel_lua_apply_preset(NebelLuaBridge *bridge, const char *preset_name);

#endif /* NEBEL_SCRIPT_LUA_BRIDGE_H */

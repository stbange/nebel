# Tasks: Nebel v2 Photographic Realism & Pro Export

## 1. Crepuscular Sunbeams (God Rays)
- [x] Add `god_rays` parameter to `NebelFogParams`, `app_state`, `fog_layer.c`, `composite.frag`, `renderer.c`, `presets.lua`, `pipeline.lua`
- [x] Add unit & E2E checks verifying radial light shafts brighten fogged areas toward sun

## 2. Auto-Anchored Brush Depth
- [x] Add `auto_depth_brush` to `NebelAppState` with 5x5 local depth sampling on fog stroke begin + auto-pause on manual `depth_min` edit
- [x] Expose `nebel.brush.get_auto_depth()` / `set_auto_depth(bool)` in Lua, `AutoLock` UI toggle, and `A` hotkey

## 3. Shadow / Trough Pooling Bias
- [x] Add `shadow_pool` parameter to `NebelFogParams`, `app_state`, `fog_layer.frag`, `fog_layer.c`, `presets.lua`, `pipeline.lua`
- [x] Add unit test verifying stronger fog optical depth in shadowed hollows vs bright surfaces

## 4. 16-Bit PNG & TIFF Export + Desktop ONNX Runtime
- [x] Implement pure-C 16-bit RGBA PNG and 16-bit RGB TIFF encoders (`image.h/.c`), `app_state`, `lua_bridge`, `file_dialog.c`, `ui.lua`
- [x] Implement dynamic `SDL_LoadObject` ONNX Runtime C API dispatch in `src/platform/depth_onnx.c` using `third_party/onnxruntime_c_api.h`
- [x] Verify 16-bit PNG & TIFF with PIL, test real Linux Desktop ONNX inference, confirm >=95% line coverage, archive OpenSpec, push `main`, verify CI & Pages

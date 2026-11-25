# Tasks: Nebel v1 Studio

## Phase 1: Bootstrap OpenSpec & Build System
- [x] Configure `openspec/` specs & validate with `openspec validate nebel-v1-studio`
- [x] Add `README.md` (Pages link at top), `LICENSE-ART` (CC BY 4.0), `.gitignore`, vendored `stb_*` headers
- [x] Create unified `CMakeLists.txt` for Desktop (Win/Linux/Mac), Emscripten Web, Unit/E2E tests, and coverage

## Phase 2: Pure C11 Core & CV Engine (`src/core/`)
- [x] Implement `image.h/.c` (linear float RGBA, sRGB conversion, `stb_image`/`stb_image_write`, sensor grain estimator)
- [x] Implement `fog_layer.h/.c` (multi-layer state, Koschmieder + height + perspective fBm + Henyey-Greenstein + bloom + grain CPU reference)
- [x] Implement `brush.h/.c` (soft marker, eraser, foreground-protect, interpolated polyline stamping)
- [x] Implement `cv_depth.h/.c` (horizon, defocus, Dark Channel Prior, O(1) Guided Filter matting)
- [x] Implement `undo.h/.c`, `touch.h/.c`, `ui_state.h/.c`, `manifest.h/.c`, `app_state.h/.c`
- [x] Write unit tests achieving >= 95% line coverage across `src/core/`

## Phase 3: Lua 5.4 Runtime & External Scripts (`src/script/`, `scripts/`)
- [x] Implement `src/script/lua_bridge.h/.c` exposing `nebel` API + live hot-reload + error isolation
- [x] Author external `scripts/{main,ui,tools,presets,pipeline}.lua`
- [x] Add Lua bridge unit tests keeping overall core+script coverage >= 95%

## Phase 4: SDL3 + OpenGL 3.3 / WebGL 2.0 Renderer, Audio & CC BY 4.0 Assets
- [x] Implement `src/render/{gl_api,renderer,font}.h/.c` and external `assets/shaders/*.vert|frag`
- [x] Implement `src/platform/{audio,file_dialog,depth_onnx}.h/.c` and `src/main.c`
- [x] Generate CC BY 4.0 icons, subtle SFX WAVs, sample landscape/wildlife photos, font, and `assets/manifest.json`

## Phase 5: Emscripten Web Build, Mobile Touch & Optional ONNX Depth
- [x] Author `web/index.html` and `web/nebel_web.js` (loose-asset MEMFS preloader, file picker/download, lazy `onnxruntime-web` Depth Anything V2 Small bridge)
- [x] Compile and verify Emscripten Web build locally with loose `assets/` and `scripts/`

## Phase 6: Automated E2E Simulation Test, CI & GitHub Pages
- [x] Write `tests/e2e/test_app_simulation.c` exercising mouse, touch, Lua presets, multi-layer fog, foreground protection, undo/redo, and PNG export under ASan/UBSan
- [x] Add `.github/workflows/ci-and-pages.yml` (coverage gate + Linux/Win/Mac/Web builds + `actions/deploy-pages`)
- [x] Archive OpenSpec change into `openspec/specs/`, push `main`, verify CI green & live GitHub Pages URL

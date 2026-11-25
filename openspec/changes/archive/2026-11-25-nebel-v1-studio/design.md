# Design: Nebel v1 Studio

## Architecture
- `src/core/`: Pure C11 modules with zero SDL/OpenGL dependencies (`image`, `fog_layer`, `brush`, `cv_depth`, `undo`, `touch`, `ui_state`, `manifest`, `app_state`). Directly unit-tested and used for exact full-resolution export and deterministic headless simulation tests.
- `src/script/`: Embedded Lua 5.4 VM (`lua_bridge`) loading external `scripts/*.lua` files and exposing a minimal `nebel` API (`layers`, `brush`, `view`, `io`, `ui`, `audio`).
- `src/render/`: Unified OpenGL 3.3 Core (via SDL3 GL proc loader) and OpenGL ES 3.0 / WebGL 2.0 (Emscripten) FBO renderer loading loose GLSL shaders from `assets/shaders/`, plus `stb_truetype` font rendering and 2D darkroom UI quad batching.
- `src/platform/`: Thin OS/Web adapters (`main.c`, SDL3 audio stream SFX, native SDL3 / Web JS file dialogs, optional ONNX depth bridge with classical fallback).

## Loose Assets on Desktop and Web
- Desktop reads `./assets/` and `./scripts/` relative to the executable working directory.
- Emscripten web build hosts loose `assets/` and `scripts/` beside `nebel.wasm` and pre-fetches them into MEMFS at startup using `assets/manifest.json`—never baking assets into `.wasm`.

## Key Tradeoffs
- Dual CPU+GPU fog equation: live 60 FPS interaction runs on GPU FBOs (capped at 2048px working texture), while full-res DSLR export and headless CI tests execute the matching float32 CPU pipeline in `src/core/fog_layer.c` without GPU size limits.
- Optional ONNX depth: lazy-loaded on demand (`onnxruntime-web` on Web; dynamic `SDL_LoadObject` on desktop) to avoid committing 25 MB weights to git or breaking zero-dependency native builds.

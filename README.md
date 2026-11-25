# Nebel

**Live Web App:** https://stbange.github.io/nebel/

**Nebel** is a lightweight, cross-platform darkroom studio for placing physically realistic atmospheric fog onto landscape and wildlife photographs. Built in **C11 + SDL3 + OpenGL 3.3 / WebGL 2.0** with external **Lua 5.4** scripts and loose external assets.

## Features

- **Volumetric linear-light fog physics:** 5-slice fixed-plane ray-marching with Koschmieder scattering, exponential height/valley falloff, stratified billowy wisps, shadow/trough pooling bias (`Shadow Pool`), depth-occluded crepuscular sunbeams (`God Rays`), scene-ambient illumination coupling, Henyey-Greenstein sun forward-scattering, veiling glare bloom, and sensor grain matching.
- **Wildlife & foliage edge matting:** Built-in O(1) Guided Filter + classical depth cues + browser (`onnxruntime-web`) & native desktop (`libonnxruntime` + `assets/models/depth_anything_v2_vits.onnx` or `model_quantized.onnx`) **Depth Anything V2 Small** 3D depth with automatic classical fallback and `AutoLock` (`A`) click-to-anchor brush depth.
- **Photographer workflow:** Multiple independent fog layers, transparent Fog Marker, Eraser, Foreground-Protect brush, Sun-direction picker, Depth Map overlay (`D`), Before/After split comparison (`Space`), Undo/Redo, Lua-scripted presets, and full-resolution **8-bit PNG, JPEG, 16-bit lossless PNG, and 16-bit print TIFF** export.
- **Adaptive Web & Mobile Touch:** Runs on Windows, Linux, macOS, and Web (Emscripten/WebGL2) with automatic touch controls (1-finger paint, 2-finger pinch-zoom/pan, 2-finger tap undo, collapsible mobile drawer).
- **Extendable by design:** Shaders (`assets/shaders/`), icons, audio, sample photos (`assets/`), and application logic/UI/presets (`scripts/*.lua`) live beside the binary—never baked into the executable or `.wasm`.

## Building

### Native (Linux / macOS / Windows)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/nebel
```

### Unit Tests, Coverage (≥ 95%) & Automated Simulation Test
```bash
cmake -B build-test -DCMAKE_BUILD_TYPE=Debug -DNEBEL_COVERAGE=ON -DNEBEL_SANITIZERS=ON
cmake --build build-test --parallel
ctest --test-dir build-test --output-on-failure
gcovr --root src --filter 'src/(core|script)/' --fail-under-line 95 build-test
```

### Web (Emscripten)
```bash
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --parallel
# Serve build-web/dist/ via any static HTTP server (loose assets/ and scripts/ sit beside nebel.wasm)
```

## Extending Nebel

All editable files sit right beside the binary (and beside `nebel.wasm` in the web build):
- `scripts/presets.lua` — add or tweak fog presets
- `scripts/ui.lua` — customize desktop sidebar and mobile touch drawer layouts
- `scripts/tools.lua` — adjust brush curves, hotkeys, and gesture behavior
- `scripts/pipeline.lua` — tune multi-layer defaults and render parameters
- `assets/shaders/*.frag` — customize fog scattering, bloom, or tone mapping (press `F5` to reload live)

## Development Process

- **OpenSpec:** Living specifications and change records live in [`openspec/`](openspec/).
- **Ponytail:** Smallest complete diffs, minimal dependencies, strict validation at trust boundaries.

## Licenses

- **Code (`src/`, `scripts/`, `web/`, `tests/`, build files):** [MIT License](LICENSE)
- **Art & Media Assets (`assets/icons/`, `assets/sounds/`, `assets/samples/`):** [CC BY 4.0](LICENSE-ART) (see `LICENSE-ART` for attributions and third-party font licensing)

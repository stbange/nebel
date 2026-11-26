# Nebel

**Live Web App:** https://stbange.github.io/nebel/  
**Desktop Downloads (Windows / macOS / Linux):** https://github.com/stbange/nebel/releases  
**Online Help (English / Deutsch / Polski):** https://stbange.github.io/nebel/help.html

**Nebel** is a cross-platform atmospheric fog darkroom studio for landscape and wildlife photography. Built in **C11 + SDL3 + OpenGL 3.3 / WebGL 2.0** with external **Lua 5.4** scripts and loose external assets — zero generative AI pixel hallucination.

## Highlights (v3 Tiered Studio)

- **3-Tier Hardware Architecture:** Automatically adapts between **Native Desktop GPU** (Windows/macOS/Linux), **Web Desktop** (WebGPU + WebGL 2.0), and **Web Mobile** (touch-optimized, RAM-safe).
- **Explicit `Mark → Render` Darkroom Workflow:** Paint fog zones (`B`) and protect foreground details (`P`) instantly with a translucent depth-aware marker overlay, then click **`Render`** (`Enter`) to run multi-stage GPU volumetric calculations with live progressive convergence and `Cancel` (`Esc`).
- **Studio Volumetric Fog Physics:** Terrain-guided valley fluid advection (`Valley Flow`), multi-slice volumetric ray-marching with **3D heightfield shadow cones**, **multi-octave Mie multiple scattering**, **intrinsic shadow softening** under thick fog, shadow/trough pooling, crepuscular god rays, depth-weighted optical halation (PSF), and sensor grain matching.
- **High-Detail Apache-2.0 Depth Pipeline:** Aspect-preserving bilinear inference, multi-scale **PatchFusion tile stitching** (3×3 on Desktop Ultra, 2×2 on Web Desktop), full **3-channel RGB covariance Guided Filter**, 32-bit/16-bit float GPU depth textures, GPU ONNX providers (**DirectML / CoreML / CUDA / WebGPU**), optional **`DA3Mono-Large`** (Apache 2.0) support, `AutoLock` (`A`), and manual **Depth Touch-Up** sculpting (`N` / `F` in Depth View `D`).
- **Print-Grade Full-Resolution Export:** Desktop GPU-tiled & CPU-mirrored **16-bit lossless RGB TIFF**, **16-bit RGBA PNG**, 8-bit PNG, and JPEG up to 45+ MP.
- **Multi-Language, Tooltips, Tutorial & Help:** Instant **English (`EN`) / Deutsch (`DE`) / Polski (`PL`)** switching (full UTF-8 Latin Extended-A font rendering), hover tooltips on every control, a 4-step Quick-Start Tutorial window, and localized online/offline help (`F1`).
- **Extendable by Design:** Shaders (`assets/shaders/`), icons, audio, samples (`assets/`), Lua UI/presets/locales (`scripts/*.lua`, `scripts/locales/*.lua`) live beside the binary — never baked into the executable or `.wasm`.

## Building

### Native (Linux / macOS / Windows)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/nebel
```
To unlock the optional Apache-2.0 **`DA3Mono-Large`** HD depth model on Desktop, run `./scripts/download_hd_depth.sh` (Linux/macOS) or `.\scripts\download_hd_depth.ps1` (Windows).

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
```

## Extending Nebel

All editable files sit right beside the binary (and beside `nebel.wasm` in the web build):
- `scripts/locales/{en,de,pl}.lua` — edit translations or add new languages
- `scripts/presets.lua` — add or tweak fog presets
- `scripts/ui.lua` & `scripts/tutorial.lua` — customize layout, tooltips, and tutorial steps
- `scripts/tools.lua` & `scripts/pipeline.lua` — adjust brush curves, hotkeys, and sliders
- `assets/shaders/*.frag` — customize fluid advection, volumetric scattering, bloom, or compositing (press `F5` to reload live)

## Development Process

- **OpenSpec:** Living specifications and change records live in [`openspec/`](openspec/).
- **Ponytail:** Smallest complete diffs, minimal dependencies, strict validation at trust boundaries.

## Licenses

- **Code (`src/`, `scripts/`, `web/`, `tests/`, build files):** [MIT License](LICENSE)
- **Art & Media Assets (`assets/icons/`, `assets/sounds/`, `assets/samples/`):** [CC BY 4.0](LICENSE-ART)
- **AI Depth Models:** Apache 2.0 (`Depth Anything V2 Small`, optional `Depth Anything 3 DA3Mono-Large`)

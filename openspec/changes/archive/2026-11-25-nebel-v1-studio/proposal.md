# Proposal: Nebel v1 Studio

## Why
Landscape and wildlife photographers need a fast, non-destructive, locally runnable tool to add physically realistic atmospheric fog onto photographs without generative AI hallucinations.

## What Changes
- Build a C11 core engine (`src/core/`, `src/script/`, `src/render/`, `src/platform/`) using SDL3, OpenGL 3.3 Core / WebGL 2.0 (Emscripten), and embedded Lua 5.4.
- Implement multi-layer linear-light Koschmieder fog rendering with height falloff, perspective-foreshortened turbulence, directional sun scattering (Henyey-Greenstein), highlight bloom, and sensor grain matching.
- Implement hybrid depth/structure analysis: classical CV (horizon gradient, defocus sharpness, Dark Channel Prior, O(1) Guided Filter matting) plus optional lazy-loaded Depth Anything V2 Small (ONNX) with automatic classical fallback.
- Split application workflow, darkroom UI layout (desktop + mobile touch drawer), tools, and fog presets into editable external Lua scripts (`scripts/`) and loose CC BY 4.0 assets (`assets/`) beside the binary (never baked in).
- Provide ≥95% line coverage unit tests on core C/Lua modules, a headless automated E2E simulation test, cross-platform CI (Linux/Win/Mac/Web), and GitHub Pages deployment from `main`.

## Out of Scope (v1)
- Camera RAW decoding (LibRaw), C++ GUI frameworks, server-side inference, compiling ONNX Runtime from source, and secondary git branches.

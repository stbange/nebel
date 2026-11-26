# Proposal: Nebel v3 Tiered Studio Render, HD Depth, i18n & GitHub Releases

## Motivation
Desktop GPUs (Windows/macOS/Linux) can run dramatically more detailed depth estimation and multi-scatter volumetric fog physics than mobile browsers, while interactive brushing should remain instantaneous before clicking an explicit **Render** button. In addition, German and Polish photographers benefit from native localization, tooltips, a quick-start tutorial window, localized online help, and one-click downloadable desktop releases.

## Capabilities
- Three-tier runtime split (`desktop`, `web_desktop`, `web_mobile`) adapting depth resolution, PatchFusion tiling, fluid advection, and progressive volumetric sampling automatically.
- Explicit `Mark -> Render` workflow with translucent depth-aware marker overlay, prominent `Render` / `Cancel` controls, live progressive convergence bar, and pixel-exact full-resolution export (GPU-tiled on Desktop, CPU-mirrored on Web).
- Maximum-detail Apache-2.0 depth pipeline: aspect-preserving bilinear preprocessing, least-squares affine PatchFusion tile stitching, 3-channel RGB covariance Guided Filter, float GPU depth textures (`GL_R32F`/`GL_R16F`), GPU ONNX execution providers (DirectML/CoreML/CUDA/WebGPU), `DA3Mono-Large` support, and manual Depth Touch-Up brush.
- Multi-scatter volumetric lighting with 3D heightfield shadow cones, terrain-guided fluid advection, intrinsic shadow de-lighting under thick fog, and depth-weighted optical halation.
- Full UTF-8 (`U+0020..U+017F`) font atlas, external Lua locales (`en`, `de`, `pl`), hover tooltips, 4-step interactive tutorial modal, localized `help.html`, and automated GitHub Releases for Windows/macOS/Linux.

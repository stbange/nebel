# Proposal: Nebel v2 Photographic Realism & Pro Export

## Why
Photographers want atmospheric fog that reacts to directional sunlight shafts, naturally pools in cool shadowed hollows, auto-locks onto clicked 3D depth planes during painting, exports print-grade 16-bit files without 8-bit banding, and supports native desktop ONNX depth estimation when available.

## What Changes
1. **Crepuscular sunbeams (volumetric god rays):** Radial depth-occluded light-shaft scattering from the `Sun` position through fogged pixels in both GPU (`composite.frag`) and CPU (`fog_layer.c`).
2. **Auto-anchored brush depth:** Optional `AutoLock` mode (`A` hotkey) sampling 3D depth around the brush click point to automatically set `Near Depth` behind foreground objects, yielding cleanly to manual slider edits.
3. **Shadow / trough pooling bias:** `shadow_pool` parameter attenuating fog on bright sunlit surfaces while concentrating mist in shaded hollows and dark forest troughs.
4. **16-bit lossless PNG & 16-bit TIFF export + Desktop ONNX runtime:** Zero-dependency C11 16-bit RGBA PNG and 16-bit RGB Baseline TIFF encoders (`image.c`), plus dynamic `SDL_LoadObject` ONNX Runtime C API v1.20 dispatch (`depth_onnx.c`) with automatic classical fallback.

## Out of Scope
- Camera RAW demosaicing, bundling native `libonnxruntime` binaries inside git, or server-side processing.

# Design: Nebel v2 Photographic Realism & Pro Export

## 1. Crepuscular Sunbeams (God Rays)
- Evaluated inside `composite.frag` (GPU) and at the end of `nebel_fog_render_cpu` (CPU export) with zero additional FBOs.
- Casts 16 jittered radial samples from pixel `v_uv` toward active `u_sun_pos`, accumulating bright background sky/sunlit gaps (`depth > 0.42`, `luma > 0.35`) occluded by nearer foreground silhouettes, weighted by local fog presence and pixel depth occlusion.

## 2. Auto-Anchored Brush Depth
- `app->auto_depth_brush` (toggled via UI `AutoLock` button or `A` key) samples a 5x5 patch of `app->depth_map` at stroke start `(u, v)` for the Fog tool and sets `depth_min = clamp(d_click - 0.14, 0.0, 0.85)`.
- Manual changes to `depth_min` automatically pause `auto_depth_brush` so explicit slider tuning is never overwritten.

## 3. Shadow / Trough Pooling Bias
- `NebelFogParams.shadow_pool` (`[0, 1]`) modulates optical depth `tau` via `mix(1.0, 0.35 + 1.15 * darkness, shadow_pool)` where `darkness` is derived from underlying linear pixel luminance, concentrating mist in shaded hollows and under canopies.

## 4. 16-Bit PNG/TIFF Export & Dynamic Desktop ONNX
- **16-bit PNG:** Pure C chunk writer (`IHDR` bit_depth=16 color_type=6 + big-endian RGBA16 scanlines compressed via `stb_image_write`'s built-in `stbiw__zlib_compress` & CRC32).
- **16-bit TIFF:** Pure C uncompressed Baseline RGB16 writer (standard 11-tag IFD + `uint16_t` RGB pixels).
- **Desktop ONNX (`depth_onnx.c`):** Uses MIT-licensed `third_party/onnxruntime_c_api.h` + `SDL_LoadObject` (`libonnxruntime.so` / `.dylib` / `.dll`) to run `assets/models/depth_anything_v2_vits.onnx` (or `model_quantized.onnx`) at 392x392 on CPU, falling back cleanly to classical CV if absent.

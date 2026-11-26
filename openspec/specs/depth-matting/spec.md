# depth-matting Specification

## Purpose

Hybrid 3D depth estimation and edge-preserving matting so atmospheric fog wraps cleanly behind wildlife silhouettes, antlers, fur, and foliage.

## Requirements

### Requirement: Classical Depth Cues and O(1) Guided Filter Matting
The C core SHALL compute a normalized depth map from horizon gradient, local defocus sharpness, and Dark Channel Prior haze cues, and SHALL refine painted fog and foreground-protect masks via an O(1) Guided Filter guided by the original photograph.

#### Scenario: Foreground-protected silhouette resists fog bleed
- **WHEN** a sharp dark silhouette is marked with the Foreground-Protect brush adjacent to a fog-painted background
- **THEN** Guided Filter refinement keeps the silhouette substantially clearer than the surrounding fogged background with minimal edge halo

### Requirement: Optional ONNX Depth With Classical Fallback
The platform layer SHALL accept an external monocular depth map (Depth Anything V2 Small via ONNX on Web/Desktop) when available and requested, and SHALL seamlessly fall back to the built-in classical depth map whenever neural inference is unavailable or fails.

#### Scenario: Missing ONNX runtime or model falls back cleanly
- **WHEN** auto-depth is triggered without an ONNX runtime or model present
- **THEN** the application reports classical fallback status without crashing and produces a valid normalized depth map

### Requirement: Auto-Anchored Brush Depth
When `auto_depth_brush` is enabled and the user begins a Fog Marker stroke at `(u, v)`, the engine SHALL sample the local 3D depth around `(u, v)` and set `depth_min` slightly closer than the clicked plane so foreground objects remain clear automatically.

#### Scenario: Clicking background plane auto-anchors Near Depth behind foreground
- **WHEN** `auto_depth_brush` is enabled and a fog stroke starts on a background region of depth `0.80`
- **THEN** `depth_min` automatically updates to approximately `0.66`, keeping closer foreground subjects (`d < 0.50`) crisp

### Requirement: Native Desktop Dynamic ONNX Depth Dispatch
Desktop builds SHALL dynamically load `libonnxruntime` via `SDL_LoadObject` when present alongside an ONNX Depth Anything V2 model under `assets/models/`, run 392x392 inference, refine the depth map via Guided Filter, and fall back cleanly to classical CV whenever either library or model is absent.

#### Scenario: Desktop ONNX runs when library and model are present and falls back otherwise
- **WHEN** desktop auto-depth runs with `libonnxruntime` and model present
- **THEN** `depth_is_neural` becomes true with a valid normalized depth map; when absent, classical CV depth runs without error

### Requirement: RGB Covariance Guided Filter and Multi-Scale PatchFusion Tile Stitching
The depth module SHALL provide a 3-channel RGB covariance Guided Filter (`nebel_guided_filter_rgb`) that preserves isoluminant color boundaries and a multi-scale PatchFusion tile stitcher (`nebel_depth_fuse_tiles`) that aligns overlapping high-resolution depth tiles to a global depth field via closed-form least-squares affine scaling with smooth feather blending.

#### Scenario: RGB guided filter preserves isoluminant color edge
- **WHEN** an image contains two regions of equal luminance but distinct hues and a coarse depth step straddling the color boundary
- **THEN** `nebel_guided_filter_rgb` aligns the depth transition tightly to the color boundary

#### Scenario: Affine PatchFusion stitching aligns scaled tile smoothly to global depth
- **WHEN** overlapping depth tiles with differing arbitrary scale and offset are fused onto a global depth map
- **THEN** `nebel_depth_fuse_tiles` produces a continuous normalized `[0, 1]` depth map matching global layout with fine tile detail preserved

### Requirement: Interactive Depth Touch-Up Sculpting
The engine SHALL provide a smooth depth touch-up brush (`nebel_depth_paint_brush`) allowing photographers in Depth View to nudge local depth values closer or farther with soft falloff and undo support.

#### Scenario: Painting closer lowers depth values smoothly within brush radius
- **WHEN** the depth brush paints nearer at `(0.5, 0.5)` on a uniform `0.6` depth map
- **THEN** center pixels move strictly closer (`< 0.6`) while distant pixels outside the radius remain unchanged

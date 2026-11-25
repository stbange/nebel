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

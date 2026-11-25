# Depth and Edge Matting Spec Delta

## ADDED Requirements

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

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

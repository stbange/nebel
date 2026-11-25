# fog-pipeline Specification

## Purpose

Physically based multi-layer linear-light atmospheric fog rendering, highlight veiling glare, sensor grain matching, and full-resolution image I/O.

## Requirements

### Requirement: Linear-Light Multi-Layer Koschmieder Rendering
The engine SHALL decode JPEG and PNG photographs into 32-bit linear floating-point RGBA, support up to 8 independent fog layers (each with its own painted mask and physics parameters), and apply Koschmieder atmospheric scattering with exponential height falloff, perspective-foreshortened turbulence, directional Henyey-Greenstein sun forward-scattering, highlight bloom, and matched sensor grain before encoding back to sRGB.

#### Scenario: Multi-layer optical extinction increases monotonically
- **WHEN** two visible fog layers overlap over a dark background pixel with positive density
- **THEN** the rendered linear luminance moves strictly closer to the airlight color than with a single layer alone, and remains clamped within valid `[0, 1]` sRGB output

#### Scenario: Full-resolution PNG and JPEG export preserves dimensions
- **WHEN** a photograph of dimensions `W x H` is edited and exported to PNG or JPEG
- **THEN** the exported file decodes without error at the exact `W x H` resolution with all visible fog layers applied

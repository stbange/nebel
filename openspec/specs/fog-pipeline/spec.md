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

### Requirement: Volumetric God Rays and Shadow Pooling
The fog pipeline SHALL support per-layer `god_rays` (crepuscular radial light shafts occluded by foreground depth and scattered by fog) and `shadow_pool` (concentrating mist inside shadowed hollows relative to bright sunlit surfaces) identically in GPU shaders and CPU full-resolution rendering.

#### Scenario: Shadow pooling concentrates fog in dark hollows over bright areas
- **WHEN** `shadow_pool` is set to `1.0` over a photo containing both a dark shadowed hollow and a bright sunlit patch at equal depth
- **THEN** the fog optical lift in the shadowed hollow is strictly greater than on the bright sunlit surface

#### Scenario: Crepuscular sunbeams brighten fogged pixels along sunlit paths
- **WHEN** `god_rays` is enabled with a bright sun disc in the background and fog painted nearby
- **THEN** fogged pixels along rays toward the sun receive positive warm light-shaft illumination while foreground-occluded pixels remain protected

### Requirement: Print-Grade 16-Bit PNG and TIFF Export
The image module SHALL encode and save 16-bit per channel lossless RGBA PNG and 16-bit per channel RGB TIFF files directly from internal 32-bit linear floating-point buffers without external image library dependencies.

#### Scenario: 16-bit PNG and TIFF exports produce valid full-resolution files
- **WHEN** a linear float image of size `W x H` is exported as 16-bit PNG and 16-bit TIFF
- **THEN** both outputs contain valid headers (PNG bit depth 16, TIFF BitsPerSample 16) and decode cleanly at exact `W x H` dimensions

# Fog Pipeline Spec Delta

## ADDED Requirements

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

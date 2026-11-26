# Depth Matting Spec Delta

## ADDED Requirements

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

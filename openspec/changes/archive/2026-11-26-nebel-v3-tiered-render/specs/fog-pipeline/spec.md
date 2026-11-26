# Fog Pipeline Spec Delta

## ADDED Requirements

### Requirement: Tiered Mark-to-Render Progressive Volumetric Pipeline
The engine SHALL distinguish three hardware tiers (`desktop`, `web_desktop`, `web_mobile`) and separate interactive fog marking (`MARK`) from explicit progressive volumetric rendering (`RENDERING` -> `DONE`) with cancellation, terrain-guided fluid advection, 3D heightfield shadow cones, multi-octave Mie multiple scattering, and intrinsic shadow de-lighting under thick fog.

#### Scenario: Mark-to-Render state transitions cleanly and supports cancellation
- **WHEN** a user marks a fog region, starts a render, and either completes all progressive steps or cancels mid-render
- **THEN** `render_phase` transitions cleanly from `MARK` through `FLUID`/`RENDERING` to `DONE` (or returns to `MARK` on cancel) without corrupting masks or image buffers

#### Scenario: Intrinsic de-lighting softens harsh shadow contrast inside thick fog
- **WHEN** `delight_strength` is positive over a high-contrast shadowed scene covered by dense fog
- **THEN** local dark-to-bright contrast under thick fog is reduced relative to `delight_strength = 0` while un-fogged foreground remains unchanged

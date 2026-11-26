# lua-runtime Specification

## Purpose

Separation of core C engine primitives from user-modifiable external Lua scripts and loose CC BY 4.0 assets placed beside the binary.

## Requirements

### Requirement: External Loose Assets and Modifiable Lua Layer
All shaders, icons, sounds, sample photographs, and Lua scripts SHALL reside in external `assets/` and `scripts/` directories beside the native binary and alongside `nebel.wasm` on Web, never baked into the compiled binary.

#### Scenario: Lua preset and UI scripts drive engine state and support live reload
- **WHEN** the engine boots or reloads external `scripts/*.lua`
- **THEN** tools, presets, input bindings, and UI layout are populated from Lua, and malformed Lua edits return a logged error without crashing the C engine

#### Scenario: Manifest prevents path traversal
- **WHEN** a manifest or script requests an asset path containing `..` or absolute paths
- **THEN** the C path validator rejects the request safely

### Requirement: External Locale Scripts and Platform/Render Lua Bridge
The Lua bridge SHALL expose `nebel.platform` (tier, locale, open_help, save/load preference) and `nebel.render` (start, cancel, phase, progress, quality) APIs while keeping all locale tables (`scripts/locales/*.lua`) and tutorial scripts (`scripts/tutorial.lua`) loose beside the binary.

#### Scenario: Lua queries tier, locale, and triggers Mark-to-Render pipeline
- **WHEN** Lua scripts query `nebel.platform.tier()` and call `nebel.render.start()` / `nebel.render.cancel()`
- **THEN** the C engine reflects the platform tier and updates render state accurately

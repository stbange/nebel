# Lua Runtime and Loose Assets Spec Delta

## Purpose
Separation of core C engine primitives from user-modifiable external Lua scripts and loose CC BY 4.0 assets placed beside the binary.

## ADDED Requirements

### Requirement: External Loose Assets and Modifiable Lua Layer
All shaders, icons, sounds, sample photographs, and Lua scripts SHALL reside in external `assets/` and `scripts/` directories beside the native binary and alongside `nebel.wasm` on Web, never baked into the compiled binary.

#### Scenario: Lua preset and UI scripts drive engine state and support live reload
- **WHEN** the engine boots or reloads external `scripts/*.lua`
- **THEN** tools, presets, input bindings, and UI layout are populated from Lua, and malformed Lua edits return a logged error without crashing the C engine

#### Scenario: Manifest prevents path traversal
- **WHEN** a manifest or script requests an asset path containing `..` or absolute paths
- **THEN** the C path validator rejects the request safely

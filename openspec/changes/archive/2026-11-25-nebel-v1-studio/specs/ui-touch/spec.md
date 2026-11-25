# Darkroom UI and Mobile Touch Spec Delta

## Purpose
Minimal custom OpenGL darkroom UI configured from Lua with automatic mobile touch switching, gesture controls, before/after comparison, and undo/redo.

## ADDED Requirements

### Requirement: Adaptive Desktop and Mobile Touch Controls
The engine SHALL automatically switch from mouse/keyboard desktop mode to touch mode upon touch input or mobile viewport detection, exposing 1-finger painting, 2-finger pinch-zoom/pan, 2-finger tap undo, 3-finger tap redo, and a collapsible bottom drawer layout.

#### Scenario: Two-finger pinch cancels accidental paint and zooms canvas
- **WHEN** a user places a second finger down during a touch interaction and spreads both fingers apart
- **THEN** any tentative single-finger stroke within the deadzone is cancelled, touch mode is active, and canvas zoom scales proportionally

#### Scenario: Undo and redo restore exact mask and parameter state
- **WHEN** a user paints a fog stroke and invokes undo followed by redo
- **THEN** undo restores bit-for-bit pre-stroke mask bytes and redo re-applies the stroke identically

# ui-touch Specification

## Purpose

Minimal custom OpenGL darkroom UI configured from Lua with automatic mobile touch switching, gesture controls, before/after comparison, and undo/redo.

## Requirements

### Requirement: Adaptive Desktop and Mobile Touch Controls
The engine SHALL automatically switch from mouse/keyboard desktop mode to touch mode upon touch input or mobile viewport detection, exposing 1-finger painting, 2-finger pinch-zoom/pan, 2-finger tap undo, 3-finger tap redo, and a collapsible bottom drawer layout.

#### Scenario: Two-finger pinch cancels accidental paint and zooms canvas
- **WHEN** a user places a second finger down during a touch interaction and spreads both fingers apart
- **THEN** any tentative single-finger stroke within the deadzone is cancelled, touch mode is active, and canvas zoom scales proportionally

#### Scenario: Undo and redo restore exact mask and parameter state
- **WHEN** a user paints a fog stroke and invokes undo followed by redo
- **THEN** undo restores bit-for-bit pre-stroke mask bytes and redo re-applies the stroke identically

### Requirement: Multi-Language Localization, Tooltips, Tutorial Window and Online Help
The UI SHALL support live switching between English (`en`), German (`de`), and Polish (`pl`) with full UTF-8 Latin-1 and Latin Extended-A rendering (`U+0020..U+017F`), contextual hover tooltips clamped to viewport bounds, a dismissible 4-step quick-start tutorial modal, and a localized online/offline help guide (`help.html`).

#### Scenario: Tooltips clamp inside viewport and UTF-8 labels measure codepoints
- **WHEN** a German or Polish tooltip is requested near the right or bottom edge of the viewport
- **THEN** the emitted tooltip rectangle stays strictly within `[0, viewport_w]` and `[0, viewport_h]` and UTF-8 accented characters render without dropping glyphs

#### Scenario: All EN, DE, and PL locale tables define complete translation keys
- **WHEN** `scripts/locales/en.lua`, `scripts/locales/de.lua`, and `scripts/locales/pl.lua` are loaded by `scripts/i18n.lua`
- **THEN** every key present in English has a non-empty translation in German and Polish

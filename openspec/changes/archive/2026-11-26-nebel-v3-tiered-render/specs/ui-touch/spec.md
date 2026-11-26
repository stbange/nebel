# UI & Touch Spec Delta

## ADDED Requirements

### Requirement: Multi-Language Localization, Tooltips, Tutorial Window and Online Help
The UI SHALL support live switching between English (`en`), German (`de`), and Polish (`pl`) with full UTF-8 Latin-1 and Latin Extended-A rendering (`U+0020..U+017F`), contextual hover tooltips clamped to viewport bounds, a dismissible 4-step quick-start tutorial modal, and a localized online/offline help guide (`help.html`).

#### Scenario: Tooltips clamp inside viewport and UTF-8 labels measure codepoints
- **WHEN** a German or Polish tooltip is requested near the right or bottom edge of the viewport
- **THEN** the emitted tooltip rectangle stays strictly within `[0, viewport_w]` and `[0, viewport_h]` and UTF-8 accented characters render without dropping glyphs

#### Scenario: All EN, DE, and PL locale tables define complete translation keys
- **WHEN** `scripts/locales/en.lua`, `scripts/locales/de.lua`, and `scripts/locales/pl.lua` are loaded by `scripts/i18n.lua`
- **THEN** every key present in English has a non-empty translation in German and Polish

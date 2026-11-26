# Lua Runtime Spec Delta

## ADDED Requirements

### Requirement: External Locale Scripts and Platform/Render Lua Bridge
The Lua bridge SHALL expose `nebel.platform` (tier, locale, open_help, save/load preference) and `nebel.render` (start, cancel, phase, progress, quality) APIs while keeping all locale tables (`scripts/locales/*.lua`) and tutorial scripts (`scripts/tutorial.lua`) loose beside the binary.

#### Scenario: Lua queries tier, locale, and triggers Mark-to-Render pipeline
- **WHEN** Lua scripts query `nebel.platform.tier()` and call `nebel.render.start()` / `nebel.render.cancel()`
- **THEN** the C engine reflects the platform tier and updates render state accurately

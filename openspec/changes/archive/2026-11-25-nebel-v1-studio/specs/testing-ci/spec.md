# Testing, Coverage, and Delivery Spec Delta

## Purpose
Strict automated verification (unit coverage >=95%, headless simulation E2E test) and single-branch CI/CD publishing to GitHub Pages.

## ADDED Requirements

### Requirement: Automated Unit Coverage and Headless App Simulation
The build SHALL provide pure C unit tests achieving at least 95% line coverage across `src/core/` and `src/script/`, plus an automated headless simulation test driving the complete C+Lua+Assets loop with synthetic mouse and touch inputs.

#### Scenario: Coverage gate enforces >=95% line coverage
- **WHEN** `gcovr` evaluates `src/core/` and `src/script/` after running `ctest`
- **THEN** total line coverage is >= 95.0% and every unit and E2E simulation test passes cleanly under ASan/UBSan

### Requirement: Single-Branch Multi-Platform CI and GitHub Pages Deployment
GitHub Actions on `main` SHALL verify tests/coverage, compile Windows/Linux/macOS/Web targets, and deploy the Emscripten web app plus loose assets to GitHub Pages via `actions/deploy-pages` without creating additional branches.

#### Scenario: Web deployment preserves README top link and loose assets
- **WHEN** changes are pushed to `main`
- **THEN** the workflow publishes `index.html`, `nebel.wasm`, `scripts/`, and `assets/` to `https://stbange.github.io/nebel/`

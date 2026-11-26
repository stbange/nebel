# testing-ci Specification

## Purpose

Strict automated verification (unit coverage >=95%, headless simulation E2E test) and single-branch CI/CD publishing to GitHub Pages.

## Requirements

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

### Requirement: Automated Cross-Platform GitHub Releases
A GitHub Actions release workflow triggered by version tags on `main` SHALL package portable Windows, macOS, and Linux archives containing the native binary, loose `assets/` and `scripts/`, offline `help.html`, and SHA-256 checksums, attaching them to a public GitHub Release.

#### Scenario: Release workflow packages loose external assets and help alongside binaries
- **WHEN** a version tag is pushed on `main`
- **THEN** the release workflow builds Linux, macOS, and Windows release bundles and publishes downloadable archives to GitHub Releases without creating extra branches

# Testing & CI Spec Delta

## ADDED Requirements

### Requirement: Automated Cross-Platform GitHub Releases
A GitHub Actions release workflow triggered by version tags on `main` SHALL package portable Windows, macOS, and Linux archives containing the native binary, loose `assets/` and `scripts/`, offline `help.html`, and SHA-256 checksums, attaching them to a public GitHub Release.

#### Scenario: Release workflow packages loose external assets and help alongside binaries
- **WHEN** a version tag is pushed on `main`
- **THEN** the release workflow builds Linux, macOS, and Windows release bundles and publishes downloadable archives to GitHub Releases without creating extra branches

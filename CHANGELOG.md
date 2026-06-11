# Changelog

All notable changes to Slice DCMT are documented here.

This project did not use formal release tags through most of its history, so this changelog is organized by dated development eras rather than semantic-version releases.

## [Unreleased]

### Added

- Added a curated root changelog derived from the full repository and history review.

### Changed

- Updated active firmware metadata to reference CRUMBS `0.12.4` and
  `bread-crumbs-contracts` `0.4.3`.

## [2026-05-12] Firmware Runtime Hardening

### Added

- Added a 1 second AVR watchdog to the active PlatformIO firmware.

### Changed

- Reworked shared state access so firmware snapshots and commits are protected with interrupt guards.
- Replaced Arduino `String`-based serial command parsing with a fixed static command buffer.

### Fixed

- Fixed serial command trimming so empty strings are handled safely.
- Added portability fixes for megaAVR/Nano Every targets.
- Reduced runtime heap-fragmentation risk in serial command handling.

## [2026-04-01 to 2026-04-20] Dependency And Organization Cleanup

### Changed

- Bumped active firmware dependencies to CRUMBS `0.12.0` and `bread-crumbs-contracts` `0.4.1`.
- Updated repository, docs, and workflow references from `FEASTorg` casing to `feastorg`.
- Refreshed generated KiBot documentation output after the organization rename.

## [2026-03-08 to 2026-03-18] PlatformIO Firmware Modernization

### Added

- Added active PlatformIO build environments for `gen1_nano`, `gen2_nano`, `gen1_nanoevery`, and `gen2_nanoevery`.
- Added Nano Every support with board-dependent speed-loop defaults.
- Added optional closed-loop speed support through `DCMotorTacho` for capable builds.
- Added closed-loop position support through `DCMotorServo`.
- Added bitwise capability replies for baseline, closed-position, PID tuning, and optional closed-speed support.
- Added build-flag controlled I2C address selection.
- Added build-flag controlled CRUMBS/debug output.
- Added archive preservation for the former Arduino and PlatformIO firmware generations.
- Added `.gitattributes` for cross-platform file normalization.

### Changed

- Converted active firmware to newer CRUMBS and published BREAD/DCMT contracts.
- Moved active firmware away from vendored CRUMBS and LMD18200 code toward published package dependencies.
- Separated hardware generation, MCU target/profile, control capability, and I2C address selection.
- Made setpoints and PID tunings preloadable independent of the active control mode.
- Made module version ownership come from the published contract rather than a duplicate local definition.
- Reworked CRUMBS state replies to use a fixed payload layout across modes.
- Reorganized active and archived firmware directories.

### Fixed

- Restored proper closed-loop position behavior through the DCMotorServo backend.
- Improved e-stop handling with debounce and internal pull-up input behavior.
- Prevented repeated PID reapplication when tuning values have not changed.
- Removed setpoint gating that prevented controllers from preloading values.
- Added explicit invalid-data sentinels for unsupported or inactive state fields.
- Inset encoder range reporting to avoid false sentinel-value collisions.
- Fixed servo function declaration ordering.
- Reduced serial string-handling fragility.

## [2026-01-22 to 2026-02-01] Legacy Firmware Organization And Template Review

### Added

- Added older generation firmware scripts for historical context.

### Changed

- Reorganized firmware layout before the March 2026 active firmware rewrite.
- Reviewed and revised the repo against the current project template and KMLib-era conventions.
- Refreshed generated KiBot index and board render outputs through CI reruns.

## [2025-10-15 to 2025-10-22] Firmware Versioning And Split

### Added

- Added explicit firmware versioning.
- Added changelog files for generation-specific Arduino firmware snapshots.

### Changed

- Updated firmware for different board revisions.
- Updated uploaded/default address tracking.
- Renamed firmware folders/files to better match hardware revisions.
- Split firmware organization into Arduino and PlatformIO tracks.

## [2025-09-05 to 2025-09-24] Docs, KiBot, And GitHub Pages Automation

### Added

- Added GitHub Actions automation to build KiBot outputs and publish them to GitHub Pages.
- Added a docs site structure with index, component sourcing, architecture, testing, changelog, TODO, and KiBot output pages.
- Added KiBot output indexing for generated artifacts.
- Added top and bottom PCB render assets for the docs site.
- Added interactive HTML BOM publishing.
- Added KiBot 3D model caching and retry behavior for more reliable CI runs.

### Changed

- Switched docs to Just the Docs/Jekyll configuration.
- Moved KiBot site configuration under `docs/kibot/`.
- Centralized hardware/docs publishing through reusable infrastructure workflows, first under `slice-infra` and then under `bread-infra`.
- Renamed workflow roles toward the current `docs-pipeline` and `publish-kibot` flow.
- Moved component sourcing notes into their own document.

### Fixed

- Fixed Pages deployment ordering so published pages align with the generated KiBot index commit.
- Fixed artifact download, staging, push/rebase, and generated-index behavior across the docs pipeline.
- Fixed Jekyll/front matter issues that affected navigation and page rendering.
- Fixed iBoM and artifact glob handling so all matching KiBot outputs are published.
- Fixed PCB render generation, including true bottom-view rendering.
- Removed brittle yq handling in favor of native Python parsing in the automation path.
- Cleaned up temporary debug and generated-index workflow churn after the pipeline stabilized.

## [2025-05-06 to 2025-05-19] Hardware Revision And First KiBot Workflow

### Added

- Added the main current KiCad hardware source under `hardware/`.
- Added `motor_driver_lmd18500.kicad_sch` for the motor driver sheet.
- Added KiBot configuration for schematic PDF, PCB PDF, BOM, iBoM, Gerbers, drill files, position files, and board renders.
- Added a hardware Makefile with KiBot targets for ERC, DRC, schematic fabrication output, and PCB fabrication output.
- Added early GitHub Actions workflow support for KiBot runs.
- Added notes for alternative future motor-driver parts.

### Changed

- Consolidated newer hardware work from temporary/new folders into the primary `hardware/` directory.
- Moved historical hardware snapshots into archive locations.
- Updated the PCB around electrolytic capacitance, filled zones, thermal/tab layout, and 12 V routing constraints.
- Moved the KiBot config under `hardware/`.

### Fixed

- Corrected initial KiBot workflow naming, paths, root references, Makefile location, and config placement.

## [2025-02-20 to 2025-04-19] Hardware Archive And Firmware Control Expansion

### Added

- Added historical PCB snapshots from 2021 and 2023.
- Added braking support to the firmware lineage.
- Added newer DCMT firmware sketches and preserved older sketches under legacy paths.
- Added LMD18200 library usage to the firmware.
- Added DCMT-specific request/message handlers, serial commands, and serial output helpers.
- Added early position and speed controller support.
- Added a new hardware design branch before it was later consolidated into the main hardware folder.

### Changed

- Reorganized firmware into active and legacy sketch directories.
- Renamed RLHT-oriented handler naming to DCMT-specific naming.
- Evolved firmware from basic motor output toward position/speed control behavior.

### Fixed

- Removed temporary KiCad autosave and lock artifacts that were introduced during early hardware editing.

## [2024-05-23 to 2024-09-10] Project Foundation

### Added

- Added the initial KiCad hardware project for the DC Motor Driver IC Carrier Slice.
- Added the initial license, `.gitignore`, README, BOM material, and generated fabrication outputs.
- Added `BOM_DCMT_R1.ods` as the early BOM artifact.
- Added the first firmware seed sketch under `firmware/`.

### Changed

- Refreshed the initial KiCad board and schematic after the first repository seed.
- Simplified the README as the repo shifted from hardware-only scaffolding toward a combined hardware/firmware project.

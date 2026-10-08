# Changelog

All notable changes to Slice DCMT are documented here.

This project did not use formal release tags through most of its history, so this changelog is organized by dated development eras rather than semantic-version releases.

## [Unreleased]

### Fixed

- **The Docs Pipeline runs again.** `hardware/.history` was committed as a
  gitlink (mode `160000`) with no `.gitmodules` entry, so `actions/checkout`
  failed with `fatal: No url found for submodule path 'hardware/.history'`
  and all three KiBot jobs — Fab, ERC and DRC — died before running, taking
  the index and Pages deploy with them. The directory is KiCad 10's own
  Local History: KiCad runs `git_repository_init` on `<project>/.history`,
  so committing it records a gitlink to a repository this one knows nothing
  about. It is recreated on every project open, on every machine, which is
  why the ignore rule matters rather than a one-time cleanup.

- **Corrected the G1 pin map in `docs/hardware-revisions.md`.** D8 and D12
  were listed as unused; G1 routes `/THRM2` to D8 and `/THRM1` to D12, on
  Nano pads 11 and 15. D13 is the unused one. The whole table has since been
  regenerated from the board files. No firmware impact — the thermal flags
  are not read anywhere in `firmware/`.

- **The status LED no longer drives motor 1's direction line on gen1** (#15).
  `LED_PIN` sits in the shared "General BREAD" block and is inherited by every
  slice whether or not that board populates an LED — `Slice_RLHT` carries the
  identical line. Gen1 DCMT boards do not populate it, and gen1 additionally maps
  `MOTOR1_DIR` to the same MCU pin (5), so FastLED's NeoPixel bit-banging wrote
  onto motor 1's direction line. Motor 1 is the sample pump on `dcmt0` and a
  dosing pump on `dcmt1`; the impeller is motor 2 and is unaffected.

  `Slice_RLHT` encoded the same fact as `RLHT_HAS_STATUS_LED` when the gen1/gen2
  split was done (2026-03-09); DCMT's split landed the same day without it. This
  adds the mirror-image `DCMT_HAS_STATUS_LED`.

  The worst case was the e-stop path rather than boot. `LMD18200::brake()` never
  touches DIR and is idempotent, and a held e-stop or watchdog trip calls only
  `brake()` — so a direction bit scrambled by `FastLED.show()` during an e-stop
  **persisted until the next `write()`**, rather than being corrected on the next
  control cycle. (At boot the LED write is overwritten microseconds later by
  `motor1Driver.begin()`/`write(0)`, so the boot case was benign.)

  `LED_PIN` itself is now defined only on boards that have the LED, so a future
  call site that reaches for it on gen1 is a build failure rather than a silent
  repeat of this bug.

  **Gen1 boards no longer have a local e-stop indicator.** The red/green LED was
  the only unconditional at-the-bench signal; `SLICE_DEBUG` serial output is
  compiled out by default. E-stop state remains readable over the bus via
  `reply_get_state`.

### Added

- Added a curated root changelog derived from the full repository and history review.

- **An explicit clear for a latched watchdog trip** (#26). The firmware
  handles `BREAD_OP_CLEAR_WATCHDOG_TRIP` (`0x7C`, empty payload), which
  clears the trip and nothing else: the timeout, the armed state and the
  trip count are unchanged. A frame with a non-empty payload is ignored
  and the trip stays set. `GET_CAPS` advertises
  `DCMT_CAP_CLEAR_WATCHDOG_TRIP` (bit 6) beside `DCMT_CAP_CMD_WATCHDOG`,
  so a controller can tell this firmware from one where `SET_WATCHDOG`
  still clears. The serial console gains the matching `WDCLEAR` command;
  `firmware/README.md` describes both.

### Changed

- **A watchdog trip latches until an operator clears it** (#26, supersedes
  #14). A trip used to be cleared by any valid command frame, by every
  `SET_WATCHDOG`, and by any line on the serial console. The controller's
  e-stop ladder drives its safe state as ordinary command frames, so
  pressing e-stop on a tripped board cleared the trip and the
  `OPEN_LOOP` `write(0)` that followed released the brake the watchdog had
  engaged (anolishq/anolis#261). Now only `BREAD_OP_CLEAR_WATCHDOG_TRIP`,
  the serial `WDCLEAR` command or a reboot clears it. Command frames,
  reply builds and serial input (including `READ`) still refresh
  liveness, so a returning master does not cause a fresh trip.
  `SET_WATCHDOG` and serial `WDOG=<ms>` set the timeout (`0` disarms) and
  refresh liveness without clearing, so re-arming is safe while tripped;
  disarming does not release a held trip either.

- **Clearing a watchdog trip resumes nothing** (#26). A trip now also sets
  both brake flags, alongside zeroing the PWM and speed setpoints, and
  keeps both position setpoints at the encoder positions for as long as it
  is held, so they follow a shaft that coasts after the (dynamic) brake
  engages. `GET_STATE` after a trip shows both brakes engaged and, in
  closed-loop position, the setpoint at the position where the shaft
  stopped. While a trip is
  held, `SET_OPEN_LOOP`, `SET_SETPOINT`, `SET_MODE` and a `SET_BRAKE` that
  would release a brake are ignored, as are the serial `MODE=`, `M1PWM=`,
  `M2PWM=`, `M1POS=`, `M2POS=`, `M1SPEED=`, `M2SPEED=`, `BRAKE1=0` and
  `BRAKE2=0`; engaging a brake, PID tuning and the watchdog commands still
  work. Previously a clear released the hold into the stored state: a
  closed-loop position trip mid-move finished the move after the clear,
  and commands sent during the hold took effect on it. To resume after a
  trip: clear it, release the brakes with `SET_BRAKE(0, 0)` (the motors
  then hold position, coast at PWM 0 or stay stopped, by mode), then send
  setpoints and then the mode. Closed-loop position is limited to encoder
  counts within ±32767 (positions are `int16` on the wire): beyond that the
  setpoint a trip writes is clamped, and a brake release in position mode
  drives toward the clamp value.

- **SET payloads are unpacked and the `GET_STATE` reply is packed with the
  shared contracts codec** (#28). Each SET handler reads its payload with
  the generated `dcmt_*_unpack()` from `bread/dcmt_ops.h` instead of
  reading at hand-written offsets, and `GET_STATE` fills a `dcmt_state_t`
  and packs it with `dcmt_state_pack()`. Handler behaviour is unchanged,
  and so are the bytes on the wire: a short payload is still ignored
  whole, trailing bytes are still accepted, and every `GET_STATE` frame
  is byte-identical. `SET_WATCHDOG` keeps its direct `u16` read, since the
  contracts declare no payload layout for it.

- **Requires CRUMBS `0.14.0` and `bread-crumbs-contracts` `0.6.0`**
  (`platformio.ini`: `^0.14.0`, `^0.6.0`; previously `^0.12.4` and
  `^0.4.5`). The codec and the clear-trip op first ship in contracts
  0.6.0, which needs CRUMBS 0.14. The version reply now reports CRUMBS
  `1400` instead of `1205`. Until contracts 0.6.0 is on the PlatformIO
  registry, the Firmware Build workflow cannot resolve it and fails.

- **Archived board directories now name their generation**:
  `archive/hw_archive/g1-2021-06-08-mtu/` and
  `archive/hw_archive/g2-2024-02-13-finn/`. The dates are the boards' own;
  the G2 one is marked on the JLC silkscreen. Directory names were the only
  record of which archived project was which generation, and they did not
  say, which is what made a G1 board in service look like the board in
  `hardware/`.

- **Removed the redundant second G1 archive** (`archive/hw_archive/2023-12-13/`).
  Converting both projects to a common format showed it was a KiCad 6
  re-save rather than a second fabrication run: identical pad-to-net map
  (112 pads), identical copper geometry (198 tracks, arcs and vias) and a
  byte-identical exported netlist. Recoverable from git history.

- Updated active firmware metadata to reference CRUMBS `0.12.4` and
  `bread-crumbs-contracts` `0.4.3`.

### Fixed

- Fixed intermittent CRUMBS query failures (all-`0xFF` non-responses and
  CRC-corrupt replies, ~1-47% of reads from a Raspberry Pi master) caused by
  the whole-struct interrupt-masked snapshot in `motorControlLogic()`
  introduced by the 2026-05-12 shared-state hardening. The ~72-byte copy
  masked interrupts for ~20-36 µs every loop iteration, delaying TWI ISR
  entry at the SLA+R boundary; the resulting clock stretch is mishandled by
  the Pi's I2C controller. The snapshot now copies only the scalar control
  inputs in a short window, with PID tunings fetched separately only in
  closed-loop modes. Verified on hardware: 0/1050 failed queries versus
  26/1050 on a same-bus stock control board. (#3)

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

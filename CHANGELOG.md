# Changelog

All notable changes to Slice DCMT are documented here.

This project did not use formal release tags through most of its history, so this changelog is organized by dated development eras rather than semantic-version releases.

## [Unreleased]

### Added

- **A command lease for the watchdog** (`DCMT_CAP_WATCHDOG_LEASE`,
  bread-crumbs-contracts#63). A 3-byte `SET_WATCHDOG` with
  `BREAD_WATCHDOG_FLAG_COMMANDS_ONLY` lets only `SET_OPEN_LOOP`, `SET_BRAKE`,
  `SET_MODE`, `SET_SETPOINT` and `SET_WATCHDOG` refresh it, so a poller that
  outlives the control path no longer keeps the motors driven. The 2-byte
  form and serial `WDOG=` end the lease; an undefined flag is refused and arms
  nothing.
- **SET_SETPOINT names its loop** (#57, `DCMT_CAP_SETPOINT_LOOP`). The
  5-byte form `[loop:u8][target1:i16][target2:i16]` writes only that loop's
  setpoints, `DCMT_PID_LOOP_POSITION` in counts or `DCMT_PID_LOOP_SPEED` in
  RPM; the 4-byte form now writes the position setpoints only. Before, the
  4-byte form wrote both, so a position target became the speed request at
  the next switch to closed-loop speed: 5000 counts became a 5000 RPM
  request. Other lengths are refused `MALFORMED`, an
  undefined loop `UNDEFINED_VALUE`, and loop SPEED without the speed loop
  `NOT_BUILT`. A controller that set speed through the 4-byte form now gets
  speed 0 (bread-crumbs-contracts `docs/capabilities.md` lists this as a
  fail-safe change).
- **Bench records** in `docs/bench/`: one file per session on real
  hardware, indexed in `docs/bench/README.md`, following bread-infra's
  `spec/slice/bench-records.md`. The first is a backfill of the 2026-10-20
  results posted on #45 and #55.
- **Host tests** (#58). `firmware/test/host` builds the firmware's own
  sources against the real CRUMBS core and the contracts headers, with the
  Arduino core and motor libraries stubbed, once per PlatformIO env. The
  first tests cover command dispatch (#45) and the boot status LED. CI runs
  them beside the four builds.

### Fixed

- **Serial numbers are parsed strictly** (#60), with bread-slice-core's
  `parseLong()` and `parseFloat()`. The whole token must parse and be in
  range for its field, or the command is refused with no change and an
  `Ignored:` line. `M1POS=abc` used to command position 0, `M1POS=40000`
  wrapped to -25536, `WDOG=abc` disarmed the watchdog, and `PIDPOS=` took
  trailing junk, NaN, inf and negative gains. The ranges: PWM -255 to 255,
  refused where it used to clamp, as SET_OPEN_LOOP refuses it; positions
  and speeds -32767 to 32767, since -32768 is GET_STATE's invalid sentinel;
  `WDOG=` 0 to 65535; each PID gain 0 to 65.535, SET_PID's range. Flash
  grows by about 1 KB, mostly `strtol`.
- **A held shaft no longer lunges on release in speed mode** (#63). The
  speed loop drove its position target ahead of the encoder without
  limit, so on the 2026-10-22 bench a shaft held for ~10 s at 60 RPM ran
  at 240 to 285 RPM on release. Both speed loops now cap that lead at
  `DCMT_TACHO_MAX_LEAD` (300 counts), DCMotorServo's `setMaxLead()`. With
  that cap a 4 s hold showed no lunge. Nothing yet stops the drive into a
  stall (#64).
- **A burst of serial input no longer holds the loop** (#49). The serial
  reader handled every buffered line in one call, and each reply blocks
  once the transmit buffer fills, so the e-stop went unsampled until the
  whole burst was answered. A continuous stream could hold it until the
  1 s hardware watchdog reset the board. It now handles one line per loop
  pass, and the e-stop is sampled between lines.
- **Serial `M1PWM=`/`M2PWM=`, `PIDPOS=` and `PIDSPEED=` write under a
  masked window** (#37). The I2C handlers write the same fields from the
  TWI interrupt, so a `SET_OPEN_LOOP` or `SET_PID` landing mid-write could
  leave a PWM with one byte from each command, or a motor with gains from
  two commands. The PWM commands also check the mode inside that window,
  so a `SET_MODE` between the check and the write can no longer leave an
  open-loop PWM stored in closed loop.
- **GET_STATE `m1_pwm`/`m2_pwm` report the PWM the motors are driven at**
  (#61), as the contract describes them. They reported the open-loop
  request, so in position and speed mode they read 0 while the servo drove
  up to ±150. Every driver write and brake now goes through
  `motor1_write`/`motor1_brake` (and motor 2's), which record the clamped
  output or 0. The open-loop request stays in `slice.motorNPWM`. Right
  after a mode change, GET_STATE shows the old output until the next loop
  pass applies the new mode.
- **Serial `MODE=` clears the open-loop PWM when it leaves open loop**
  (#36), as I2C `SET_MODE` already did. Before, `M1PWM=200` then
  `MODE=POS` left GET_STATE reporting `m1_pwm=200` in position mode, and
  a later `MODE=OPEN` or `SET_MODE(0)` drove motor 1 at 200 with no new
  PWM command.
- **The status LED shows blue through boot again** (gen2). Since the e-stop
  sampling change (#47), the 1 s boot wait polls the e-stop, and
  `pollEStop()` also wrote the LED, so its first pass turned the boot blue
  green (or red) within microseconds. The LED update is now
  `updateStatusLed()`, called from `loop()` only: blue holds through the
  boot wait, and the first loop pass shows the e-stop state. A press during
  boot is still asserted at once; only its red waits for the boot wait to
  end.
- **A GET opcode sent as a command frame is refused** (#45). GET_STATE,
  GET_WATCHDOG and GET_CAPS are queries, answered through SET_REPLY. Sent
  as a command frame, nothing handled them, yet GET_ACK reported them
  applied. They are now refused with `BREAD_REFUSE_UNKNOWN_OP`, like any
  other opcode with no command behind it.
- **A frame typed for another family is rejected** (#45). The firmware
  never declared its type to CRUMBS, so a frame carrying RLHT's type was
  dispatched as a DCMT command, and the families' opcodes overlap. It now
  calls `crumbs_set_type_id(DCMT_TYPE_ID)`: such a frame is rejected with
  `CRUMBS_ACK_REJECT_TYPE` before any handler runs, and does not refresh
  the watchdog. Type `0x00` is still accepted. GET_ACK replies now carry
  type `0x02` instead of `0x00`.
- **Commands sent while the e-stop is held no longer run at the release**
  (#46, #30). The press braked the motors but left the brake flags and
  position setpoints alone, and every motion command sent during the hold
  was stored, so releasing the line ran the stored PWM, mode, setpoint or
  brake release; in position mode the motor drove back toward its old
  target. The e-stop now holds the way a watchdog trip does, as
  `DCMT_CAP_ESTOP_HOLD` defines it, and GET_CAPS advertises the flag:
  - the press sets both brake flags and moves the position setpoints to
    the encoders, besides zeroing PWM and speed setpoints as before;
  - while the line is held, SET_OPEN_LOOP, SET_MODE, SET_SETPOINT and a
    brake release are refused with `BREAD_REFUSE_ESTOP`, which wins over
    `BREAD_REFUSE_WATCHDOG_TRIP` when both holds are in force; the serial
    `MODE=`, `M*PWM=`, `M*POS=`, `M*SPEED=` and `BRAKE*=0` are ignored the
    same way. A SET_BRAKE that engages one brake and releases the other
    engages it, then refuses;
  - releasing the line moves nothing. Resume with SET_BRAKE(0,0), then
    setpoints, then the mode.
- **A brake release after a hold no longer drives back the creep**. After
  an e-stop release or a cleared watchdog trip, each motor's position
  setpoint now keeps following its encoder until that motor's brake is
  released or a new setpoint for it arrives, instead of stopping at the
  release, so SET_BRAKE(0,0) holds where the shaft is. This changes the
  watchdog path too: before, the follow stopped at the clear. A setpoint
  sent before the brake release ends the follow and stands as sent, so
  the brake release drives to it.
- **GET_STATE positions no longer freeze during a hold**: the hold
  publishes the encoder positions every loop.
- **An edge train on the e-stop line can no longer postpone the stop**
  (#39). The debounce restarted on every edge, so edges less than 25 ms
  apart kept the e-stop from asserting for as long as they lasted, whatever
  the line's level. The line is now sampled on every loop pass instead of
  edge-triggered: the first LOW sample asserts the e-stop, and release
  needs 25 ms with no LOW sample. The edge interrupt is gone. The
  trade-off: a LOW glitch of any length that a loop pass samples, such as
  driver noise on an unpressed line, now asserts the e-stop, where a
  glitch shorter than 25 ms used to be ignored; the outputs it zeroes stay
  zeroed until the controller sends new commands. `setup()` enables the
  pull-up first and samples the line through its 1 s boot wait instead of
  sleeping, so a line held at power-up asserts before `loop()` acts on any
  command; as before, it counts as one press in `estop_count`.
- **GET_ACK now reports a dropped SET as refused** (#43). Before this, no
  handler called `crumbs_refuse()`, and `on_crumbs_message`, registered as
  `on_message`, made CRUMBS count every decoded frame as applied. On
  dcmt0, a SPEED SET_PID with the speed loop not built, a SET_PID with
  loop `0x07`, a 12-byte SET_PID and opcode `0x3F` all read applied. Each
  early return now refuses with a `BREAD_REFUSE_*` reason
  (bread-crumbs-contracts `bread_refuse.h`):
  - `MALFORMED`: a payload that does not unpack, a SET_PID that is not 13
    bytes, a SET_WATCHDOG without its u16, a CLEAR_WATCHDOG_TRIP with a
    payload;
  - `WATCHDOG_TRIP`: SET_OPEN_LOOP, SET_MODE and SET_SETPOINT while a trip
    is held, and a SET_BRAKE that asks for a release then. That SET_BRAKE
    still engages any brake it names before it refuses;
  - `UNDEFINED_VALUE`: a SET_MODE or SET_PID loop the contract does not
    define, and a SET_OPEN_LOOP PWM outside `-DCMT_PWM_MAX` to
    `DCMT_PWM_MAX` (255). That PWM was clamped to ±255 before; now its
    motor keeps its PWM, the other motor still takes its own, and the
    frame is refused;
  - `NOT_BUILT`: SET_MODE closed-loop speed or SET_PID loop SPEED with
    `DCMT_ENABLE_SPEED_LOOP=0`;
  - `WRONG_MODE`: SET_OPEN_LOOP outside open loop;
  - `UNKNOWN_OP`: an opcode neither DCMT nor bread-common defines, refused
    in `on_crumbs_message`. Its watchdog stamp still runs first for every
    valid frame, a refused one included.

  GET_CAPS advertises `DCMT_CAP_REFUSES`. The serial console's `M1PWM=` and
  `M2PWM=` still clamp to ±255; they are not the I2C contract.

### Changed

- Requires bread-crumbs-contracts `^0.11.0` (`firmware/platformio.ini` and
  `slice.yaml`), the release that defines the setpoint-loop form of
  SET_SETPOINT (#57), the watchdog lease and the stall faults (#64). It is
  additive: DCMT advertises none of the new flags yet, and the gen1 Nano
  image is the same size as on 0.10.0 (22344 B of flash).
- **The e-stop input, command watchdog and status LED come from
  bread-slice-core 0.1** (feastorg/.github#4). `bsc::EStop`,
  `bsc::bootWait()`, `bsc::CommandWatchdog` and `bsc::StatusLed` replace
  `pollEStop()`, the `wd*` globals, `slice.eStop`, `estopCount` and FastLED.
  The sampling, debounce, latch and colours are unchanged; what a press or a
  trip does stays here. Three differences:
  - the boot wait restamps the watchdog when it ends, so a
    `DCMT_WATCHDOG_BOOT_MS` under a second no longer trips on the first
    loop pass;
  - the press and the trip are latched, with interrupts off, before the
    motors are braked; they were latched after;
  - the timeout comparison runs inside the window that sets the latch.

  FastLED is gone from `lib_deps`; this replaces #55. Gen2 flash drops by
  2410 to 2422 B and RAM by 76 to 108 B. Gen1 has no LED; it grows by 310
  to 366 B of flash and 4 B of RAM for the core's e-stop and watchdog, which
  keep the pin, polarity and debounce as members where the old code had
  constants. New host tests cover the
  e-stop hold and the watchdog trip end to end.
- Requires DCMotorServo `^1.2.0`, LMD18200 `^0.1.5` and
  bread-crumbs-contracts `^0.10.0` (`firmware/platformio.ini` and
  `slice.yaml`; #63). `^1.0.1` let a tree with a cached 1.0.1 keep it,
  which drives forward at skip PWM for one cycle on every `run()` after
  `stop()`. 1.2.0 carries the speed-loop fixes behind #62
  (CameronBrooks11/DCMotorServo#12, CameronBrooks11/DCMotorServo#13) and
  `setMaxLead()`; 0.1.5 sets PWM low before releasing the brake.
  Contracts 0.10.0 only adds flags DCMT does not advertise.
- The PlatformIO platforms are pinned: `platformio/atmelavr @ 5.3.0` and
  `platformio/atmelmegaavr @ 1.10.0`, the versions local builds and CI
  already resolved. Before, a clean build took whatever platform release
  was newest, so the compiler and Arduino core could change between two
  builds of the same commit. A platform update is now a change to
  `firmware/platformio.ini`.
- Requires bread-crumbs-contracts `^0.9.0` (`firmware/platformio.ini` and
  `slice.yaml`), for `bread_refuse.h`, `DCMT_CAP_REFUSES` and
  `DCMT_PWM_MAX`.
- **Migrated to CRUMBS 0.16.0 and bread-crumbs-contracts 0.8.0** (was CRUMBS
  0.14.0 / contracts 0.6.0 in `firmware/platformio.ini`; `slice.yaml`
  `lib_deps` were stale at 0.12.4 / 0.4.3 and now match). The version reply's
  CRUMBS field reports 1600.
- **SET_PID adopts the contracts 0.7.0 layout (module major 2).** The payload
  is 13 bytes, `[loop:u8][kp1..kd2 u16 x1000]`: `dcmt_parse_set_pid_payload`
  rejects any other length, and `loop` (`DCMT_PID_LOOP_POSITION` or
  `DCMT_PID_LOOP_SPEED`) selects which loop of both motors the gains tune,
  leaving the other loop's gains unchanged. A loop the firmware does not
  define -- `DCMT_PID_LOOP_SPEED` when the speed loop is not built -- is
  rejected and nothing is applied. Was six `u8` gains x10 written to both
  loops at once. `DCMT_MODULE_VER_MAJOR` is now 2 (from contracts); a
  controller built for one major refuses firmware reporting the other.
- **GET_STATE carries `estop_count`.** The reply is the fixed 20-byte
  `dcmt_state` layout with `estop_count` at `DCMT_STATE_OFF_ESTOP_COUNT`: a
  count of hardware e-stop presses since boot, wrapping at 255, so a
  controller sees a press it did not poll during. A new volatile `estopCount`
  is incremented on the released->pressed transition in `processEStop()`,
  mirroring `wdTripCount`.

### Fixed

- **A build without `DCMT_HW_GEN` now fails instead of building gen1** (#17).
  `config.h` defaulted the generation to 1 when the flag was absent, so the
  `#error "Unsupported DCMT_HW_GEN value"` guards in `config_hardware.h`
  could only catch an out-of-range value, never a missing one. A gen2 board
  built outside the PlatformIO envs got the gen1 motor pin map: `MOTOR1_DIR`
  5, `MOTOR1_BRAKE` 7, `MOTOR2_DIR` 9 and `MOTOR2_BRAKE` 11 instead of 7, 8,
  11 and 12. The `gen*` envs all set the flag and are unaffected;
  `nano_base` and `nanoevery_base` now fail to build, as they should.
  Firmware Build checks that a build without the flag is rejected.

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

# Host tests

The firmware's own sources (`firmware/src`), built with the host C++ compiler
and run on the host. Nothing is reimplemented:

- **Real:** `main.cpp`, `dcmt_handlers.cpp`, `serialCommands.cpp`, the CRUMBS
  core, the contracts headers and bread-slice-core. These come from
  PlatformIO's libdeps, so they are the versions the firmware builds with.
  bread-slice-core is built with `BSC_HOST_TEST`, so its status LED records
  the last frame (`bsc_host_led_frame`) instead of sending it.
- **Stubbed** (`stubs/`): the Arduino core, the AVR watchdog, the LMD18200
  driver, Encoder, DCMotorServo and DCMotorTacho. The driver stub records
  what the firmware last asked of each motor. The servo stub's `run()` writes
  `host_servo_output[n]` through the firmware's motor callback, as the real
  library's output does, and `stop()` brakes through it.

The motor tests (`make motor`) build the same firmware with the real
DCMotorServo, DCMotorTacho and PID_v1 from libdeps in place of the two
motor-library stubs, so the closed loop runs as it does on the board, with the
firmware's own gains, PWM limits, speed interval and lead cap. `motor_sim.cpp`
models a motor on each channel between the LMD18200 and Encoder stubs.

Frames go in through `crumbs_peripheral_handle_receive()` and replies come
back through `crumbs_peripheral_build_reply()`, the two calls the I2C
interrupts make. So a test sees what a controller on the bus would see,
including GET_ACK.

## Run

```sh
cd firmware
pio pkg install -e gen2_nano   # once, for the libraries
make -C test/host              # the stub tests, then the motor tests
make -C test/host test         # the stub tests only
make -C test/host motor        # the motor tests only
```

Each test is built once per PlatformIO env (`gen1_nano`, `gen2_nano`,
`gen1_nanoevery`, `gen2_nanoevery`), with that env's `DCMT_HW_GEN` and MCU
macros. The pin map, status LED and speed loop then follow the same `#if`
paths as on the board. The motor tests are built for `gen1_nanoevery` and
`gen2_nanoevery` only, the envs that build the speed loop. The four
`_noenc` envs, with no encoder on either port, run `test_noenc` and the stub
tests that don't use a closed loop (`NOENC_SINGLE` in the Makefile). `make`
exits non-zero if any check fails.

## Tests

| File | Covers |
| --- | --- |
| `test_dispatch.cpp` | Which frames are taken as commands: every opcode is swept, GET opcodes sent as commands are refused, queries answer through SET_REPLY, and another family's type is rejected (#45, #50) |
| `test_noenc.cpp` | A build with no encoder on either port (the `_noenc` envs): no Encoder constructed, GET_CAPS level 1 with flags `0xDE3`, the closed-loop modes, setpoints and gains refused NOT_BUILT and stored nowhere, open loop driving both motors with positions 0, a smuggled closed-loop mode falling back to braked open loop, the serial position commands absent, GET_FAULTS 0 |
| `test_applied_pwm.cpp` | GET_STATE `m1_pwm`/`m2_pwm` report the PWM each motor is driven at: open loop, braked, position and speed (the servo stub's output), and e-stop (#61) |
| `test_mode_pwm.cpp` | Leaving open loop clears the open-loop PWM through serial `MODE=` and through `SET_MODE`, so GET_STATE reads 0 and a return to open loop doesn't resume the old PWM (#36) |
| `test_serial_burst.cpp` | A burst of serial lines is answered one line per `loop()` pass, so an e-stop pressed mid-burst is asserted before the next line and the lines after it are refused (#49) |
| `test_max_lead.cpp` | `setup()` caps both speed loops' lead at `DCMT_TACHO_MAX_LEAD` where the speed loop is built, and sets no cap where it isn't (#63) |
| `test_estop_hold.cpp` | An e-stop press during open-loop drive: GET_STATE reports it and its count, both motors brake, a SET is refused `ESTOP`, the release is debounced and resumes nothing, and a second press counts (`DCMT_CAP_ESTOP_HOLD`) |
| `test_watchdog_trip.cpp` | A command-watchdog trip: GET_WATCHDOG reports it, both motors brake, a SET is refused `WATCHDOG_TRIP`, a re-arm leaves it latched, and `CLEAR_WATCHDOG_TRIP` lifts it, keeps the count and resumes nothing |
| `test_serial_numbers.cpp` | Serial numbers are parsed strictly: each row of #60 (trailing junk, empty, NaN, inf, out of range, wrong field count) is refused with no change, and in-range values still apply (#60) |
| `test_setpoint_loop.cpp` | SET_SETPOINT names its loop: the 4-byte form writes the position setpoints only and the 5-byte form the named loop's; other lengths are refused `MALFORMED`, an undefined loop `UNDEFINED_VALUE`, and loop SPEED without the speed loop `NOT_BUILT`. Speed mode after a position target requests 0, and after a speed target that target; a speed target keeps the post-hold position follow, a position target ends it; GET_CAPS sets `DCMT_CAP_SETPOINT_LOOP` (#57) |
| `test_watchdog_lease.cpp` | The commands-only lease: polls, SET_PID and serial lines no longer refresh the watchdog while SET_MODE, SET_BRAKE and a re-sent SET_WATCHDOG do; the 2-byte form and serial `WDOG=` end the lease; an undefined flag is refused and arms nothing; GET_CAPS sets `DCMT_CAP_WATCHDOG_LEASE` (bread-crumbs-contracts#63) |
| `test_stall_fault.cpp` | Stall faults with the servo stub's stall latch standing in for the library's: setup() enables detection at `DCMT_STALL_TIMEOUT_MS`/`DCMT_STALL_MIN_COUNTS`; a stall latches `DCMT_FAULT_M1_STALL`/`_M2_STALL`, brakes both motors and leaves no motion pending; SET_OPEN_LOOP, SET_SETPOINT, SET_MODE and a brake release are refused `FAULT` (an e-stop's reason wins); GET_FAULTS, CLEAR_FAULTS (a payload is `MALFORMED`) and serial `FAULTCLEAR` clear and resume nothing; GET_STATE stays 20 bytes; GET_CAPS sets `DCMT_CAP_FAULTS` (#64) |
| `test_speed_freeze.cpp` | The speed loop's integral freeze (#89): while a motor's lead reaches `DCMT_TACHO_MAX_LEAD` either way, its speed PID runs with Ki 0 and the runtime Kp and Kd, including a SET_PID sent while clamped; the other motor, and the same one once the lead drops, run the runtime gains in full. Skips where the speed loop isn't built |
| `test_hold_brakes.cpp` | `motorControlLogic()`'s hold: with an e-stop, a watchdog trip or a stall fault each alone, both motors are braked on every pass and no PWM is written, even with the brake flags cleared and a PWM stored |
| `test_status_led.cpp` | Blue through the boot wait, then green or red on the first `loop()` pass, with the e-stop released or held from power-up; no LED driven on gen1 (#56) |
| `test_status_led_hold.cpp` | Amber while a command-watchdog trip or a latched stall fault holds the motors, red over amber while the e-stop is held, amber again on the release while the hold stays latched, and green once it is cleared; no LED driven on gen1 |

Motor tests (`make motor`):

| File | Covers |
| --- | --- |
| `test_speed_entry_62.cpp` | Closed-loop speed entered after open-loop travel starts from where the shaft is: after full reverse PWM, and after full forward PWM and a stop, speed mode at +60 RPM never drives backwards or at `DCMT_SERVO_MAX_PWM`, and the stopped shaft never moves back (#62) |
| `test_speed_baseline_90.cpp` | The #90 regression, with the default speed gains: at 60 RPM, a 200 ms mean is within 5 % of the target by 2 s after the command, speed is within 1 RPM over 9-10 s and 15-20 s, and the peak 200 ms mean overshoots by under 10 % |
| `test_stall_release_89.cpp` | The #89 regression: at 60 RPM with the shaft held for 100 ms under `DCMT_STALL_TIMEOUT_MS`, no fault latches and the peak 1 s speed after release is within 5 RPM of the target, where the wound-up integral used to lunge; a 4 s hold, #89's own, latches a stall fault and the shaft stays braked after release (#64) |
| `test_speed_zero_reads_0.cpp` | GET_STATE `spd1` reads 0 once motor 1 stops at a speed setpoint of 0, after 3 s at 60 RPM; before DCMotorServo 1.2.1 it kept the last running speed (feastorg/dcmotorservo#18) |
| `test_stall_fault_64.cpp` | Stall detection with the real libraries (#64), one process per case: a shaft held at 60 RPM latches `M1_STALL` `DCMT_STALL_TIMEOUT_MS` +/- 50 ms after the hold and stays braked and still after release; position mode at rest inside the deadband for 10 s never latches; a shaft held 5000 counts from its position target latches; 20 s at 10 RPM never latches |

## Writing a test

- Include `host.h` for the fake clock and e-stop line, and for
  `host_send()`, `host_query()`, `host_read_ack()` and `host_check()`.
- `millis()` advances 1 ms per `digitalRead()`, so `setup()`'s one-second
  boot wait ends after about 1000 reads. Set `host_now` to jump ahead.
- `setup()` and `loop()` keep state in file statics. Each case that needs a
  cold boot runs as its own process: see the `test` target in the Makefile.
- Add the file to `SINGLE` in the Makefile and a row to the table above.

## Writing a motor test

- Include `motor_sim.h`. `sim_boot()` runs `setup()` and one pass;
  `sim_run(ms)` runs `loop()` for that much fake time and steps both motor
  models after every pass, and records motor 1's encoder count and applied
  PWM in `sim_trace`.
- `sim_speed_run(rpm, duration, holdFrom, holdTo)` puts motor 1 in speed mode
  at `rpm` through SET_SETPOINT and SET_MODE, holds its shaft over
  `[holdFrom, holdTo)` ms after the command, and returns the command's time
  `t0`. Measure with `sim_rpm(t0 + a, t0 + b)` (mean RPM from position) or
  `sim_peak_rpm(from, to, window)`.
- `sim_motor[n]` is each motor's model: set `held` directly to hold a shaft
  outside `sim_speed_run()`, or its gain, deadband and time constant.
- To run against another DCMotorServo checkout, pass `DCMS=<its src/>` and a
  separate `OUT=`, so the default build isn't replaced.
- Add the file to `MOTOR_SINGLE` in the Makefile and a row to the table
  above.

The motor model is a first-order fit (24.3 counts/s per PWM above a deadband
of 10, 30 ms), not a measured motor, so the motor tests show what the
firmware and libraries do with it. Anything that needs the board is #32.

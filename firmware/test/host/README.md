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

Frames go in through `crumbs_peripheral_handle_receive()` and replies come
back through `crumbs_peripheral_build_reply()`, the two calls the I2C
interrupts make. So a test sees what a controller on the bus would see,
including GET_ACK.

## Run

```sh
cd firmware
pio pkg install -e gen2_nano   # once, for CRUMBS and the contracts
make -C test/host
```

Each test is built once per PlatformIO env (`gen1_nano`, `gen2_nano`,
`gen1_nanoevery`, `gen2_nanoevery`), with that env's `DCMT_HW_GEN` and MCU
macros. The pin map, status LED and speed loop then follow the same `#if`
paths as on the board. `make` exits non-zero if any check fails.

## Tests

| File | Covers |
| --- | --- |
| `test_dispatch.cpp` | Which frames are taken as commands: every opcode is swept, GET opcodes sent as commands are refused, queries answer through SET_REPLY, and another family's type is rejected (#45, #50) |
| `test_applied_pwm.cpp` | GET_STATE `m1_pwm`/`m2_pwm` report the PWM each motor is driven at: open loop, braked, position and speed (the servo stub's output), and e-stop (#61) |
| `test_mode_pwm.cpp` | Leaving open loop clears the open-loop PWM through serial `MODE=` and through `SET_MODE`, so GET_STATE reads 0 and a return to open loop doesn't resume the old PWM (#36) |
| `test_serial_burst.cpp` | A burst of serial lines is answered one line per `loop()` pass, so an e-stop pressed mid-burst is asserted before the next line and the lines after it are refused (#49) |
| `test_max_lead.cpp` | `setup()` caps both speed loops' lead at `DCMT_TACHO_MAX_LEAD` where the speed loop is built, and sets no cap where it isn't (#63) |
| `test_estop_hold.cpp` | An e-stop press during open-loop drive: GET_STATE reports it and its count, both motors brake, a SET is refused `ESTOP`, the release is debounced and resumes nothing, and a second press counts (`DCMT_CAP_ESTOP_HOLD`) |
| `test_watchdog_trip.cpp` | A command-watchdog trip: GET_WATCHDOG reports it, both motors brake, a SET is refused `WATCHDOG_TRIP`, a re-arm leaves it latched, and `CLEAR_WATCHDOG_TRIP` lifts it, keeps the count and resumes nothing |
| `test_serial_numbers.cpp` | Serial numbers are parsed strictly: each row of #60 (trailing junk, empty, NaN, inf, out of range, wrong field count) is refused with no change, and in-range values still apply (#60) |
| `test_status_led.cpp` | Blue through the boot wait, then green or red on the first `loop()` pass, with the e-stop released or held from power-up; no LED driven on gen1 (#56) |

## Writing a test

- Include `host.h` for the fake clock and e-stop line, and for
  `host_send()`, `host_query()`, `host_read_ack()` and `host_check()`.
- `millis()` advances 1 ms per `digitalRead()`, so `setup()`'s one-second
  boot wait ends after about 1000 reads. Set `host_now` to jump ahead.
- `setup()` and `loop()` keep state in file statics. Each case that needs a
  cold boot runs as its own process: see the `test` target in the Makefile.
- Add the file to `SINGLE` in the Makefile and a row to the table above.

Not covered here: the closed-loop control itself, which belongs to the
DCMotorServo library (CameronBrooks11/DCMotorServo#12 and #13 add its first
host tests), and anything that needs the board (#32).

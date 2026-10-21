# Host tests

The firmware's own sources (`firmware/src`), built with the host C++ compiler
and run on the host. Nothing is reimplemented:

- **Real:** `main.cpp`, `dcmt_handlers.cpp`, `serialCommands.cpp`, the CRUMBS
  core, and the contracts headers. CRUMBS and the contracts come from
  PlatformIO's libdeps, so they are the versions the firmware builds with.
- **Stubbed** (`stubs/`): the Arduino core, the AVR watchdog, FastLED, the
  LMD18200 driver, Encoder, DCMotorServo and DCMotorTacho. The driver stub
  records what the firmware last asked of each motor, and the FastLED stub
  records every colour shown.

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
| `test_mode_pwm.cpp` | Leaving open loop clears the open-loop PWM through serial `MODE=` and through `SET_MODE`, so GET_STATE reads 0 and a return to open loop doesn't resume the old PWM (#36) |
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

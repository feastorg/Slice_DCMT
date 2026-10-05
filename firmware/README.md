# DCMT Firmware Profiles

This firmware separates three concerns:

1. Hardware generation (`DCMT_HW_GEN`)
- selects pin/electrical mapping only.

2. MCU performance profile
- derived from board target by default.
- classic Nano (AVR): speed loop disabled by default.
- Nano Every (megaAVR): speed loop enabled by default.

3. I2C address (`I2C_ADR`)
- independent from generation and board.
- override with `-DI2C_ADR=<addr>`.

## Environment Matrix

- `gen1_nano`
- `gen2_nano`
- `gen1_nanoevery`
- `gen2_nanoevery`

All environments support:

- open-loop
- closed-loop position

Speed closed-loop is controlled by `DCMT_ENABLE_SPEED_LOOP`.
Default behavior is board-dependent (see above), and can be overridden via build flag.

## Closed-Loop Backend

The runtime uses:

- `DCMotorServo` for closed-loop position control
- `DCMotorTacho` for cascaded closed-loop speed control

Current defaults mirror archive-proven values:

- `MOTOR1_CPR = 798`
- `MOTOR2_CPR = 798`
- outer PID defaults: `0.15 / 0.01 / 0.01`
- inner PID defaults: `0.15 / 0.01 / 0.01`
- servo PWM skip: `15`
- servo max PWM: `150`
- servo accuracy: `10`
- tacho interval: `5 ms`

Speed commands/capability are only available when `DCMT_ENABLE_SPEED_LOOP=1`.

## Capability Mapping (`GET_CAPS`)

- Level 2: baseline + closed-position + PID tuning
- Level 3: Level 2 + closed-speed

Controllers should gate behavior by capability flags, not by generation labels.

All levels also advertise `DCMT_CAP_CMD_WATCHDOG` and
`DCMT_CAP_CLEAR_WATCHDOG_TRIP`.

## Command Watchdog

The watchdog boots disarmed (unless built with `-DDCMT_WATCHDOG_BOOT_MS=<ms>`).
Once armed, by `BREAD_OP_SET_WATCHDOG` or `WDOG=<ms>`, it trips when no I2C
command, I2C reply or serial line has arrived within the timeout. A trip
brakes both motors and holds them braked until the trip is cleared; it zeroes
the PWM commands and speed setpoints and keeps the mode and the position
setpoints.

A trip latches. It is cleared only by:

- `BREAD_OP_CLEAR_WATCHDOG_TRIP` (`0x7C`, empty payload) over the bus; a
  frame with a non-empty payload is ignored and the trip stays set;
- the serial `WDCLEAR` command;
- a reboot.

`BREAD_OP_SET_WATCHDOG` and the serial `WDOG=<ms>` command set the timeout
(`0` disarms) and refresh liveness, but do not clear a trip. Any other bus
frame or serial line (including `READ`) refreshes liveness only. Clearing
leaves the timeout, the armed state and the trip count as they were.

Clearing releases the hold into whatever the stored state then commands.
SET commands received while tripped are stored, not ignored. In open-loop
the motors are driven at the stored PWM, which is 0 unless a
`SET_OPEN_LOOP` arrived during the hold (the brake is released unless
`BRAKE1`/`BRAKE2` is set); in closed-loop position the servos resume driving
to the position setpoints. A motor can therefore move as soon as the trip
is cleared.

Serial commands (115200 baud, newline-terminated):

- `WDOG=<ms>` - arm with that timeout, or disarm with `0`
- `WDCLEAR` - clear a latched trip
- `READ` - print state, ending `WDOG:<timeout|off>, WDTRIP:<tripped>/<trip count>`

## References

- Arduino Nano: https://store.arduino.cc/products/arduino-nano
- Arduino Nano Every: https://store.arduino.cc/products/nano-every
- Nano Every docs: https://docs.arduino.cc/hardware/nano-every

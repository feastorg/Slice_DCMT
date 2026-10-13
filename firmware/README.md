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
brakes both motors and leaves no motion pending: it zeroes the PWM commands
and speed setpoints, sets both brake flags, and keeps both position
setpoints at the encoder positions for as long as the trip is held, so they
follow a shaft that coasts to a stop after the brake engages. The mode is
kept. `GET_STATE` after a trip therefore shows both brakes engaged and, in
closed-loop position, the setpoint at the position where the shaft stopped.

A trip latches. It is cleared only by:

- `BREAD_OP_CLEAR_WATCHDOG_TRIP` (`0x7C`, empty payload) over the bus; a
  frame with a non-empty payload is ignored and the trip stays set;
- the serial `WDCLEAR` command;
- a reboot.

`BREAD_OP_SET_WATCHDOG` and the serial `WDOG=<ms>` command set the timeout
(`0` disarms) and refresh liveness, but do not clear a trip. Any other bus
frame or serial line (including `READ`) refreshes liveness only. Clearing
leaves the timeout, the armed state and the trip count as they were.

While a trip is held, commands that could make a motor move are ignored,
not stored: `SET_OPEN_LOOP`, `SET_SETPOINT`, `SET_MODE` and a `SET_BRAKE`
that would release a brake, and over serial `MODE=`, `M1PWM=`/`M2PWM=`,
`M1POS=`/`M2POS=`, `M1SPEED=`/`M2SPEED=`, `BRAKE1=0` and `BRAKE2=0`.
Engaging a brake, PID tuning (`SET_PID`, `PIDPOS=`, `PIDSPEED=`),
`SET_WATCHDOG`/`WDOG=` and `READ` still work.

Clearing a trip resumes nothing; the brakes stay engaged and no motor
moves. To resume:

1. clear the trip (`BREAD_OP_CLEAR_WATCHDOG_TRIP` or `WDCLEAR`);
2. release the brakes (`SET_BRAKE(0, 0)`, or `BRAKE1=0` and `BRAKE2=0`).
   In closed-loop position the motors then hold the position where they
   stopped, in open-loop they coast at PWM 0, and in closed-loop speed
   they stay stopped;
3. send new commands: setpoints first, then the mode. A mode change to
   closed-loop position drives to whatever setpoint is stored at that
   moment.

Positions are `int16` on the wire, so closed-loop position works only while
the encoder count stays within ±32767. Beyond that the stored setpoint is
clamped to ±32767, including the one a trip writes, and releasing the brakes
in position mode drives the motor toward the clamp value. Keep position-mode
travel inside that range.

Serial commands (115200 baud, newline-terminated):

- `WDOG=<ms>` - arm with that timeout, or disarm with `0`
- `WDCLEAR` - clear a latched trip
- `READ` - print state, ending `WDOG:<timeout|off>, WDTRIP:<tripped>/<trip count>`

## References

- Arduino Nano: https://store.arduino.cc/products/arduino-nano
- Arduino Nano Every: https://store.arduino.cc/products/nano-every
- Nano Every docs: https://docs.arduino.cc/hardware/nano-every

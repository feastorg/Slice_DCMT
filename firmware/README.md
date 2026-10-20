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

All levels also advertise `DCMT_CAP_CMD_WATCHDOG`,
`DCMT_CAP_CLEAR_WATCHDOG_TRIP`, `DCMT_CAP_ESTOP_HOLD` and
`DCMT_CAP_REFUSES`.

## Refused Commands

A SET the firmware does not act on in full is refused with
`crumbs_refuse()`, so CRUMBS's GET_ACK reports it refused, with a reason from
bread-crumbs-contracts `bread_refuse.h`:

| Reason | When |
| --- | --- |
| `BREAD_REFUSE_MALFORMED` | the payload does not unpack; SET_PID is not 13 bytes; SET_WATCHDOG lacks its u16; CLEAR_WATCHDOG_TRIP carries a payload |
| `BREAD_REFUSE_ESTOP` | SET_OPEN_LOOP, SET_MODE or SET_SETPOINT while the hardware e-stop is held, or a SET_BRAKE that asks for a release then; this reason wins when a watchdog trip is held too |
| `BREAD_REFUSE_WATCHDOG_TRIP` | SET_OPEN_LOOP, SET_MODE or SET_SETPOINT while a trip is held, or a SET_BRAKE that asks for a release then |
| `BREAD_REFUSE_UNDEFINED_VALUE` | a SET_MODE or SET_PID loop the contract does not define; a SET_OPEN_LOOP PWM outside `-DCMT_PWM_MAX` to `DCMT_PWM_MAX` (255) |
| `BREAD_REFUSE_NOT_BUILT` | closed-loop speed (SET_MODE or SET_PID loop SPEED) when the speed loop is not built |
| `BREAD_REFUSE_WRONG_MODE` | SET_OPEN_LOOP outside open loop |
| `BREAD_REFUSE_UNKNOWN_OP` | an opcode that is not a DCMT or bread-common command, including a GET opcode sent as a command frame instead of through SET_REPLY |

A SET_BRAKE that asks for a release while tripped or e-stopped still engages
any brake it names, then refuses, and a SET_OPEN_LOOP with one PWM out of range still sets
the other motor's, then refuses. GET_STATE shows what took effect. Every valid frame
refreshes the watchdog, a refused one included.

The firmware declares its type (`DCMT_TYPE_ID`) to CRUMBS, so a frame typed
for another family is rejected before any handler runs: GET_ACK reports it
rejected with `CRUMBS_ACK_REJECT_TYPE`, and it does not refresh the
watchdog. Frames of type `0x00` are accepted. GET_ACK replies carry type
`0x02`.

## Command Watchdog

The watchdog boots disarmed (unless built with `-DDCMT_WATCHDOG_BOOT_MS=<ms>`).
Once armed, by `BREAD_OP_SET_WATCHDOG` or `WDOG=<ms>`, it trips when no I2C
command, I2C reply or serial line has arrived within the timeout. A trip
brakes both motors and leaves no motion pending: it zeroes the PWM commands
and speed setpoints, sets both brake flags, and keeps each position
setpoint at its encoder position until that motor's brake is released or
a new setpoint for it arrives, through the clear, so it follows a shaft
that coasts or creeps after the brake engages. The mode is kept. `GET_STATE` after a trip therefore shows both brakes engaged and, in
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
The I2C ones are refused with `BREAD_REFUSE_WATCHDOG_TRIP`, so GET_ACK says
so (see Refused Commands). Engaging a brake, PID tuning (`SET_PID`,
`PIDPOS=`, `PIDSPEED=`), `SET_WATCHDOG`/`WDOG=` and `READ` still work.

Clearing a trip resumes nothing; the brakes stay engaged and no motor
moves. To resume:

1. clear the trip (`BREAD_OP_CLEAR_WATCHDOG_TRIP` or `WDCLEAR`);
2. release the brakes (`SET_BRAKE(0, 0)`, or `BRAKE1=0` and `BRAKE2=0`).
   In closed-loop position the motors then hold the position where they
   are, in open-loop they coast at PWM 0, and in closed-loop speed they
   stay stopped. A setpoint sent before this step ends the follow and
   stands as sent, so the brake release drives to it;
3. send new commands: setpoints first, then the mode. A mode change to
   closed-loop position drives to whatever setpoint is stored at that
   moment.

Positions are `int16` on the wire, so closed-loop position works only while
the encoder count stays within ±32767. Beyond that the stored setpoint is
clamped to ±32767, including the one a trip or an e-stop press writes, and
releasing the brakes
in position mode drives the motor toward the clamp value. Keep position-mode
travel inside that range.

Serial commands (115200 baud, newline-terminated):

- `WDOG=<ms>` - arm with that timeout, or disarm with `0`
- `WDCLEAR` - clear a latched trip
- `READ` - print state, ending `WDOG:<timeout|off>, WDTRIP:<tripped>/<trip count>`

## Hardware E-stop

The e-stop input is D2 with the internal pull-up; a press pulls it LOW. The
firmware samples it on every loop pass and through the 1 s boot wait. The
first LOW sample asserts the e-stop, and release needs the line HIGH for
25 ms without a LOW sample in between, so a chattering line stays stopped.
A LOW glitch that a loop pass samples, such as driver noise on an
unpressed line, asserts the e-stop too.

A press holds the way a watchdog trip does (`DCMT_CAP_ESTOP_HOLD`): both
motors brake, the PWM commands and speed setpoints go to 0, both brake
flags are set, and each position setpoint follows its encoder until that
motor's brake is released or a new setpoint for it arrives. While the
e-stop is held, the I2C commands the watchdog section lists are refused
with `BREAD_REFUSE_ESTOP`, which wins when a trip is held too, and the
serial ones are ignored with a message. `GET_STATE` shows `estop` 1 and
the shafts' positions, and `estop_count` counts the press.

Releasing the e-stop moves nothing. Resume as after a cleared trip: release
the brakes, then send setpoints, then the mode.

The firmware hold is the only stop on gen1 boards, which have no hardware
cut of motor power on the e-stop line. Check a later board's schematic
before relying on a hardware cut; where one exists, the firmware hold
keeps the motors from running stored commands when that power returns.
An e-stop that cuts the supply instead
reboots the slice into open loop at PWM 0; power the Nano from the switched
supply too, since a Nano kept alive over USB sees no stop and runs its
stored commands when motor power returns. bread-crumbs-contracts
`docs/controller-compatibility.md` covers both wirings.

## References

- Arduino Nano: https://store.arduino.cc/products/arduino-nano
- Arduino Nano Every: https://store.arduino.cc/products/nano-every
- Nano Every docs: https://docs.arduino.cc/hardware/nano-every

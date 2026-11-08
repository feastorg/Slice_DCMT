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

4. What each encoder header carries (`DCMT_PORT1_INPUT`, `DCMT_PORT2_INPUT`)
- `DCMT_INPUT_ENCODER` (the default): a quadrature encoder.
- `DCMT_INPUT_NONE`: nothing; no Encoder is built on that port's pins and
  the motor's position reads 0. That 0 is not a position: GET_STATE has no
  sentinel for it yet (bread-crumbs-contracts#73), so the missing
  closed-loop flags in GET_CAPS are what say it means nothing.
- `DCMT_INPUT_ANALOG`: no Encoder either, and the port's two pins are read
  as analog inputs, reported by GET_INPUTS (see Analog Inputs). The motor's
  position reads 0, as with `NONE`.
- port 1 is motor 1, port 2 motor 2. Closed loop is built only when both
  ports carry an encoder, since one mode applies to both motors.

## Environment Matrix

- `gen1_nano`
- `gen2_nano`
- `gen1_nanoevery`
- `gen2_nanoevery`

These support:

- open-loop
- closed-loop position

Speed closed-loop is controlled by `DCMT_ENABLE_SPEED_LOOP`.
Default behavior is board-dependent (see above), and can be overridden via build flag.

Each has a `_noenc` twin for motors without encoders (`gen1_nano_noenc`,
`gen2_nano_noenc`, `gen1_nanoevery_noenc`, `gen2_nanoevery_noenc`): both
ports `DCMT_INPUT_NONE`, open loop only. Flash these to a board whose
motors have no encoders, so it doesn't advertise closed loop it can't run.
The closed-loop modes, setpoints and gains are refused
`BREAD_REFUSE_NOT_BUILT` over I2C; over serial, `MODE=POS`, `M1POS=`,
`M2POS=` and `PIDPOS=` don't exist, as the speed commands don't on a build
without the speed loop.

The `_analog1` envs (`gen1_nano_analog1`, `gen2_nano_analog1`,
`gen1_nanoevery_analog1`, `gen2_nanoevery_analog1`) set port 1
`DCMT_INPUT_ANALOG` and port 2 `DCMT_INPUT_NONE`, for a board that reads an
analog sensor on port 1, such as the turbidity sensor of #106. They are
open loop only, with the `_noenc` refusals and serial commands, and
GET_CAPS adds `DCMT_CAP_INPUTS`. Other mixes come from the build flags.

## Analog Inputs (`GET_INPUTS`)

A port built `DCMT_INPUT_ANALOG` reads both of its pins: port 1
`MOTOR1_ENCODER_PIN1`/`PIN2` (A2/A3) as `p1a`/`p1b`, port 2
`MOTOR2_ENCODER_PIN1`/`PIN2` (A0/A1) as `p2a`/`p2b`. Which header
terminal reaches which pin is in `docs/hardware-revisions.md`.

- **Reader** (`src/analog_inputs.cpp`): one `analogRead()` per `loop()`
  pass, round-robin over the analog pins, with no interrupts masked during
  the conversion. Each pin's 16 reads are averaged, rounded to the nearest
  count, into one published value, so a value refreshes every 16 x (analog
  pins) passes: 32 on the `_analog1` envs.
- **Reference:** the Nano Every calls `analogReference(VDD)` at setup, since
  the megaAVR core's `DEFAULT` is `INTERNAL0V55` (its `init()` already
  selects VDD; the call makes it this firmware's choice rather than the
  core's). The classic Nano keeps its default, AVcc.
- **GET_INPUTS** (`DCMT_OP_GET_INPUTS`, 0x81, through SET_REPLY like
  GET_STATE), 10 bytes from bread-crumbs-contracts 0.12
  (`dcmt_inputs_pack`): `[valid:u8][bits:u8][p1a:u16][p1b:u16][p2a:u16][p2b:u16]`.
  `valid` has `DCMT_INPUT_VALID_P1A`/`P1B`/`P2A`/`P2B` (0x01/0x02/0x04/0x08),
  set once that pin's first average is published. `bits` is 10, so
  V = counts x 5 V / 1024. A pin that is not analog, or not yet averaged,
  reads 0 with its bit clear.
- Built only when a port is `ANALOG`. On other builds GET_INPUTS has no
  reply handler, so a query gets no frame back, and `DCMT_CAP_INPUTS` is
  not advertised: query it only when the flag is.

## Closed-Loop Backend

The runtime uses:

- `DCMotorServo` for closed-loop position control
- `DCMotorTacho` (DCMotorServo 2.x) for closed-loop speed control: a PI on
  PWM around a per-motor feedforward (see Speed Loop)

These defaults are a starting point for one motor, not properties of the
firmware. They come from a single bench motor: the position gains and servo
values are the archive implementation's, and the speed gains were checked
against a first-order model fitted to that motor (#90; the model lives in
the host motor tests, `firmware/test/host/motor_sim.h`). Tune them for each
motor: the gains at runtime with `SET_PID` (or serial `PIDPOS=` /
`PIDSPEED=`), the speed feedforward with `SET_SPEED_FF` (or serial
`SPEEDFF=`), and every value per build with a flag such as
`-DDCMT_SPEED_PID_KI=<ki>` (each is `#ifndef`-guarded in
`include/config_hardware.h`).

- `MOTOR1_CPR = 848`
- `MOTOR2_CPR = 848` (measured on the bench, #103)
- outer (position) PID defaults: `0.15 / 0.01 / 0.01`
- speed PI defaults: Kp `0.6` PWM per RPM, Ki `3.0` PWM per RPM-second
  (Kd `0.0`, accepted and unused)
- speed feedforward default: none (static 0, RPM per PWM 0, trim 255): a
  plain PI until a controller sends `SET_SPEED_FF`
- servo PWM skip: `15`
- servo max PWM: `150`
- servo accuracy: `10`
- tacho interval: `5 ms`
- stall timeout / minimum counts: `1000 ms` / `4` (see Stall Faults)

Speed commands/capability are only available when `DCMT_ENABLE_SPEED_LOOP=1`.

## Speed Loop

Module major 3 (bread-crumbs-contracts 0.13.0, DCMotorServo 2.0.1). Every
speed interval (`DCMT_TACHO_INTERVAL_MS`) each motor is driven at

```text
pwm         = feedforward + trim
feedforward = sign(target) * static + target / rpm_per_pwm
trim        = Kp * error + integral, bounded to +-trim
```

with error the target RPM less the measured RPM, and the result bounded to
the servo max PWM. The integral doesn't accumulate while the trim or the
output is at its bound in the error's direction, so a held shaft banks at
most `trim` of drive and doesn't lunge on release (#89). Stall detection
(see Stall Faults) still latches a shaft held for the stall timeout.

- **Gains:** `SET_PID` with `DCMT_PID_LOOP_SPEED`, or serial
  `PIDSPEED=kp,ki,kd`. Kp is PWM per RPM of speed error and Ki PWM per
  RPM-second. Kd is accepted and stored, and unused: the loop is a PI. Gains
  from module major 2 firmware (DCMotorServo 1.x) don't carry over; they
  were in the old loop's units.
- **Feedforward:** `SET_SPEED_FF` (`DCMT_OP_SET_SPEED_FF`, advertised as
  `DCMT_CAP_SPEED_FEEDFORWARD`, bit 13, only where the speed loop is built),
  8 bytes `[static1:u8][rpm_per_pwm1:u16][trim1:u8][static2:u8][rpm_per_pwm2:u16][trim2:u8]`:
  - `static`: PWM that just starts the shaft turning, 0 to 255;
  - `rpm_per_pwm`: output RPM per PWM step above `static`, x1000 (0 to
    65.535); 0 is no speed term. The firmware converts it to the library's
    counts/s per PWM step with the motor's CPR;
  - `trim`: the bound on the PI trim, PWM; 255 is no bound. About 20 keeps
    a held shaft from banking more than that.

  Stored per motor and applied on the next speed-mode pass. A payload that
  isn't 8 bytes is refused `MALFORMED`, and a build without the speed loop
  refuses it `NOT_BUILT`; either stores nothing. Like `SET_PID`, it is a
  configuration write and is taken during a watchdog trip, e-stop or
  fault. There is no readback.
- **Serial:** `SPEEDFF=static1,rpp1,trim1,static2,rpp2,trim2`, the same six
  fields in the same order, with `rpp` in RPM per PWM step as a decimal
  (`SPEEDFF=10,1.719,20,10,1.719,20`). Only where the speed loop is built.

### Calibrating the feedforward

The values come from the motor and its load, so there are no useful
defaults. In open loop, sweep the PWM in steps (`SET_OPEN_LOOP` or
`M1PWM=`) and at each step read the steady speed from GET_STATE (or `READ`)
once it settles. Fit a line, RPM = `rpm_per_pwm` * (PWM - `static`), over
the PWMs where the shaft turns: `rpm_per_pwm` is the slope and `static`
where the line crosses 0 RPM (the intercept on the PWM axis). Err towards
less drive: a lower `static` and a higher `rpm_per_pwm`. Feedforward that
under-drives costs a little start-up time, which the trim closes, while
over-drive overshoots at start-up. If the sweep is done in both directions,
take the lower `static` and the higher `rpm_per_pwm` of the two.

In the host motor model (24.3 counts/s per PWM above a deadband of 10 PWM,
at CPR 848) that is `static` 10 and `rpm_per_pwm` 24.3 * 60 / 848 = 1.719,
and with trim 20 and the default gains 60 RPM is within 5 % 210 ms after
the command (`test_speed_baseline_90`). Not yet checked on a board.

## Capability Mapping (`GET_CAPS`)

- Level 1: baseline (open loop and brakes); the `_noenc` and `_analog1` envs
- Level 2: baseline + closed-position + PID tuning
- Level 3: Level 2 + closed-speed, with `DCMT_CAP_SPEED_FEEDFORWARD`
  (`SET_SPEED_FF`)

Controllers should gate behavior by capability flags, not by generation labels.

All levels also advertise `DCMT_CAP_CMD_WATCHDOG`,
`DCMT_CAP_CLEAR_WATCHDOG_TRIP`, `DCMT_CAP_ESTOP_HOLD`, `DCMT_CAP_REFUSES`
and `DCMT_CAP_WATCHDOG_LEASE`. Levels 2 and 3 also advertise
`DCMT_CAP_SETPOINT_LOOP` and `DCMT_CAP_FAULTS`. Level 1's flags are
`0x5E3`: its only fault is a stall, which needs closed loop, so it doesn't
advertise faults, though GET_FAULTS (always 0) and CLEAR_FAULTS are still
answered.

A build with an analog port also advertises `DCMT_CAP_INPUTS` (bit 12):
the `_analog1` envs report `0x15E3`.

## Setpoints

`SET_SETPOINT` names its loop (`DCMT_CAP_SETPOINT_LOOP`, #57):

- 4 bytes, `[target1:i16][target2:i16]`: the position setpoints, in counts;
- 5 bytes, `[loop:u8][target1:i16][target2:i16]`: the setpoints of `loop`,
  `DCMT_PID_LOOP_POSITION` (counts) or `DCMT_PID_LOOP_SPEED` (RPM), as
  `SET_PID` names its loop.

Each loop keeps its own setpoints across mode changes, so a controller can
load both before the mode. A switch to closed-loop speed requests the speed
setpoint, which only a speed-loop `SET_SETPOINT` or serial `M1SPEED=`/`M2SPEED=`
sets; a position target never becomes a speed request. Before, the 4-byte
form wrote both loops.

## Refused Commands

A SET the firmware does not act on in full is refused with
`crumbs_refuse()`, so CRUMBS's GET_ACK reports it refused, with a reason from
bread-crumbs-contracts `bread_refuse.h`:

| Reason | When |
| --- | --- |
| `BREAD_REFUSE_MALFORMED` | the payload does not unpack; SET_PID is not 13 bytes; SET_SPEED_FF is not 8 bytes; SET_SETPOINT is not 4 or 5 bytes; SET_WATCHDOG lacks its u16; CLEAR_WATCHDOG_TRIP or CLEAR_FAULTS carries a payload |
| `BREAD_REFUSE_ESTOP` | SET_OPEN_LOOP, SET_MODE or SET_SETPOINT while the hardware e-stop is held, or a SET_BRAKE that asks for a release then; this reason wins when a watchdog trip is held too |
| `BREAD_REFUSE_WATCHDOG_TRIP` | SET_OPEN_LOOP, SET_MODE or SET_SETPOINT while a trip is held, or a SET_BRAKE that asks for a release then; this reason wins over a latched fault |
| `BREAD_REFUSE_FAULT` | SET_OPEN_LOOP, SET_MODE or SET_SETPOINT while a stall fault is latched, or a SET_BRAKE that asks for a release then |
| `BREAD_REFUSE_UNDEFINED_VALUE` | a SET_MODE, SET_PID or SET_SETPOINT loop the contract does not define; a SET_WATCHDOG flag other than `BREAD_WATCHDOG_FLAG_COMMANDS_ONLY`; a SET_OPEN_LOOP PWM outside `-DCMT_PWM_MAX` to `DCMT_PWM_MAX` (255) |
| `BREAD_REFUSE_NOT_BUILT` | closed-loop speed (SET_MODE, or a SET_PID or SET_SETPOINT loop SPEED) and SET_SPEED_FF when the speed loop is not built |
| `BREAD_REFUSE_WRONG_MODE` | SET_OPEN_LOOP outside open loop |
| `BREAD_REFUSE_UNKNOWN_OP` | an opcode with no registered command handler, including a GET opcode sent as a command frame instead of through SET_REPLY |

A SET_BRAKE that asks for a release while tripped, e-stopped or faulted still engages
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
command, I2C reply or serial line has arrived within the timeout. A
controller can arm it as a lease instead (`DCMT_CAP_WATCHDOG_LEASE`): a
3-byte `SET_WATCHDOG`, `[timeout_ms:u16][flags:u8]`, with
`BREAD_WATCHDOG_FLAG_COMMANDS_ONLY`. Then only `SET_OPEN_LOOP`, `SET_BRAKE`,
`SET_MODE`, `SET_SETPOINT` and `SET_WATCHDOG` refresh it; replies, other
commands and serial lines do not, so a status poller that outlives the
control path no longer keeps the motors driven. The control path re-sends
`SET_WATCHDOG` as its keepalive. The 2-byte form and serial `WDOG=` end the
lease, and a flag this firmware does not define is refused
`BREAD_REFUSE_UNDEFINED_VALUE` and arms nothing. A trip
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
so (see Refused Commands). Engaging a brake, PID and feedforward tuning (`SET_PID`, `SET_SPEED_FF`,
`PIDPOS=`, `PIDSPEED=`, `SPEEDFF=`), `SET_WATCHDOG`/`WDOG=` and `READ` still
work.

Clearing a trip resumes nothing; the brakes stay engaged and no motor
moves. To resume:

1. clear the trip (`BREAD_OP_CLEAR_WATCHDOG_TRIP` or `WDCLEAR`);
2. release the brakes (`SET_BRAKE(0, 0)`, or `BRAKE1=0` and `BRAKE2=0`).
   In closed-loop position the motors then hold the position where they
   are, in open-loop they coast at PWM 0, and in closed-loop speed they
   stay stopped. A setpoint sent before this step ends the follow and
   stands as sent, so the brake release drives to it;
3. send new commands: setpoints first, then the mode. A mode change to
   closed-loop position drives to the stored position setpoint, and one to
   closed-loop speed runs at the stored speed setpoint (0 after a hold, until
   a speed-loop `SET_SETPOINT` sets one).

Positions are `int16` on the wire, so closed-loop position works only while
the encoder count stays within ±32767. Beyond that the stored setpoint is
clamped to ±32767, including the one a trip or an e-stop press writes, and
releasing the brakes
in position mode drives the motor toward the clamp value. Keep position-mode
travel inside that range.

Serial commands (115200 baud, newline-terminated):

- `WDOG=<ms>` - arm with that timeout, or disarm with `0`
- `WDCLEAR` - clear a latched trip
- `FAULTCLEAR` - clear latched stall faults (see Stall Faults)
- `READ` - print state, ending `WDOG:<timeout|off>, WDTRIP:<tripped>/<trip count>, FAULT:0x<bits>`

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

## Stall Faults

In closed loop (position or speed), a motor driven for
`DCMT_STALL_TIMEOUT_MS` (1000 ms) without its encoder moving
`DCMT_STALL_MIN_COUNTS` (4) counts latches a stall fault (#64):
`DCMT_FAULT_M1_STALL` or `DCMT_FAULT_M2_STALL`. DCMotorServo's stall
detection does the check, so a motor at rest inside the servo's deadband,
which is not driven, never latches. Open loop is not checked, so a `_noenc`
build, which has only open loop, never latches a fault and doesn't
advertise `DCMT_CAP_FAULTS`. Both values are build flags and starting points, not
measurements; stall detection has not been checked on the bench yet.

A fault holds the way a watchdog trip does: both motors brake, the PWM
commands and speed setpoints go to 0, both brake flags are set, each position
setpoint follows its encoder, and the mode is kept. While any fault is
latched, the I2C commands the watchdog section lists are refused with
`BREAD_REFUSE_FAULT` (an e-stop or a watchdog trip, when held too, reports
its own reason), and the serial ones are ignored with a message.

`BREAD_OP_GET_FAULTS` (`0x7A`) replies `[faults:u8]`, the `DCMT_FAULT_*`
bits. GET_STATE is unchanged at 20 bytes. A fault is cleared only by:

- `BREAD_OP_CLEAR_FAULTS` (`0x7B`, empty payload) over the bus; a frame with
  a payload is refused `BREAD_REFUSE_MALFORMED` and the faults stay;
- the serial `FAULTCLEAR` command;
- a reboot.

Clearing resumes nothing. Resume as after a cleared trip: release the brakes,
then send setpoints, then the mode. A shaft that is still blocked latches
again once it has been driven for the timeout.

GET_CAPS advertises `DCMT_CAP_FAULTS` on the builds with closed loop.

## References

- Arduino Nano: https://store.arduino.cc/products/arduino-nano
- Arduino Nano Every: https://store.arduino.cc/products/nano-every
- Nano Every docs: https://docs.arduino.cc/hardware/nano-every

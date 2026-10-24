---
title: "2026-10-24: bread-slice-core, lead cap, serial and gen1"
parent: Bench Records
---

# 2026-10-24: bread-slice-core, lead cap, serial and gen1

**Status: in progress.** The gen2 Nano Every is done; the gen1 Nano is next.
Expected values come from the host tests, the issues and the
[2026-10-22 record](2026-10-22-applied-pwm-and-library-fixes.md).

**Verifies:** on `main` at `720879b`,
[#76](https://github.com/feastorg/Slice_DCMT/pull/76) (DCMotorServo 1.2.0,
LMD18200 0.1.5, contracts 0.10.0, `setMaxLead(300)`),
[#77](https://github.com/feastorg/Slice_DCMT/pull/77) (e-stop, watchdog and
status LED from bread-slice-core 0.1) and
[#79](https://github.com/feastorg/Slice_DCMT/pull/79) (strict serial numbers,
#60). Also the serial blocks the 2026-10-22 session did not run
([#49](https://github.com/feastorg/Slice_DCMT/issues/49),
[#36](https://github.com/feastorg/Slice_DCMT/issues/36)) and the first gen1
run since 2026-10-20.
**By:** @CameronBrooks11

## Coverage

What each change can differ by, and the board that covers it:

| Change | Differs by | Covered on |
| --- | --- | --- |
| #77 status LED | MCU (the D5 port differs), gen (gen1 has none, and D5 is motor 1's direction) | gen2 Nano Every, gen2 Nano; gen1 Nano for no LED and an undisturbed D5 |
| #77 e-stop and watchdog | MCU (interrupt masking) | gen2 Nano Every, gen2 Nano, gen1 Nano |
| #76 lead cap | the speed loop, built on the Nano Every only | gen2 Nano Every |
| #79 strict parsing | nothing board-specific; avr-libc is the same on both MCUs. The speed fields exist on the Every only | gen2 Nano Every |
| #49 serial burst, #36 serial `MODE=` | nothing board-specific | gen2 Nano Every |
| gen1 pin map | gen | gen1 Nano |

Not run, and why:

- gen1 Nano Every: its pin map is covered by the gen1 Nano and its MCU by the
  gen2 Nano Every; nothing in these changes depends on the combination.
- DCMotorServo 1.2.0's speed-loop fixes (#12, #13, #11): benched on
  2026-10-22 at `cc8bad6`. 1.2.0 adds only the `fabs()` change (#19), which
  behaves the same for errors under 32,768 counts. DCMT's int16 setpoints
  and the 300-count lead cap keep the error below that, so #19 cannot be
  reached from DCMT.
- LMD18200 0.1.5: benched on 2026-10-22 at `b11192a`, the same code.
- #37 masked writes: a torn write can't be provoked; host-tested.
- The boot watchdog restamp and the latch-before-brake order: the first acts
  only with `DCMT_WATCHDOG_BOOT_MS` set, the second within microseconds;
  both host-tested.
- PWM range refusal: telling it from the old clamp needs a motor driven at
  up to 255; host-tested.

## Setup

| | |
| --- | --- |
| Firmware | `720879b`, envs `gen2_nanoevery`, `gen2_nano`, `gen1_nano`. bread-slice-core 0.1.0, DCMotorServo 1.2.0, LMD18200 0.1.5, CRUMBS 0.16.2, bread-crumbs-contracts 0.10.0 |
| Boards | gen2 Nano Every (ATmega4809), gen2 Nano (ATmega328P), gen1 Nano (ATmega328P). The gen2 boards have the D10/D11 motor-2 rework |
| Controller | Raspberry Pi 4 Model B, I2C at 50 kHz, address 21, CRUMBS Python binding 0.16.2 and contracts 0.10.0, via [`dcmt_bench.py`](assets/2026-10-22-applied-pwm-and-library-fixes/dcmt_bench.py). USB serial only on the Nano Every, for D6 to D9 |
| Power | backplane (I2C and 12 V), one DCMT on the bus at a time |
| Outputs | an encoder motor on M1; M2 empty. E-stop: a jumper from D2 to GND (D2 has a pull-up; LOW is e-stop) |

## Order

The rig is the 2026-10-22 one throughout; only the board changes.

1. gen2 Nano Every: D1 to D9.
2. gen2 Nano: D1 to D4, if time allows (see Not Checked).
3. gen1 Nano: D1 to D5.

Flash each from the workstation, then put it on the backplane. Flashing, and
every step that can turn the motor, waits for your go.

## Steps and Results: gen2 Nano Every

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| D1 boot LED: power-cycle, D2 open; then with D2 to GND | blue ~1 s then green; blue then red | blue then green; blue then red | pass |
| D2 baseline: `version`, `caps`, `state`, `wdog` | module 2.0.0; caps level 3, flags `0x1FF`; mode 0, PWMs 0, estop 0; watchdog disarmed | CRUMBS 1602, module 2.0.0; caps level 3, flags `0x1FF`; mode 0, PWMs 0, `estop=0`; watchdog disarmed. `brakes=0b11` and `estop_count=1` came from the boot just before, with D2 held, then released | pass |
| D3 e-stop hold: `open 60 0`; D2 to GND, `state`; `open 60 0`; D2 open, `state` | M1 turns; on the press `estop=1`, count +1, `brakes=0b11`, `m1_pwm=0` within one poll; `open` refused reason 3 (ESTOP); after release `estop=0`, brakes stay `0b11`, nothing turns | `m1_pwm=60`, pos rising; on the press `estop=1`, count 1 -> 2, `brakes=0b11`, `m1_pwm=0`, pos held at 19129; `open` acked status 2 reason 3; after release `estop=0`, brakes `0b11`, pos 19129 over 1.3 s | pass |
| D4 watchdog trip: `brake 0 0`, `open 60 0`, `wd 500`, wait 1.2 s, `wdog`, `state`; `open 60 0`; `wd 1000`, `wdog`; `clear`, `wdog`, `state`; `wd 0` | tripped 1, trips 1, `brakes=0b11`, `m1_pwm=0`; `open` refused reason 2 (WATCHDOG_TRIP); still tripped after `wd 1000`; `clear` applied, tripped 0, trips 1, nothing turns | `wdog` `01 f401 01 01` after the wait; `brakes=0b11`, `m1_pwm=0`, pos 20197; `open` acked status 2 reason 2; after `wd 1000` `01 e803 01 01`; `clear` applied, `01 e803 00 01`; pos 20197 after. 1.36 s of silence with 1000 ms still armed then tripped it again (trips 2); that trip held through `wd 0` and a re-arm (`00 0000 01 02`, `01 e803 01 02`) until a `clear` | pass |
| D6 lead cap (#76): `brake 0 0`, `ser "M1SPEED=60"`, `mode 2`; hold the shaft ~4 s, release; poll `state` for 5 s; `brake 1 1`, `mode 0` | while held, `m1_pwm` rises but stays well below 150; on release no burst at 150 PWM and no overshoot of hundreds of counts; then back near the pre-stall speed (about 42 to 56 RPM on 2026-10-22, below the 60 target). On 2026-10-22 the uncapped build ran 240 to 285 RPM on release | held 45.1 to 50.2 s: `m1_pwm` 66 to 82, pos +28 counts; first 0.3 s after release +212 counts (~53 RPM at 800 counts/rev), `m1_pwm=40`. Before the stall ~680 counts/s (~51 RPM); after release ~900 counts/s (~68 RPM) for the 2.7 s still polled; see Notes | pass |
| D7 serial burst (#49): `prep`, then `flood 15`; ~5 s in, D2 to GND for 2 s, then open | `estop=1` within one or two 20 ms polls of the press, count +1; no failed polls; no reboot (a reboot shows as a boot blue and the count dropping back) | 15 s run: 3208 lines, 401 polls, 0 failed; no press landed in it. 30 s run: `estop=1`, count 1 at 23:37:51.495, `estop=0` at 23:37:55.912; 6416 lines, 802 polls (~37 ms apart), 0 failed; the count never dropped back | pass |
| D8 serial `MODE=` (#36), left through SPEED rather than POS: pos had passed 32767 since boot, so no int16 setpoint could sit at the shaft. After a reset, in OPEN: `brake 0 0`, `ser "M1SPEED=0" "M1PWM=80"`, `state`; `ser "MODE=SPEED"`, `state`; `ser "MODE=OPEN"`, `state` twice; `brake 1 1` | M1 turns; in SPEED at 0 it stops, `m1_pwm=0`; back in OPEN it does not start again. Before #36 the last step resumed at 80 | `m1_pwm=80`, pos 6754; SPEED: `m1_pwm=0`, pos 7376; OPEN: `m1_pwm=0`, pos 7376 over 2.6 s | pass |
| D9 strict parsing (#60): `mode 0`, `brake 1 1`, then `ser "M1POS=1234" "M1POS=abc" "M1SPEED=30" "M1SPEED=40000" "WDOG=60000" "WDOG=abc" "PIDPOS=0.15,0.01,0.01" "PIDPOS=1,2,3garbage" "READ" "WDOG=0"`; power-cycle afterwards | no motion (open loop, PWM 0, braked). Valid lines reply `M1POS-> 1234`, `M1SPEED-> 30`, `WDOG-> 60000 ms`, `Position PID updated`; `M1POS=abc`, `M1SPEED=40000` and `WDOG=abc` reply `Ignored: expected a whole number from ...`; `PIDPOS=1,2,3garbage` replies `PIDPOS format: ...`. READ shows `M1POS_SP:1234`, `M1SPD_SP:30`, `WDOG:60000`. Before #79 these read 0, -25536 and off. The power-cycle drops the stored setpoints before any later closed-loop run | as expected, line for line: the four valid lines applied, the four malformed ones refused with those replies; READ `M1POS_SP:1234`, `M1SPD_SP:30`, `WDOG:60000`. Not power-cycled: the board came off the bus for the next one | pass |

## Steps and Results: gen2 Nano

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| D1 boot LED | as on the Nano Every | | |
| D2 baseline | caps level 2, flags `0x1F7` (no speed loop); otherwise as on the Nano Every | | |
| D3 e-stop hold | as on the Nano Every | | |
| D4 watchdog trip | as on the Nano Every | | |

## Steps and Results: gen1 Nano

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| D1 boot: power-cycle | no LED activity (none fitted); M1 does not move | | |
| D2 baseline | as on the gen2 Nano | | |
| D3 e-stop hold | as on the Nano Every | | |
| D4 watchdog trip | as on the Nano Every | | |
| D5 direction (D5 is motor 1's DIR on gen1): `brake 0 0`, `open 80 0`, `state`; `open -80 0`, `state`; `open 0 0`, `brake 1 1` | M1 turns one way, then the other; `pos` rises, then falls. The LED driver is not built on gen1 and must not disturb D5 | | |

## Raw Output

The commands behind each step and their output:
[`raw-session.txt`](assets/2026-10-24-core-and-serial/raw-session.txt).

## Not Checked

- gen2 Nano, unless run later: it builds the same sources as the Nano Every
  apart from the LED port and the speed loop. Its LED port is the
  ATmega328P path of the driver benched on a gen2 Nano on 2026-10-20, and its
  interrupt masking is avr-libc's `ATOMIC_BLOCK`, as on the 4809.
- Motor 2: no motor fitted.
- The e-stop release debounce time: the jumper is placed by hand, so 25 ms
  can't be resolved; host-tested.

## Notes

- **Speed after a stall overshoots the target.** In D6 the shaft ran ~51 RPM
  before the hold and ~68 RPM, against 60, for at least 2.7 s after the
  release. On 2026-10-22 the capped build showed the same pattern (~42 RPM
  before, ~56 after). A guess, not measured: the speed PID's integral winds
  up while the shaft is held (`m1_pwm` climbed 66 to 82 at zero speed) and
  unwinds slowly. Three polls 30 s later read `spd=60` and `m1_pwm` 37 to 41,
  near the pre-stall 31 to 39, but at 15 RPM per step that only suggests it
  settled. The lead cap bounds the position target, not this integral.
- **GET_STATE positions saturate at 32767.** Within a minute at 60 RPM the
  encoder passed int16 and `pos` read 32767 from then on, so speed could no
  longer be measured from position, and no `SET_SETPOINT` could sit at the
  shaft (hence D8 through SPEED).
- **`prep`'s first port open resets the board.** The ack counter restarted at
  1 and `M1SPEED=60`, sent inside the 1 s boot wait, was not applied; resent
  after the wait, it was.

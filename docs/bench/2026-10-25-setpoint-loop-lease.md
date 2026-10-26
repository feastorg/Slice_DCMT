---
title: "2026-10-25: setpoint loops, the e-stop release and the watchdog lease"
parent: Bench Records
---

# 2026-10-25: setpoint loops, the e-stop release and the watchdog lease

**Status: complete.** Every step passed. Expected values come from the host
tests and the PRs.

**Verifies:** on `main` at `718a41f`,
[#84](https://github.com/feastorg/Slice_DCMT/pull/84) (SET_SETPOINT names its
loop, #57) and [#85](https://github.com/feastorg/Slice_DCMT/pull/85) (the
commands-only watchdog lease), with bread-crumbs-contracts 0.11.0
([#82](https://github.com/feastorg/Slice_DCMT/pull/82)). Also the
position-mode half of [#30](https://github.com/feastorg/Slice_DCMT/issues/30)
(e-stop release), which needed a board with an encoder.
**By:** @CameronBrooks11

## Setup

| | |
| --- | --- |
| Firmware | `718a41f`, env `gen2_nanoevery`. bread-slice-core 0.1.0, DCMotorServo 1.2.0, LMD18200 0.1.5, CRUMBS 0.16.2, bread-crumbs-contracts 0.11.0 |
| Board | gen2 Nano Every (ATmega4809), with the D10/D11 motor-2 rework |
| Controller | Raspberry Pi 4 Model B, I2C at 50 kHz, address 21, CRUMBS Python binding 0.16.2 and contracts 0.11.0, via [`dcmt_bench.py`](assets/2026-10-25-setpoint-loop-lease/dcmt_bench.py) (this record's copy adds `sp5`, `wdl`, `raw` and `watch`) |
| Power | backplane (I2C and 12 V), one DCMT on the bus |
| Outputs | an encoder motor on M1; M2 empty. E-stop: a jumper from D2 to GND |

Power-cycle before S2 and S4, so the encoder starts at 0: GET_STATE
positions saturate at ±32767 (2026-10-24 notes).

## Steps and Results

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| S1 baseline: `version`, `caps`, `state`, `wdog` | module 2.0.0; caps level 3, flags `0x7FF` (`0x1FF` plus `DCMT_CAP_SETPOINT_LOOP` and `DCMT_CAP_WATCHDOG_LEASE`); mode 0, PWMs 0, estop 0; watchdog disarmed | CRUMBS 1602, module 2.0.0; caps `0103ff070000`: level 3, flags `0x7FF`; mode 0, PWMs 0, `pos=0,0`, estop 0, count 0; watchdog disarmed | pass |
| S2 a position target is not a speed request (#57): after a power-cycle, `brake 1 1`, `mode 0`, `sp 5000 0`, `mode 2`, `brake 0 0`, `watch 3`; then `brake 1 1`, `mode 0` | `sp` acked status 1; in speed mode GET_STATE `sp=0,0` and M1 stays still (pos within a few counts). Before #84, 5000 became the speed request (host-tested) | all acks status 1; in speed mode `sp=0,0`, `spd=0`, `m1_pwm=0`, and `pos=0` before and after the 3 s with the brakes released | pass |
| S3 the speed form: `sp5 2 60 0`, `mode 2`, `brake 0 0`, `watch 5`, `brake 1 1`, `mode 0`; then `sp5 3 1 1` and `raw 0x04 010203040506` | `sp5 2` acked status 1; in speed mode `sp=60,0` and `spd` near 60 RPM (51 to 68 on 2026-10-24). `sp5 3` acked status 2 reason 4 (UNDEFINED_VALUE); the 6-byte frame status 2 reason 1 (MALFORMED) | `sp5 2 60 0` acked status 1; in speed mode `sp=60,0`; `pos` 495 to 2799 in 4.0 s, about 43 RPM at 800 counts/rev (`spd` read 30, 45, 60 in 15 RPM steps), `m1_pwm` 30 to 37. `sp5 3` acked status 2 reason 4; the 6-byte frame status 2 reason 1 | pass |
| S4 e-stop release in position mode (#30): after a power-cycle, `mode 1`, `sp 2000 0`, `brake 0 0`, `watch 4`; `sp 8000 0` and jumper D2 while it moves; remove the jumper, `watch 2`; turn the shaft by hand about a quarter turn, `brake 0 0`, `watch 2`; jumper and release again, `sp 1000 0`, `brake 0 0`, `watch 4`; `brake 1 1` | M1 drives to about 2000 and holds. On the press it brakes (`brakes=0b11`, estop 1); after the release it does not drive toward 8000. After turning it by hand, the brake release holds it where it is: `sp` follows the encoder until then. After the second hold, the brake release drives it to 1000 | not power-cycled first (pos 2997, inside ±32767). M1 drove to 2007 and stopped. The first press landed after it had settled: brakes `0b11`, estop 1, `sp` followed to 2007, and `sp 8000 0` during the hold was refused reason 3. After the release, the brake release and `sp 8000 0` started the move; the second press landed at pos 3136: brakes `0b11`, `m1_pwm=0`, `sp` followed the coast to 3252 (count 2 to 3: the jumper bounced). Turned by hand to 2977 with `sp` following; the brake release held at 2977 for 2 s. After a third press and release, `sp 1000 0` was applied with the brakes on (`sp=1000`, pos 2977) and the release drove M1 to 1004 in about 1 s | pass |
| S5 the lease: `brake 0 0`, `mode 0`, `open 60 0`, `wdl 1000 1`, `watch 3`; `wd 0`, `clear` (disarm first: under the lease `clear` refreshes nothing, so an armed watchdog would trip again at once); `brake 0 0`, `open 60 0`, `wd 1000`, `watch 3`; `wd 0`, `brake 1 1`; `wdl 1000 2`, `wdog` | with the lease, GET_STATE polls alone let it trip about 1 s after `wdl`: `brakes=0b11`, `m1_pwm=0`, GET_WATCHDOG tripped 1. With the 2-byte `wd 1000` the same polls keep it alive: no trip in 3 s. `wdl 1000 2` acked status 2 reason 4, and `wdog` reads disarmed | `wdl 1000 1` applied at 22:31:18.881; the poll at 22:31:20.083 read `brakes=0b11`, `m1_pwm=0`, and GET_WATCHDOG `01 e803 01 01`. `wd 0` then `clear`: `00 0000 00 01`. With `wd 1000` the motor ran through the 3 s of polls, GET_WATCHDOG `01 e803 00 01`. `wdl 1000 2` acked status 2 reason 4, and `wdog` read `00 0000 00 01` | pass |

## Raw Output

The commands behind each step and their output:
[`raw-session.txt`](assets/2026-10-25-setpoint-loop-lease/raw-session.txt).

## Not Checked

- Other boards: the gen2 Nano and gen1 boards were not run. #84 and #85
  change the handlers only, which build the same for each MCU apart from
  the speed loop, and the speed loop is on the Nano Every.
- Motor 2: no motor fitted.
- How soon the lease trips: the polls are 200 ms apart and print only on a
  change, so the trip is bracketed between 0.3 s and 1.2 s after `wdl`.

## Notes

- **S1's first queries timed out.** They landed during the boot wait; the
  next run, a few seconds later, answered.
- **Closed-loop speed runs below the target.** 60 RPM requested, about
  43 RPM measured from position, as on 2026-10-24 (42 to 56). #84 changes
  which setpoint is used, not the speed loop.
- **The e-stop count went up twice on one press** (2 to 3 within 0.2 s)
  when the jumper was placed by hand: the line bounced, and each LOW after a
  release longer than the 25 ms debounce counts. The hold behaved the same.
- **S4 was not preceded by a power-cycle.** Its targets (2000, 8000, 1000)
  stayed inside ±32767 from the starting position 2997.

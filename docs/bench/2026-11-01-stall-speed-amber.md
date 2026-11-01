---
title: "2026-11-01: stall latch, closed-loop speed and the amber LED"
parent: Bench Records
---

# 2026-11-01: stall latch, closed-loop speed and the amber LED

**Status: complete, with three new findings.** The stall latch, the speed
loop fixes, the zero-speed reading, the speed-entry fix and the amber LED
work on the board. Three things did not go as expected: the board held the
I2C bus with a motor running (D3), a small position step latched a stall
(D1), and M2's speed overshoots after a short grip (D3).

**Verifies:** on `main` at `6a8df02`,
[#97](https://github.com/feastorg/Slice_DCMT/pull/97) (stall latch, speed
integral freeze, speed Ki 0.2),
[#98](https://github.com/feastorg/Slice_DCMT/pull/98) (amber on a watchdog
trip or stall fault) and
[#99](https://github.com/feastorg/Slice_DCMT/pull/99) (DCMotorServo 1.2.1:
a stopped motor reads 0, feastorg/dcmotorservo#18). Also re-checks #62.
**By:** @CameronBrooks11

## Setup

| | |
| --- | --- |
| Firmware | `6a8df02`, env `gen2_nanoevery` (29915 B flash, 1363 B RAM). CRUMBS 0.17.0, bread-crumbs-contracts 0.11.1, DCMotorServo 1.2.1, LMD18200 0.1.5, bread-slice-core 0.2.0 |
| Board | gen2 Nano Every (ATmega4809) with the D10/D11 motor-2 rework |
| Motors | two E-S Motor 36PG-3429-19-EN 12 V planetary gearmotors, one on M1 and one on M2. Datasheet: 1:19, 12 PPR two-channel magnetic encoder, 490 RPM no load, 247 RPM and < 2.5 A rated, < 5 A stall. Firmware `MOTOR1_CPR` and `MOTOR2_CPR` 798 (see Notes) |
| Controller | Raspberry Pi 4 Model B, I2C at 50 kHz, address 21, CRUMBS Python binding 0.16.2 and contracts 0.11.0, via this session's [`dcmt_bench.py`](assets/2026-11-01-stall-speed-amber/dcmt_bench.py) (adds `faults`, `fclear`, `pid` and `speedlog`) and [`d1.sh`](assets/2026-11-01-stall-speed-amber/d1.sh) |
| Power | backplane (I2C, 12 V and the e-stop line), with Slice_RLHT's gen2 Nano on the bus at address 19. The e-stop line is shared: a jumper from D2 to GND on either board counts on both |

## Steps and Results

Times are the controller's clock. `t` in a speed log is from its first poll,
about 0.3 s after the brake release is acknowledged.

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| B0 baseline: `version`, `caps`, `state`, `wdog`, `faults` | CRUMBS 1700, module 2.0.0; caps `0103ff0f0000` (adds `DCMT_CAP_FAULTS`); mode 0, PWM 0; watchdog disarmed; no faults | as expected | pass |
| D1 position mode, hold 3 s, step the target 12 counts, 10 s (`sh d1.sh 1`, `sh d1.sh 2`) | no stall latch | M1: moved 0 to 3 and stopped inside the 10-count deadband (`DCMT_SERVO_ACCURACY`), no latch. M2: drove at PWM 16, moved 0 to 1 (error 11), and latched `M2_STALL` about 1.2 s after the step: both brakes on, target reset to 1. After `fclear`, a 100-count step on M2 reached 94 at PWM 17 with no latch | **fail** on M2 (new issue) |
| H home both shafts to -28000 (position mode, 20 s) | within 10 counts, no faults | -28009, -28008; `faults 0x00 none` | pass |
| D2 60 RPM for 60 s, then setpoint 0 for 2 s with the brake released (#90, #18) | within 5 % by 3.0 s after the brake ack; last-5-s mean 57.0 to 63.0; no latch; then `spd=0` | M1: within 5 % (200 ms mean) 1.21 s after the brake ack, last-5-s mean 60.7 RPM, peak 1 s mean 62.1, PWM 33 to 45, 600 polls 0 failed, `spd=0,0` 2 s after setpoint 0. M2: 1.01 s, 60.1 RPM, peak 62.0, 600 polls 0 failed, `spd=0,0` | pass |
| D3 60 RPM 15 s, grip the shaft for about 0.5 s and release (#89) | no latch; peak 1 s mean after release <= 65 RPM | M1, first run: the grip held the shaft still for about 1.0 s and latched `M1_STALL` (used for D4). M1, second run: stopped 0.3 s (2.43 to 2.72 s), peak 1 s mean 64.4 RPM, peak 200 ms mean 68.4; then the bus hung (below). M2 (watchdog lease armed): four grips of 0.1 to 0.4 s, no latch; peak 1 s means 66.8 and 67.4 RPM after the second and third, peak 200 ms means 71.8 to 79.7 | M1 pass; M2 **fail** against 65 (#89 improved, not fixed) |
| D3 bus hang | the bus keeps answering | M1 second run: last good poll 10:50:18.688 at 60.5 RPM, then every poll and the brake command timed out (`TimeoutError(110)`). M1 kept running at about 60 RPM until the e-stop jumper. After the jumper was removed the status LED stayed red. The Pi read SCL (GPIO3) low and SDA high, both boards timed out, the kernel log showed no I2C error. A reset of the DCMT alone brought SCL high and both boards back | **fail** (new issue) |
| D4 hold the shaft at 60 RPM until it latches (#97, #98) | latch about 1 s after the shaft stops: both brakes, `faults` bit set, LED amber; SET_* refused reason 8; `fclear` clears and resumes nothing; LED green | M1 (first D3 run): shaft still from 3.62 s, latched by 4.72 s, PWM held to 70 or less while stopped, `faults 0x01 M1_STALL`; `sp5`, `mode` refused reason 8; `fclear` cleared, brakes stayed on; LED not observed. M2 (lease armed): still from 1.92 s, latched at 2.82 s, PWM 66 or less, `faults 0x02 M2_STALL`; `sp5`, `mode` refused reason 8; LED amber; after `fclear` `faults 0x00 none`, brakes stayed on, LED green | pass |
| D5 10 RPM for 20 s per motor (lease armed) | no latch | M1: last-5-s mean 10.0 RPM, peak 1 s 11.0; M2: 10.2 RPM, peak 11.1; 200 polls each, 0 failed; `faults 0x00 none` | pass |
| D6 open loop -255 for about 0.8 s, then speed mode at +60 for 5 s (#62, lease armed) | no negative PWM after entry; position rising within 0.5 s | M1 reversed from 2706 to about -2500. First poll 0.29 s after the mode change: position rising, PWM 41; PWM 29 to 52 in all 50 rows; within 5 % at 0.32 s; no faults | pass |
| D7 watchdog trip, motors braked: `wdl 1000 1`, `watch 3`; jumper on, off; `wd 0`, `clear` (#98) | LED amber; red with the jumper; amber after release; green after the clear | `wdog 01e8030101`; LED amber; jumper: `estop_count` 0 to 1, LED red; released: LED amber; after `clear` `wdog 0000000001`, LED green | pass |

## Raw Output

[`raw-session.txt`](assets/2026-11-01-stall-speed-amber/raw-session.txt):
every DCMT command and its output, plus the bus probes after the hang. The
CPR checks and the hand-turn readings are the `sp 20222`, `sp 28210`,
`sp 20221` and `mode 0` blocks between D2 and the second homing.

## Not Checked

- The status LED during M1's latch; the M2 latch showed amber.
- D3 with a single short grip on M2: four grips were given.
- Whether the firmware's loop was still running after the bus hang. The
  e-stop stopped M1, which only the loop can do, but the LED stayed red after
  the release.
- D1 to D7 on the gen2 Nano and on gen1 boards.
- The 0.29 s between the D6 mode change and its first poll.

## Notes

- **Bus hang.** The DCMT held SCL low about 1.7 s after the second D3
  grip, while M1 recovered to 60 RPM. The bus had answered every poll
  through D1, D2 and the first D3 run, and through 2026-10-25 and 2026-10-31.
  After the reset every motor step ran with the watchdog lease armed at
  1000 ms, any frame (`wdl 1000 0`), so that a dead bus brakes both motors
  within a second; the hang did not recur in D3 M2, D4 M2, D5 or D6.
- **Counts per output revolution.** The firmware uses 798; the datasheet's
  12 PPR x 4 x 1:19 is 912. Two driven moves in position mode, +7988 and
  -7989 counts, brought a tape flag on M1's shaft back to its start (10.01
  turns at 798, 8.76 at 912). Turning the shaft by hand with the brake
  released and PWM 0 gave 2583 counts for 3 turns (861 per turn) and 8457
  for 10 (846 per turn). The two methods disagree; the speed figures above
  are in the firmware's 798 units.
- **e-stop line.** Slice_RLHT's `estop_count` rose with each DCMT jumper
  press: the backplane carries one e-stop line to both boards.

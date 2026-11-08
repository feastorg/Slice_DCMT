---
title: "2026-11-07: the feedforward speed loop, and a position-mode fix"
parent: Bench Records
---

# 2026-11-07: the feedforward speed loop, and a position-mode fix

**Status: pass, with three findings.** On a gen2 Nano Every with both motors, the DCMotorServo 2.0 speed loop holds its target from 10 to 150 RPM in both directions:

- each motor's feedforward came from an open-loop sweep and was sent with `SET_SPEED_FF`;
- every run reached the target within 0.22 s, and averaged within 0.4 RPM of it over the last 5 s;
- a grip, and entry from full reverse, recover without driving backwards.

The findings:

- **A position-mode stuck stop.** A long position move could stop past the target and stay there. DCMotorServo 2.0.1 fixes it (feastorg/dcmotorservo#30), and the fix was checked here before release.
- **CRC failures on the bus.** About 1 % of `GET_STATE` replies failed CRC with both motors running. There was no lockup.
- **Stall under a steady load.** A steady load on a held position latches a stall within about a second (#102).

**Verifies:** #116 (DCMotorServo 2.0, `SET_SPEED_FF`, CPR 848), the bench step of feastorg/bread-crumbs-contracts#75, #105 on hardware, and feastorg/dcmotorservo#30.
**By:** @CameronBrooks11

## Setup

| | |
| --- | --- |
| Firmware A | `c276d88` (main, with #116), env `gen2_nanoevery`. 32677 B flash, 1573 B RAM. CRUMBS 0.17.0, bread-crumbs-contracts 0.13.0, LMD18200 0.1.5, DCMotorServo 2.0.0, bread-slice-core 0.2.0 |
| Firmware B | Firmware A with `DCMotorServo.cpp` from feastorg/dcmotorservo `38dec7c` (#30, released as 2.0.1) in the local library cache. 32791 B flash. Steps P2 onwards |
| Board | gen2 DCMT on a Nano Every with the D10/D11 rework, I2C address 21: the board from the 11-01 and 11-02 records |
| Motors | both connected, each with its encoder, shafts free. `MOTOR1_CPR` and `MOTOR2_CPR` 848 |
| Position settings | Kp 0.15, Ki 0.01, Kd 0.01, pwm skip 15, max PWM 150, accuracy 10 (`config_hardware.h`) |
| Speed settings | Kp 0.6, Ki 3.0 (the defaults), trim 20, feedforward from step F3 |
| Controller | Raspberry Pi 4, I2C at 50 kHz, CRUMBS Python binding 0.16.2, through [`dcmt_bench.py`](assets/2026-11-07-speed-ff-and-position/dcmt_bench.py) (md5 `61a4726a…`). It adds `ffsweep M PWM...`, which runs each PWM open loop forward and then reverse, with a 0.5 s coast at PWM 0 before each step, and fits RPM = rpm_per_pwm × (PWM − static) per direction. The venv's contracts predates `SET_SPEED_FF`, so F3 sends it with `raw` |
| Lease | `wdl 1000 0` armed for every motion step |

## Steps and Results

Times are the controller's clock.

### Feedforward

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| F1 `version`, `caps`, `state`, `wdog`, `faults` | module major 3, caps bit 13 | `major=3, minor=0, patch=0`; caps `0103ff2f0000` (flags `0x2FFF`, bit 13 set); mode 0, no faults | pass |
| F2 `ffsweep 1 30 60 90 120 150` | RPM linear in PWM above a small static | +35.6/−39.4 at 30 up to +266.0/−283.4 at 150. Forward 1.9118 RPM/PWM, static 10.9; reverse 2.0235, static 10.8 | pass |
| F2 `ffsweep 2 30 60 90 120 150` | as F2 | +45.0/−45.8 at 30 up to +277.8/−286.4 at 150. Forward 1.9429, static 7.2; reverse 2.0046, static 7.0. 0 bad replies (third attempt; Notes) | pass |
| F3 `raw 0x06 0ae8071407d50714`: M1 static 10, 2.024 RPM/PWM, trim 20; M2 static 7, 2.005, trim 20 | ack OK | status 1 | pass |
| F4 `raw 0x06 0ae80714` (4 bytes) | refused | status 2, reason 1 | pass |

F3 takes the lower static and the higher RPM per PWM of each motor's two directions, which is the lower drive.

### Speed

Each run is 12 s, closed loop (mode 2), with the other motor braked. "Last 5 s" is the mean of the position-derived RPM over 6.9–11.9 s.

| Step | Target RPM | M1: within 5 % (200 ms RPM), last 5 s | M2: same | Result |
| --- | --- | --- | --- | --- |
| S1 | +10 | 0.22 s, 10.0 | 0.22 s, 10.1 | pass |
| S2 | −60 | 0.23 s, −60.2 | 0.22 s, −60.1 | pass |
| S3 | +120 | 0.22 s, 120.3 | 0.22 s, 120.3 | pass |
| S4 | −150 | 0.23 s, −150.4 | 0.22 s, −150.4 | pass |

Peak 1 s means were at most 0.7 RPM past the target. Polls failing CRC: M1 0, 0, 0, 2; M2 0, 1, 0, 2, out of 120 per run.

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| S5 M1 at 60 RPM, shaft gripped by hand about 3.4 s in, for about 0.5 s | PWM rises to feedforward plus trim; recovers on release without overshoot; no stall latch | At 3.42–3.92 s the speed fell to 0 and PWM went from 40 to 60, the feedforward plus the trim bound of 20. On release it was back at 62.6 by 4.12 s and at 60.1 for the last 5 s. No fault | pass |
| S6 M1 open loop at −255, then speed mode at +60 RPM (#62) | PWM never negative after entry | PWM 39–41 in every one of 51 polls; within 5 % at 0.22 s; no fault | pass |

### Position

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| P1 firmware A: from M1 −16622 and M2 −7282, `home 0 0` | both reach the deadband | **M2 stopped at +48, PWM 0, for 60 s.** M1 reached 5. No fault | **fail** (Notes) |
| P1b firmware A: `home 2000 2000`, then `home 0 0` | reached | 1.0 s, at 1996/1998; 1.1 s, at 4/4 | pass |
| P2 firmware B: `home -7282 -7282` | reached | 2.6 s, at −7276 | pass |
| P3 firmware B: `home 0 0`, then `home 16000 16000`, then `home 0 0` | reached | 2.7 s at −6; 5.1 s at 15994; 5.0 s at 6 (both motors) | pass |
| P4 firmware B: M1 from 20725 (left by S7) to 0 | reached | at 6, 5.6 s after the first poll of the step | pass |
| P5 firmware B: M1 holds 0; pushed away by hand repeatedly for about 10 s | drive restores the position, never away from the target | Deflections of up to 185 counts (and one of +234 in the other direction); the drive, 15–49 PWM, returned it to within 6 counts each time. No fault | pass |
| P6 firmware B: M1 holds 0; one steady push for about 8 s | drive restores the position or holds against the load | PWM rose slowly from 15 to 22 at a 53-count deflection, then **`M1_STALL` latched** and the drive stopped; the shaft was pushed on to −155 | finding (#102) |

### Bus and firmware B speed

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| B1 firmware A: both motors at 120 RPM for five 60 s runs, alternating direction, polling `GET_STATE` every 100 ms (#109) | no lockup, no lease trip | No lockup, no trip, no fault; `GET_VERSION` answered afterwards. **32 of 2968 polls failed CRC** (4, 3, 9, 2 and 14 per run). Reverse runs averaged −120.3 for the last 5 s; forward runs pin `pos` at 32767 (Notes) | pass, with the CRC finding |
| S7 firmware B: M1 at 120 RPM for 12 s | as S3 | 0.22 s, 120.3, 0 failed polls | pass |

## Raw Output

- [`ffsweep-m1.txt`](assets/2026-11-07-speed-ff-and-position/ffsweep-m1.txt) and [`ffsweep-m2.txt`](assets/2026-11-07-speed-ff-and-position/ffsweep-m2.txt): F2 (the M2 file is the third attempt).
- [`speed-m1.txt`](assets/2026-11-07-speed-ff-and-position/speed-m1.txt) and [`speed-m2.txt`](assets/2026-11-07-speed-ff-and-position/speed-m2.txt): S1–S4.
- [`grip-m1.txt`](assets/2026-11-07-speed-ff-and-position/grip-m1.txt): S5. [`entry-m1.txt`](assets/2026-11-07-speed-ff-and-position/entry-m1.txt): S6.
- [`bus-120.txt`](assets/2026-11-07-speed-ff-and-position/bus-120.txt): B1, every poll.
- [`hold-m1.txt`](assets/2026-11-07-speed-ff-and-position/hold-m1.txt): P5. [`hold-steady-m1.txt`](assets/2026-11-07-speed-ff-and-position/hold-steady-m1.txt): P4 and P6.

P1–P3 and S7 were read from the commands' own output, which isn't kept as a file. It is quoted in the table.

## Not Checked

- The saturated-hold case of the #30 fix: a load needing near-full drive to hold position. The fix zeroes the integral each time the drive saturates, which could saw-tooth under such a load. P5 and P6 never went above 49 PWM.
- Grip and reverse entry on M2.
- Speed below 10 RPM, and above 150.
- A position step just outside the deadband (#102's original case). The 12-count step on M2 tried here started 8 counts away, inside the deadband, and didn't move.
- The analog-input builds on this board, and port 2 as analog (#106).

## Notes

- **The stuck stop (P1).** After a long move with the drive saturated, PID_v1's integral is wound towards the move. When the shaft coasts past the target, the PID output still points the old way, the "never drive away from the target" rule zeroes it, and the integral unwinds at about 0.5 per second at Ki 0.01. In Slice_DCMT's host motor model the same move stopped at +50 and took 174 s to settle. feastorg/dcmotorservo#30 zeroes the integral while the drive is saturated; P2–P4 are the same moves with it.
- **M2 sweep attempts.** The first attempt stopped at −90 on a reply that failed CRC; the script then had no retry. On the second, the lease tripped at the first hard reversal from +60 to −60, and every later step read 0 RPM. With a retry on bad replies and a 0.5 s coast before each step, the third ran clean. What dropped the host's writes long enough to trip a 1 s lease is not known.
- **CRC failures (B1).** About 1 % of replies failed CRC with both motors running, and fewer with one. Each was reported as a CRC mismatch; there was no lockup and no timeout. 11-02 did not run both motors this hard, so there is no baseline. An 8-bit CRC passes about 1 in 256 corrupted replies (CRUMBS#130), so at 10 polls a second and this rate one bad reply would be accepted about every 43 minutes (estimate: 0.1 bad replies/s × 1/256).
- **`pos` saturates.** `GET_STATE` reports each position clamped to 16 bits. A 60 s forward run at 120 RPM (about 100 000 counts) pins it at 32767, so the bench script can't compute speed from it for those runs; the reverse runs started from the clamp and read correctly.
- **The steady-load stall (P6).** The stall check (`DCMT_STALL_TIMEOUT_MS` 1000, `DCMT_STALL_MIN_COUNTS` 4) sees a held shaft with drive applied, which is what it is for. With Kp 0.15 the position loop adds about 1 PWM per 7 counts of error, so a modest steady load holds the shaft still at low drive and latches the fault. That is a second way into #102.
- **The lease trip at 19:43.** A 3 s `sleep` between commands, with the lease armed, tripped it and braked both motors; three P3 moves then didn't start. The firmware did what the lease is for. P3 was rerun with `watch` polling between moves.

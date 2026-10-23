---
title: "2026-10-22: applied PWM, watchdog, e-stop and library fixes"
parent: Bench Records
---

# 2026-10-22: applied PWM, watchdog, e-stop and library fixes

**Verifies:** on `main`, [#56](https://github.com/feastorg/Slice_DCMT/issues/56) boot LED
and [#61](https://github.com/feastorg/Slice_DCMT/issues/61) applied PWM in GET_STATE,
plus the watchdog and e-stop paths. Against unreleased library `main` branches,
DCMotorServo [#11](https://github.com/CameronBrooks11/DCMotorServo/issues/11),
[#12](https://github.com/CameronBrooks11/DCMotorServo/issues/12) and
[#13](https://github.com/CameronBrooks11/DCMotorServo/issues/13)
(with [#62](https://github.com/feastorg/Slice_DCMT/issues/62)), and lmd18200
[#2](https://github.com/CameronBrooks11/lmd18200/issues/2). Picks the lead cap for
[#63](https://github.com/feastorg/Slice_DCMT/issues/63).
**By:** @CameronBrooks11

## Setup

| | |
| --- | --- |
| Image A | `4254ea5`. Envs `gen2_nano`, `gen2_nanoevery`. CRUMBS 0.16.2, bread-crumbs-contracts 0.9.1, DCMotorServo 1.1.0, LMD18200 0.1.4 |
| Image B | `4254ea5` with DCMotorServo pinned to `cc8bad6` and lmd18200 to `b11192a`, plus bench-only flags; the changes are in [`image-b.patch`](assets/2026-10-22-applied-pwm-and-library-fixes/image-b.patch). Envs `bench_b` (no lead cap), `bench_b_lead300` (`setMaxLead(300)`), `bench_b_acc300` (`setAccuracy(300)`). Not in the repo |
| B5 sketch | [`bench-current`](assets/2026-10-22-applied-pwm-and-library-fixes/bench-current/), lmd18200 `b11192a`, env `gen2_nanoevery` |
| Boards | gen2 Nano (ATmega328P); gen2 Nano Every (ATmega4809). Both have the D10/D11 motor-2 rework. Assembly not inspected |
| Controller | Raspberry Pi 4 Model B, I2C at 50 kHz, address 21, CRUMBS Python binding 0.16.2 and contracts 0.10.0, via [`dcmt_bench.py`](assets/2026-10-22-applied-pwm-and-library-fixes/dcmt_bench.py). USB serial only for B3 part 1 and B5 |
| Power | backplane (I2C and 12 V), one DCMT on the bus at a time |
| Outputs | an encoder motor on M1; M2 empty. E-stop: a jumper from D2 to GND (D2 has a pull-up; LOW is e-stop). B5: a DC ammeter in series with M1 |

## Steps and Results: Image A

### gen2 Nano

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| Boot LED (#56): power-cycle, D2 open; then with D2 to GND | blue ~1 s then green; blue then red | blue then green; blue then red | pass |
| Baseline: `version`, `caps`, `state`, `wdog` | module 2.x, position cap, idle, disarmed | CRUMBS 1602, module 2.0.0; caps level 2, flags `0x1F7` (no speed loop); mode 0, PWMs 0, `sp`/`spd` -32768 sentinel; watchdog disarmed | pass |
| Open loop (#61): `open 80 0`, `state`; `brake 1 0`, `state` | `m1_pwm=80`; then 0 | `m1_pwm=80`, pos 2236 then 4565; after brake `m1_pwm=0`, `brakes=0b01` | pass |
| Position (#61): `sp 5547 0`, `mode 1`, poll; `brake 1 1` | non-zero while moving, ~0 settled within ±10 | `m1_pwm` 28, 19, then 0 at pos 5538 (9 counts short); after brake 0, `brakes=0b11` | pass |
| Watchdog: `open 60 0`, `wd 500`, wait 1.2 s; `open 60 0`; `clear` | trip and brake; SET refused; clear applies | `wdog` armed 500 ms, tripped 1, trips 1; `brakes=0b11`, `m1_pwm=0`; `open` acked status 2 reason 2 (WATCHDOG_TRIP); `clear` applied, tripped 0 | pass |
| E-stop during motion: `open 60 0`, D2 to GND, then removed | brake within a poll; SETs refused; nothing resumes | `estop=1`, `estop_count=1`, `brakes=0b11`, `m1_pwm=0` within one 0.3 s poll, pos froze at 12921; `open` refused reason 3 (ESTOP); after removal `estop=0`, brakes stayed `0b11` | pass |
| Serial flood (#49), serial `MODE=` (#36), masked writes (#37) | | not run | skipped |

### gen2 Nano Every

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| Boot LED (#56) | as on the Nano | blue then green; blue then red | pass |
| Baseline | adds the speed cap | caps level 3, flags `0x1FF`; otherwise as on the Nano | pass |
| Speed (#61): `sp 30 0`, `mode 2`, 20 polls; `brake 1 1` | `m1_pwm` non-zero, `spd` near 30 | `m1_pwm` 18 to 28 on every poll; pos 53 to 1688 in 5.70 s (287 counts/s, ~21.5 RPM at 800 counts/rev); `spd` read only 0, 15 or 30, mean 19.5 | pass for #61; speed below target, see Notes |

## Steps and Results: Image B (gen2 Nano Every)

Over I2C, SET_SETPOINT writes one value into both the position and the speed
setpoint ([#57](https://github.com/feastorg/Slice_DCMT/issues/57)), so B3 part 1
set its targets with the serial commands `M1SPEED=` and `M1POS=`.

| Step | Env | Expected | Observed | Result |
| --- | --- | --- | --- | --- |
| B1 (#12): speed 5, then 0, then 10 RPM | `bench_b` | never runs backwards | 5 RPM: did not start (`m1_pwm` 0, pos 0 to 1). 0: held at pos 8. 10 RPM: pos 12 to 1467 without decreasing, ~99 counts/s (~7.4 RPM); `spd` only 0 or 15. One poll read `m1_pwm=-15` (23:19:42.878) while pos still rose 876 to 903 | pass |
| B2 (#13): 60 RPM, shaft held by hand, released | `bench_b` | baseline, no cap | ~42 RPM before; held ~10 s at `m1_pwm=150`; on release 240 to 285 RPM at 150, 2500 counts before the brake and 1168 more while braking | lunge, as expected without a cap |
| B2 (#13) | `bench_b_lead300` | no lunge, still reaches 60 RPM | ~42 RPM before (564 counts/s); held ~4 s, PWM rose 62 to 77; on release one poll at 75 RPM, then 60; afterwards ~56 RPM (749 counts/s over 10.8 s), PWM 33 to 44 | pass |
| B3 part 1 (#13, #62): `M1SPEED=10`, `M1POS=1500`, `mode 1`, then `mode 2` mid-move | `bench_b` | straight to ~10 RPM, no 150 burst | `mode 2` 0.31 s after `mode 1`, pos 924 at the first poll; `m1_pwm` 0 to 24, pos 924 to 1335 without decreasing (~7 RPM) | pass |
| B3 part 2 (#13, #62): `open 120 0` ~2 s, `open 0 0`, `sp 10 0`, `mode 2` | `bench_b` | no full-PWM reverse | pos 0 to 6041 in open loop; in speed mode `m1_pwm` 0 to 22, never negative, pos 6986 to 7526 | pass |
| B4 (#11): `sp 1000 0`, `mode 1`; then `sp 941 0` from rest | `bench_b_acc300` | stops within 300; a 200-count step does not move | stopped at 741 (259 short), `m1_pwm=0`; after `sp 941` pos stayed 741 for 2.4 s | pass |

`bench_b_lead900` was built but not run: 300 was the smallest cap built and it
passed, so **the lead cap for #63 is 300**.

## Steps and Results: B5 Current Sense (lmd18200 #2)

The sketch holds M1 at PWM 0, 64, 128, 192 and 255 for 6 s each and prints the
32-sample average of `readCurrent(4420)` every 0.5 s. Meter readings were taken
from a video of the ammeter; sketch values are the mean of the 12 lines per step
in the raw log.

| PWM | Meter (A) | `readCurrent()` (A) | Against the meter |
| --- | --- | --- | --- |
| 0 | 0 | 0.045 | offset |
| 64 | 0.204 to 0.208 | 0.338 | +64 % |
| 128 | 0.262 to 0.265 | 0.450 | +71 % |
| 192 | 0.293 to 0.300 | 0.387 | +30 % |
| 255 | 0.330 to 0.337 | 0.322 | -3.6 %; -17 % with the 0.045 offset subtracted |

Thermal flag 0 throughout. **Result:** pass for #2's scaling at DC drive (255).
At zero current the sense path reads 0.045 A (about 15 ADC counts). With that
offset subtracted, 255 sits just below the datasheet's minimum sensitivity
(325 µA/A, about 14 % low against the 377 µA/A typical). Below 255 the reading
does not track the meter.

## Raw Output

The commands behind each step and their output, then the B5 serial log:
[`raw-session.txt`](assets/2026-10-22-applied-pwm-and-library-fixes/raw-session.txt).

## Not Checked

- gen1 boards.
- Serial flood (#49), serial `MODE=` (#36) and masked writes (#37).
- Motor 2: no motor fitted.
- The Nano Every flash was not read back after upload.
- B5 used one board and one motor. The fitted sense resistor was not measured,
  so the offset and the low reading cannot yet be split between the board and
  the library.
- DCMotorServo #18 (frozen speed reading at setpoint 0) was not observable:
  the shaft did not move at setpoint 0.
- Polling was every ~0.3 s, so peaks shorter than that can be missed.

## Notes

- **Speed loop holds below target.** 30 RPM ran at ~21.5 RPM (image A) and
  60 RPM at ~42 RPM before the stall on both image B variants. On
  `bench_b_lead300` it ran ~56 RPM after the release. Not explained. The speed
  measurement steps in 15 RPM units (one count per 5 ms window).
- **No stall cutout.** On `bench_b` the firmware drove 150 PWM into the held
  shaft for ~10 s. Nothing in the firmware limits this.

---
title: "2026-11-02: the I2C lockup, its fix, and counts per turn"
parent: Bench Records
---

# 2026-11-02: the I2C lockup, its fix, and counts per turn

**Status: complete.** The I2C lockup of 2026-11-01 happened again with a
motor running (V1). A starvation guard caught its signature (V3), and a
collision injected on SDA reproduced it on demand: 5 lockups on the guard
build, none with the core's TWI slave handler fixed (I1, I2). Counts per
output turn measure 848, not 798 (C1 to C4). On the bench, closed-loop speed
at 120 RPM climbs past the target and collapses, as #105 predicted from the
model (V2).

**Verifies:** [#107](https://github.com/feastorg/Slice_DCMT/pull/107)
(starvation guard) and [#109](https://github.com/feastorg/Slice_DCMT/pull/109)
(TWI slave flags cleared after a collision, closing #101). Also re-checks
#89 and #103, and is the bench evidence for #105.
**By:** @CameronBrooks11

## Setup

| | |
| --- | --- |
| Firmware | B0 to V1: `6a8df02`, env `gen2_nanoevery`, as flashed on 2026-11-01 (main's firmware unchanged through `2c6eb1b`). V2 to I1: `b73133a`, #107's branch before its CHANGELOG-only amend (`b3878a2`, merged as `c1be995`). I2 and C2 to C4: `29b9bdf`, #109's head (merged as `91faa36`). Libraries as on 2026-11-01: CRUMBS 0.17.0, bread-crumbs-contracts 0.11.1, DCMotorServo 1.2.1, LMD18200 0.1.5, bread-slice-core 0.2.0 |
| Board | gen2 Nano Every (ATmega4809) with the D10/D11 motor-2 rework |
| Motors | two E-S Motor 36PG-3429-19-EN 12 V planetary gearmotors, as on 2026-11-01. Firmware `MOTOR1_CPR` and `MOTOR2_CPR` 798, so every RPM figure here is in 798 units: about 6 % high (C1 to C4) |
| Controller | Raspberry Pi 4 Model B, I2C at 50 kHz, address 21, CRUMBS Python binding 0.16.2 and contracts 0.11.0, via [`dcmt_bench.py`](assets/2026-11-02-bus-lockup-and-cpr/dcmt_bench.py) (md5 `fbae80ff…` at the end of the session; adds `speedto`, `home`, `prep` and `ser` to the 2026-11-01 version), [`twi_trial.sh`](assets/2026-11-02-bus-lockup-and-cpr/twi_trial.sh) and [`replylog.py`](assets/2026-11-02-bus-lockup-and-cpr/replylog.py). From V2 on, the board's USB was also plugged into the Pi, for the serial `STARVE?` command |
| Power | backplane (I2C, 12 V and the shared e-stop line), with Slice_RLHT's gen2 Nano on the bus at address 19 |

Every motor step ran with the command-watchdog lease armed at 1000 ms, any
frame (`wdl 1000 0`), unless the step says otherwise.

## Steps and Results

Times are the controller's clock.

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| B0 baseline: `version`, `state`, `wdog`, `faults` | CRUMBS 1700, module 2.0.0; idle; watchdog disarmed; no faults | as expected | pass |
| S1 M1 at 10 RPM for 58 s | within 5 %; no latch | within 5 % (200 ms mean) at 1.32 s, peak 1 s mean 11.0, last-5-s mean 10.1 RPM; 580 polls, 0 failed; no faults | pass |
| C1 M1 at 10 RPM until 7980 counts (`speedto 1 7980 10`; #103) | the tape flag back at its start if 798 is right | 7972 to 15953, 7981 counts in 60.2 s (about 133 counts/s). Flag about 0.6 turn short. The lease tripped during this step: `speedto`'s 1.0 s settle sleep left a 1.018 s gap between frames, a script error. The trip held both motors braked, and G1's first attempt was refused reason 2 (`WATCHDOG_TRIP`) until `clear` | data (C4) |
| G1 M2 at 60 RPM 15 s, one short grip (#89) | no latch; peak 1 s mean after release <= 65 RPM | gripped at 3.2 s for about 0.2 s (200 ms mean 7.9 RPM, PWM 65). Peak 1 s mean after release 63.8 RPM, peak 200 ms mean 69.2; last-5-s mean 60.0; 150 polls, 0 failed; no latch | pass |
| V1 M1 at 120 RPM for 60 s (#105), after homing to -30000 | no lockup | 200 ms mean through 120 at 8.2 s. The bus then hung, about 37.7 s in, near +28 000 counts: every poll and command timed out. SCL low. M1 ran on with the lease armed until the e-stop jumper braked it. The LED stayed red after the release, while Slice_RLHT went back to green. The DCMT's reset button freed the bus | **fail** (#101, second occurrence) |
| Flash `b73133a` (#107 guard), then `version`, `caps`, `state`, `wdog`, `faults`, `STARVE?` | caps `0103ff0f0000`; idle; `STARVE: none recorded` | as expected; Pi GPIO3 (SCL) high | pass |
| V2 M1 at 120 RPM for 60 s, after homing to -30000 | within 5 %; the guard quiet | within 5 % at 6.92 s, then up to a peak 1 s mean of 146.4 RPM at 15 s, back to 120 by 22 s, collapse to 86.6 at 26 s, and back up to 139.8 by 38 s. From 40 s the reported position stays at 32 767 (the i16 limit, feastorg/bread-crumbs-contracts#73). 600 polls, 0 failed; no faults; `STARVE` none | the climb and collapse **fail** (#105); no lockup |
| V3 M1 at 120 RPM, after homing to -30000 | as V2 | the homing step hadn't finished (`state`: PWM -150, position 3255), so speed mode began with M1 reversing at full drive: a script error. 2.9 s in, 13 polls failed with `OSError(5)` within 25 ms, then the board answered from reset: position 0, brakes released, lease untripped, no faults. `STARVE: count 1 SREG 0x82 CPUINT.STATUS 0x00 TWI0.SSTATUS 0xBB TWI0.SCTRLA 0xE1 TWI0.MSTATUS 0x03 TCB3.INTCTRL 0x01 TCB3.INTFLAGS 0x01 RSTFR 0x08` | pass for the guard: it recorded and recovered a lockup |
| R1 `twi_trial.sh 5`: full reverse for about 1 s, then speed mode at +120 RPM, 8 s each | no lockup | trials 1 to 4: 80 polls each, 0 failed; peak 1 s means 115.3 to 117.1 RPM; `STARVE` count stayed 1. Trial 5 stopped before it began: `home`'s GET_STATE reply failed its CRC (`0xE6`, expected `0x39`) and the script didn't retry (it does now). Board braked at position 2988 | no lockup in 4 trials; the reversal does not reproduce it on demand |
| I1 collision injection, guard build: GET_STATE every 100 ms for 90 s with retries off (`replylog.py 21 2 90`), both motors braked, SDA touched to GND through 220 ohm, 5 to 10 brief taps | the guard catches each lockup | 748 polls, 241 failed (mostly all-zero replies while SDA was held). `STARVE` count 1 to 6: 5 lockups, each a guard reset. Last record `TWI0.SSTATUS 0xAB` | lockup reproduced on demand |
| I2 flash `29b9bdf` (#109), then the same injection | no lockup | 766 polls, 214 failed, again mostly while SDA was held; the slave answered as soon as a tap ended. `STARVE: none recorded`; no reset; LED stayed green | pass |
| C2 M1 position move `home 7980 0` from a flag set on a mark at position 0 (#103) | flag back on the mark at 798 | +7988 counts in 2.6 s (about 3000 counts/s); flag about 0.6 turn short of the mark | data |
| C3 M1 at 10 RPM, `speedto 1 7980 10` | as C2 | 7988 to 15978, +7990 counts in 60.4 s; flag another 0.6 turn short | data |
| C4 M1 `home -16000 0` | flag back near the mark | -31 986 counts in 8.7 s (about 3700 counts/s), to -16 008; flag 1/8 turn from the mark. Net -16 008 counts = 18.875 turns: **848 counts per output turn**. C2 and C3 agree at about 0.6 turn short per 7990 counts at both speeds, so the count per turn doesn't depend on speed | pass; 848 |

## Raw Output

[`raw-session.txt`](assets/2026-11-02-bus-lockup-and-cpr/raw-session.txt):
every DCMT command of the session and its output, in order, with the two
collision-injection runs at the end (their last 25 lines, as captured).
The controller's address is replaced by `<controller>`.

V1's log is incomplete: the capture lost the polls from about 8.6 s until
the hang and the first errors. The 37.7 s and +28 000 counts above are
from the full log as read at the time (posted on #101).

## Not Checked

- What causes the collisions during motor runs. Motor current and PWM noise
  on SDA/SCL is the leading suspect, since Slice_RLHT, with no motors, has
  not locked up on the same bus. No scope was on the lines.
- A motor run on `29b9bdf` long enough to show the lockup is gone in
  service, rather than under injection: only C2 to C4 ran on it.
- The status LED during V3 and I1's resets.
- M2's counts per turn; C1 to C4 are M1 only.
- G1 is one grip on one motor; 2026-11-01's M2 grips peaked at 66.8 and
  67.4 RPM.
- The gen2 Nano and gen1 boards.

## Notes

- **The lockup.** In framework-arduino-megaavr 1.8.8's
  `TWI_SlaveInterruptHandler`, the collision and bus-error branches leave
  the TWI slave's data interrupt flag set, so the interrupt re-enters
  without end, CLKHOLD keeps SCL low, and the millis timer and encoder
  interrupts starve. `loop()` still gets one instruction per return, which
  is why the e-stop worked and the 1 s hardware watchdog never fired, while
  the lease, which counts `millis()`, never tripped. #107 detects this and
  resets; #109 clears the flags. Reported upstream as
  arduino/ArduinoCore-megaavr#152.
- **Counts per turn.** 848 is 12 PPR x 4 x about 17.7, not the datasheet's
  nominal 1:19 (912). The 798 in the firmware came from a misread flag on
  2026-11-01: every measurement since, by three methods, gives 846 to 861.
  The firmware default stays 798 until the speed-mode redesign
  (feastorg/dcmotorservo#26) lands, since it moves every speed figure by
  6 %.
- **#105 on the bench.** V2 shows the climb past the target and the
  collapse that the host model predicted for speeds above about 90 RPM.
  Here the peak came at 15 s; in the model, at about 20 s.
- **Script errors.** C1's lease trip, V3's unfinished homing and R1's
  unretried CRC failure were the bench script's. `dcmt_bench.py` now polls
  during settles, waits for `home` to arrive and stop, and retries a bad
  reply.

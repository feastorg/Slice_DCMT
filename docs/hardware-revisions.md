---
title: "Hardware Revisions"
description: "Board generations G1/G2/G3, required hand reworks, MCU PWM differences, and wiring conventions."
---

Which board generation is which, what changed between them, and which reworks
and wiring conventions are load-bearing but not visible in any file.

Every open hardware issue on this repo traces back to the absence of this page.
Two real defects were caught by hand at the bench and neither was written down,
so both were rediscovered months later from the KiCad files alone — and one was
initially misdiagnosed because the current board in `hardware/` is not the board
in service.

## Generations

| | Board files | Status |
|---|---|---|
| **G1** | `archive/hw_archive/2021-06-08/`, `archive/hw_archive/2023-12-13/` (netlists byte-identical) | Superseded, still **supported**. In service on the reference rig. |
| **G2** | `archive/hw_archive/recent_finn/` | Next onto the bench. **Requires hand rework — see below.** |
| **G3** | `hardware/` (current KiCad) | **Not deployed.** Still in bench testing. No firmware profile exists. |

`hardware/` holds G3. The board actually in service is under `archive/`. That is
counter-intuitive and is worth remembering before concluding anything from the
current schematic.

## Pin maps

Nano footprint pad → Arduino pin: pad 5 = D2, pad 8 = D5, pad 13 = D10,
pads 19–22 = A0–A3. LMD18200: pin 3 = DIRECTION, 4 = BRAKE, 5 = PWM.

| Arduino pin | G1 | G2 (**as fabricated** — see reworks below) | G3 |
|---|---|---|---|
| D5 | `/DIR2` | `/LED` | `/LED` |
| D6 | `/MC2` (PWM) | `/DIR1` | `/PWM2` |
| D7 | `/BR2` | `/MC1` (PWM) | `/DIR2` |
| D8 | — | `/BR1` | `/BR2` |
| D9 | `/DIR1` | `/THRM1` | `/THRM2` |
| D10 | `/MC1` (PWM) | `/DIR2` | `/PWM1` |
| D11 | `/BR1` | `/MC2` (PWM) | `/DIR1` |
| D12 | — | `/BR2` | `/BR1` |
| D13 | — | `/THRM2` | `/THRM1` |
| A0–A3 | `/A1 /B1 /A2 /B2` | `/B2 /A2 /B1 /A1` | `/B1 /A1 /B2 /A2` |

## G1 has no status LED

Not unpopulated — **absent from the design**. Zero WS2812 parts and no `/LED`
net in either G1 board file, and no G1 board ever carried an LED of any kind.

`LED_PIN 5` lives in the shared "General BREAD" block that every slice inherits
(`Slice_RLHT` carries the identical line), so it was defined on G1 regardless.
Because G1 also maps `MOTOR1_DIR` to pin 5, the LED code was bit-banging NeoPixel
timing into a motor driver's DIRECTION input to drive a part that does not exist.

Fixed by `DCMT_HAS_STATUS_LED` (#15), mirroring `Slice_RLHT`'s
`RLHT_HAS_STATUS_LED`. RLHT received that guard in the gen1/gen2 split on
2026-03-09; DCMT's split landed the same day without it.

## G2 requires two trace swaps, and both are expected

A G2 board is **not** considered correct as fabricated. Both swaps below were
applied to the entire G2 fleet (six boards) on 2026-09-09, and the `gen2`
firmware map describes the board **after** both. Treat them as part of the board
definition, not as modifications.

| # | swap | motor | why |
|---|---|---|---|
| 1 | **D6 ↔ D7** | motor 1 | as fabricated `/MC1` (PWM) is on **D7**, which has no hardware timer on *either* MCU |
| 2 | **D10 ↔ D11** | motor 2 | as fabricated `/MC2` (PWM) is on **D11**, which has a timer on the ATmega328P but **not** on the ATmega4809 |

After both, PWM lands on **D6** and **D10** — timer pins on both MCUs — and
DIRECTION on D7 and D11, which only ever need digital pins.

Swap 2 in detail, since it is the more recent: motor 2's driver is **U3**.
`/DIR2` ran Nano pad 13 (**D10**) → U3 pin 3 (DIRECTION), and `/MC2` ran Nano pad
14 (**D11**) → U3 pin 5 (PWM). The swap exchanges them at the MCU end. `/BR2`
(D12 → pin 4) and `/THRM2` (D13 → pin 9) are untouched.

Neither swap costs anything on the Nano: Timer1 (D10) and Timer2 (D11) both
default to roughly 490 Hz, so the PWM frequency is unchanged, and nothing in the
firmware or its libraries claims a timer register. On the ATmega4809, `millis()`
uses a TCB rather than TCA0, so D10 is uncontended there too.

**A G2 board without both swaps is mismatched by the current firmware**, and the
mismatch is the dangerous direction — PWM driving the direction line means a
command of zero produces full output. The bus cannot tell them apart: the
reported module version is `1.0.0` regardless (#13). Mark reworked boards
physically.

The firmware now refuses to build a map that puts PWM on a non-timer pin
(`DCMT_PIN_HAS_TIMER`), so this class of defect cannot recur silently.

## PWM capability differs by MCU, and both are used

Both the Nano (ATmega328P) and the Nano Every (ATmega4809) are run on G1 and G2.
The Nano Every is used when running encoder closed loop, for the headroom to do
it smoothly.

| MCU | PWM-capable pins |
|---|---|
| ATmega328P (Nano) | D3, D5, D6, **D9**, D10, **D11** |
| ATmega4809 (Nano Every) | D3, D5, D6, **D9**, D10 |

From `framework-arduino-avr/variants/standard/pins_arduino.h` and
`framework-arduino-megaavr/variants/nona4809/pins_arduino.h`.

**D11 has hardware PWM on the Nano but not on the Nano Every.**

`analogWrite()` on a non-timer pin does not fail — it degrades to a digital
write at a threshold of 128 (`wiring_analog.c`). So the failure is silent.

| | motor 1 PWM | motor 2 PWM | Nano | Nano Every |
|---|---|---|---|---|
| G1 | D6 | D10 | ok | ok |
| G2 as fabricated (never run) | D7 | D11 | no | no |
| G2 after both reworks | D6 | D10 | ok | ok |
| G3 | D6 | D10 | ok | ok |

Before its second rework, G2 + Nano Every gave motor 2 no proportional control —
off below 128, full output at or above — in exactly the configuration chosen for
closed loop. The D10↔D11 swap resolved it; **G3 was already laid out that way.**

The guard added alongside that fix means a map putting PWM on a non-timer pin now
fails the build rather than shipping. Resolved; see #18 for the history.

## The encoder connector is mirrored between G1 and G2, and a lead swap compensates

The connector pinout is reversed end-for-end:

- **G1**: `A, B, 5V, GND`
- **G2**: `GND, 5V, B, A`

A cable cannot be plugged in the same orientation on both — reversed, it would
put 5V on GND.

The firmware's encoder macros are shared across generations
(`MOTOR1_ENCODER_PIN1/2 = A2/A3`, `MOTOR2_ENCODER_PIN1/2 = A0/A1`), so the
firmware reads channels in the order (A, B) on G1 and (B, A) on G2. Transposed
channels negate the encoder count.

**On G2 a second physical inversion cancels this**, and closed-loop position and
speed control were both validated on G2 at the bench in early 2026. There is no
firmware invert flag — none exists in `firmware/`, `DCMotorServo` takes no
polarity argument, and the DCMT op set has no polarity field — so the correction
is necessarily in the wiring.

Which wiring is **not currently recorded**. Two candidates produce the same
cancellation:

| what was swapped | closed loop | open-loop direction vs G1 |
|---|---|---|
| **motor leads** at the screw terminal | converges | **reversed** — positive PWM spins the other way |
| **encoder leads** (A/B) in the cable | converges | **same as G1** |

They are not interchangeable. Motor-lead reversal also flips the open-loop
direction sense, which matters to anything that assumes a direction — a stirring
impeller, a peristaltic pump's flow direction. Encoder-lead swapping does not.

> **To settle it:** on a G2 board, command a small positive open-loop PWM and
> compare the shaft direction against G1 under the same command. Same direction
> means the encoder leads were swapped; opposite means the motor leads were.
> Record the answer here.

> **Whichever it is, both inversions must be present or neither.** A G2 board
> wired without the compensating swap has inverted closed-loop feedback, and no
> code change reveals it. Equally, transposing the encoder macros in firmware to
> "fix" G2 would double-apply the correction and turn a converging axis into a
> diverging one. See #19.

## G3 has no firmware profile

`DCMT_HW_GEN` accepts only 1 or 2. Flashing a `gen2` build onto a G3 board gives
correct pins for motor 1 but **swaps motor 2's PWM and DIRECTION**, because G3
uses `D10 = /PWM1`, `D11 = /DIR1` while `gen2` expects the opposite. Tracked in
#18.

Note also that `DCMT_HW_GEN` defaults to 1 when the build flag is absent, so the
`#error` guard never fires for a *missing* generation — only for an out-of-range
one. Tracked in #17.

## Firmware version cannot identify the build

`reply_version` reports `DCMT_MODULE_VER_*`, hardcoded at `1.0.0` in
`bread-crumbs-contracts` and unchanged by behaviour fixes. A board reports
byte-identical version before and after any of the changes on this page.

**Record the flashed git SHA by hand at flash time.** Tracked in #13.

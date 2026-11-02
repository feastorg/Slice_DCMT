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
| **G1** | `archive/hw_archive/g1-2021-06-08-mtu/` | Superseded, still **supported**. In service on the reference rig. |
| **G2** | `archive/hw_archive/g2-2024-02-13-finn/` | On the bench; the records in `docs/bench/` use it. **Requires hand rework — see below.** |
| **G3** | `hardware/` (current KiCad) | **Not deployed.** Still in bench testing. No firmware profile exists. |

Archive directory names carry the generation, the board date and where the
boards came from. The G2 date was read off a physical board — a JLCPCB date
mark — not from the KiCad files, whose silkscreen carries only `R1 2024`.

`hardware/` holds G3. The board actually in service is under `archive/`. That is
counter-intuitive and is worth remembering before concluding anything from the
current schematic.

A second G1 directory, `archive/hw_archive/2023-12-13/`, was removed on
2026-09-10. It was a KiCad 6 re-save of the same project rather than a second
fabrication run: with both projects converted to a common format, its pad → net
map and its copper geometry — 112 pads, 198 tracks, arcs and vias — were
identical to `2021-06-08`, and its exported netlist was byte-identical. It is
recoverable from git history if that provenance is ever wanted.

## Pin maps

Nano footprint pad → Arduino pin: pad 5 = D2, pad 8 = D5, pad 13 = D10,
pads 19–22 = A0–A3. LMD18200: pin 3 = DIRECTION, 4 = BRAKE, 5 = PWM.

| Arduino pin | G1 | G2 (**as fabricated** — see reworks below) | G3 |
|---|---|---|---|
| D5 | `/DIR2` | `/LED` | `/LED` |
| D6 | `/MC2` (PWM) | `/DIR1` | `/PWM2` |
| D7 | `/BR2` | `/MC1` (PWM) | `/DIR2` |
| D8 | `/THRM2` | `/BR1` | `/BR2` |
| D9 | `/DIR1` | `/THRM1` | `/THRM2` |
| D10 | `/MC1` (PWM) | `/DIR2` | `/PWM1` |
| D11 | `/BR1` | `/MC2` (PWM) | `/DIR1` |
| D12 | `/THRM1` | `/BR2` | `/BR1` |
| D13 | — | `/THRM2` | `/THRM1` |
| A0–A3 | `/A1 /B1 /A2 /B2` | `/B2 /A2 /B1 /A1` | `/B1 /A1 /B2 /A2` |

Every column above was regenerated from the board files on 2026-09-10 by
reading each `A1` pad's net out of the PCB, so it is the boards' own account of
themselves rather than a transcription.

**The table names the board's nets, and on G1 those run opposite to the
firmware's motor numbering.** Firmware `MOTOR1_*` (D6, D5, D7) lands on `/MC2`,
`/DIR2`, `/BR2` — driver U2, whose outputs go to J7 — while `MOTOR2_*` (D10, D9,
D11) lands on U1 and J6. On G2 the two agree. On G3 they run opposite again:
firmware motor 1 (D6, D7, D8) lands on `/PWM2`, `/DIR2`, `/BR2`. All are
correct; it means the G1 row of the PWM table below, which uses firmware
numbering, refers to the opposite driver from the G1 column here.

The thermal flags are wired on all three generations but are not read anywhere
in `firmware/`; `MOTOR1_THERMAL_PIN` and `MOTOR2_THERMAL_PIN` are defined only
in the gen2 map. D13 is unconnected on G1, where the thermal flags sit on D8
and D12 instead; every pin in the table is used on G2 and G3. D0 and D1, the
serial pins, are unconnected on all three.

## G1 has no status LED

Not unpopulated — **absent from the design**. Zero WS2812 parts and no `/LED`
net in the G1 board file, and no G1 board ever carried an LED of any kind.

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
reported module version is the same with or without the rework (#13). Mark
reworked boards physically.

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

## The two motors run at different PWM frequencies on the Nano

Every generation puts motor 1's PWM on D6 and motor 2's on D10 (table above).
On the Nano those pins are on different timers, set up differently by the
Arduino core:

| MCU | D6 (motor 1) | D10 (motor 2) |
|---|---|---|
| ATmega328P (Nano) | Timer0, fast PWM: 16 MHz / 64 / 256 = **976.6 Hz** | Timer1, 8-bit phase-correct: 16 MHz / 64 / 510 = **490.2 Hz** |
| ATmega4809 (Nano Every) | TCB0, clocked from TCA0: 250 kHz / 256 = **976.6 Hz** | TCA0, single slope: 250 kHz / 256 = **976.6 Hz** |

From `framework-arduino-avr/cores/arduino/wiring.c` (`init()`), and
`framework-arduino-megaavr/variants/nona4809/variant.c` (`setup_timers()`,
which the core's `init()` calls) with `timers.h` (`PWM_TIMER_PERIOD` 0xFF).
Computed from the cores' timer setup, not measured.

So on a Nano the same duty gives motor 2 half the switching frequency of
motor 1: a different current ripple and response, so closed-loop gains tuned
on one motor don't transfer exactly to the other. Both frequencies are
audible. On the Nano Every the two match.

Not changed in firmware. Matching them on the Nano means retiming Timer1,
which `millis()` doesn't use, or putting both PWMs on one timer in a board
revision. Decide with the next board revision (#68).

## The encoder headers, by generation

Each board has two 4-terminal encoder headers. The positions below are given in
one frame, which all three boards share: component side up, backplane connector
J1 on the left. The terminals then run down the right-hand edge, motor output
and supply terminals at the top and the encoder headers below them, and each
list reads from top to bottom.
Without a stated frame, the same header reads in either order.

| | Upper header | Lower header |
|---|---|---|
| Firmware motor | **1** | **2** |
| G1 | J3, silk `ENC 2`: `GND, 5V, B, A` | J2, silk `ENC 1`: `GND, 5V, B, A` |
| G2 | J6–J9, silk `ENCODER 1` (terminal labels on the back): `A, B, 5V, GND` | J2–J5, silk `ENCODER 2`: `A, B, 5V, GND` |
| G3 | J3, no silk labels: `A, B, 5V, GND` | J2, no silk labels: `A, B, 5V, GND` |

The MCU pin each signal terminal reaches:

| Terminal | G1 upper / lower | G2 upper / lower | G3 upper / lower |
|---|---|---|---|
| A | A2 / A0 | A3 / A1 | A3 / A1 |
| B | A3 / A1 | A2 / A0 | A2 / A0 |

Both tables come from the board files: each header's pad positions and nets,
the silkscreen text beside it, and the Nano pad nets in the pin-map table above.
The upper header is firmware motor 1 on every generation. On G1 its silkscreen
says `ENC 2`, matching that board's driver net numbers, which also run opposite
to the firmware's.

Every header carries the MCU's own 5 V rail and two MCU pins, with no series
resistor, bias or protection on any generation.

### G1's terminal order is mirrored on G2 and G3

G1 reads `GND, 5V, B, A` from the top; G2 and G3 read `A, B, 5V, GND`. Read from
the bottom instead, G1 is `A, B, 5V, GND` and G2 is `GND, 5V, B, A`, which is
how this page listed them before. It is the same fact.

A harness moved terminal-for-terminal from a G1 header to a G2 header lands
every wire on a different signal: the encoder's GND wire on `A`, its 5 V wire on
`B`, its B output on `5V` and its A output on `GND`. The encoder is then fed
through the MCU's input pins, and an output driven against the 5 V rail or
ground can short it. Wire each generation to its own table, and check
continuity with the board unpowered before connecting.

### G2 and G3 transpose A and B at the MCU, and the wiring compensates

This is separate from the terminal order. The firmware's encoder macros are
shared across generations (`MOTOR1_ENCODER_PIN1/2 = A2/A3`,
`MOTOR2_ENCODER_PIN1/2 = A0/A1`), and terminal `A` reaches a different MCU pin
on G2 and G3 than on G1 (table above). So the firmware reads channels in the
order (A, B) on G1 and (B, A) on G2 and G3. Transposed channels negate the
encoder count.

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

`DCMT_HW_GEN` accepts only 1 or 2. Since the G2 D10↔D11 rework, a `gen2` build
puts every drive pin where G3 expects it: motor 1 on D6, D7 and D8 (`/PWM2`,
`/DIR2`, `/BR2`), motor 2 on D10, D11 and D12 (`/PWM1`, `/DIR1`, `/BR1`). That
is a coincidence of layout, not a compatibility guarantee, and G3 also
transposes the encoder channels as G2 does, so closed loop on G3 needs the same
compensating wiring. Nothing declares a `gen2` binary acceptable on G3. Tracked
in #18.

`DCMT_HW_GEN` has no default: a build without the flag fails with an `#error`
in `firmware/include/config.h` rather than picking a pin map (#17). Build one
of the `gen*` environments in `firmware/platformio.ini`.

## Firmware version cannot identify the build

`reply_version` reports `DCMT_MODULE_VER_*`, a constant in
`bread-crumbs-contracts` (`2.0.0` as of contracts 0.11.1). It changes with the
wire contract, not with firmware behaviour fixes, so a board reports
byte-identical version before and after any of the changes on this page.

**Record the flashed git SHA by hand at flash time.** Tracked in #13.

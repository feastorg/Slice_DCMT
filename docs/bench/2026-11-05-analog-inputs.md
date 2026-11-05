---
title: "2026-11-05: analog inputs on the encoder header (gen1)"
parent: Bench Records
---

# 2026-11-05: analog inputs on the encoder header (gen1)

**Status: complete for gen1.** On a gen1 Nano with port 1 built as analog, the board reports the new capability and reads each pin on its own:

- a known 3.3 V and GND read correctly;
- the readings didn't move by a count while motor 2 ran and reversed at full power;
- a turbidity head reads through it: 848 counts in clear water, falling to 806, 120 and 52 at 100 mg/L, 5 g/L and 10 g/L of kaolin, and to 4 with its gap blocked.

The gen1 silkscreen numbering sent the first test voltage to the wrong header (Notes).

**Verifies:** [#114](https://github.com/feastorg/Slice_DCMT/pull/114) (`DCMT_INPUT_ANALOG`, `GET_INPUTS`), step 2 of [#106](https://github.com/feastorg/Slice_DCMT/issues/106).
**By:** @CameronBrooks11

## Setup

| | |
| --- | --- |
| Firmware | `b77311c` (main, with #114), env `gen1_nano_analog1`: port 1 `ANALOG`, port 2 `NONE`, open loop only. 17968 B flash, 914 B RAM. CRUMBS 0.17.0, bread-crumbs-contracts 0.12.0, LMD18200 0.1.5, DCMotorServo 1.2.1, bread-slice-core 0.2.0 |
| Board | gen1 DCMT on a classic Nano (ATmega328P), I2C address 21 |
| Port 1 | the upper encoder header, J3, silk `ENC 2`. Terminal A reaches A2 (`p1a`), terminal B reaches A3 (`p1b`) (`docs/hardware-revisions.md`) |
| Motor 2 | connected, no encoder |
| Controller | Raspberry Pi 4, I2C at 50 kHz, CRUMBS Python binding 0.16.2, through [`dcmt_bench.py`](assets/2026-11-05-analog-inputs/dcmt_bench.py) (md5 `fb17649b…`). It adds `inputs [N]`, which queries GET_INPUTS (0x81) and decodes the 10-byte reply itself, so the venv's contracts 0.11.0 is enough. Its volts assume a 5.000 V reference |
| Power | backplane; the board's 5 V rail measured 5.0x V |
| Sensor | a TST-10-type turbidity probe head in its stock housing, on a DFRobot SEN0189 adapter, mode switch on `A` (analog output). Adapter signal to terminal B, VCC to 5V, GND to GND. Results are for this head, housing and adapter only: a different adapter, a modified one or the head in another housing reads differently |

## Steps and Results

Times are the controller's clock.

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| A1 `version`, `caps`, `state` after flashing | caps level 1, flags `0x15E3` (open loop as `_noenc`, plus `DCMT_CAP_INPUTS`); mode 0 | `caps 0101e3150000`: level 1, flags `0x15E3`; mode 0, brakes released, no e-stop | pass |
| A2 `inputs`, nothing on port 1 | valid `0x03` (p1a, p1b), bits 10, port 2 not valid | valid `0x03`, bits 10, p1a and p1b 22–25 counts (floating), p2a/p2b not valid | pass |
| A3 3.3 V on port 1 terminal B | p1b about 3.3 × 1024 / 5.0x | p1b 665–666: 3.25 V at a 5.00 V reference, 3.31 V at 5.09 V. p1a, floating, followed it at 661 | pass |
| A4 terminal A to GND, 3.3 V still on B | p1a 0, p1b unchanged | p1a 0, p1b 665, over 6 reads | pass |
| A5 motor 2 open loop at PWM 150 for about 10 s, then -255, then +255, with the watchdog lease armed (`wdl 1000 0`) | no change on p1a or p1b | 32 reads across the three phases: p1a 0–0, p1b 665–665. The motor turned and reversed (seen). No failed read, no lease trip. Both brakes on at the end | pass |
| T1 turbidity head, dry, ambient light | a steady value | p1b 717–718 (3.50 V) | pass |
| T2 the head's gap blocked with an opaque card | near 0 | p1b 4 (0.02 V) | pass |
| T3 card removed | back to T1 | p1b 718 | pass |
| T4 head in clear DI water | about 4.1 ± 0.3 V (DFRobot's clear-water figure) | p1b 848–849, mean 848.5 over 20 reads (4.14 V) | pass |
| T5 kaolin 100 mg/L (1 mL of 10 g/L in 99 mL DI) | below T4 | p1b 805–806, mean 805.6 (3.93 V), 5 % below clear | pass |
| T6 kaolin 5 g/L (50 mL of 10 g/L in 50 mL DI) | well below T5 | p1b 119–120, mean 119.8 (0.58 V), 86 % below clear | pass |
| T7 kaolin 10 g/L (the stock) | below T6, above the blocked floor (T2) | p1b 51–52, mean 52.0 (0.25 V), 94 % below clear | pass |

T4 to T7 were read 20 times each, about 500 ms apart ([`turbidity-samples.txt`](assets/2026-11-05-analog-inputs/turbidity-samples.txt): T4 at 14:29, T5 at 14:33–14:34, T6 at 14:35, T7 at 14:37). Each held within one count, and the reading falls as turbidity rises.

## Raw Output

- [`raw-session.txt`](assets/2026-11-05-analog-inputs/raw-session.txt): every DCMT command of the session and its output, with the controller's address replaced by `<controller>`.
- [`motor-inputs.txt`](assets/2026-11-05-analog-inputs/motor-inputs.txt): step A5 in full.

## Not Checked

- The gen2 Nano Every builds (`gen2_nanoevery_analog1`, and `analogReference(VDD)` on the bench).
- Port 2 as analog.
- Encoder counts per output turn with the reader running, on a build with an encoder on the other port (#106's gate).
- How often each value refreshes in milliseconds (16 reads per pin, one read per `loop()` pass).
- Turbidity readings converted to NTU; the host does that, and it is out of scope here.

## Notes

- **Gen1 silkscreen.** The first 3.3 V went to terminal B of the header silked `ENC 1`. On gen1 that is the lower header, J2: firmware port 2, pin A1. This build doesn't read it, so nothing changed. Moved to the upper header (silk `ENC 2`, port 1), it read at once. `docs/hardware-revisions.md` records the mismatch; wire by position, not by the silk.
- **Floating pins** read whatever charge the previous conversion left, so an unconnected pin follows its neighbour (A3, A4). Tie an unused analog pin to GND, or ignore its value.

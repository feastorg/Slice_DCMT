---
title: "Architecture Overview"
description: "High-level architecture: electrical, firmware, and mechanical design notes."
---

Overview of the electrical, firmware, and mechanical design of this Slice.
Generation-specific detail (pin maps, reworks, connector order) is in
[Hardware Revisions](hardware-revisions.md); firmware behaviour (modes,
capabilities, refusals, watchdog, e-stop, faults) is in the
[firmware README](https://github.com/feastorg/Slice_DCMT/blob/main/firmware/README.md).

## 🧠 Purpose

This Slice drives two brushed DC motors, each through an LMD18200 H-bridge,
under commands from the BREAD controller over CRUMBS I2C. Both motors share one
control mode: open loop (signed PWM per motor) or, with quadrature encoders
fitted, closed-loop position or speed. A hardware e-stop, a command watchdog
and latched stall faults each brake both motors.

## 🔌 Interfaces

| Interface | Connector | Signals | Notes |
| --- | --- | --- | --- |
| Backplane | J1, 10-pin | 1 GND, 2 +12V, 3 I2C clock, 4 I2C data, 5 GND, 6 E-STOP, 7 INT, 8 SYNC, 9 +12V, 10 GND | Same pin order on G1, G2 and G3. On G3 the ground pins are driver ground (`GNDPWR`), joined to logic ground at NT1. E-STOP reaches D2, INT reaches D3; the firmware uses neither INT nor SYNC. |
| Motor outputs | one 2-terminal output per motor | H-bridge outputs | The driver numbering on the board does not always match the firmware's motor numbering, and G2 marks motor polarity opposite to G1; see Hardware Revisions. |
| Motor supply | per motor, a supply selection and supply terminals | backplane +12V, or a separate supply | G2 and G3: a switch per motor (SW1/SW2, S1/S2) chooses between them. G1: a 3-pin header per motor (J4/J5); see Hardware Revisions before fitting a shunt. G2's `+`/`-` silkscreen on these terminals is reversed. |
| Encoder headers | two 4-terminal headers, one per motor | A, B, +5V, GND | Order and MCU pins differ by generation; see Hardware Revisions. Unprotected MCU pins. |
| 5 V I2C | J18, G2 only: JST-SH, Qwiic form factor | GND, +5V, I2C data, I2C clock | The backplane I2C bus, brought out at 5 V. Qwiic parts are usually 3.3 V. G1 and G3 have none. |
| Status LED | WS2812B on D5, G2 and G3 | — | G1 has no LED. |

## 🪛 MCU Pin Mapping

The MCU is an Arduino Nano (ATmega328P) or Nano Every (ATmega4809) module;
both are used on G1 and G2.

| MCU pin | Function |
| --- | --- |
| D2 | E-stop input (internal pull-up) |
| D5 | Status LED (G2, G3); motor DIRECTION on G1 |
| D6, D10 | Motor 1 and motor 2 PWM (every generation, after the G2 reworks) |
| D7–D9, D11–D13 | Direction, brake and thermal flag; assignment differs by generation |
| A0–A3 | Encoder channels: motor 1 on A2/A3, motor 2 on A0/A1 |
| A4, A5 | I2C data, clock |
| A6, A7 | Driver current sense: passed to the driver library, never read by the firmware (#67) |

The full per-generation table is in Hardware Revisions, and the firmware's map
is `firmware/include/config_hardware.h`.

## 🔋 Power Domains

- Logic power (5 V): on G1, the Nano's own regulator, fed +12V on VIN. On G2
  and G3, an AP63205 buck converter from +12V; the Nano's VIN is unconnected.
  The encoder headers, the status LED and the Qwiic connector take this rail.
- High-current domain: each LMD18200 is fed from its motor's selector, either
  backplane +12V or that motor's separate supply input. G1 and G2 share one
  ground between logic and drivers. G3 separates driver ground (`GNDPWR`) and
  joins it to logic ground at one net tie (NT1).

## 🧱 Firmware

PlatformIO, Arduino framework. One build per board generation and MCU:
`gen1_nano`, `gen2_nano`, `gen1_nanoevery`, `gen2_nanoevery`. Closed-loop speed
is built by default on the Nano Every only (`DCMT_ENABLE_SPEED_LOOP`).

- `setup()` enables the e-stop input, starts CRUMBS on the I2C address and
  registers the command handlers, starts both drivers at zero output, then
  samples the e-stop for one second before enabling the 1 s AVR watchdog.
- `loop()` runs, in order: AVR watchdog reset, e-stop, status LED, command
  watchdog, motor control, stall-fault check, one serial command line, debug
  output.
- CRUMBS handlers run in the I2C interrupt and write the shared command state.
  The control loop copies that state in short interrupt-masked windows and acts
  on the copy, so a handler never drives a motor directly.
- Wire format: `bread-crumbs-contracts` (`dcmt_ops.h`). Libraries: CRUMBS,
  LMD18200, DCMotorServo (position servo and speed loop, with PID_v1), Encoder,
  and bread-slice-core (e-stop, command watchdog, status LED).

## 🧩 Mechanical

- Mounting pattern: `slice-standard-v0` (`slice.yaml`): four holes, 5 mm in
  from each corner, at the same positions on G1, G2 and G3. G1's are drilled
  3.2 mm; G2's and G3's are drilled 5 mm.
- Dimensions: 70 mm × 100 mm board outline, the same on G1, G2 and G3.
- Notes: the backplane connector J1 is on one long edge; the motor, supply and
  encoder terminals are on the opposite edge.

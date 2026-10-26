---
title: Bench Records
permalink: /bench/
has_children: true
---

# Bench Records

Results of testing this Slice's firmware on real hardware, one file per bench
session. Host tests (`firmware/test/host`) cover the firmware's logic; these
records cover what it does on a board.

## Writing a Record

Copy the [template](https://github.com/feastorg/bread-infra/blob/main/templates/bench-record.md)
to `docs/bench/YYYY-MM-DD-<short-slug>.md`. A record should let someone who
wasn't there tell what was tested and how far to trust it:

- the date, who ran it, the firmware commit and env, and the board and MCU;
- the rig: the controller, the link (serial or I2C), and what is on the outputs;
- each step with what you expected, what you saw, and pass, fail or skipped;
- the raw output, pasted or attached;
- what wasn't checked.

Add the record's row below in the same PR, and link it from the issue or PR it
verifies. The full guide is
[Bench Records](https://github.com/feastorg/bread-infra/blob/main/spec/slice/bench-records.md)
in bread-infra.

## Records

| Date | Record | Firmware | Boards | Result |
| --- | --- | --- | --- | --- |
| 2026-10-20 | [GET opcodes, type check and status LED](2026-10-20-get-opcodes-and-status-led.md) (backfill) | `cbb8313`, `8a0604b` | gen1 Nano ×2, gen2 Nano, gen2 Nano Every | pass; D5 timing not scoped |
| 2026-10-22 | [Applied PWM, watchdog, e-stop and library fixes](2026-10-22-applied-pwm-and-library-fixes.md) | `4254ea5`; `4254ea5` + DCMotorServo `cc8bad6`, lmd18200 `b11192a` | gen2 Nano, gen2 Nano Every | pass; serial blocks not run; current-sense zero offset |
| 2026-10-24 | [bread-slice-core, lead cap, serial and gen1](2026-10-24-core-and-serial.md) | `720879b` | gen2 Nano Every, gen1 Nano | pass; gen2 Nano not run |
| 2026-10-25 | [Setpoint loops, the e-stop release and the watchdog lease](2026-10-25-setpoint-loop-lease.md) | `718a41f` | gen2 Nano Every | pass |

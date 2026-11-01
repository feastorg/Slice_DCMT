---
title: "2026-10-31: opcode refusal and reply integrity"
parent: Bench Records
---

# 2026-10-31: opcode refusal and reply integrity

**Status: complete.** U1 passed. B1: DCMT's GET_STATE replies came through
intact, except one cut short on the gen2 Nano. That was the control for
feastorg/Slice_RLHT#81. Nothing moved: no motor step was run.

**Verifies:** on `main` at `c3e6fca`,
[#87](https://github.com/feastorg/Slice_DCMT/pull/87) (refuse unhandled
opcodes from the handler table), with CRUMBS 0.17.0 and bread-crumbs-contracts
0.11.1.
**By:** @CameronBrooks11

## Setup

| | |
| --- | --- |
| Firmware | `c3e6fca`, env `gen2_nanoevery`; then env `gen2_nano` for the second B1 run. CRUMBS 0.17.0, bread-crumbs-contracts 0.11.1, DCMotorServo 1.2.0, LMD18200 0.1.5, bread-slice-core 0.1.0 |
| Board | gen2 Nano Every (ATmega4809) with the D10/D11 motor-2 rework; gen2 Nano (ATmega328P) for the second B1 run |
| Controller | Raspberry Pi 4 Model B, I2C at 50 kHz, address 21, CRUMBS Python binding 0.16.2 and contracts 0.11.0, via the 2026-10-25 record's [`dcmt_bench.py`](assets/2026-10-25-setpoint-loop-lease/dcmt_bench.py) and Slice_RLHT's [`replylog.py`](https://github.com/feastorg/Slice_RLHT/blob/main/docs/bench/assets/2026-10-31-refusal-latch-bus/replylog.py) |
| Power | backplane (I2C and 12 V), with Slice_RLHT on the bus at address 19 |
| Outputs | gen2 Nano Every: as on 2026-10-25, not driven. gen2 Nano: motors unplugged (#66) |

## Steps and Results

| Step | Expected | Observed | Result |
| --- | --- | --- | --- |
| B0 baseline: `version`, `caps`, `state`, `wdog` | CRUMBS 1700, module 2.0.0; caps `0103ff070000`; mode 0, PWM 0 | as expected | pass |
| U1 query opcodes as commands (#87): `raw 0x80`, `raw 0x7F`, `raw 0x7D`, `raw 0x40`; control `raw 0x03 00` | each refused, status 2 reason 7 (UNKNOWN_OP); SET_MODE 0 applies | GET_STATE, GET_CAPS, GET_WATCHDOG and the unassigned 0x40: status 2 reason 7. SET_MODE 0: status 1. A first control, `raw 0x01 00`, was SET_OPEN_LOOP with 1 byte of its 4, and was refused reason 1 (MALFORMED); nothing drove | pass |
| B1 GET_STATE every 100 ms for 300 s with the binding's query retries off | every reply decodes | gen2 Nano Every: 0 of 2554 failed. gen2 Nano: 1 of 2555, `028014000000000000c0ff...ff`: cut short after a corrupt byte, as Slice_RLHT's are. Slice_RLHT's gen2 Nano, on the same bus: 90 and 48 of 2553 | pass |

## Raw Output

[`raw-session.txt`](assets/2026-10-31-refusal-and-bus/raw-session.txt).

## Not Checked

- U1 on the gen2 Nano and the gen1 boards.
- Any motor step. #87 changes only which frames are refused.

## Notes

- The one cut-short reply on the gen2 Nano has the same shape as Slice_RLHT's
  (feastorg/Slice_RLHT#81), at about a fiftieth of their rate, on the same
  MCU, bus and controller.

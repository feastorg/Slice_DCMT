---
title: "2026-10-20: GET opcodes, type check and status LED"
parent: Bench Records
---

# 2026-10-20: GET opcodes, type check and status LED

**Backfill.** This record was written on 2026-10-21, from results first
posted as issue and PR comments on 2026-10-20. It summarises those comments,
which hold the original wording; it adds no new results.

**Verifies:** [#45](https://github.com/feastorg/Slice_DCMT/issues/45) (fixed by
[#50](https://github.com/feastorg/Slice_DCMT/pull/50)), and the in-house status LED
driver in the draft [#55](https://github.com/feastorg/Slice_DCMT/pull/55), including
the boot-blue fix [#56](https://github.com/feastorg/Slice_DCMT/pull/56).

## Session 1: GET opcodes and type check

Source: [#45 comment](https://github.com/feastorg/Slice_DCMT/issues/45#issuecomment-6302818956).

| | |
| --- | --- |
| Firmware | `cbb8313` (the #50 fix plus a PlatformIO platform pin), CRUMBS 0.16.2 in the version reply |
| Boards | two gen1 Nano DCMTs |
| Controller | CRUMBS Python binding, `send_confirmed`, over I2C |
| Outputs | not recorded |

| Frame | Expected | Observed | Result |
| --- | --- | --- | --- |
| GET_STATE 0x80, GET_WATCHDOG 0x7D, GET_CAPS 0x7F as command frames | refused UNKNOWN_OP | REFUSED UNKNOWN_OP (0x07); on `d4a2dbc` the same frames read APPLIED | pass |
| SET_MODE 0x00 typed 0x01 (RLHT) | rejected, TYPE | REJECTED, reason TYPE | pass |
| SET_WATCHDOG 500 typed 0x01 | rejected, TYPE; watchdog unchanged | REJECTED, reason TYPE; GET_WATCHDOG stayed disarmed, timeout 0 | pass |
| the suite's other DCMT cases | as on `d4a2dbc` | as on `d4a2dbc`: refusal reasons, trip-guarded SETs, partial SETs; queries through SET_REPLY answered as before | pass |

## Session 2: status LED driver

Sources: [gen2 Nano](https://github.com/feastorg/Slice_DCMT/pull/55#issuecomment-6303222428),
[gen2 Nano Every](https://github.com/feastorg/Slice_DCMT/pull/55#issuecomment-6303359134).

| | |
| --- | --- |
| Firmware | `8a0604b`: the LED driver branch with `main` merged, including the boot-blue fix |
| Boards | gen2 Nano (ATmega328P); gen2 Nano Every (ATmega4809, `fuse2 = 0x01`, 16 MHz), flashed with `--upload-port /dev/ttyACM0` |
| Controller | none; the LED was watched |
| Outputs | not recorded |

| Step | Expected | Observed, both boards | Result |
| --- | --- | --- | --- |
| power-up and reset | blue through the boot second, then green | blue for the boot second, then green | pass |
| short the e-stop line, then release | red while shorted, green after | red while shorted, green after release | pass |

## Not Checked

- D5 was not scoped, so the WS2812 timing margins are unmeasured.
- The raw controller output from session 1 was not kept with the comment.

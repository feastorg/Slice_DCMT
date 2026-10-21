---
title: "Testing Instructions"
description: "Hardware and firmware testing checklist and procedures."
---

Testing procedures for this Slice. Covers manual validation and optional automated checks.

## ✅ Hardware Checklist

- [ ] Visual inspection (solder joints, polarity, orientation)
- [ ] No shorts across power rails
- [ ] All headers and connectors populated

## ⚡ Power-On Verification

1. Apply logic power (e.g., 5V).
2. Confirm LED heartbeat (G2/G3 only — G1 has no LED, see [Hardware Revisions](hardware-revisions.md)).
3. Measure voltage rails at test points.

## 📟 Functional Tests

| Test        | Method                            | Expected Result                     |
| ----------- | --------------------------------- | ----------------------------------- |
| UART output | Connect USB-Serial, open terminal | Data output at expected baud rate   |
| Input test  | Drive digital/PWM input           | MCU registers or reacts accordingly |
| Output test | Scope or multimeter at output pin | Output toggles or changes as coded  |

## 🔁 Firmware Regression

Host tests build the firmware's sources against the real CRUMBS core and run
them on the host. CI runs them on every firmware change. See
[`firmware/test/host/README.md`](../firmware/test/host/README.md).

```bash
cd firmware
pio pkg install -e gen2_nano
make -C test/host
```

## 📷 Test Setup

<!-- insert annotated image of bench setup under docs/assets/ and insert here -->

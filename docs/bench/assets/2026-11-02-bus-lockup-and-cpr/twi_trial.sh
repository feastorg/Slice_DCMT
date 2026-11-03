#!/bin/sh
# #101 trigger, N trials on M1: home to 0, full reverse open loop for about
# 1 s (polled, so an armed lease stays fed), then speed mode at +120 RPM with
# the brake released, a full-current reversal while polling. 8 s logged,
# then brake. Prints each trial's poll summary, the watchdog, and the
# STARVE record count. Brakes both motors on exit, whatever happened.
# Run from ~/dcmt-bench: sh twi_trial.sh 5
N="${1:-1}"
B="venv/bin/python -u dcmt_bench.py"
trap '$B brake 1 1 >/dev/null 2>&1; $B wd 0 >/dev/null 2>&1' EXIT
i=1
while [ "$i" -le "$N" ]; do
  echo "=== trial $i"
  $B home 0 0 || { echo "home failed, stopping"; exit 1; }
  $B wdl 1000 0 >/dev/null
  $B mode 0 >/dev/null && $B open -255 0 >/dev/null && $B brake 0 1 >/dev/null
  $B watch 1 | tail -1
  $B mode 2 && $B sp5 2 120 0
  $B speedlog 1 8 120 | grep -E "poll failed|summary|peak" | sed 's/^[0-9:.]* //' | sort | uniq -c | sed 's/^ *//' | head -5
  $B brake 1 1 >/dev/null
  $B wd 0 >/dev/null
  $B wdog
  $B ser "STARVE?" | tail -1
  i=$((i + 1))
done

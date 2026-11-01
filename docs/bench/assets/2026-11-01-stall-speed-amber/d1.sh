#!/bin/sh
# D1: position mode on motor $1 (1 or 2). Hold the current position for 3 s,
# step that motor's target by 12 counts, then 10 s at rest. Brakes both
# motors on exit, whatever happened.
# Run from ~/dcmt-bench: sh d1.sh 1
M="$1"
B="venv/bin/python -u dcmt_bench.py"
trap '$B brake 1 1' EXIT
$B brake 1 1 || exit 1
$B mode 1 || exit 1
POS=$($B state | sed -E 's/.* pos=(-?[0-9]+),(-?[0-9]+) .*/\1 \2/')
P1=${POS% *}
P2=${POS#* }
echo "start pos $P1 $P2"
$B sp "$P1" "$P2" || exit 1
$B brake 0 0 || exit 1
$B watch 3
if [ "$M" = 1 ]; then
  $B sp $((P1 + 12)) "$P2" || exit 1
else
  $B sp "$P1" $((P2 + 12)) || exit 1
fi
$B watch 10
$B faults
$B state

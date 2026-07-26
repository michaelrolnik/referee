#!/usr/bin/env bash
#
# Stream thermostat states to stdout, one every DELAY seconds, so `referee
# monitor` prints its verdicts live rather than all at once:
#
#   ./examples/monitor/feed.sh | ./build/referee monitor examples/monitor/thermostat.ref
#
# Pace it with DELAY (default 1 second):
#
#   DELAY=0.3 ./examples/monitor/feed.sh | ./build/referee monitor examples/monitor/thermostat.ref
#
# The scenario warms up to comfort -- so `reaches_comfort` settles PASS -- then
# injects two faults: the heater is left on past 90 degrees (breaks
# `heater_off_hot`) and the temperature runs away to 105 (breaks
# `never_overheat`). Each violation prints the instant its state arrives.

set -euo pipefail

# temp,heater
rows=(
  "20,true"     # cold, heating
  "38,true"
  "55,true"
  "68,false"    # comfort reached -> reaches_comfort settles PASS, heater off
  "82,false"
  "90,true"     # FAULT: heater energised while hot -> heater_off_hot violation
  "105,true"    # FAULT: overheat                   -> never_overheat violation
  "60,false"    # recovered (the safety verdicts stay FAIL -- they are settled)
)

echo "__time__,temp,heater"
t=0
for r in "${rows[@]}"; do
  echo "${t},${r}"
  t=$(( t + 1000 ))
  sleep "${DELAY:-1}"
done

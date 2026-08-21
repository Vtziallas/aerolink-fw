#!/bin/bash
LOG=/tmp/px4_sitl_aircraft_net.log
START_LINE=$(wc -l < "$LOG")
for i in $(seq 1 30); do
  if tail -n +"$START_LINE" "$LOG" | grep -q 'Takeoff detected'; then
    echo "takeoff confirmed"
    exit 0
  fi
  sleep 1
done
echo "timed out waiting for takeoff"
exit 1

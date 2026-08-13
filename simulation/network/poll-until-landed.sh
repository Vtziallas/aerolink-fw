#!/bin/bash
# Polls PX4's own log every 10s (up to 4 minutes) until a NEW mission-complete
# or forced-land marker appears -- tracks the line count at start so it
# doesn't false-positive on old completion messages already in the log
# from earlier tests. No sudo needed -- just reads a log file.
LOG=/tmp/px4_sitl_aircraft_net.log
START_LINE=$(wc -l < "$LOG")
for i in $(seq 1 24); do
  if tail -n +"$START_LINE" "$LOG" | grep -q 'Mission finished, landed\|Landing at current position'; then
    elapsed=$((i * 10))
    echo "-- completed after ~${elapsed}s"
    break
  fi
  sleep 10
done
tail -n +"$START_LINE" "$LOG"

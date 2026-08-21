#!/bin/bash
# Test-only: launches PX4 SITL alone inside aircraft-net, with no other
# process (Mission Agent or otherwise) tied to its lifetime in any way.
# Run in its own terminal window, same pattern as every other single-process
# script in this directory (launch-ground.sh etc) -- do not try to combine
# this with another process launch in the same script/session. That was
# tried (a decoupled-but-combined script backgrounding PX4 then exec-ing
# into the agent) and PX4 still died once the script's process image
# changed via exec, even with no explicit wait/trap linking them. The only
# reliably-proven pattern in this project is one foreground process per
# script per terminal.
#
# Use this + launch-agent-only.sh (in a second terminal) instead of
# launch-aircraft.sh when a test specifically needs to kill the Mission
# Agent without affecting PX4 -- e.g. a companion-computer-crash fault test.
#
# Must be run with sudo (ip netns exec requires root).

set -euo pipefail

REAL_USER="${SUDO_USER:-youruser}"
PX4_DIR="/home/$REAL_USER/src/PX4-Autopilot/build/px4_sitl_default/src/modules/simulation/simulator_sih"
PX4_BIN="/home/$REAL_USER/src/PX4-Autopilot/build/px4_sitl_default/bin/px4"
LOG_FILE="/tmp/px4_sitl_aircraft_net.log"

exec ip netns exec aircraft-net sudo -u "$REAL_USER" env \
  PX4_SIM_MODEL=sihsim_standard_vtol PX4_SIMULATOR=sihsim \
  bash -c "cd '$PX4_DIR' && exec '$PX4_BIN' -d" > "$LOG_FILE" 2>&1 < /dev/null

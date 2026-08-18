#!/bin/bash
# Launches PX4 SITL and the Mission Agent together inside the aircraft-net
# namespace (see setup-netns-wireguard.sh). The Mission Agent connects to
# PX4's local onboard MAVLink link (udp://:14540) and is what the backend
# now talks to across the WireGuard tunnel -- see
# docs/architecture/mission-agent-design.md.
#
# Must be run with sudo (ip netns exec requires root). Runs in the
# foreground -- invoke via the harness's `!` prefix so it's automatically
# tracked as a long-running background task, same as every other SITL
# launch in this project.

set -euo pipefail

REAL_USER="${SUDO_USER:-youruser}"
PX4_DIR="/home/$REAL_USER/src/PX4-Autopilot/build/px4_sitl_default/src/modules/simulation/simulator_sih"
PX4_BIN="/home/$REAL_USER/src/PX4-Autopilot/build/px4_sitl_default/bin/px4"
AGENT_BIN="/mnt/c/Users/youruser/git/uav_project/onboard/mission-agent/build/mission_agent"
PX4_LOG_FILE="/tmp/px4_sitl_aircraft_net.log"
AGENT_LOG_FILE="/tmp/mission_agent_aircraft_net.log"

ip netns exec aircraft-net sudo -u "$REAL_USER" env \
  PX4_SIM_MODEL=sihsim_standard_vtol PX4_SIMULATOR=sihsim \
  bash -c "cd '$PX4_DIR' && exec '$PX4_BIN' -d" > "$PX4_LOG_FILE" 2>&1 < /dev/null &
PX4_PID=$!

# Give PX4 a moment to open its onboard MAVLink listener before the agent
# tries to connect.
sleep 3

ip netns exec aircraft-net sudo -u "$REAL_USER" env \
  MAVLINK_URL="udp://:14540" AGENT_PORT="5760" \
  "$AGENT_BIN" > "$AGENT_LOG_FILE" 2>&1 < /dev/null &
AGENT_PID=$!

wait "$PX4_PID" "$AGENT_PID"

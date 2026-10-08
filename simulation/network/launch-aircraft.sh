#!/bin/bash
# Launches PX4 SITL and the Mission Agent together inside the aircraft-net
# namespace (see setup-netns-wireguard.sh). The Mission Agent connects to
# PX4's local onboard MAVLink link (udp://:14540) and is what the backend
# now talks to across the WireGuard tunnel -- see
# docs/architecture/mission-agent-design.md.
#
# PX4_SIM_MODEL=sihsim_standard_vtol (QuadPlane -- 4 lift motors + 1 pusher,
# see docs/adr/0002-quadplane-vtol-airframe.md), not sihsim_airplane as used
# through Phase 1-6. PX4 only picks up a new SYS_AUTOSTART/airframe from
# PX4_SIM_MODEL on a genuinely fresh boot -- once it's saved one to
# rootfs/parameters.bson it reuses that on every future launch regardless of
# this env var. If this script's PX4_SIM_MODEL is ever changed again, the
# rootfs/parameters.bson, parameters_backup.bson, dataman, and eeprom/ files
# must be cleared (backed up, not just deleted) first, the same way the
# 2026-08-12 fixed-wing -> VTOL switch was done (see SIMULATION.md).
#
# Must be run with sudo (ip netns exec requires root). Runs in the
# foreground -- invoke via the harness's `!` prefix so it's automatically
# tracked as a long-running background task, same as every other SITL
# launch in this project. Do not add your own nohup/disown/setsid wrapping;
# that was found unreliable in earlier testing (see SIMULATION.md).

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

REAL_USER="${SUDO_USER:-$(logname 2>/dev/null || whoami)}"
PX4_DIR="/home/$REAL_USER/src/PX4-Autopilot/build/px4_sitl_default/src/modules/simulation/simulator_sih"
PX4_BIN="/home/$REAL_USER/src/PX4-Autopilot/build/px4_sitl_default/bin/px4"
AGENT_BIN="$PROJECT_ROOT/onboard/mission-agent/build/mission_agent"
PX4_LOG_FILE="/tmp/px4_sitl_aircraft_net.log"
AGENT_LOG_FILE="/tmp/mission_agent_aircraft_net.log"

# The Mission Agent re-validates every command against this geofence
# independently of the backend (see docs/architecture/mission-agent-design.md's
# "validation stays double-layered"). The two sides are only in sync if they
# get the same values, and they are launched by two different scripts -- so
# any override here must be made in launch-ground.sh's env block too, and
# vice versa. Same defaults as ground-station/backend/app/config.py.
GEOFENCE_CENTER_LAT_DEG="${GEOFENCE_CENTER_LAT_DEG:-47.397742}"
GEOFENCE_CENTER_LON_DEG="${GEOFENCE_CENTER_LON_DEG:-8.545593}"
GEOFENCE_RADIUS_M="${GEOFENCE_RADIUS_M:-2000.0}"
MIN_ALTITUDE_M="${MIN_ALTITUDE_M:-10.0}"
MAX_ALTITUDE_M="${MAX_ALTITUDE_M:-120.0}"

PX4_PID=""
AGENT_PID=""
# If either process dies (or this script is interrupted), don't leave the
# other orphaned inside the namespace -- an orphaned PX4 keeps port 14540
# bound and an orphaned agent keeps port 5760 bound, both of which make the
# next launch fail confusingly.
trap 'kill $PX4_PID $AGENT_PID 2>/dev/null' EXIT

ip netns exec aircraft-net sudo -u "$REAL_USER" env \
  PX4_SIM_MODEL=sihsim_standard_vtol PX4_SIMULATOR=sihsim \
  bash -c "cd '$PX4_DIR' && exec '$PX4_BIN' -d" > "$PX4_LOG_FILE" 2>&1 < /dev/null &
PX4_PID=$!

# Give PX4 time to open its onboard MAVLink listener before the agent tries
# to connect. The agent's own connect timeout is 10s on top of this, and it
# now exits non-zero with a readable message rather than aborting if PX4
# still isn't there -- check "$AGENT_LOG_FILE" if it gives up.
sleep 10

ip netns exec aircraft-net sudo -u "$REAL_USER" env \
  MAVLINK_URL="udp://:14540" AGENT_PORT="5760" \
  GEOFENCE_CENTER_LAT_DEG="$GEOFENCE_CENTER_LAT_DEG" \
  GEOFENCE_CENTER_LON_DEG="$GEOFENCE_CENTER_LON_DEG" \
  GEOFENCE_RADIUS_M="$GEOFENCE_RADIUS_M" \
  MIN_ALTITUDE_M="$MIN_ALTITUDE_M" \
  MAX_ALTITUDE_M="$MAX_ALTITUDE_M" \
  "$AGENT_BIN" > "$AGENT_LOG_FILE" 2>&1 < /dev/null &
AGENT_PID=$!

# wait -n (not `wait "$PX4_PID" "$AGENT_PID"`) returns as soon as EITHER
# process exits, so a single crashed child triggers script exit here --
# either directly (a non-zero exit under `set -e`) or via the explicit
# kill/exit below (a clean exit) -- and the EXIT trap above then cleans up
# the survivor instead of leaving it orphaned in the namespace with its
# port still bound. The `|| true` guards mean a process that's already
# gone by the time we try to kill/wait it doesn't itself trip `set -e`.
wait -n || true
kill "$PX4_PID" "$AGENT_PID" 2>/dev/null || true
wait "$PX4_PID" "$AGENT_PID" 2>/dev/null || true
exit 0

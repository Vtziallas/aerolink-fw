#!/bin/bash
# Test-only: launches the Mission Agent alone inside aircraft-net, connecting
# to PX4's onboard link. Run in its own terminal window, AFTER
# launch-px4-only.sh is up and showing "Ready for takeoff!" in a separate
# terminal. See launch-px4-only.sh's header comment for why these are two
# scripts instead of one.
#
# Must be run with sudo (ip netns exec requires root).

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

REAL_USER="${SUDO_USER:-$(logname 2>/dev/null || whoami)}"
AGENT_BIN="$PROJECT_ROOT/onboard/mission-agent/build/mission_agent"
LOG_FILE="/tmp/mission_agent_aircraft_net.log"

GEOFENCE_CENTER_LAT_DEG="${GEOFENCE_CENTER_LAT_DEG:-47.397742}"
GEOFENCE_CENTER_LON_DEG="${GEOFENCE_CENTER_LON_DEG:-8.545593}"
GEOFENCE_RADIUS_M="${GEOFENCE_RADIUS_M:-2000.0}"
MIN_ALTITUDE_M="${MIN_ALTITUDE_M:-10.0}"
MAX_ALTITUDE_M="${MAX_ALTITUDE_M:-120.0}"

exec ip netns exec aircraft-net sudo -u "$REAL_USER" env \
  MAVLINK_URL="udp://:14540" AGENT_PORT="5760" \
  GEOFENCE_CENTER_LAT_DEG="$GEOFENCE_CENTER_LAT_DEG" \
  GEOFENCE_CENTER_LON_DEG="$GEOFENCE_CENTER_LON_DEG" \
  GEOFENCE_RADIUS_M="$GEOFENCE_RADIUS_M" \
  MIN_ALTITUDE_M="$MIN_ALTITUDE_M" \
  MAX_ALTITUDE_M="$MAX_ALTITUDE_M" \
  "$AGENT_BIN" > "$LOG_FILE" 2>&1 < /dev/null

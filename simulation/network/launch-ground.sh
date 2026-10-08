#!/bin/bash
# Launches the ground-station backend inside the ground-net namespace,
# connecting to the Mission Agent through the WireGuard tunnel (the agent
# runs inside aircraft-net alongside PX4 -- see launch-aircraft.sh and
# docs/architecture/mission-agent-design.md). The backend no longer speaks
# MAVLink at all; MISSION_AGENT_HOST/PORT point at the agent's TCP protocol.
#
# Must be run with sudo (ip netns exec requires root). Runs in the
# foreground -- invoke via the harness's `!` prefix, same as
# launch-aircraft.sh.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

REAL_USER="${SUDO_USER:-$(logname 2>/dev/null || whoami)}"
BACKEND_DIR="$PROJECT_ROOT/ground-station/backend"
LOG_FILE="/tmp/backend_ground_net.log"

# The backend and the Mission Agent validate missions against this geofence
# independently (see docs/architecture/mission-agent-design.md's "validation
# stays double-layered"), so the two only agree if they get the same values --
# and they are launched by two different scripts. Any override here must be
# made in launch-aircraft.sh's matching env block too, and vice versa.
# Same defaults as ground-station/backend/app/config.py.
GEOFENCE_CENTER_LAT_DEG="${GEOFENCE_CENTER_LAT_DEG:-47.397742}"
GEOFENCE_CENTER_LON_DEG="${GEOFENCE_CENTER_LON_DEG:-8.545593}"
GEOFENCE_RADIUS_M="${GEOFENCE_RADIUS_M:-2000.0}"
MIN_ALTITUDE_M="${MIN_ALTITUDE_M:-10.0}"
MAX_ALTITUDE_M="${MAX_ALTITUDE_M:-120.0}"

exec ip netns exec ground-net sudo -u "$REAL_USER" env \
  MISSION_AGENT_HOST="10.99.0.2" MISSION_AGENT_PORT="5760" \
  GEOFENCE_CENTER_LAT_DEG="$GEOFENCE_CENTER_LAT_DEG" \
  GEOFENCE_CENTER_LON_DEG="$GEOFENCE_CENTER_LON_DEG" \
  GEOFENCE_RADIUS_M="$GEOFENCE_RADIUS_M" \
  MIN_ALTITUDE_M="$MIN_ALTITUDE_M" \
  MAX_ALTITUDE_M="$MAX_ALTITUDE_M" \
  bash -c "cd '$BACKEND_DIR' && exec /home/$REAL_USER/venvs/aerolink-backend/bin/python -m uvicorn app.main:app --host 0.0.0.0 --port 8000" \
  > "$LOG_FILE" 2>&1 < /dev/null

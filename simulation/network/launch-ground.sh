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

REAL_USER="${SUDO_USER:-youruser}"
BACKEND_DIR="/mnt/c/Users/youruser/git/uav_project/ground-station/backend"
LOG_FILE="/tmp/backend_ground_net.log"

exec ip netns exec ground-net sudo -u "$REAL_USER" env \
  MISSION_AGENT_HOST="10.99.0.2" MISSION_AGENT_PORT="5760" \
  bash -c "cd '$BACKEND_DIR' && exec /home/$REAL_USER/venvs/aerolink-backend/bin/python -m uvicorn app.main:app --host 0.0.0.0 --port 8000" \
  > "$LOG_FILE" 2>&1 < /dev/null

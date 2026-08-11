#!/bin/bash
# Launches the ground-station backend inside the ground-net namespace,
# connecting to PX4 through the WireGuard tunnel (10.99.0.2:18570 -- PX4's
# GCS-style MAVLink instance, which listens and learns the peer address
# dynamically, unlike the "onboard" instance used in same-machine testing --
# see SIMULATION.md's Phase 4 findings for why that distinction matters).
#
# Must be run with sudo (ip netns exec requires root). Runs in the
# foreground -- invoke via the harness's `!` prefix, same as launch-aircraft.sh.

set -euo pipefail

REAL_USER="${SUDO_USER:-youruser}"
BACKEND_DIR="/mnt/c/Users/youruser/git/uav_project/ground-station/backend"
LOG_FILE="/tmp/backend_ground_net.log"

exec ip netns exec ground-net sudo -u "$REAL_USER" env \
  VEHICLE_SYSTEM_ADDRESS="udpout://10.99.0.2:18570" \
  bash -c "cd '$BACKEND_DIR' && exec /home/$REAL_USER/venvs/aerolink-backend/bin/python -m uvicorn app.main:app --host 0.0.0.0 --port 8000" \
  > "$LOG_FILE" 2>&1 < /dev/null

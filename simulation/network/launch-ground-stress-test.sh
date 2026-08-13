#!/bin/bash
# Variant of launch-ground.sh for mission-tuning stress testing: relaxes
# MAX_TURN_ANGLE_DEG and MIN_WAYPOINT_SEPARATION_M via the env-var overrides
# app/config.py already supports, so deliberately extreme missions can be
# uploaded through the REAL REST API (exercising the actual code path a
# real operator would use) instead of bypassing the backend with a separate
# MAVSDK connection. Use this instead of launch-ground.sh only while stress
# testing; switch back to the normal script afterward -- this is not the
# config real missions should fly under.
#
# Must be run with sudo (ip netns exec requires root). Runs in the
# foreground, same invocation pattern as launch-ground.sh.

set -euo pipefail

REAL_USER="${SUDO_USER:-youruser}"
BACKEND_DIR="/mnt/c/Users/youruser/git/uav_project/ground-station/backend"
LOG_FILE="/tmp/backend_ground_net.log"

exec ip netns exec ground-net sudo -u "$REAL_USER" env \
  VEHICLE_SYSTEM_ADDRESS="udpout://10.99.0.2:18570" \
  MAX_TURN_ANGLE_DEG="179" \
  MIN_WAYPOINT_SEPARATION_M="1" \
  bash -c "cd '$BACKEND_DIR' && exec /home/$REAL_USER/venvs/aerolink-backend/bin/python -m uvicorn app.main:app --host 0.0.0.0 --port 8000" \
  > "$LOG_FILE" 2>&1 < /dev/null

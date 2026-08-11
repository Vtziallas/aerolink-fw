#!/bin/bash
# Launches PX4 SITL inside the aircraft-net namespace (see
# setup-netns-wireguard.sh), running as the normal user (not root) so log
# files and build artifacts keep sane ownership.
#
# Must be run with sudo (ip netns exec requires root). Runs in the
# foreground -- invoke via the harness's `!` prefix so it's automatically
# tracked as a long-running background task, same as every other SITL
# launch in this project. Do not add your own nohup/disown/setsid wrapping;
# that was found unreliable in earlier testing (see SIMULATION.md).

set -euo pipefail

REAL_USER="${SUDO_USER:-youruser}"
PX4_DIR="/home/$REAL_USER/src/PX4-Autopilot/build/px4_sitl_default/src/modules/simulation/simulator_sih"
PX4_BIN="/home/$REAL_USER/src/PX4-Autopilot/build/px4_sitl_default/bin/px4"
LOG_FILE="/tmp/px4_sitl_aircraft_net.log"

exec ip netns exec aircraft-net sudo -u "$REAL_USER" env \
  PX4_SIM_MODEL=sihsim_airplane PX4_SIMULATOR=sihsim \
  bash -c "cd '$PX4_DIR' && exec '$PX4_BIN' -d" > "$LOG_FILE" 2>&1 < /dev/null

#!/bin/bash
# Reverts set-geofence-test.sh: clears the uploaded geofence and restores
# GF_ACTION to PX4's default (2, Hold mode). Must be run with sudo.

set -euo pipefail

REAL_USER="${SUDO_USER:-$(logname 2>/dev/null || whoami)}"

ip netns exec ground-net sudo -u "$REAL_USER" \
  /home/"$REAL_USER"/venvs/aerolink-backend/bin/python3 -c "
import asyncio
from mavsdk import System

async def run():
    drone = System()
    await drone.connect(system_address='udpout://10.99.0.2:18570')
    async for state in drone.core.connection_state():
        if state.is_connected:
            break

    await drone.geofence.clear_geofence()
    await drone.param.set_param_int('GF_ACTION', 2)
    print('Geofence cleared, GF_ACTION reset to 2 (Hold mode)')

asyncio.run(run())
"

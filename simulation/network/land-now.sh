#!/bin/bash
# One-shot: commands an immediate landing at the current position. Useful
# for recovering from a state (like a failsafe-triggered RTL loiter) that
# doesn't auto-land on its own -- see SAFETY.md's note on GEOFENCE_BREACH
# recovery. Must be run with sudo (ip netns exec requires root).

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
    await drone.action.land()
    print('Land command sent')

asyncio.run(run())
"

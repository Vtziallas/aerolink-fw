#!/bin/bash
# One-shot: prints every individual MAVSDK telemetry.health() flag plus
# position/home state, to find out *which* specific check is behind a
# generic "Arming denied: Resolve system health failures first" message
# (PX4's own log doesn't say which one at INFO/WARN verbosity). Read-only.
# Must be run with sudo (ip netns exec requires root).

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

    async for health in drone.telemetry.health():
        print('health:', health)
        break

    async for pos in drone.telemetry.position():
        print('position:', pos)
        break

    async for gps in drone.telemetry.gps_info():
        print('gps_info:', gps)
        break

    async for armed in drone.telemetry.armed():
        print('armed:', armed)
        break

    # status_text() only emits on new messages, not a periodic snapshot --
    # don't block forever if PX4 has nothing new to say.
    try:
        async def first_status():
            async for status in drone.telemetry.status_text():
                return status
        status = await asyncio.wait_for(first_status(), timeout=5)
        print('status_text:', status)
    except asyncio.TimeoutError:
        print('status_text: (no new message in 5s)')

asyncio.run(run())
"

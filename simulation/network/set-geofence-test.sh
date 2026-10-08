#!/bin/bash
# One-shot: uploads a real PX4 onboard geofence (inclusion circle, 200m
# radius around home) and sets GF_ACTION=3 (Return mode) so an in-flight
# breach is genuinely enforced by PX4 itself -- distinct from our backend's
# client-side geofence check, which only validates before upload and has
# nothing to do with what PX4 itself will do if the aircraft ends up outside
# the safe area in flight. See tests/simulation/geofence_breach.md.
#
# Must be run with sudo (ip netns exec requires root).

set -euo pipefail

REAL_USER="${SUDO_USER:-$(logname 2>/dev/null || whoami)}"

ip netns exec ground-net sudo -u "$REAL_USER" \
  /home/"$REAL_USER"/venvs/aerolink-backend/bin/python3 -c "
import asyncio
from mavsdk import System
from mavsdk.geofence import Circle, FenceType, GeofenceData, Point

async def run():
    drone = System()
    await drone.connect(system_address='udpout://10.99.0.2:18570')
    async for state in drone.core.connection_state():
        if state.is_connected:
            break

    await drone.param.set_param_int('GF_ACTION', 3)  # Return mode

    home_lat, home_lon = 47.397742, 8.545593
    circle = Circle(Point(home_lat, home_lon), 200.0, FenceType.INCLUSION)
    await drone.geofence.upload_geofence(GeofenceData([], [circle]))

    val = await drone.param.get_param_int('GF_ACTION')
    print(f'GF_ACTION = {val}')
    print('Geofence uploaded: 200m inclusion circle around home')

asyncio.run(run())
"

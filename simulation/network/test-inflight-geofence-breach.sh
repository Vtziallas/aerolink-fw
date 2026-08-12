#!/bin/bash
# In-flight geofence breach test, done as a single continuous script instead
# of separately-timed manual steps (which failed on 2026-08-11 -- our test
# flights complete faster than a human can run two sudo commands in
# sequence). This script uploads a mission, starts it, waits a calibrated
# delay while it's genuinely airborne, then uploads a tighter geofence and
# watches PX4's response -- all within one process, so timing is exact.
#
# Must be run with sudo (ip netns exec requires root).

set -euo pipefail

REAL_USER="${SUDO_USER:-youruser}"

ip netns exec ground-net sudo -u "$REAL_USER" \
  /home/"$REAL_USER"/venvs/aerolink-backend/bin/python3 -c "
import asyncio
from mavsdk import System
from mavsdk.mission import MissionItem, MissionPlan
from mavsdk.geofence import Circle, FenceType, GeofenceData, Point

HOME_LAT, HOME_LON = 47.397742, 8.545593

def item(lat, lon, alt, action=MissionItem.VehicleAction.NONE):
    return MissionItem(
        lat, lon, alt,
        15,      # speed_m_s -- matches this airframe's FW_AIRSPD_TRIM
        True,    # is_fly_through
        float('nan'), float('nan'),
        MissionItem.CameraAction.NONE,
        float('nan'), float('nan'),
        10,      # acceptance_radius_m -- matches PX4's real NAV_ACC_RAD default
        float('nan'), float('nan'),
        action)

async def run():
    drone = System()
    await drone.connect(system_address='udpout://10.99.0.2:18570')
    async for state in drone.core.connection_state():
        if state.is_connected:
            break
    async for health in drone.telemetry.health():
        if health.is_armable:
            break

    # Make sure no fence is active yet so this mission (deliberately outside
    # where the later 200m fence will sit) is accepted.
    await drone.geofence.clear_geofence()
    await drone.param.set_param_int('GF_ACTION', 3)  # Return mode

    # Same waypoint pattern used throughout Phase 3-6 testing (gentle ~36-56
    # degree turns, well under the 70-degree threshold found to destabilize
    # the aircraft -- see SIMULATION.md's Phase 3 findings). Do not change
    # this geometry without re-checking turn angles.
    mission_items = [
        item(HOME_LAT + 0.0020, HOME_LON, 30),
        item(HOME_LAT + 0.0020, HOME_LON + 0.0030, 30),
        item(HOME_LAT + 0.0005, HOME_LON + 0.0055, 30),
        item(HOME_LAT, HOME_LON, 0, MissionItem.VehicleAction.LAND),
    ]
    await drone.mission.upload_mission(MissionPlan(mission_items))

    print('-- Arming and starting mission')
    await drone.action.arm()
    await drone.mission.start_mission()

    print('-- Waiting 18s for the aircraft to be genuinely mid-route and outside 200m...')
    await asyncio.sleep(18)

    async for pos in drone.telemetry.position():
        print(f'-- Position before fence upload: lat={pos.latitude_deg:.6f} lon={pos.longitude_deg:.6f} alt={pos.relative_altitude_m:.1f}m')
        break

    print('-- Uploading tighter 200m geofence NOW, while airborne')
    circle = Circle(Point(HOME_LAT, HOME_LON), 200.0, FenceType.INCLUSION)
    await drone.geofence.upload_geofence(GeofenceData([], [circle]))
    print('-- Fence uploaded, watching response for 40s...')

    last_mode = None
    for _ in range(20):
        async for fm in drone.telemetry.flight_mode():
            mode = str(fm)
            break
        async for pos in drone.telemetry.position():
            p = pos
            break
        if mode != last_mode:
            print(f'-- mode={mode} lat={p.latitude_deg:.6f} lon={p.longitude_deg:.6f} alt={p.relative_altitude_m:.1f}m')
            last_mode = mode
        await asyncio.sleep(2)

asyncio.run(run())
"

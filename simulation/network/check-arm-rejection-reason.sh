#!/bin/bash
# telemetry.health()'s 7 booleans were all True (see check-health-flags.sh
# output) yet is_armable was False and arm() was denied -- that struct
# doesn't expose PX4's full prearm check set (battery, ESC, EKF variances,
# etc.). PX4 announces the *specific* failing check as a STATUSTEXT message
# at the moment an arm command is rejected, not as passive telemetry, so
# this subscribes to status_text() first, then actually attempts to arm and
# captures whatever PX4 says. Also prints battery state, since repeated
# rapid-cycle test flights are a plausible way to run the simulated battery
# down. Read-only in effect (arm attempt will be denied, not left armed).
# Must be run with sudo (ip netns exec requires root).

set -euo pipefail

REAL_USER="${SUDO_USER:-youruser}"

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

    async for battery in drone.telemetry.battery():
        print('battery:', battery)
        break

    messages = []
    async def collect():
        async for status in drone.telemetry.status_text():
            messages.append(status)

    collector = asyncio.create_task(collect())
    await asyncio.sleep(0.5)

    print('-- attempting arm() to trigger PX4 rejection reason --')
    try:
        await drone.action.arm()
        print('-- arm() unexpectedly succeeded')
    except Exception as e:
        print(f'-- arm() rejected: {e}')

    await asyncio.sleep(2)
    collector.cancel()

    print(f'-- {len(messages)} status_text message(s) captured:')
    for m in messages:
        print('  ', m)

asyncio.run(run())
"

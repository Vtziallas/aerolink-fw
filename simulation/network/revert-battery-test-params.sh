#!/bin/bash
# Reverts the overrides made by set-battery-test-params.sh back to PX4's
# real defaults. Must be run with sudo.

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

    await drone.param.set_param_float('SIM_BAT_MIN_PCT', 50.0)
    await drone.param.set_param_float('SIM_BAT_DRAIN', 60.0)
    await drone.param.set_param_int('COM_LOW_BAT_ACT', 0)

    for name in ['SIM_BAT_MIN_PCT', 'SIM_BAT_DRAIN', 'COM_LOW_BAT_ACT']:
        try:
            val = await drone.param.get_param_float(name)
        except Exception:
            val = await drone.param.get_param_int(name)
        print(f'{name} = {val}')

asyncio.run(run())
"

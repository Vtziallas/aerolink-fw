#!/bin/bash
# One-shot: connects through the WireGuard tunnel (from inside ground-net,
# same as the backend does) to override PX4's battery-simulation params for
# failsafe testing -- see tests/simulation/battery_failsafe.md.
#
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

    # Remove the 50%-floor default (see SIMULATION.md Phase 3 findings)
    await drone.param.set_param_float('SIM_BAT_MIN_PCT', 0.0)
    # Drain fast enough to cross the 7%/5% thresholds within one short test
    # flight (~30-40s): 1% per 0.3s instead of the default 1% per 60s.
    await drone.param.set_param_float('SIM_BAT_DRAIN', 0.3)
    # Enable the actual failsafe action (default is warning-only)
    await drone.param.set_param_int('COM_LOW_BAT_ACT', 3)

    for name in ['SIM_BAT_MIN_PCT', 'SIM_BAT_DRAIN', 'COM_LOW_BAT_ACT']:
        try:
            val = await drone.param.get_param_float(name)
        except Exception:
            val = await drone.param.get_param_int(name)
        print(f'{name} = {val}')

asyncio.run(run())
"

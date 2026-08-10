"""MAVSDK connection to a single vehicle, re-published as a plain state snapshot.

This is the only module in the backend that imports mavsdk. Everything else
(the FastAPI app, the WebSocket layer) talks to `VehicleConnection`, never to
MAVSDK directly -- see ARCHITECTURE.md's non-critical/mission-critical split:
this backend is non-critical, and its job is narrow (read state, later send
validated commands), not to embed flight logic.
"""

from __future__ import annotations

import asyncio
import logging
from dataclasses import asdict, dataclass

from mavsdk import System
from mavsdk.mission import MissionItem, MissionPlan

from app import config
from app.mission import Waypoint

logger = logging.getLogger(__name__)


@dataclass
class VehicleState:
    is_connected: bool = False

    latitude_deg: float | None = None
    longitude_deg: float | None = None
    absolute_altitude_m: float | None = None
    relative_altitude_m: float | None = None

    roll_deg: float | None = None
    pitch_deg: float | None = None
    yaw_deg: float | None = None

    airspeed_m_s: float | None = None
    groundspeed_m_s: float | None = None
    heading_deg: float | None = None

    battery_voltage_v: float | None = None
    battery_remaining_pct: float | None = None

    flight_mode: str | None = None
    armed: bool = False

    is_gps_ok: bool = False
    is_armable: bool = False

    mission_uploaded: bool = False
    mission_current: int | None = None
    mission_total: int | None = None

    def to_dict(self) -> dict:
        return asdict(self)


class VehicleConnection:
    """Owns one MAVSDK System connection and fans out state updates to subscribers."""

    def __init__(self, system_address: str) -> None:
        self._system_address = system_address
        self._drone = System()
        self.state = VehicleState()
        self._subscribers: set[asyncio.Queue] = set()
        self._tasks: list[asyncio.Task] = []

    async def connect(self) -> None:
        logger.info("Connecting to vehicle at %s", self._system_address)
        await self._drone.connect(system_address=self._system_address)

        async for conn_state in self._drone.core.connection_state():
            if conn_state.is_connected:
                break
        self.state.is_connected = True
        logger.info("Vehicle connected")

        # Wait for a real health/home-position fix before this connection is
        # usable for mission upload. Skipping this was found to leave
        # is_armable (and PX4's internal home-altitude reference) in a
        # not-yet-settled state, which caused PX4 to spuriously reject an
        # otherwise-valid mission's landing approach -- see SIMULATION.md's
        # Phase 3 findings.
        async for health in self._drone.telemetry.health():
            self.state.is_gps_ok = health.is_global_position_ok
            self.state.is_armable = health.is_armable
            if health.is_armable:
                break
        logger.info("Vehicle armable, home position settled")

        self._tasks = [
            asyncio.create_task(self._watch_position()),
            asyncio.create_task(self._watch_attitude()),
            asyncio.create_task(self._watch_fixedwing_metrics()),
            asyncio.create_task(self._watch_battery()),
            asyncio.create_task(self._watch_flight_mode()),
            asyncio.create_task(self._watch_armed()),
            asyncio.create_task(self._watch_health()),
            asyncio.create_task(self._watch_mission_progress()),
        ]

    async def disconnect(self) -> None:
        for task in self._tasks:
            task.cancel()
        self._tasks.clear()

    def subscribe(self) -> asyncio.Queue:
        queue: asyncio.Queue = asyncio.Queue(maxsize=1)
        self._subscribers.add(queue)
        return queue

    def unsubscribe(self, queue: asyncio.Queue) -> None:
        self._subscribers.discard(queue)

    def _notify(self) -> None:
        snapshot = self.state.to_dict()
        for queue in self._subscribers:
            if queue.full():
                try:
                    queue.get_nowait()
                except asyncio.QueueEmpty:
                    pass
            queue.put_nowait(snapshot)

    async def _watch_position(self) -> None:
        async for position in self._drone.telemetry.position():
            self.state.latitude_deg = position.latitude_deg
            self.state.longitude_deg = position.longitude_deg
            self.state.absolute_altitude_m = position.absolute_altitude_m
            self.state.relative_altitude_m = position.relative_altitude_m
            self._notify()

    async def _watch_attitude(self) -> None:
        async for attitude in self._drone.telemetry.attitude_euler():
            self.state.roll_deg = attitude.roll_deg
            self.state.pitch_deg = attitude.pitch_deg
            self.state.yaw_deg = attitude.yaw_deg
            self._notify()

    async def _watch_fixedwing_metrics(self) -> None:
        async for metrics in self._drone.telemetry.fixedwing_metrics():
            self.state.airspeed_m_s = metrics.airspeed_m_s
            self.state.groundspeed_m_s = metrics.groundspeed_m_s
            self.state.heading_deg = metrics.heading_deg
            self._notify()

    async def _watch_battery(self) -> None:
        async for battery in self._drone.telemetry.battery():
            self.state.battery_voltage_v = battery.voltage_v
            self.state.battery_remaining_pct = battery.remaining_percent
            self._notify()

    async def _watch_flight_mode(self) -> None:
        async for mode in self._drone.telemetry.flight_mode():
            self.state.flight_mode = str(mode)
            self._notify()

    async def _watch_armed(self) -> None:
        async for armed in self._drone.telemetry.armed():
            self.state.armed = armed
            self._notify()

    async def _watch_health(self) -> None:
        async for health in self._drone.telemetry.health():
            self.state.is_gps_ok = health.is_global_position_ok
            self.state.is_armable = health.is_armable
            self._notify()

    async def _watch_mission_progress(self) -> None:
        async for progress in self._drone.mission.mission_progress():
            self.state.mission_current = progress.current
            self.state.mission_total = progress.total
            self._notify()

    async def upload_mission(self, waypoints: list[Waypoint]) -> None:
        """Upload a validated waypoint list, appending a landing item.

        Fixed-wing missions must end with an explicit landing item or PX4
        rejects them outright -- see SIMULATION.md's Phase 1 findings. The
        landing point is the fixed test-geofence center for now; per-mission
        home positions are future work.
        """
        items = [
            MissionItem(
                wp.latitude_deg,
                wp.longitude_deg,
                wp.altitude_m,
                wp.speed_m_s if wp.speed_m_s is not None else config.DEFAULT_CRUISE_SPEED_M_S,
                True,
                float("nan"),
                float("nan"),
                MissionItem.CameraAction.NONE,
                wp.loiter_duration_s if wp.loiter_duration_s is not None else float("nan"),
                float("nan"),
                float("nan"),
                float("nan"),
                float("nan"),
                MissionItem.VehicleAction.NONE,
            )
            for wp in waypoints
        ]
        items.append(
            MissionItem(
                config.GEOFENCE_CENTER_LAT_DEG,
                config.GEOFENCE_CENTER_LON_DEG,
                0,
                config.DEFAULT_CRUISE_SPEED_M_S,
                True,
                float("nan"),
                float("nan"),
                MissionItem.CameraAction.NONE,
                float("nan"),
                float("nan"),
                float("nan"),
                float("nan"),
                float("nan"),
                MissionItem.VehicleAction.LAND,
            )
        )

        await self._drone.mission.upload_mission(MissionPlan(items))
        self.state.mission_uploaded = True
        self._notify()

    async def start_mission(self) -> None:
        """Arm and start. Deliberately does not gate on self.state.is_armable
        first -- that telemetry flag was found to be an overly conservative,
        possibly-stale advisory signal (observed false immediately after an
        autonomous landing, while a direct arm command succeeded right away).
        PX4 is the actual authority on whether it can arm; we attempt the
        arm and let its own rejection (if any) surface as the real error
        instead of guessing preemptively. See SIMULATION.md Phase 3 findings.
        """
        await self._drone.action.arm()
        await self._drone.mission.start_mission()

    async def return_to_launch(self) -> None:
        """Emergency recall -- independent of mission state, always available
        while connected and armed. See SAFETY.md's failsafe state machine:
        this is the same RTH behavior the aircraft applies to itself on
        link loss, just triggered manually here instead."""
        if not self.state.is_connected:
            raise RuntimeError("Vehicle is not connected")
        await self._drone.action.return_to_launch()

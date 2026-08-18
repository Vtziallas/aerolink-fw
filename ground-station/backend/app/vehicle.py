"""Connection to the Mission Agent, re-published as a plain state snapshot.

This is the only module in the backend that imports MissionAgentClient --
everything else (the FastAPI app, the WebSocket layer) talks to
VehicleConnection, never to the Mission Agent directly. See
ARCHITECTURE.md's non-critical/mission-critical split: this backend is
non-critical, and its job is narrow (send validated-looking commands, read
state), not to embed flight logic -- that now lives in the Mission Agent.
"""

from __future__ import annotations

import asyncio
import logging
from dataclasses import asdict, dataclass

from app.mission import Waypoint
from app.mission_agent_client import MissionAgentClient

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
    """Owns one connection to the Mission Agent and fans out state updates to subscribers."""

    def __init__(self, agent_host: str, agent_port: int) -> None:
        self._client = MissionAgentClient(agent_host, agent_port)
        self.state = VehicleState()
        self._subscribers: set[asyncio.Queue] = set()

    async def connect(self) -> None:
        logger.info("Connecting to mission agent at %s:%s", self._client._host, self._client._port)
        self._client.on_telemetry(self._on_telemetry)
        await self._client.connect()
        self.state.is_connected = True
        logger.info("Mission agent connected")

    async def disconnect(self) -> None:
        self.state.is_connected = False

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

    def _on_telemetry(self, message: dict) -> None:
        self.state.armed = message.get("armed", self.state.armed)
        self.state.flight_mode = message.get("flight_mode", self.state.flight_mode)
        self.state.latitude_deg = message.get("latitude_deg", self.state.latitude_deg)
        self.state.longitude_deg = message.get("longitude_deg", self.state.longitude_deg)
        self.state.relative_altitude_m = message.get("relative_altitude_m", self.state.relative_altitude_m)
        self.state.battery_remaining_pct = message.get("battery_remaining_pct", self.state.battery_remaining_pct)
        self.state.is_gps_ok = message.get("is_global_position_ok", self.state.is_gps_ok)
        self.state.mission_current = message.get("mission_current", self.state.mission_current)
        self.state.mission_total = message.get("mission_total", self.state.mission_total)
        self._notify()

    async def upload_mission(self, waypoints: list[Waypoint]) -> None:
        parameters = {
            "waypoints": [
                {
                    "latitude_deg": wp.latitude_deg,
                    "longitude_deg": wp.longitude_deg,
                    "altitude_m": wp.altitude_m,
                    "speed_m_s": wp.speed_m_s,
                    "loiter_duration_s": wp.loiter_duration_s,
                }
                for wp in waypoints
            ]
        }
        acks = await self._client.send_command("UPLOAD_MISSION", parameters)
        if acks[-1].status == "REJECTED":
            raise RuntimeError(acks[-1].reason)
        self.state.mission_uploaded = True
        self._notify()

    async def start_mission(self) -> None:
        acks = await self._client.send_command("START_MISSION", {})
        if acks[-1].status == "REJECTED":
            raise RuntimeError(acks[-1].reason)

    async def return_to_launch(self) -> None:
        if not self.state.is_connected:
            raise RuntimeError("Vehicle is not connected")
        acks = await self._client.send_command("RETURN_TO_LAUNCH", {})
        if acks[-1].status == "REJECTED":
            raise RuntimeError(acks[-1].reason)

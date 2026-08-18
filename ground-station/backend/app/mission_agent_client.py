"""TCP/JSON client for the C++ Mission Agent.

See docs/architecture/mission-agent-design.md for the protocol this
implements. Replaces VehicleConnection's former direct MAVSDK connection --
the backend now talks to the Mission Agent, which owns the actual MAVSDK
link to PX4.
"""

from __future__ import annotations

import asyncio
import datetime
import json
import logging
import uuid
from dataclasses import dataclass
from typing import Callable

logger = logging.getLogger(__name__)

# Matches docs/architecture/mission-agent-design.md's ack-chain semantics:
# REJECTED always terminates the chain; otherwise each command type has
# its own terminal status.
_TERMINAL_STATUS = {
    "UPLOAD_MISSION": "ACCEPTED",
    "START_MISSION": "PX4_ACTION_STARTED",
    "RETURN_TO_LAUNCH": "PX4_ACTION_STARTED",
}


@dataclass
class Ack:
    command_id: str
    status: str
    reason: str = ""


class MissionAgentClient:
    """One persistent TCP connection to the Mission Agent."""

    def __init__(self, host: str, port: int) -> None:
        self._host = host
        self._port = port
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._telemetry_callback: Callable[[dict], None] | None = None
        self._pending_acks: dict[str, asyncio.Queue] = {}

    async def connect(self) -> None:
        self._reader, self._writer = await asyncio.open_connection(self._host, self._port)
        asyncio.create_task(self._read_loop())

    def on_telemetry(self, callback: Callable[[dict], None]) -> None:
        self._telemetry_callback = callback

    async def _read_loop(self) -> None:
        assert self._reader is not None
        while True:
            line = await self._reader.readline()
            if not line:
                logger.warning("Mission agent connection closed")
                return
            message = json.loads(line)
            if "command_id" in message and message["command_id"] in self._pending_acks:
                await self._pending_acks[message["command_id"]].put(Ack(**message))
            elif self._telemetry_callback is not None:
                self._telemetry_callback(message)

    async def send_command(
        self,
        command_type: str,
        parameters: dict,
        mission_version: int = 1,
        aircraft_id: str = "aerolink-1",
    ) -> list[Ack]:
        assert self._writer is not None
        command_id = str(uuid.uuid4())
        queue: asyncio.Queue = asyncio.Queue()
        self._pending_acks[command_id] = queue

        message = {
            "command_id": command_id,
            "aircraft_id": aircraft_id,
            "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "command_type": command_type,
            "parameters": parameters,
            "mission_version": mission_version,
        }
        self._writer.write((json.dumps(message) + "\n").encode())
        await self._writer.drain()

        terminal_status = _TERMINAL_STATUS[command_type]
        acks: list[Ack] = []
        while True:
            ack = await queue.get()
            acks.append(ack)
            if ack.status in ("REJECTED", terminal_status):
                break

        del self._pending_acks[command_id]
        return acks

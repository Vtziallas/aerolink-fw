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

# How long a single send_command call waits for its next ack before giving
# up. Defense-in-depth against a dropped connection that _read_loop somehow
# fails to detect -- normal acks arrive within milliseconds.
_DEFAULT_ACK_TIMEOUT_S = 30.0


@dataclass
class Ack:
    command_id: str
    status: str
    reason: str = ""


class MissionAgentClient:
    """One persistent TCP connection to the Mission Agent."""

    def __init__(self, host: str, port: int, ack_timeout: float = _DEFAULT_ACK_TIMEOUT_S) -> None:
        self._host = host
        self._port = port
        self._ack_timeout = ack_timeout
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._telemetry_callback: Callable[[dict], None] | None = None
        self._pending_acks: dict[str, asyncio.Queue] = {}
        self._read_task: asyncio.Task | None = None

    async def connect(self) -> None:
        self._reader, self._writer = await asyncio.open_connection(self._host, self._port)
        self._read_task = asyncio.create_task(self._read_loop())

    async def disconnect(self) -> None:
        """Tear down the connection: cancel the read loop, close the socket,
        and unblock any in-flight send_command calls with an error instead
        of leaving them hanging."""
        if self._read_task is not None:
            self._read_task.cancel()
            try:
                await self._read_task
            except (asyncio.CancelledError, Exception):
                pass
            self._read_task = None

        if self._writer is not None:
            self._writer.close()
            try:
                await self._writer.wait_closed()
            except Exception:
                pass
            self._writer = None

        self._fail_all_pending(ConnectionError("Mission agent connection closed"))

    def on_telemetry(self, callback: Callable[[dict], None]) -> None:
        self._telemetry_callback = callback

    def _fail_all_pending(self, exc: Exception) -> None:
        """Wake up every send_command call still waiting on an ack with the
        given exception, and drop their queues -- otherwise a dropped
        connection leaves send_command awaiting forever and _pending_acks
        accumulating stale entries (see Task 9 review, Findings 1 & 2)."""
        for queue in self._pending_acks.values():
            queue.put_nowait(exc)
        self._pending_acks.clear()

    async def _read_loop(self) -> None:
        assert self._reader is not None
        try:
            while True:
                line = await self._reader.readline()
                if not line:
                    logger.warning("Mission agent connection closed")
                    return
                message = json.loads(line)
                if "command_id" in message and message["command_id"] in self._pending_acks:
                    ack = Ack(
                        command_id=message["command_id"],
                        status=message["status"],
                        reason=message.get("reason", ""),
                    )
                    await self._pending_acks[message["command_id"]].put(ack)
                elif self._telemetry_callback is not None:
                    self._telemetry_callback(message)
        except asyncio.CancelledError:
            raise
        except Exception as exc:  # noqa: BLE001 -- any read/parse failure means the connection is unusable
            logger.warning("Mission agent read loop failed: %s", exc)
        finally:
            # Whether we hit EOF, an exception, or were cancelled by
            # disconnect(), nothing more will ever arrive on this
            # connection -- unblock anyone still waiting on an ack.
            self._fail_all_pending(ConnectionError("Mission agent connection lost"))

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
        try:
            while True:
                try:
                    item = await asyncio.wait_for(queue.get(), timeout=self._ack_timeout)
                except asyncio.TimeoutError:
                    raise ConnectionError(
                        f"Timed out waiting for mission agent ack to {command_type}"
                    ) from None
                if isinstance(item, Exception):
                    raise item
                acks.append(item)
                if item.status in ("REJECTED", terminal_status):
                    break
        finally:
            self._pending_acks.pop(command_id, None)

        return acks

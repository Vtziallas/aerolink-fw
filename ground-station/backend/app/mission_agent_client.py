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
# up. Defense-in-depth against a dropped connection that the read loop
# somehow fails to detect -- normal acks arrive within milliseconds.
_DEFAULT_ACK_TIMEOUT_S = 30.0

# Reconnect backoff. The Mission Agent's TCP link runs over the same LTE
# tunnel everything else does, so a drop is an expected event, not an error
# (SAFETY.md's LTE_LOST case) -- the client reconnects on its own rather
# than needing the backend process restarted. Exponential up to a cap so a
# long outage doesn't turn into a reconnect storm.
_DEFAULT_RECONNECT_INITIAL_DELAY_S = 1.0
_DEFAULT_RECONNECT_MAX_DELAY_S = 30.0


@dataclass
class Ack:
    command_id: str
    status: str
    reason: str = ""


class MissionAgentClient:
    """One persistent TCP connection to the Mission Agent."""

    def __init__(
        self,
        host: str,
        port: int,
        ack_timeout: float = _DEFAULT_ACK_TIMEOUT_S,
        reconnect_initial_delay: float = _DEFAULT_RECONNECT_INITIAL_DELAY_S,
        reconnect_max_delay: float = _DEFAULT_RECONNECT_MAX_DELAY_S,
    ) -> None:
        self._host = host
        self._port = port
        self._ack_timeout = ack_timeout
        self._reconnect_initial_delay = reconnect_initial_delay
        self._reconnect_max_delay = reconnect_max_delay
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._telemetry_callback: Callable[[dict], None] | None = None
        self._connection_callback: Callable[[bool], None] | None = None
        self._pending_acks: dict[str, asyncio.Queue] = {}
        self._supervisor_task: asyncio.Task | None = None
        # Set by disconnect() so the supervisor stops reconnecting instead of
        # treating an intentional teardown as a link drop.
        self._closing = False

    async def connect(self) -> None:
        """Open the connection and start the supervisor that keeps it open.

        Returns as soon as the first attempt has been made. If that attempt
        fails, or if the link later drops, the supervisor keeps retrying with
        backoff -- reconnection is internal to this client, so nothing above
        it has to be restarted or reconstructed.
        """
        self._closing = False
        connected = await self._open_connection()
        self._supervisor_task = asyncio.create_task(self._supervise(connected))

    async def disconnect(self) -> None:
        """Tear down the connection for good: stop the supervisor, close the
        socket, and unblock any in-flight send_command calls with an error
        instead of leaving them hanging."""
        self._closing = True

        if self._supervisor_task is not None:
            self._supervisor_task.cancel()
            try:
                await self._supervisor_task
            except (asyncio.CancelledError, Exception):
                pass
            self._supervisor_task = None

        await self._close_writer()
        self._fail_all_pending(ConnectionError("Mission agent connection closed"))
        self._notify_connection(False)

    def on_telemetry(self, callback: Callable[[dict], None]) -> None:
        self._telemetry_callback = callback

    def on_connection_change(self, callback: Callable[[bool], None]) -> None:
        """Register a callback invoked with True on (re)connect and False on
        disconnect, so callers can stop reporting a link that is actually
        down (see VehicleConnection.state.is_connected)."""
        self._connection_callback = callback

    def _notify_connection(self, connected: bool) -> None:
        if self._connection_callback is not None:
            try:
                self._connection_callback(connected)
            except Exception:  # noqa: BLE001 -- a bad subscriber must not break the link
                logger.exception("Mission agent connection callback failed")

    async def _open_connection(self) -> bool:
        """One connection attempt. Returns True if the link is now up."""
        try:
            self._reader, self._writer = await asyncio.open_connection(self._host, self._port)
        except (OSError, asyncio.TimeoutError) as exc:
            logger.warning(
                "Mission agent connection to %s:%s failed: %s", self._host, self._port, exc
            )
            self._reader = None
            self._writer = None
            return False
        logger.info("Mission agent connected at %s:%s", self._host, self._port)
        self._notify_connection(True)
        return True

    async def _close_writer(self) -> None:
        if self._writer is None:
            return
        writer, self._writer = self._writer, None
        self._reader = None
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:  # noqa: BLE001 -- already-broken sockets raise here
            pass

    async def _supervise(self, connected: bool) -> None:
        """Own the connection for its whole lifetime: read while it's up,
        reconnect with backoff while it's down, until disconnect()."""
        delay = self._reconnect_initial_delay
        try:
            while not self._closing:
                if connected:
                    await self._read_until_closed()
                    if self._closing:
                        break
                    # The link is down. Unblock in-flight commands and tell
                    # subscribers immediately -- an is_connected that keeps
                    # reporting True makes /api/status lie and lets commands
                    # through that should have been refused up front.
                    await self._close_writer()
                    self._fail_all_pending(ConnectionError("Mission agent connection lost"))
                    self._notify_connection(False)
                    connected = False
                    delay = self._reconnect_initial_delay

                await asyncio.sleep(delay)
                if self._closing:
                    break
                connected = await self._open_connection()
                if connected:
                    delay = self._reconnect_initial_delay
                else:
                    delay = min(delay * 2, self._reconnect_max_delay)
        except asyncio.CancelledError:
            raise
        finally:
            self._fail_all_pending(ConnectionError("Mission agent connection lost"))

    def _fail_all_pending(self, exc: Exception) -> None:
        """Wake up every send_command call still waiting on an ack with the
        given exception, and drop their queues -- otherwise a dropped
        connection leaves send_command awaiting forever and _pending_acks
        accumulating stale entries (see Task 9 review, Findings 1 & 2)."""
        for queue in self._pending_acks.values():
            queue.put_nowait(exc)
        self._pending_acks.clear()

    async def _read_until_closed(self) -> None:
        """Read and dispatch messages until this connection ends. Returns
        (rather than raising) once nothing more will arrive on it -- the
        supervisor decides what to do next."""
        reader = self._reader
        if reader is None:
            return
        try:
            while True:
                line = await reader.readline()
                if not line:
                    logger.warning("Mission agent connection closed")
                    return
                self._dispatch(json.loads(line))
        except asyncio.CancelledError:
            raise
        except Exception as exc:  # noqa: BLE001 -- any read/parse failure means the connection is unusable
            logger.warning("Mission agent read loop failed: %s", exc)

    def _dispatch(self, message: dict) -> None:
        command_id = message.get("command_id")
        if command_id is not None and command_id in self._pending_acks:
            self._pending_acks[command_id].put_nowait(
                Ack(
                    command_id=command_id,
                    status=message["status"],
                    reason=message.get("reason", ""),
                )
            )
            return

        if "status" in message:
            # A status-bearing message that matches no in-flight command.
            # The agent emits exactly this with an empty command_id when a
            # command's JSON was unparseable (it has no id to quote back),
            # and it must not be mistaken for a telemetry sample: silently
            # feeding it to the telemetry callback is how this path stayed
            # invisible until the in-flight command timed out.
            logger.warning(
                "Unmatched mission agent ack (command_id=%r status=%s reason=%s) -- "
                "no in-flight command matches it",
                command_id,
                message.get("status"),
                message.get("reason", ""),
            )
            return

        if self._telemetry_callback is not None:
            self._telemetry_callback(message)

    async def send_command(
        self,
        command_type: str,
        parameters: dict,
        mission_version: int = 1,
        aircraft_id: str = "aerolink-1",
    ) -> list[Ack]:
        writer = self._writer
        if writer is None:
            # Mid-reconnect (or never connected). Fail honestly instead of
            # asserting -- the caller surfaces this to the operator as "not
            # connected" rather than a 30s hang followed by a 500.
            raise ConnectionError("Not connected to the mission agent")

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
        writer.write((json.dumps(message) + "\n").encode())
        await writer.drain()

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

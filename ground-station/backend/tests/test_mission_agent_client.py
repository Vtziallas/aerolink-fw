"""Tests MissionAgentClient against a real local TCP server (not PX4) --
mirrors the Mission Agent's protocol without needing the C++ binary."""
import asyncio
import json

import pytest

from app.mission_agent_client import MissionAgentClient


@pytest.mark.asyncio
async def test_send_command_returns_full_ack_chain():
    port = 15761
    received: list[dict] = []
    responses = [
        {"command_id": "will-be-overwritten", "status": "RECEIVED", "reason": ""},
        {"command_id": "will-be-overwritten", "status": "VALIDATED", "reason": ""},
        {"command_id": "will-be-overwritten", "status": "ACCEPTED", "reason": ""},
    ]

    async def handle(reader, writer):
        line = await reader.readline()
        cmd = json.loads(line)
        received.append(cmd)
        for resp in responses:
            resp = dict(resp, command_id=cmd["command_id"])
            writer.write((json.dumps(resp) + "\n").encode())
            await writer.drain()

    server = await asyncio.start_server(handle, "127.0.0.1", port)
    async with server:
        asyncio.create_task(server.serve_forever())

        client = MissionAgentClient("127.0.0.1", port)
        await client.connect()
        acks = await client.send_command("UPLOAD_MISSION", {"waypoints": []})

        assert [a.status for a in acks] == ["RECEIVED", "VALIDATED", "ACCEPTED"]
        assert received[0]["command_type"] == "UPLOAD_MISSION"

        await client.disconnect()


@pytest.mark.asyncio
async def test_send_command_stops_at_rejected():
    port = 15762

    async def handle(reader, writer):
        line = await reader.readline()
        cmd = json.loads(line)
        writer.write((json.dumps({"command_id": cmd["command_id"], "status": "RECEIVED", "reason": ""}) + "\n").encode())
        writer.write((json.dumps({"command_id": cmd["command_id"], "status": "REJECTED", "reason": "outside geofence"}) + "\n").encode())
        await writer.drain()

    server = await asyncio.start_server(handle, "127.0.0.1", port)
    async with server:
        asyncio.create_task(server.serve_forever())

        client = MissionAgentClient("127.0.0.1", port)
        await client.connect()
        acks = await client.send_command("UPLOAD_MISSION", {"waypoints": []})

        assert acks[-1].status == "REJECTED"
        assert acks[-1].reason == "outside geofence"

        await client.disconnect()


@pytest.mark.asyncio
async def test_telemetry_callback_receives_non_ack_lines():
    port = 15763
    telemetry_received: list[dict] = []

    async def handle(reader, writer):
        writer.write((json.dumps({"armed": True, "latitude_deg": 47.4}) + "\n").encode())
        await writer.drain()
        await asyncio.sleep(0.1)

    server = await asyncio.start_server(handle, "127.0.0.1", port)
    async with server:
        asyncio.create_task(server.serve_forever())

        client = MissionAgentClient("127.0.0.1", port)
        client.on_telemetry(lambda msg: telemetry_received.append(msg))
        await client.connect()
        await asyncio.sleep(0.05)

        assert len(telemetry_received) == 1
        assert telemetry_received[0]["latitude_deg"] == 47.4

        await client.disconnect()


@pytest.mark.asyncio
async def test_send_command_raises_when_connection_drops_mid_command():
    """Regression test for Task 9 review Finding 1/2: if the Mission Agent
    connection drops while a command is in flight, send_command must raise
    a clear error (and clean up its pending-ack bookkeeping) instead of
    hanging forever."""
    port = 15764

    async def handle(reader, writer):
        # Read the in-flight command, then simulate the Mission Agent
        # process dying (or the TCP link dropping) without ever acking --
        # never write a response, just close the connection.
        await reader.readline()
        writer.close()
        await writer.wait_closed()

    server = await asyncio.start_server(handle, "127.0.0.1", port)
    async with server:
        asyncio.create_task(server.serve_forever())

        client = MissionAgentClient("127.0.0.1", port)
        await client.connect()

        # Bounded wait so a regression (a hang) fails this test loudly
        # instead of hanging the whole suite.
        with pytest.raises(ConnectionError):
            await asyncio.wait_for(
                client.send_command("UPLOAD_MISSION", {"waypoints": []}), timeout=2
            )

        assert client._pending_acks == {}

    await client.disconnect()


@pytest.mark.asyncio
async def test_client_reconnects_by_itself_after_the_link_drops():
    """Final-review Finding C1: the read loop returned permanently on EOF and
    nothing ever reconnected, so a link drop needed a backend restart. The
    project test tests/simulation/link_interruption_reconnection.md already
    recorded automatic resync as a property of this system.

    Note what this test does NOT do: it never calls connect() again and never
    rebuilds the client. Reconnection has to be internal.
    """
    port = 15765
    link_states: list[bool] = []

    async def handle_and_drop(reader, writer):
        writer.close()
        await writer.wait_closed()

    async def handle_normally(reader, writer):
        line = await reader.readline()
        cmd = json.loads(line)
        for status in ("RECEIVED", "VALIDATED", "ACCEPTED"):
            resp = {"command_id": cmd["command_id"], "status": status, "reason": ""}
            writer.write((json.dumps(resp) + "\n").encode())
        await writer.drain()

    # Tight backoff so the test doesn't sit through the production 1s floor.
    client = MissionAgentClient("127.0.0.1", port, reconnect_initial_delay=0.02)
    client.on_connection_change(link_states.append)

    first_server = await asyncio.start_server(handle_and_drop, "127.0.0.1", port)
    async with first_server:
        await client.connect()
        assert link_states == [True]

        # The server hangs up immediately; wait for the client to notice.
        for _ in range(100):
            if link_states[-1] is False:
                break
            await asyncio.sleep(0.01)
        assert link_states[-1] is False, "client never reported the link going down"

    # Same port, new server -- as far as the client is concerned the Mission
    # Agent restarted (or the tunnel came back).
    second_server = await asyncio.start_server(handle_normally, "127.0.0.1", port)
    async with second_server:
        for _ in range(200):
            if link_states[-1] is True:
                break
            await asyncio.sleep(0.01)
        assert link_states[-1] is True, "client never reconnected on its own"

        acks = await asyncio.wait_for(
            client.send_command("UPLOAD_MISSION", {"waypoints": []}), timeout=2
        )
        assert [a.status for a in acks] == ["RECEIVED", "VALIDATED", "ACCEPTED"]

    await client.disconnect()
    assert link_states[-1] is False


@pytest.mark.asyncio
async def test_status_bearing_message_with_unmatched_command_id_is_not_telemetry():
    """Final-review Finding I9: the C++ server answers unparseable JSON with
    a REJECTED carrying an empty command_id (it has no id to quote back).
    That matches no pending ack, and used to fall through to the telemetry
    callback, which absorbed it silently -- the real in-flight command then
    hung until its timeout. It must be logged as an unmatched ack instead.
    """
    port = 15766
    telemetry_received: list[dict] = []

    async def handle(reader, writer):
        writer.write(
            (
                json.dumps({"command_id": "", "status": "REJECTED", "reason": "malformed command"})
                + "\n"
            ).encode()
        )
        # A real telemetry sample after it, to prove the callback still works
        # and that only the status-bearing message was withheld.
        writer.write((json.dumps({"armed": False, "latitude_deg": 47.4}) + "\n").encode())
        await writer.drain()
        await asyncio.sleep(0.2)

    server = await asyncio.start_server(handle, "127.0.0.1", port)
    async with server:
        client = MissionAgentClient("127.0.0.1", port)
        client.on_telemetry(telemetry_received.append)
        await client.connect()
        await asyncio.sleep(0.1)

        assert telemetry_received == [{"armed": False, "latitude_deg": 47.4}]

        await client.disconnect()

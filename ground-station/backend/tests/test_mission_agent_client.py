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

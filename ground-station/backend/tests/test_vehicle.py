import asyncio

import pytest

from app.mission_agent_client import Ack
from app.vehicle import VehicleConnection, VehicleState


class FakeMissionAgentClient:
    """Stands in for MissionAgentClient -- no real TCP connection, just
    records what VehicleConnection sends and returns pre-scripted acks."""

    def __init__(self):
        self._host = "127.0.0.1"
        self._port = 5760
        self.connected = False
        self.telemetry_callback = None
        self.sent_commands: list[tuple[str, dict]] = []
        self.next_acks: list[Ack] = []

    async def connect(self) -> None:
        self.connected = True

    def on_telemetry(self, callback):
        self.telemetry_callback = callback

    async def send_command(self, command_type, parameters, mission_version=1, aircraft_id="aerolink-1"):
        self.sent_commands.append((command_type, parameters))
        return self.next_acks


def make_vehicle():
    vehicle = VehicleConnection(agent_host="127.0.0.1", agent_port=5760)
    vehicle._client = FakeMissionAgentClient()
    return vehicle


def test_vehicle_state_to_dict_is_json_serializable():
    state = VehicleState(is_connected=True, latitude_deg=47.4, armed=False)
    d = state.to_dict()
    assert d["is_connected"] is True
    assert d["latitude_deg"] == 47.4
    assert d["armed"] is False


async def test_connect_marks_state_connected_and_registers_telemetry_callback():
    vehicle = make_vehicle()

    await vehicle.connect()

    assert vehicle.state.is_connected is True
    assert vehicle._client.connected is True
    assert vehicle._client.telemetry_callback == vehicle._on_telemetry


async def test_subscribers_receive_state_on_notify():
    vehicle = make_vehicle()
    queue = vehicle.subscribe()

    vehicle.state.relative_altitude_m = 30.0
    vehicle._notify()

    snapshot = await asyncio.wait_for(queue.get(), timeout=1)
    assert snapshot["relative_altitude_m"] == 30.0


async def test_unsubscribe_stops_delivery():
    vehicle = make_vehicle()
    queue = vehicle.subscribe()
    vehicle.unsubscribe(queue)

    vehicle.state.armed = True
    vehicle._notify()

    assert queue.empty()


async def test_slow_subscriber_queue_drops_oldest_instead_of_blocking():
    vehicle = make_vehicle()
    queue = vehicle.subscribe()

    vehicle.state.heading_deg = 10.0
    vehicle._notify()
    vehicle.state.heading_deg = 20.0
    vehicle._notify()

    snapshot = await asyncio.wait_for(queue.get(), timeout=1)
    assert snapshot["heading_deg"] == 20.0
    assert queue.empty()


async def test_on_telemetry_updates_state_and_notifies_subscribers():
    vehicle = make_vehicle()
    queue = vehicle.subscribe()

    vehicle._on_telemetry({"armed": True, "latitude_deg": 47.5, "flight_mode": "HOLD"})

    assert vehicle.state.armed is True
    assert vehicle.state.latitude_deg == 47.5
    assert vehicle.state.flight_mode == "HOLD"

    snapshot = await asyncio.wait_for(queue.get(), timeout=1)
    assert snapshot["armed"] is True
    assert snapshot["flight_mode"] == "HOLD"


async def test_upload_mission_sets_mission_uploaded_on_success():
    vehicle = make_vehicle()
    vehicle._client.next_acks = [
        Ack(command_id="c1", status="RECEIVED"),
        Ack(command_id="c1", status="VALIDATED"),
        Ack(command_id="c1", status="ACCEPTED"),
    ]

    await vehicle.upload_mission([])

    assert vehicle.state.mission_uploaded is True
    assert vehicle._client.sent_commands == [("UPLOAD_MISSION", {"waypoints": []})]


async def test_upload_mission_raises_on_rejected_and_does_not_set_uploaded():
    vehicle = make_vehicle()
    vehicle._client.next_acks = [
        Ack(command_id="c1", status="RECEIVED"),
        Ack(command_id="c1", status="REJECTED", reason="outside geofence"),
    ]

    with pytest.raises(RuntimeError, match="outside geofence"):
        await vehicle.upload_mission([])

    assert vehicle.state.mission_uploaded is False


async def test_start_mission_raises_on_rejected():
    vehicle = make_vehicle()
    vehicle._client.next_acks = [
        Ack(command_id="c1", status="REJECTED", reason="not armable"),
    ]

    with pytest.raises(RuntimeError, match="not armable"):
        await vehicle.start_mission()


async def test_start_mission_succeeds_when_accepted():
    vehicle = make_vehicle()
    vehicle._client.next_acks = [
        Ack(command_id="c1", status="PX4_ACTION_STARTED"),
    ]

    await vehicle.start_mission()  # should not raise

    assert vehicle._client.sent_commands == [("START_MISSION", {})]


async def test_return_to_launch_raises_if_not_connected():
    vehicle = make_vehicle()

    with pytest.raises(RuntimeError, match="not connected"):
        await vehicle.return_to_launch()

    assert vehicle._client.sent_commands == []


async def test_return_to_launch_raises_on_rejected():
    vehicle = make_vehicle()
    vehicle.state.is_connected = True
    vehicle._client.next_acks = [
        Ack(command_id="c1", status="REJECTED", reason="not flying"),
    ]

    with pytest.raises(RuntimeError, match="not flying"):
        await vehicle.return_to_launch()


async def test_return_to_launch_succeeds_when_connected_and_accepted():
    vehicle = make_vehicle()
    vehicle.state.is_connected = True
    vehicle._client.next_acks = [
        Ack(command_id="c1", status="PX4_ACTION_STARTED"),
    ]

    await vehicle.return_to_launch()  # should not raise

    assert vehicle._client.sent_commands == [("RETURN_TO_LAUNCH", {})]

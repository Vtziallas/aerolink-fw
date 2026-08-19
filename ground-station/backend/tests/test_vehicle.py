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
        self.disconnect_called = False
        self.telemetry_callback = None
        self.connection_callback = None
        self.sent_commands: list[tuple[str, dict]] = []
        self.next_acks: list[Ack] = []

    async def connect(self) -> None:
        self.connected = True
        self._report_connection(True)

    async def disconnect(self) -> None:
        self.disconnect_called = True
        self.connected = False
        self._report_connection(False)

    def on_telemetry(self, callback):
        self.telemetry_callback = callback

    def on_connection_change(self, callback):
        self.connection_callback = callback

    def _report_connection(self, connected: bool) -> None:
        """The real client drives is_connected through this callback (it owns
        reconnection), so the fake has to as well."""
        if self.connection_callback is not None:
            self.connection_callback(connected)

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


async def test_disconnect_marks_state_disconnected_and_tears_down_client():
    """Regression test for Task 9 review Finding 3: disconnect() must
    actually close the underlying MissionAgentClient connection (and cancel
    its read loop), not just flip the local is_connected flag."""
    vehicle = make_vehicle()
    await vehicle.connect()

    await vehicle.disconnect()

    assert vehicle.state.is_connected is False
    assert vehicle._client.disconnect_called is True


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


async def test_link_drop_marks_state_disconnected_and_notifies_subscribers():
    """Final-review Finding C1: is_connected was set True once in connect()
    and never set back, so /api/status kept reporting a connected vehicle
    after the link dropped and the /api/missions 503 gate never fired."""
    vehicle = make_vehicle()
    await vehicle.connect()
    queue = vehicle.subscribe()

    vehicle._client._report_connection(False)

    assert vehicle.state.is_connected is False
    snapshot = await asyncio.wait_for(queue.get(), timeout=1)
    assert snapshot["is_connected"] is False

    # ...and back up again when the client reconnects on its own.
    vehicle._client._report_connection(True)
    assert vehicle.state.is_connected is True
    snapshot = await asyncio.wait_for(queue.get(), timeout=1)
    assert snapshot["is_connected"] is True


async def test_on_telemetry_populates_every_field_the_frontend_renders():
    """Final-review Finding I2: eight fields TelemetryPanel.tsx/MapView.tsx
    render were dropped from the agent's telemetry and stayed null."""
    vehicle = make_vehicle()

    vehicle._on_telemetry(
        {
            "armed": True,
            "flight_mode": "MISSION",
            "latitude_deg": 47.4,
            "longitude_deg": 8.5,
            "relative_altitude_m": 30.0,
            "absolute_altitude_m": 518.0,
            "roll_deg": 1.5,
            "pitch_deg": -2.5,
            "yaw_deg": 91.0,
            "airspeed_m_s": 16.5,
            "groundspeed_m_s": 18.25,
            "heading_deg": 274.0,
            "battery_remaining_pct": 85.5,
            "battery_voltage_v": 22.1,
            "is_global_position_ok": True,
            "is_armable": True,
            "mission_current": 2,
            "mission_total": 4,
        }
    )

    state = vehicle.state
    assert state.absolute_altitude_m == 518.0
    assert (state.roll_deg, state.pitch_deg, state.yaw_deg) == (1.5, -2.5, 91.0)
    assert state.airspeed_m_s == 16.5
    assert state.groundspeed_m_s == 18.25
    assert state.heading_deg == 274.0
    assert state.battery_voltage_v == 22.1
    assert state.is_armable is True
    assert state.is_gps_ok is True
    assert (state.mission_current, state.mission_total) == (2, 4)


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

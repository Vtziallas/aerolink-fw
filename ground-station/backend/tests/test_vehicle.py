import asyncio

from app.vehicle import VehicleConnection, VehicleState


def test_vehicle_state_to_dict_is_json_serializable():
    state = VehicleState(is_connected=True, latitude_deg=47.4, armed=False)
    d = state.to_dict()
    assert d["is_connected"] is True
    assert d["latitude_deg"] == 47.4
    assert d["armed"] is False


async def test_subscribers_receive_state_on_notify():
    vehicle = VehicleConnection(system_address="udpin://0.0.0.0:0")
    queue = vehicle.subscribe()

    vehicle.state.relative_altitude_m = 30.0
    vehicle._notify()

    snapshot = await asyncio.wait_for(queue.get(), timeout=1)
    assert snapshot["relative_altitude_m"] == 30.0


async def test_unsubscribe_stops_delivery():
    vehicle = VehicleConnection(system_address="udpin://0.0.0.0:0")
    queue = vehicle.subscribe()
    vehicle.unsubscribe(queue)

    vehicle.state.armed = True
    vehicle._notify()

    assert queue.empty()


async def test_slow_subscriber_queue_drops_oldest_instead_of_blocking():
    vehicle = VehicleConnection(system_address="udpin://0.0.0.0:0")
    queue = vehicle.subscribe()

    vehicle.state.heading_deg = 10.0
    vehicle._notify()
    vehicle.state.heading_deg = 20.0
    vehicle._notify()

    snapshot = await asyncio.wait_for(queue.get(), timeout=1)
    assert snapshot["heading_deg"] == 20.0
    assert queue.empty()

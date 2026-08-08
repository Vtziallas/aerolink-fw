"""AeroLink-FW ground station backend.

Phase 2 scope only: connect to a vehicle via MAVSDK and re-publish its
telemetry over WebSocket. No command sending, no mission upload, no
persistence yet -- those are Phase 3+ (see ARCHITECTURE.md, SIMULATION.md).
"""

from __future__ import annotations

import asyncio
import logging
import os
from contextlib import asynccontextmanager

from fastapi import FastAPI, WebSocket, WebSocketDisconnect

from app.vehicle import VehicleConnection

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

VEHICLE_SYSTEM_ADDRESS = os.environ.get("VEHICLE_SYSTEM_ADDRESS", "udpin://0.0.0.0:14540")


@asynccontextmanager
async def lifespan(app: FastAPI):
    vehicle = VehicleConnection(VEHICLE_SYSTEM_ADDRESS)
    app.state.vehicle = vehicle
    connect_task = asyncio.create_task(vehicle.connect())
    yield
    connect_task.cancel()
    await vehicle.disconnect()


app = FastAPI(title="AeroLink-FW Ground Station Backend", lifespan=lifespan)


@app.get("/api/status")
async def status():
    vehicle: VehicleConnection = app.state.vehicle
    return {
        "vehicle_connected": vehicle.state.is_connected,
        "system_address": VEHICLE_SYSTEM_ADDRESS,
    }


@app.websocket("/ws/telemetry")
async def telemetry_ws(websocket: WebSocket):
    await websocket.accept()
    vehicle: VehicleConnection = app.state.vehicle
    queue = vehicle.subscribe()
    try:
        await websocket.send_json(vehicle.state.to_dict())
        while True:
            snapshot = await queue.get()
            await websocket.send_json(snapshot)
    except WebSocketDisconnect:
        logger.info("Telemetry client disconnected")
    finally:
        vehicle.unsubscribe(queue)

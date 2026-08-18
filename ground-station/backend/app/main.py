"""AeroLink-FW ground station backend.

Phase 2: connect to a vehicle via MAVSDK and re-publish its telemetry over
WebSocket. Phase 3 adds mission upload/start through this API -- the browser
still never talks MAVLink directly. No command auth/ack pipeline or
persistence yet -- those are Phase 4+ (see ARCHITECTURE.md, SIMULATION.md).
"""

from __future__ import annotations

import asyncio
import logging
import os
from contextlib import asynccontextmanager

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware

from app import config
from app.mission import MissionRequest, validate_mission
from app.vehicle import VehicleConnection

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

MISSION_AGENT_HOST = os.environ.get("MISSION_AGENT_HOST", "127.0.0.1")
MISSION_AGENT_PORT = int(os.environ.get("MISSION_AGENT_PORT", "5760"))

# The frontend dev server runs on a different origin (port), so the browser
# requires CORS to allow the REST calls (WebSocket connections aren't
# subject to the same preflight restriction, which is why telemetry worked
# before mission upload did). Comma-separated so a deployed frontend origin
# can be added via env var without code changes.
ALLOWED_ORIGINS = os.environ.get(
    "ALLOWED_ORIGINS", "http://localhost:5173,http://127.0.0.1:5173"
).split(",")


@asynccontextmanager
async def lifespan(app: FastAPI):
    vehicle = VehicleConnection(MISSION_AGENT_HOST, MISSION_AGENT_PORT)
    app.state.vehicle = vehicle
    connect_task = asyncio.create_task(vehicle.connect())
    yield
    connect_task.cancel()
    await vehicle.disconnect()


app = FastAPI(title="AeroLink-FW Ground Station Backend", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=ALLOWED_ORIGINS,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.get("/api/status")
async def status():
    vehicle: VehicleConnection = app.state.vehicle
    return {
        "vehicle_connected": vehicle.state.is_connected,
        "mission_agent_address": f"{MISSION_AGENT_HOST}:{MISSION_AGENT_PORT}",
    }


@app.get("/api/config")
async def get_config():
    """Static config the frontend needs to render the map sensibly -- home
    position and geofence, currently fixed server-side (see config.py)."""
    return {
        "home_lat_deg": config.GEOFENCE_CENTER_LAT_DEG,
        "home_lon_deg": config.GEOFENCE_CENTER_LON_DEG,
        "geofence_radius_m": config.GEOFENCE_RADIUS_M,
        "min_altitude_m": config.MIN_ALTITUDE_M,
        "max_altitude_m": config.MAX_ALTITUDE_M,
    }


@app.post("/api/missions")
async def upload_mission(mission: MissionRequest):
    vehicle: VehicleConnection = app.state.vehicle

    result = validate_mission(mission)
    if not result.is_valid:
        raise HTTPException(status_code=422, detail={"errors": result.errors})

    if not vehicle.state.is_connected:
        raise HTTPException(status_code=503, detail={"errors": ["Vehicle not connected"]})

    try:
        await vehicle.upload_mission(mission.waypoints)
    except Exception as exc:  # noqa: BLE001 -- surfaced as a rejection, not a 500
        raise HTTPException(status_code=422, detail={"errors": [str(exc)]}) from exc

    return {"status": "uploaded", "waypoint_count": len(mission.waypoints)}


@app.post("/api/missions/current/start")
async def start_mission():
    vehicle: VehicleConnection = app.state.vehicle

    if not vehicle.state.mission_uploaded:
        raise HTTPException(status_code=409, detail={"errors": ["No mission uploaded"]})

    try:
        await vehicle.start_mission()
    except Exception as exc:  # noqa: BLE001
        raise HTTPException(status_code=422, detail={"errors": [str(exc)]}) from exc

    return {"status": "started"}


@app.post("/api/emergency/rtl")
async def return_to_launch():
    vehicle: VehicleConnection = app.state.vehicle
    try:
        await vehicle.return_to_launch()
    except Exception as exc:  # noqa: BLE001
        raise HTTPException(status_code=422, detail={"errors": [str(exc)]}) from exc

    return {"status": "returning_to_launch"}


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

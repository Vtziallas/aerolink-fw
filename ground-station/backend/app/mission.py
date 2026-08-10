"""Mission model and validation.

Matches the Mission/Waypoint shape from ARCHITECTURE.md/project_prompt.txt
section 7, scoped to what Phase 3 actually needs: a flat list of waypoints
plus a home/landing position. No command auth or ack pipeline here -- that
belongs with Phase 4's real network boundary (see NETWORKING.md).
"""

from __future__ import annotations

import math
from dataclasses import dataclass

from pydantic import BaseModel, Field

from app import config


class Waypoint(BaseModel):
    latitude_deg: float = Field(ge=-90, le=90)
    longitude_deg: float = Field(ge=-180, le=180)
    altitude_m: float
    loiter_radius_m: float | None = None
    loiter_duration_s: float | None = Field(default=None, ge=0)
    speed_m_s: float | None = Field(default=None, gt=0)


class MissionRequest(BaseModel):
    aircraft_id: str
    waypoints: list[Waypoint]


@dataclass
class ValidationResult:
    is_valid: bool
    errors: list[str]


def _distance_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Haversine distance in meters."""
    r = 6_371_000.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    d_phi = math.radians(lat2 - lat1)
    d_lambda = math.radians(lon2 - lon1)
    a = math.sin(d_phi / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(d_lambda / 2) ** 2
    return 2 * r * math.asin(math.sqrt(a))


def _bearing_deg(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    p1, p2 = math.radians(lat1), math.radians(lat2)
    d_lambda = math.radians(lon2 - lon1)
    x = math.sin(d_lambda) * math.cos(p2)
    y = math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(d_lambda)
    return math.degrees(math.atan2(x, y)) % 360


def _turn_angle_deg(bearing_in: float, bearing_out: float) -> float:
    """Angle the aircraft must turn through, in [0, 180]."""
    return abs((bearing_out - bearing_in + 180) % 360 - 180)


def validate_mission(mission: MissionRequest) -> ValidationResult:
    errors: list[str] = []

    if len(mission.waypoints) == 0:
        errors.append("Mission must contain at least one waypoint")

    if len(mission.waypoints) > config.MAX_WAYPOINTS:
        errors.append(
            f"Mission has {len(mission.waypoints)} waypoints, "
            f"exceeds maximum of {config.MAX_WAYPOINTS}"
        )

    for i, wp in enumerate(mission.waypoints):
        if not (config.MIN_ALTITUDE_M <= wp.altitude_m <= config.MAX_ALTITUDE_M):
            errors.append(
                f"Waypoint {i}: altitude {wp.altitude_m} m outside allowed range "
                f"[{config.MIN_ALTITUDE_M}, {config.MAX_ALTITUDE_M}] m"
            )

        distance = _distance_m(
            config.GEOFENCE_CENTER_LAT_DEG,
            config.GEOFENCE_CENTER_LON_DEG,
            wp.latitude_deg,
            wp.longitude_deg,
        )
        if distance > config.GEOFENCE_RADIUS_M:
            errors.append(
                f"Waypoint {i}: {distance:.0f} m from geofence center, "
                f"exceeds radius of {config.GEOFENCE_RADIUS_M:.0f} m"
            )

        if i > 0:
            prev = mission.waypoints[i - 1]
            leg_distance = _distance_m(prev.latitude_deg, prev.longitude_deg, wp.latitude_deg, wp.longitude_deg)
            if leg_distance < config.MIN_WAYPOINT_SEPARATION_M:
                errors.append(
                    f"Waypoint {i}: only {leg_distance:.0f} m from waypoint {i - 1}, "
                    f"below minimum separation of {config.MIN_WAYPOINT_SEPARATION_M:.0f} m "
                    "(the aircraft can't turn tightly enough and will circle instead of "
                    "reaching the next waypoint)"
                )

    # Turn-angle check between consecutive user waypoints. Deliberately does
    # NOT extend this to the turn into the auto-appended landing point --
    # only a sharp turn between two real waypoints was actually observed to
    # cause trouble in testing, and a simple out-and-back mission naturally
    # needs a sharp turn to line up with home for landing, which isn't
    # evidence of the same problem.
    points = [(wp.latitude_deg, wp.longitude_deg) for wp in mission.waypoints]
    for i in range(1, len(points) - 1):
        bearing_in = _bearing_deg(*points[i - 1], *points[i])
        bearing_out = _bearing_deg(*points[i], *points[i + 1])
        turn_angle = _turn_angle_deg(bearing_in, bearing_out)
        if turn_angle > config.MAX_TURN_ANGLE_DEG:
            errors.append(
                f"Waypoint {i}: {turn_angle:.0f} degree turn exceeds maximum of "
                f"{config.MAX_TURN_ANGLE_DEG:.0f} degrees (add an intermediate waypoint "
                "to smooth the turn -- a turn this sharp was observed to destabilize the "
                "aircraft in testing)"
            )

    return ValidationResult(is_valid=len(errors) == 0, errors=errors)

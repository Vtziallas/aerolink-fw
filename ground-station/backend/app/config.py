"""Backend configuration, overridable via environment variables.

The geofence here is the "predefined test geofence" ARCHITECTURE.md/SAFETY.md
call for -- a hard software boundary missions are validated against before
upload, independent of whatever geofence PX4 itself may also enforce.
"""

import os


def _float_env(name: str, default: float) -> float:
    return float(os.environ.get(name, default))


# Center of the test geofence. Defaults to PX4 SITL's default home position
# (Zurich) so Phase 1-3 development needs no configuration.
GEOFENCE_CENTER_LAT_DEG = _float_env("GEOFENCE_CENTER_LAT_DEG", 47.397742)
GEOFENCE_CENTER_LON_DEG = _float_env("GEOFENCE_CENTER_LON_DEG", 8.545593)
GEOFENCE_RADIUS_M = _float_env("GEOFENCE_RADIUS_M", 2000.0)

MIN_ALTITUDE_M = _float_env("MIN_ALTITUDE_M", 10.0)
MAX_ALTITUDE_M = _float_env("MAX_ALTITUDE_M", 120.0)  # EASA Open category ceiling

MAX_WAYPOINTS = int(os.environ.get("MAX_WAYPOINTS", 10))

# Consecutive waypoints closer than this can cause the fixed-wing navigation
# controller (NPFG) to never converge onto the new leg before the next turn
# is due, resulting in the aircraft circling indefinitely instead of
# progressing -- observed directly in Phase 3 testing. Heuristic: PX4's
# default NPFG_PERIOD (10s) times our default cruise speed suggests waypoints
# need on the order of 150-200m of separation to give the controller room to
# settle; 150m is a conservative floor, not an exact requirement.
MIN_WAYPOINT_SEPARATION_M = _float_env("MIN_WAYPOINT_SEPARATION_M", 150.0)

# A turn this sharp or sharper at a single waypoint was observed in Phase 3
# testing to destabilize the aircraft (altitude excursion, loss of the next
# waypoint, and a follow-on RTL that never converged) even when both legs
# satisfied MIN_WAYPOINT_SEPARATION_M -- leg *distance* alone doesn't capture
# how much room the controller needs to complete a sharp turn. An observed
# ~89 degree turn failed; 70 degrees is a conservative reject threshold below
# that, not a value derived from PX4 turn-performance docs.
MAX_TURN_ANGLE_DEG = _float_env("MAX_TURN_ANGLE_DEG", 70.0)

# Applied when a waypoint doesn't specify its own speed. Matches the value
# validated in Phase 1 testing; leaving this NaN instead of a real number
# was observed to make PX4's landing-approach feasibility check spuriously
# reject an otherwise-valid mission (see SIMULATION.md Phase 3 findings).
DEFAULT_CRUISE_SPEED_M_S = _float_env("DEFAULT_CRUISE_SPEED_M_S", 20.0)

from app.mission import MissionRequest, Waypoint, validate_mission

HOME_LAT, HOME_LON = 47.397742, 8.545593


def make_mission(waypoints):
    return MissionRequest(aircraft_id="sitl-01", waypoints=waypoints)


def test_valid_mission_passes():
    mission = make_mission(
        [
            Waypoint(latitude_deg=HOME_LAT + 0.002, longitude_deg=HOME_LON, altitude_m=30),
            Waypoint(latitude_deg=HOME_LAT + 0.002, longitude_deg=HOME_LON + 0.003, altitude_m=30),
        ]
    )
    result = validate_mission(mission)
    assert result.is_valid
    assert result.errors == []


def test_empty_mission_rejected():
    result = validate_mission(make_mission([]))
    assert not result.is_valid
    assert "at least one waypoint" in result.errors[0]


def test_waypoint_outside_geofence_rejected():
    mission = make_mission(
        [Waypoint(latitude_deg=HOME_LAT + 1.0, longitude_deg=HOME_LON, altitude_m=30)]
    )
    result = validate_mission(mission)
    assert not result.is_valid
    assert any("geofence" in e for e in result.errors)


def test_altitude_too_low_rejected():
    mission = make_mission(
        [Waypoint(latitude_deg=HOME_LAT + 0.001, longitude_deg=HOME_LON, altitude_m=1)]
    )
    result = validate_mission(mission)
    assert not result.is_valid
    assert any("altitude" in e for e in result.errors)


def test_altitude_too_high_rejected():
    mission = make_mission(
        [Waypoint(latitude_deg=HOME_LAT + 0.001, longitude_deg=HOME_LON, altitude_m=500)]
    )
    result = validate_mission(mission)
    assert not result.is_valid
    assert any("altitude" in e for e in result.errors)


def test_too_many_waypoints_rejected():
    waypoints = [
        Waypoint(latitude_deg=HOME_LAT + 0.0001 * i, longitude_deg=HOME_LON, altitude_m=30)
        for i in range(20)
    ]
    result = validate_mission(make_mission(waypoints))
    assert not result.is_valid
    assert any("exceeds maximum" in e for e in result.errors)


def test_waypoints_too_close_together_rejected():
    mission = make_mission(
        [
            Waypoint(latitude_deg=HOME_LAT + 0.002, longitude_deg=HOME_LON, altitude_m=30),
            Waypoint(latitude_deg=HOME_LAT + 0.0021, longitude_deg=HOME_LON + 0.0001, altitude_m=30),
        ]
    )
    result = validate_mission(mission)
    assert not result.is_valid
    assert any("separation" in e for e in result.errors)


def test_sharp_turn_rejected():
    # Real coordinates from a Phase 3 flight test: the aircraft turned off
    # course and lost the mission approaching waypoint 2 here (an ~89 degree
    # turn), and a follow-on RTL never converged. Regression test for that.
    mission = make_mission(
        [
            Waypoint(latitude_deg=47.3964682, longitude_deg=8.5483627, altitude_m=30),
            Waypoint(latitude_deg=47.3951609, longitude_deg=8.546217, altitude_m=30),
            Waypoint(latitude_deg=47.3948413, longitude_deg=8.5419469, altitude_m=30),
            Waypoint(latitude_deg=47.3966715, longitude_deg=8.541625, altitude_m=30),
        ]
    )
    result = validate_mission(mission)
    assert not result.is_valid
    assert any("degree turn" in e for e in result.errors)


def test_gentle_turn_accepted():
    mission = make_mission(
        [
            Waypoint(latitude_deg=HOME_LAT + 0.002, longitude_deg=HOME_LON, altitude_m=30),
            Waypoint(latitude_deg=HOME_LAT + 0.002, longitude_deg=HOME_LON + 0.003, altitude_m=30),
            Waypoint(latitude_deg=HOME_LAT + 0.0035, longitude_deg=HOME_LON + 0.0055, altitude_m=30),
        ]
    )
    result = validate_mission(mission)
    assert result.is_valid
    assert result.errors == []


def test_multiple_errors_all_reported():
    mission = make_mission(
        [
            Waypoint(latitude_deg=HOME_LAT + 1.0, longitude_deg=HOME_LON, altitude_m=500),
        ]
    )
    result = validate_mission(mission)
    assert not result.is_valid
    assert len(result.errors) == 2

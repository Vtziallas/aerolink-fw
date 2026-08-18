#include <catch2/catch_test_macros.hpp>
#include "../src/command_validator.h"

using namespace mission_agent;

namespace {
GeofenceConfig test_geofence() {
    return GeofenceConfig{47.397742, 8.545593, 2000.0, 10.0f, 120.0f};
}

Command upload_command(std::vector<Waypoint> waypoints) {
    Command cmd;
    cmd.command_id = "id-1";
    cmd.aircraft_id = "aerolink-1";
    cmd.timestamp = "2026-08-14T10:00:00Z";
    cmd.command_type = CommandType::UploadMission;
    cmd.mission_version = 1;
    cmd.waypoints = std::move(waypoints);
    return cmd;
}
}  // namespace

TEST_CASE("distance_m returns ~0 for the same point", "[command_validator]") {
    REQUIRE(distance_m(47.397742, 8.545593, 47.397742, 8.545593) < 1.0);
}

TEST_CASE("distance_m returns a sane value for a known offset", "[command_validator]") {
    // Roughly 1 degree of latitude is about 111km.
    double d = distance_m(0.0, 0.0, 1.0, 0.0);
    REQUIRE(d > 110000.0);
    REQUIRE(d < 112000.0);
}

TEST_CASE("CommandValidator accepts a mission within the geofence", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;
    state.armed = false;

    auto cmd = upload_command({Waypoint{47.399, 8.547, 30.0f, 15.0f, std::nullopt}});
    auto result = validator.validate(cmd, state);

    REQUIRE(result.is_valid);
}

TEST_CASE("CommandValidator rejects a waypoint outside the geofence", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;

    // ~0.1 degrees away is roughly 11km, well past the 2000m radius.
    auto cmd = upload_command({Waypoint{47.497742, 8.545593, 30.0f, 15.0f, std::nullopt}});
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
    REQUIRE(result.reason.find("geofence") != std::string::npos);
}

TEST_CASE("CommandValidator rejects a waypoint above max altitude", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;

    auto cmd = upload_command({Waypoint{47.399, 8.547, 500.0f, 15.0f, std::nullopt}});
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
    REQUIRE(result.reason.find("altitude") != std::string::npos);
}

TEST_CASE("CommandValidator rejects UPLOAD_MISSION while armed", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;
    state.armed = true;

    auto cmd = upload_command({Waypoint{47.399, 8.547, 30.0f, 15.0f, std::nullopt}});
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
}

TEST_CASE("CommandValidator rejects START_MISSION with no mission uploaded", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;
    state.mission_uploaded = false;

    Command cmd;
    cmd.command_type = CommandType::StartMission;
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
}

TEST_CASE("CommandValidator accepts START_MISSION with a mission uploaded", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;
    state.mission_uploaded = true;

    Command cmd;
    cmd.command_type = CommandType::StartMission;
    auto result = validator.validate(cmd, state);

    REQUIRE(result.is_valid);
}

TEST_CASE("CommandValidator rejects RETURN_TO_LAUNCH when not connected/armed context missing", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;
    state.armed = false;

    Command cmd;
    cmd.command_type = CommandType::ReturnToLaunch;
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
}

TEST_CASE("CommandValidator accepts RETURN_TO_LAUNCH while armed", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;
    state.armed = true;

    Command cmd;
    cmd.command_type = CommandType::ReturnToLaunch;
    auto result = validator.validate(cmd, state);

    REQUIRE(result.is_valid);
}

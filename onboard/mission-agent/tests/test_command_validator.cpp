#include <catch2/catch_test_macros.hpp>
#include "../src/command_validator.h"
#include <cstdio>
#include <ctime>

using namespace mission_agent;

namespace {
GeofenceConfig test_geofence() {
    return GeofenceConfig{47.397742, 8.545593, 2000.0, 10.0f, 120.0f};
}

// A real "now" timestamp, in the format the real backend sends
// (datetime.now(timezone.utc).isoformat()) -- must stay fresh, since
// CommandValidator now rejects stale timestamps via ReplayGuard.
std::string now_timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d+00:00", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

Command upload_command(std::vector<Waypoint> waypoints, std::string command_id = "id-1") {
    Command cmd;
    cmd.command_id = std::move(command_id);
    cmd.aircraft_id = "aerolink-1";
    cmd.timestamp = now_timestamp();
    cmd.command_type = CommandType::UploadMission;
    cmd.mission_version = 1;
    cmd.waypoints = std::move(waypoints);
    return cmd;
}

// A command of the given type addressed to this aircraft, with no waypoints.
Command command_of(CommandType type, std::string command_id = "id-1") {
    Command cmd;
    cmd.command_id = std::move(command_id);
    cmd.aircraft_id = "aerolink-1";
    cmd.timestamp = now_timestamp();
    cmd.command_type = type;
    cmd.mission_version = 1;
    return cmd;
}

// PX4 reporting a good global position estimate and a settled home
// position -- the normal state once SITL has a GPS fix.
StateSnapshot settled_state() {
    StateSnapshot state;
    state.is_global_position_ok = true;
    state.is_home_position_ok = true;
    return state;
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
    StateSnapshot state = settled_state();
    state.armed = false;

    auto cmd = upload_command({Waypoint{47.399, 8.547, 30.0f, 15.0f, std::nullopt}});
    auto result = validator.validate(cmd, state);

    REQUIRE(result.is_valid);
}

TEST_CASE("CommandValidator rejects a waypoint outside the geofence", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();

    // ~0.1 degrees away is roughly 11km, well past the 2000m radius.
    auto cmd = upload_command({Waypoint{47.497742, 8.545593, 30.0f, 15.0f, std::nullopt}});
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
    REQUIRE(result.reason.find("geofence") != std::string::npos);
}

TEST_CASE("CommandValidator rejects a waypoint above max altitude", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();

    auto cmd = upload_command({Waypoint{47.399, 8.547, 500.0f, 15.0f, std::nullopt}});
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
    REQUIRE(result.reason.find("altitude") != std::string::npos);
}

TEST_CASE("CommandValidator rejects UPLOAD_MISSION while armed", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();
    state.armed = true;

    auto cmd = upload_command({Waypoint{47.399, 8.547, 30.0f, 15.0f, std::nullopt}});
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
}

TEST_CASE("CommandValidator rejects START_MISSION with no mission uploaded", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();
    state.mission_uploaded = false;

    Command cmd = command_of(CommandType::StartMission);
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
}

TEST_CASE("CommandValidator accepts START_MISSION with a mission uploaded", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();
    state.mission_uploaded = true;

    Command cmd = command_of(CommandType::StartMission);
    auto result = validator.validate(cmd, state);

    REQUIRE(result.is_valid);
}

TEST_CASE("CommandValidator rejects RETURN_TO_LAUNCH when not connected/armed context missing", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();
    state.armed = false;

    Command cmd = command_of(CommandType::ReturnToLaunch);
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
}

TEST_CASE("CommandValidator accepts RETURN_TO_LAUNCH while armed", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();
    state.armed = true;

    Command cmd = command_of(CommandType::ReturnToLaunch);
    auto result = validator.validate(cmd, state);

    REQUIRE(result.is_valid);
}

TEST_CASE("CommandValidator rejects UPLOAD_MISSION before the global position estimate is good",
          "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();
    state.is_global_position_ok = false;

    auto cmd = upload_command({Waypoint{47.399, 8.547, 30.0f, 15.0f, std::nullopt}});
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
    REQUIRE(result.reason.find("global position") != std::string::npos);
}

TEST_CASE("CommandValidator rejects UPLOAD_MISSION before the home position has settled",
          "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();
    state.is_home_position_ok = false;

    auto cmd = upload_command({Waypoint{47.399, 8.547, 30.0f, 15.0f, std::nullopt}});
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
    REQUIRE(result.reason.find("home position") != std::string::npos);
}

TEST_CASE("CommandValidator rejects START_MISSION before the global position estimate is good",
          "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();
    state.mission_uploaded = true;
    state.is_global_position_ok = false;

    Command cmd = command_of(CommandType::StartMission);
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
    REQUIRE(result.reason.find("global position") != std::string::npos);
}

TEST_CASE("CommandValidator rejects START_MISSION before the home position has settled",
          "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();
    state.mission_uploaded = true;
    state.is_home_position_ok = false;

    Command cmd = command_of(CommandType::StartMission);
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
    REQUIRE(result.reason.find("home position") != std::string::npos);
}

TEST_CASE("CommandValidator accepts START_MISSION once position and home have settled",
          "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();
    state.mission_uploaded = true;

    Command cmd = command_of(CommandType::StartMission);

    REQUIRE(validator.validate(cmd, state).is_valid);
}

TEST_CASE("CommandValidator rejects a command addressed to a different aircraft",
          "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();

    auto cmd = upload_command({Waypoint{47.399, 8.547, 30.0f, 15.0f, std::nullopt}});
    cmd.aircraft_id = "someone-elses-drone";
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
    REQUIRE(result.reason.find("aircraft_id") != std::string::npos);
}

TEST_CASE("CommandValidator honors a non-default expected aircraft_id", "[command_validator]") {
    CommandValidator validator(test_geofence(), "aerolink-2");
    StateSnapshot state = settled_state();

    auto cmd = upload_command({Waypoint{47.399, 8.547, 30.0f, 15.0f, std::nullopt}});
    REQUIRE_FALSE(validator.validate(cmd, state).is_valid);  // addressed to aerolink-1

    cmd.aircraft_id = "aerolink-2";
    REQUIRE(validator.validate(cmd, state).is_valid);
}

TEST_CASE("CommandValidator rejects a stale command timestamp", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();
    state.mission_uploaded = true;

    Command cmd = command_of(CommandType::StartMission);
    cmd.timestamp = "2020-01-01T00:00:00Z";
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
    REQUIRE(result.reason.find("freshness window") != std::string::npos);
}

TEST_CASE("CommandValidator rejects a replayed command_id", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state = settled_state();
    state.mission_uploaded = true;

    // Two otherwise-independent, individually-valid commands sharing a
    // command_id -- the second must be rejected as a replay even though
    // nothing else about it is wrong.
    Command first = command_of(CommandType::StartMission, "dup-1");
    REQUIRE(validator.validate(first, state).is_valid);

    Command second = command_of(CommandType::ReturnToLaunch, "dup-1");
    state.armed = true;
    auto result = validator.validate(second, state);

    REQUIRE_FALSE(result.is_valid);
    REQUIRE(result.reason.find("already processed") != std::string::npos);
}

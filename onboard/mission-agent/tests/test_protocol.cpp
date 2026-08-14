#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include "../src/protocol.h"

using namespace mission_agent;

TEST_CASE("parse_command parses a valid UPLOAD_MISSION command", "[protocol]") {
    std::string line = R"({
        "command_id": "abc-123",
        "aircraft_id": "aerolink-1",
        "timestamp": "2026-08-14T10:00:00Z",
        "command_type": "UPLOAD_MISSION",
        "parameters": {
            "waypoints": [
                {"latitude_deg": 47.4, "longitude_deg": 8.5, "altitude_m": 30.0, "speed_m_s": 15.0}
            ]
        },
        "mission_version": 1
    })";

    Command cmd = parse_command(line);

    REQUIRE(cmd.command_id == "abc-123");
    REQUIRE(cmd.aircraft_id == "aerolink-1");
    REQUIRE(cmd.command_type == CommandType::UploadMission);
    REQUIRE(cmd.mission_version == 1);
    REQUIRE(cmd.waypoints.size() == 1);
    REQUIRE(cmd.waypoints[0].latitude_deg == 47.4);
    REQUIRE(cmd.waypoints[0].speed_m_s.has_value());
    REQUIRE(cmd.waypoints[0].speed_m_s.value() == 15.0f);
}

TEST_CASE("parse_command parses a START_MISSION command with no waypoints", "[protocol]") {
    std::string line = R"({
        "command_id": "def-456",
        "aircraft_id": "aerolink-1",
        "timestamp": "2026-08-14T10:01:00Z",
        "command_type": "START_MISSION",
        "parameters": {},
        "mission_version": 1
    })";

    Command cmd = parse_command(line);

    REQUIRE(cmd.command_type == CommandType::StartMission);
    REQUIRE(cmd.waypoints.empty());
}

TEST_CASE("parse_command throws on malformed JSON", "[protocol]") {
    REQUIRE_THROWS_AS(parse_command("not json"), std::invalid_argument);
}

TEST_CASE("parse_command throws on unrecognized command_type", "[protocol]") {
    std::string line = R"({
        "command_id": "x", "aircraft_id": "y", "timestamp": "z",
        "command_type": "NOT_A_REAL_COMMAND", "parameters": {}, "mission_version": 1
    })";
    REQUIRE_THROWS_AS(parse_command(line), std::invalid_argument);
}

TEST_CASE("serialize_ack produces the expected JSON shape", "[protocol]") {
    Ack ack{"abc-123", AckStatus::Accepted, ""};
    std::string line = serialize_ack(ack);

    REQUIRE(line.back() == '\n');
    nlohmann::json parsed = nlohmann::json::parse(line);
    REQUIRE(parsed["command_id"] == "abc-123");
    REQUIRE(parsed["status"] == "ACCEPTED");
}

TEST_CASE("serialize_ack includes reason on REJECTED", "[protocol]") {
    Ack ack{"abc-123", AckStatus::Rejected, "outside geofence"};
    nlohmann::json parsed = nlohmann::json::parse(serialize_ack(ack));

    REQUIRE(parsed["status"] == "REJECTED");
    REQUIRE(parsed["reason"] == "outside geofence");
}

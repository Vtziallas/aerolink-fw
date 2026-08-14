#pragma once
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mission_agent {

enum class CommandType { UploadMission, StartMission, ReturnToLaunch };

struct Waypoint {
    double latitude_deg;
    double longitude_deg;
    float altitude_m;
    std::optional<float> speed_m_s;
    std::optional<float> loiter_duration_s;
};

struct Command {
    std::string command_id;
    std::string aircraft_id;
    std::string timestamp;
    CommandType command_type;
    std::vector<Waypoint> waypoints;  // only populated for UploadMission
    int mission_version;
};

enum class AckStatus { Received, Validated, Accepted, Rejected, Px4ActionStarted };

struct Ack {
    std::string command_id;
    AckStatus status;
    std::string reason;  // empty unless Rejected
};

std::string to_string(AckStatus status);
std::string to_string(CommandType type);
CommandType command_type_from_string(const std::string& s);

// Parses one line of newline-delimited JSON into a Command.
// Throws std::invalid_argument on malformed/unrecognized input.
Command parse_command(const std::string& json_line);

// Serializes an Ack to one line of JSON, including the trailing '\n'.
std::string serialize_ack(const Ack& ack);

}  // namespace mission_agent

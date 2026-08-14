#include "protocol.h"
#include <nlohmann/json.hpp>

namespace mission_agent {

using nlohmann::json;

std::string to_string(AckStatus status) {
    switch (status) {
        case AckStatus::Received: return "RECEIVED";
        case AckStatus::Validated: return "VALIDATED";
        case AckStatus::Accepted: return "ACCEPTED";
        case AckStatus::Rejected: return "REJECTED";
        case AckStatus::Px4ActionStarted: return "PX4_ACTION_STARTED";
    }
    throw std::invalid_argument("unknown AckStatus");
}

std::string to_string(CommandType type) {
    switch (type) {
        case CommandType::UploadMission: return "UPLOAD_MISSION";
        case CommandType::StartMission: return "START_MISSION";
        case CommandType::ReturnToLaunch: return "RETURN_TO_LAUNCH";
    }
    throw std::invalid_argument("unknown CommandType");
}

CommandType command_type_from_string(const std::string& s) {
    if (s == "UPLOAD_MISSION") return CommandType::UploadMission;
    if (s == "START_MISSION") return CommandType::StartMission;
    if (s == "RETURN_TO_LAUNCH") return CommandType::ReturnToLaunch;
    throw std::invalid_argument("unrecognized command_type: " + s);
}

Command parse_command(const std::string& json_line) {
    json j;
    try {
        j = json::parse(json_line);
    } catch (const json::parse_error& e) {
        throw std::invalid_argument(std::string("malformed JSON: ") + e.what());
    }

    Command cmd;
    cmd.command_id = j.at("command_id").get<std::string>();
    cmd.aircraft_id = j.at("aircraft_id").get<std::string>();
    cmd.timestamp = j.at("timestamp").get<std::string>();
    cmd.command_type = command_type_from_string(j.at("command_type").get<std::string>());
    cmd.mission_version = j.at("mission_version").get<int>();

    if (j.contains("parameters") && j["parameters"].contains("waypoints")) {
        for (const auto& wp_json : j["parameters"]["waypoints"]) {
            Waypoint wp;
            wp.latitude_deg = wp_json.at("latitude_deg").get<double>();
            wp.longitude_deg = wp_json.at("longitude_deg").get<double>();
            wp.altitude_m = wp_json.at("altitude_m").get<float>();
            if (wp_json.contains("speed_m_s") && !wp_json["speed_m_s"].is_null()) {
                wp.speed_m_s = wp_json["speed_m_s"].get<float>();
            }
            if (wp_json.contains("loiter_duration_s") && !wp_json["loiter_duration_s"].is_null()) {
                wp.loiter_duration_s = wp_json["loiter_duration_s"].get<float>();
            }
            cmd.waypoints.push_back(wp);
        }
    }

    return cmd;
}

std::string serialize_ack(const Ack& ack) {
    json j;
    j["command_id"] = ack.command_id;
    j["status"] = to_string(ack.status);
    j["reason"] = ack.reason;
    return j.dump() + "\n";
}

}  // namespace mission_agent

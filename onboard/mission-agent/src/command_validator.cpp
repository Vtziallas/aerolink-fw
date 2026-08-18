#include "command_validator.h"
#include <cmath>

namespace mission_agent {

namespace {
constexpr double kEarthRadiusM = 6371000.0;
constexpr double kPi = 3.14159265358979323846;

double to_radians(double degrees) { return degrees * kPi / 180.0; }
}  // namespace

double distance_m(double lat1, double lon1, double lat2, double lon2) {
    double phi1 = to_radians(lat1);
    double phi2 = to_radians(lat2);
    double d_phi = to_radians(lat2 - lat1);
    double d_lambda = to_radians(lon2 - lon1);

    double a = std::sin(d_phi / 2) * std::sin(d_phi / 2) +
               std::cos(phi1) * std::cos(phi2) * std::sin(d_lambda / 2) * std::sin(d_lambda / 2);
    return 2 * kEarthRadiusM * std::asin(std::sqrt(a));
}

CommandValidator::CommandValidator(GeofenceConfig geofence) : geofence_(geofence) {}

ValidationResult CommandValidator::validate(const Command& command, const StateSnapshot& state) const {
    switch (command.command_type) {
        case CommandType::UploadMission:
            return validate_upload_mission(command, state);
        case CommandType::StartMission:
            return validate_start_mission(state);
        case CommandType::ReturnToLaunch:
            return validate_return_to_launch(state);
    }
    return ValidationResult{false, "unknown command type"};
}

ValidationResult CommandValidator::validate_upload_mission(const Command& command, const StateSnapshot& state) const {
    if (state.armed) {
        return ValidationResult{false, "cannot upload a mission while armed"};
    }
    if (command.waypoints.empty()) {
        return ValidationResult{false, "mission must contain at least one waypoint"};
    }
    for (size_t i = 0; i < command.waypoints.size(); ++i) {
        const auto& wp = command.waypoints[i];
        if (wp.altitude_m < geofence_.min_altitude_m || wp.altitude_m > geofence_.max_altitude_m) {
            return ValidationResult{false, "waypoint " + std::to_string(i) + ": altitude outside allowed range"};
        }
        double d = distance_m(geofence_.center_lat_deg, geofence_.center_lon_deg, wp.latitude_deg, wp.longitude_deg);
        if (d > geofence_.radius_m) {
            return ValidationResult{false, "waypoint " + std::to_string(i) + ": outside geofence"};
        }
    }
    return ValidationResult{true, ""};
}

ValidationResult CommandValidator::validate_start_mission(const StateSnapshot& state) const {
    if (!state.mission_uploaded) {
        return ValidationResult{false, "no mission uploaded"};
    }
    return ValidationResult{true, ""};
}

ValidationResult CommandValidator::validate_return_to_launch(const StateSnapshot& state) const {
    if (!state.armed) {
        return ValidationResult{false, "vehicle is not armed"};
    }
    return ValidationResult{true, ""};
}

}  // namespace mission_agent

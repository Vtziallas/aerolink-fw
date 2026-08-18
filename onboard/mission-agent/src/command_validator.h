#pragma once
#include <string>
#include "protocol.h"
#include "state_tracker.h"

namespace mission_agent {

struct GeofenceConfig {
    double center_lat_deg;
    double center_lon_deg;
    double radius_m;
    float min_altitude_m;
    float max_altitude_m;
};

struct ValidationResult {
    bool is_valid;
    std::string reason;  // empty if is_valid
};

// Haversine distance in meters -- mirrors app/mission.py's _distance_m.
double distance_m(double lat1, double lon1, double lat2, double lon2);

class CommandValidator {
public:
    explicit CommandValidator(GeofenceConfig geofence);

    ValidationResult validate(const Command& command, const StateSnapshot& state) const;

private:
    GeofenceConfig geofence_;

    ValidationResult validate_upload_mission(const Command& command, const StateSnapshot& state) const;
    ValidationResult validate_start_mission(const StateSnapshot& state) const;
    ValidationResult validate_return_to_launch(const StateSnapshot& state) const;
};

}  // namespace mission_agent

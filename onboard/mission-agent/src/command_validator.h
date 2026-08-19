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

// Default aircraft identity. Matches MissionAgentClient.send_command's
// default aircraft_id in ground-station/backend/app/mission_agent_client.py.
inline constexpr const char* kDefaultAircraftId = "aerolink-1";

class CommandValidator {
public:
    // expected_aircraft_id is the identity half of the design doc's
    // "identity and schema" validation: any command addressed to a
    // different aircraft is rejected outright. Timestamp/replay checking is
    // deliberately NOT implemented -- the design doc puts replay protection
    // explicitly out of scope for V1 (and NETWORKING.md's Status section
    // says the same).
    explicit CommandValidator(GeofenceConfig geofence,
                              std::string expected_aircraft_id = kDefaultAircraftId);

    ValidationResult validate(const Command& command, const StateSnapshot& state) const;

private:
    GeofenceConfig geofence_;
    std::string expected_aircraft_id_;

    ValidationResult validate_upload_mission(const Command& command, const StateSnapshot& state) const;
    ValidationResult validate_start_mission(const StateSnapshot& state) const;
    ValidationResult validate_return_to_launch(const StateSnapshot& state) const;
};

}  // namespace mission_agent

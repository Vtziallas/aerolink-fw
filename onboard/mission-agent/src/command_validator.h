#pragma once
#include <string>
#include "protocol.h"
#include "replay_guard.h"
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
    // different aircraft is rejected outright. replay_guard rejects stale
    // or previously-seen command_id/timestamp pairs (see replay_guard.h);
    // it's checked after identity so a command rejected for the wrong
    // aircraft_id never burns its command_id in the replay cache.
    explicit CommandValidator(GeofenceConfig geofence,
                              std::string expected_aircraft_id = kDefaultAircraftId,
                              ReplayGuard replay_guard = ReplayGuard{});

    // Non-const: validation now has a side effect (recording command_id as
    // seen in replay_guard_) rather than being a pure function of its
    // arguments.
    ValidationResult validate(const Command& command, const StateSnapshot& state);

private:
    GeofenceConfig geofence_;
    std::string expected_aircraft_id_;
    ReplayGuard replay_guard_;

    ValidationResult validate_upload_mission(const Command& command, const StateSnapshot& state) const;
    ValidationResult validate_start_mission(const StateSnapshot& state) const;
    ValidationResult validate_return_to_launch(const StateSnapshot& state) const;
};

}  // namespace mission_agent

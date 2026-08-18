#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include "agent_server.h"
#include "command_validator.h"
#include "mavlink_connection.h"
#include "state_tracker.h"
#include "telemetry_publisher.h"

namespace {
std::string env_or(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : fallback;
}

double env_or_double(const char* name, double fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::stod(value) : fallback;
}
}  // namespace

int main() {
    // Same defaults as ground-station/backend/app/config.py, overridable
    // the same way (env vars), so both sides of the system stay in sync
    // without hand-editing two files.
    mission_agent::GeofenceConfig geofence{
        env_or_double("GEOFENCE_CENTER_LAT_DEG", 47.397742),
        env_or_double("GEOFENCE_CENTER_LON_DEG", 8.545593),
        env_or_double("GEOFENCE_RADIUS_M", 2000.0),
        static_cast<float>(env_or_double("MIN_ALTITUDE_M", 10.0)),
        static_cast<float>(env_or_double("MAX_ALTITUDE_M", 120.0)),
    };

    std::string mavlink_url = env_or("MAVLINK_URL", "udp://:14540");
    int agent_port = static_cast<int>(env_or_double("AGENT_PORT", 5760));

    std::cout << "mission_agent: connecting to PX4 at " << mavlink_url << "...\n";

    mission_agent::StateTracker state_tracker;
    mission_agent::MavlinkConnection mavlink(mavlink_url, state_tracker);
    std::cout << "mission_agent: connected to PX4\n";

    mission_agent::CommandValidator validator(geofence);
    mission_agent::AgentServer server(agent_port, validator, mavlink, state_tracker);

    mission_agent::TelemetryPublisher telemetry_publisher(state_tracker, server, std::chrono::milliseconds(250));
    telemetry_publisher.start();

    std::cout << "mission_agent: serving on port " << agent_port << "\n";
    server.run();  // blocks

    telemetry_publisher.stop();
    return 0;
}

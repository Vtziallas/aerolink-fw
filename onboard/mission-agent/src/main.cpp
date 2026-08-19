#include <csignal>
#include <chrono>
#include <cstdlib>
#include <exception>
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

namespace {

int run() {
    // Same defaults, and the same env var names, as
    // ground-station/backend/app/config.py. Note that keeping the two sides
    // in sync is NOT automatic: the backend and the agent are launched by
    // two different scripts (simulation/network/launch-ground.sh and
    // launch-aircraft.sh), so overriding the geofence means setting these
    // vars in BOTH scripts. Both scripts pass all five explicitly, in one
    // obvious block each, so there is exactly one place per script to
    // change -- but there are still two places, not one.
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

}  // namespace

int main() {
    // A backend that disappears mid-write must never take the agent (and
    // with it the only thing PX4 accepts commands from) down with it.
    // Default SIGPIPE disposition terminates the process; AgentServer also
    // uses MSG_NOSIGNAL, this is the process-wide belt to that braces.
    std::signal(SIGPIPE, SIG_IGN);

    try {
        return run();
    } catch (const std::exception& e) {
        // Covers MavlinkConnection's connect-timeout throw (PX4 still
        // booting -- routine on WSL) and env_or_double's std::stod throw on
        // a malformed env var. Both used to abort with an unhandled
        // exception and no usable message.
        std::cerr << "mission_agent: fatal: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "mission_agent: fatal: unknown exception\n";
        return 1;
    }
}

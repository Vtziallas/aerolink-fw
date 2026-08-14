#pragma once
#include <mutex>
#include <string>

namespace mission_agent {

struct StateSnapshot {
    bool armed = false;
    std::string flight_mode = "UNKNOWN";
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    float relative_altitude_m = 0.0f;
    float battery_remaining_pct = 0.0f;
    bool is_global_position_ok = false;
    bool is_home_position_ok = false;
    int mission_current = 0;
    int mission_total = 0;
    bool mission_uploaded = false;
};

// Thread-safe holder for the latest known vehicle state. MAVSDK telemetry
// callbacks (on MAVSDK's own thread) call the setters; AgentServer and
// CommandValidator (on the TCP server thread) call snapshot().
class StateTracker {
public:
    StateSnapshot snapshot() const;

    void set_armed(bool armed);
    void set_flight_mode(std::string mode);
    void set_position(double lat, double lon, float rel_alt);
    void set_battery(float remaining_pct);
    void set_health(bool global_position_ok, bool home_position_ok);
    void set_mission_progress(int current, int total);
    void set_mission_uploaded(bool uploaded);

private:
    mutable std::mutex mutex_;
    StateSnapshot state_;
};

}  // namespace mission_agent

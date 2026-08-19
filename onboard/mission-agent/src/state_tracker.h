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
    float absolute_altitude_m = 0.0f;

    // Attitude, in the same units/sign convention MAVSDK's
    // Telemetry::EulerAngle uses.
    float roll_deg = 0.0f;
    float pitch_deg = 0.0f;
    float yaw_deg = 0.0f;

    // From MAVSDK's Telemetry::FixedwingMetrics -- the frontend's
    // TelemetryPanel renders airspeed/groundspeed and MapView rotates the
    // aircraft marker by heading_deg.
    float airspeed_m_s = 0.0f;
    float groundspeed_m_s = 0.0f;
    float heading_deg = 0.0f;

    float battery_remaining_pct = 0.0f;
    float battery_voltage_v = 0.0f;

    bool is_global_position_ok = false;
    bool is_home_position_ok = false;
    // PX4's own "can this be armed right now" verdict, mirrored rather than
    // recomputed -- see the design doc's "PX4 already is that state machine".
    bool is_armable = false;

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
    void set_position(double lat, double lon, float rel_alt, float abs_alt);
    void set_attitude(float roll_deg, float pitch_deg, float yaw_deg);
    void set_fixedwing_metrics(float airspeed_m_s, float groundspeed_m_s, float heading_deg);
    void set_battery(float remaining_pct, float voltage_v);
    void set_health(bool global_position_ok, bool home_position_ok);
    void set_armable(bool armable);
    void set_mission_progress(int current, int total);
    void set_mission_uploaded(bool uploaded);

private:
    mutable std::mutex mutex_;
    StateSnapshot state_;
};

}  // namespace mission_agent

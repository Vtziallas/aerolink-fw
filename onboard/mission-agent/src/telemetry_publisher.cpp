// onboard/mission-agent/src/telemetry_publisher.cpp
#include "telemetry_publisher.h"
#include <nlohmann/json.hpp>

namespace mission_agent {

TelemetryPublisher::TelemetryPublisher(StateTracker& state_tracker, ITelemetrySink& sink, std::chrono::milliseconds interval)
    : state_tracker_(state_tracker), sink_(sink), interval_(interval) {}

TelemetryPublisher::~TelemetryPublisher() { stop(); }

void TelemetryPublisher::start() {
    if (running_.exchange(true)) {
        return;  // already running, no-op
    }
    thread_ = std::thread([this]() {
        while (running_) {
            sink_.send_line(serialize_snapshot(state_tracker_.snapshot()));
            std::this_thread::sleep_for(interval_);
        }
    });
}

void TelemetryPublisher::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

std::string TelemetryPublisher::serialize_snapshot(const StateSnapshot& s) {
    nlohmann::json j;
    j["armed"] = s.armed;
    j["flight_mode"] = s.flight_mode;
    j["latitude_deg"] = s.latitude_deg;
    j["longitude_deg"] = s.longitude_deg;
    j["relative_altitude_m"] = s.relative_altitude_m;
    j["absolute_altitude_m"] = s.absolute_altitude_m;
    j["roll_deg"] = s.roll_deg;
    j["pitch_deg"] = s.pitch_deg;
    j["yaw_deg"] = s.yaw_deg;
    j["airspeed_m_s"] = s.airspeed_m_s;
    j["groundspeed_m_s"] = s.groundspeed_m_s;
    j["heading_deg"] = s.heading_deg;
    j["battery_remaining_pct"] = s.battery_remaining_pct;
    j["battery_voltage_v"] = s.battery_voltage_v;
    j["is_global_position_ok"] = s.is_global_position_ok;
    j["is_home_position_ok"] = s.is_home_position_ok;
    j["is_armable"] = s.is_armable;
    j["mission_current"] = s.mission_current;
    j["mission_total"] = s.mission_total;
    return j.dump();
}

}  // namespace mission_agent

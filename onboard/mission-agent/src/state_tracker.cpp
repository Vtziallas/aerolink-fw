#include "state_tracker.h"

namespace mission_agent {

StateSnapshot StateTracker::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void StateTracker::set_armed(bool armed) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.armed = armed;
}

void StateTracker::set_flight_mode(std::string mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.flight_mode = std::move(mode);
}

void StateTracker::set_position(double lat, double lon, float rel_alt, float abs_alt) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.latitude_deg = lat;
    state_.longitude_deg = lon;
    state_.relative_altitude_m = rel_alt;
    state_.absolute_altitude_m = abs_alt;
}

void StateTracker::set_attitude(float roll_deg, float pitch_deg, float yaw_deg) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.roll_deg = roll_deg;
    state_.pitch_deg = pitch_deg;
    state_.yaw_deg = yaw_deg;
}

void StateTracker::set_fixedwing_metrics(float airspeed_m_s, float groundspeed_m_s, float heading_deg) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.airspeed_m_s = airspeed_m_s;
    state_.groundspeed_m_s = groundspeed_m_s;
    state_.heading_deg = heading_deg;
}

void StateTracker::set_battery(float remaining_pct, float voltage_v) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.battery_remaining_pct = remaining_pct;
    state_.battery_voltage_v = voltage_v;
}

void StateTracker::set_health(bool global_position_ok, bool home_position_ok) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.is_global_position_ok = global_position_ok;
    state_.is_home_position_ok = home_position_ok;
}

void StateTracker::set_armable(bool armable) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.is_armable = armable;
}

void StateTracker::set_mission_progress(int current, int total) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.mission_current = current;
    state_.mission_total = total;
}

void StateTracker::set_mission_uploaded(bool uploaded) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.mission_uploaded = uploaded;
}

}  // namespace mission_agent

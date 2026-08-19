// onboard/mission-agent/src/mavlink_connection.cpp
#include "mavlink_connection.h"
#include <atomic>
#include <future>
#include <sstream>
#include <stdexcept>

namespace mission_agent {

namespace {

// MAVSDK 3.17.2 doesn't provide result_str()/flight_mode_str() free
// functions for these plugins -- only stream operator<< overloads. These
// helpers give us the same "human-readable enum" behavior the brief's
// original result_str()/flight_mode_str() calls were going for.
template <typename T>
std::string to_display_string(const T& value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

}  // namespace

MavlinkConnection::MavlinkConnection(const std::string& connection_url, StateTracker& state_tracker, int connect_timeout_s)
    : mavsdk_(mavsdk::Mavsdk::Configuration{mavsdk::ComponentType::CompanionComputer}),
      state_tracker_(state_tracker) {
    auto connection_result = mavsdk_.add_any_connection(connection_url);
    if (connection_result != mavsdk::ConnectionResult::Success) {
        throw std::runtime_error("failed to add MAVSDK connection: " + connection_url);
    }

    auto system_promise = std::promise<std::shared_ptr<mavsdk::System>>{};
    auto system_future = system_promise.get_future();
    std::atomic<bool> discovered{false};

    // The handle is filled in immediately after subscribe_on_new_system()
    // returns, so the callback (which only fires on system discovery, well
    // after this constructor call returns from subscribe_on_new_system) can
    // use it to unsubscribe itself once a system with an autopilot is found.
    mavsdk::Mavsdk::NewSystemHandle new_system_handle{};
    new_system_handle = mavsdk_.subscribe_on_new_system([this, &system_promise, &discovered, &new_system_handle]() {
        auto system = mavsdk_.systems().back();
        if (system->has_autopilot() && !discovered.exchange(true)) {
            mavsdk_.unsubscribe_on_new_system(new_system_handle);
            system_promise.set_value(system);
        }
    });

    if (system_future.wait_for(std::chrono::seconds(connect_timeout_s)) == std::future_status::timeout) {
        mavsdk_.unsubscribe_on_new_system(new_system_handle);
        throw std::runtime_error("no PX4 system discovered within " + std::to_string(connect_timeout_s) + "s");
    }
    system_ = system_future.get();

    action_ = std::make_unique<mavsdk::Action>(system_);
    telemetry_ = std::make_unique<mavsdk::Telemetry>(system_);
    mission_ = std::make_unique<mavsdk::Mission>(system_);

    subscribe_telemetry();
}

void MavlinkConnection::subscribe_telemetry() {
    telemetry_->subscribe_armed([this](bool armed) { state_tracker_.set_armed(armed); });

    telemetry_->subscribe_flight_mode([this](mavsdk::Telemetry::FlightMode mode) {
        state_tracker_.set_flight_mode(to_display_string(mode));
    });

    telemetry_->subscribe_position([this](mavsdk::Telemetry::Position position) {
        state_tracker_.set_position(position.latitude_deg, position.longitude_deg,
                                    position.relative_altitude_m, position.absolute_altitude_m);
    });

    telemetry_->subscribe_attitude_euler([this](mavsdk::Telemetry::EulerAngle attitude) {
        state_tracker_.set_attitude(attitude.roll_deg, attitude.pitch_deg, attitude.yaw_deg);
    });

    // FixedwingMetrics is where PX4 reports airspeed/groundspeed/heading for
    // this airframe (a QuadPlane VTOL -- see docs/adr/0002). These are the
    // fields the frontend's TelemetryPanel renders and MapView rotates the
    // aircraft marker with, so dropping them blanks real UI.
    telemetry_->subscribe_fixedwing_metrics([this](mavsdk::Telemetry::FixedwingMetrics metrics) {
        state_tracker_.set_fixedwing_metrics(metrics.airspeed_m_s, metrics.groundspeed_m_s,
                                             metrics.heading_deg);
    });

    telemetry_->subscribe_battery([this](mavsdk::Telemetry::Battery battery) {
        state_tracker_.set_battery(battery.remaining_percent, battery.voltage_v);
    });

    telemetry_->subscribe_health([this](mavsdk::Telemetry::Health health) {
        state_tracker_.set_health(health.is_global_position_ok, health.is_home_position_ok);
        state_tracker_.set_armable(health.is_armable);
    });

    mission_->subscribe_mission_progress([this](mavsdk::Mission::MissionProgress progress) {
        state_tracker_.set_mission_progress(progress.current, progress.total);
    });
}

bool MavlinkConnection::is_connected() const {
    return system_ != nullptr && system_->is_connected();
}

MavlinkResult MavlinkConnection::upload_mission(const std::vector<Waypoint>& waypoints) {
    std::vector<mavsdk::Mission::MissionItem> items;
    for (const auto& wp : waypoints) {
        mavsdk::Mission::MissionItem item;
        item.latitude_deg = wp.latitude_deg;
        item.longitude_deg = wp.longitude_deg;
        item.relative_altitude_m = wp.altitude_m;
        item.speed_m_s = wp.speed_m_s.value_or(15.0f);
        item.is_fly_through = true;
        item.loiter_time_s = wp.loiter_duration_s.value_or(0.0f);
        item.acceptance_radius_m = 10.0f;
        item.vehicle_action = mavsdk::Mission::MissionItem::VehicleAction::None;
        items.push_back(item);
    }
    // Auto-append a landing item at the last waypoint's location, matching
    // the existing backend behavior in app/vehicle.py's upload_mission --
    // PX4 fixed-wing/VTOL missions reject outright without one.
    if (!waypoints.empty()) {
        mavsdk::Mission::MissionItem land;
        land.latitude_deg = waypoints.back().latitude_deg;
        land.longitude_deg = waypoints.back().longitude_deg;
        land.relative_altitude_m = 0.0f;
        land.speed_m_s = 15.0f;
        land.is_fly_through = true;
        land.acceptance_radius_m = 10.0f;
        land.vehicle_action = mavsdk::Mission::MissionItem::VehicleAction::Land;
        items.push_back(land);
    }

    mavsdk::Mission::MissionPlan plan;
    plan.mission_items = items;

    auto result = mission_->upload_mission(plan);
    if (result != mavsdk::Mission::Result::Success) {
        return MavlinkResult{false, "mission upload failed: " + to_display_string(result)};
    }
    state_tracker_.set_mission_uploaded(true);
    return MavlinkResult{true, ""};
}

MavlinkResult MavlinkConnection::start_mission() {
    auto arm_result = action_->arm();
    if (arm_result != mavsdk::Action::Result::Success) {
        return MavlinkResult{false, "arm failed: " + to_display_string(arm_result)};
    }
    auto start_result = mission_->start_mission();
    if (start_result != mavsdk::Mission::Result::Success) {
        return MavlinkResult{false, "mission start failed: " + to_display_string(start_result)};
    }
    return MavlinkResult{true, ""};
}

MavlinkResult MavlinkConnection::return_to_launch() {
    auto result = action_->return_to_launch();
    if (result != mavsdk::Action::Result::Success) {
        return MavlinkResult{false, "return_to_launch failed: " + to_display_string(result)};
    }
    return MavlinkResult{true, ""};
}

}  // namespace mission_agent

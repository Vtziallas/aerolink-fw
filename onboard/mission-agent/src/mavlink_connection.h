// onboard/mission-agent/src/mavlink_connection.h
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <mavsdk/mavsdk.h>
#include <mavsdk/plugins/action/action.h>
#include <mavsdk/plugins/mission/mission.h>
#include <mavsdk/plugins/telemetry/telemetry.h>
#include "protocol.h"
#include "state_tracker.h"

namespace mission_agent {

struct MavlinkResult {
    bool success;
    std::string error_message;  // empty if success
};

class IMavlinkConnection {
public:
    virtual ~IMavlinkConnection() = default;
    virtual bool is_connected() const = 0;
    virtual MavlinkResult upload_mission(const std::vector<Waypoint>& waypoints) = 0;
    virtual MavlinkResult start_mission() = 0;
    virtual MavlinkResult return_to_launch() = 0;
};

// Wraps a MAVSDK connection to PX4's local onboard link. Connects in the
// constructor (blocks until a system with an autopilot is discovered, or
// throws std::runtime_error after connect_timeout_s). Wires MAVSDK
// telemetry callbacks into the given StateTracker for the lifetime of
// this object.
class MavlinkConnection : public IMavlinkConnection {
public:
    MavlinkConnection(const std::string& connection_url, StateTracker& state_tracker, int connect_timeout_s = 10);

    bool is_connected() const override;
    MavlinkResult upload_mission(const std::vector<Waypoint>& waypoints) override;
    MavlinkResult start_mission() override;
    MavlinkResult return_to_launch() override;

private:
    mavsdk::Mavsdk mavsdk_;
    std::shared_ptr<mavsdk::System> system_;
    std::unique_ptr<mavsdk::Action> action_;
    std::unique_ptr<mavsdk::Telemetry> telemetry_;
    std::unique_ptr<mavsdk::Mission> mission_;
    StateTracker& state_tracker_;

    void subscribe_telemetry();
};

}  // namespace mission_agent

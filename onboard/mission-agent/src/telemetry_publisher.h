// onboard/mission-agent/src/telemetry_publisher.h
#pragma once
#include <atomic>
#include <chrono>
#include <thread>
#include "agent_server.h"
#include "state_tracker.h"

namespace mission_agent {

// Pushes a JSON telemetry snapshot to an ITelemetrySink at a fixed
// interval, on its own thread, until stop() is called.
class TelemetryPublisher {
public:
    TelemetryPublisher(StateTracker& state_tracker, ITelemetrySink& sink, std::chrono::milliseconds interval);
    ~TelemetryPublisher();

    void start();
    void stop();

    static std::string serialize_snapshot(const StateSnapshot& snapshot);

private:
    StateTracker& state_tracker_;
    ITelemetrySink& sink_;
    std::chrono::milliseconds interval_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace mission_agent

// onboard/mission-agent/src/agent_server.h
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include "command_validator.h"
#include "mavlink_connection.h"
#include "protocol.h"
#include "state_tracker.h"

namespace mission_agent {

class ITelemetrySink {
public:
    virtual ~ITelemetrySink() = default;
    virtual void send_line(const std::string& line) = 0;
};

// Single-connection TCP server: accepts one backend connection at a time,
// reads newline-delimited JSON commands, validates+dispatches them, and
// writes back the ack chain. run() blocks -- call it from a dedicated
// thread. Also serves as the ITelemetrySink that TelemetryPublisher
// (Task 7) writes periodic telemetry snapshots to.
class AgentServer : public ITelemetrySink {
public:
    AgentServer(int port, CommandValidator& validator, IMavlinkConnection& mavlink, StateTracker& state_tracker);
    ~AgentServer() override;

    void run();
    void stop();

    void send_line(const std::string& line) override;

private:
    int port_;
    int listen_fd_ = -1;
    int client_fd_ = -1;
    std::atomic<bool> running_{false};
    mutable std::mutex client_mutex_;
    CommandValidator& validator_;
    IMavlinkConnection& mavlink_;
    StateTracker& state_tracker_;

    void handle_connection(int client_fd);
    void send_ack(int client_fd, const Ack& ack);
};

}  // namespace mission_agent

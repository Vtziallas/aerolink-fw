#include <catch2/catch_test_macros.hpp>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <thread>
#include "../src/agent_server.h"
#include "../src/command_validator.h"
#include "../src/mavlink_connection.h"
#include "../src/state_tracker.h"

using namespace mission_agent;

namespace {

class FakeMavlinkConnection : public IMavlinkConnection {
public:
    bool is_connected() const override { return true; }
    MavlinkResult upload_mission(const std::vector<Waypoint>&) override {
        return upload_result;
    }
    MavlinkResult start_mission() override { return start_result; }
    MavlinkResult return_to_launch() override { return rtl_result; }

    MavlinkResult upload_result{true, ""};
    MavlinkResult start_result{true, ""};
    MavlinkResult rtl_result{true, ""};
};

GeofenceConfig test_geofence() {
    return GeofenceConfig{47.397742, 8.545593, 2000.0, 10.0f, 120.0f};
}

// Connects to 127.0.0.1:port with a short retry loop, since the server
// thread may not have called listen() yet.
int connect_with_retry(int port) {
    for (int attempt = 0; attempt < 50; ++attempt) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            return fd;
        }
        close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    throw std::runtime_error("could not connect to test server");
}

std::string read_line(int fd) {
    std::string line;
    char c;
    while (read(fd, &c, 1) == 1) {
        if (c == '\n') break;
        line += c;
    }
    return line;
}

}  // namespace

TEST_CASE("AgentServer sends the full ack chain for an accepted START_MISSION", "[agent_server]") {
    CommandValidator validator(test_geofence());
    FakeMavlinkConnection mavlink;
    StateTracker tracker;
    tracker.set_mission_uploaded(true);

    AgentServer server(15551, validator, mavlink, tracker);
    std::thread server_thread([&server]() { server.run(); });

    int client_fd = connect_with_retry(15551);
    std::string command = R"({"command_id":"c1","aircraft_id":"a1","timestamp":"t","command_type":"START_MISSION","parameters":{},"mission_version":1})";
    command += "\n";
    write(client_fd, command.c_str(), command.size());

    std::vector<std::string> statuses;
    for (int i = 0; i < 4; ++i) {
        statuses.push_back(read_line(client_fd));
    }

    REQUIRE(statuses[0].find("RECEIVED") != std::string::npos);
    REQUIRE(statuses[1].find("VALIDATED") != std::string::npos);
    REQUIRE(statuses[2].find("ACCEPTED") != std::string::npos);
    REQUIRE(statuses[3].find("PX4_ACTION_STARTED") != std::string::npos);

    close(client_fd);
    server.stop();
    server_thread.join();
}

TEST_CASE("AgentServer rejects START_MISSION with no mission uploaded and stops the chain", "[agent_server]") {
    CommandValidator validator(test_geofence());
    FakeMavlinkConnection mavlink;
    StateTracker tracker;  // mission_uploaded defaults to false

    AgentServer server(15552, validator, mavlink, tracker);
    std::thread server_thread([&server]() { server.run(); });

    int client_fd = connect_with_retry(15552);
    std::string command = R"({"command_id":"c2","aircraft_id":"a1","timestamp":"t","command_type":"START_MISSION","parameters":{},"mission_version":1})";
    command += "\n";
    write(client_fd, command.c_str(), command.size());

    std::string first = read_line(client_fd);
    std::string second = read_line(client_fd);

    REQUIRE(first.find("RECEIVED") != std::string::npos);
    REQUIRE(second.find("REJECTED") != std::string::npos);
    REQUIRE(second.find("no mission uploaded") != std::string::npos);

    close(client_fd);
    server.stop();
    server_thread.join();
}

TEST_CASE("AgentServer send_line reaches a connected client via the telemetry sink interface", "[agent_server]") {
    CommandValidator validator(test_geofence());
    FakeMavlinkConnection mavlink;
    StateTracker tracker;

    AgentServer server(15553, validator, mavlink, tracker);
    std::thread server_thread([&server]() { server.run(); });

    int client_fd = connect_with_retry(15553);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // let accept() register the client

    ITelemetrySink& sink = server;
    sink.send_line(R"({"latitude_deg":47.4}
)");

    std::string line = read_line(client_fd);
    REQUIRE(line.find("47.4") != std::string::npos);

    close(client_fd);
    server.stop();
    server_thread.join();
}

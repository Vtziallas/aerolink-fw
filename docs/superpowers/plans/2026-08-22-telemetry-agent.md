# Telemetry Agent Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Telemetry Agent, the second of the three onboard companion services, replacing the Mission Agent's minimal `TelemetryPublisher` stand-in with a real rate-classed telemetry mux running as its own independent process.

**Architecture:** A new `onboard/telemetry-agent/` C++/MAVSDK CMake project connects to PX4's second onboard MAVLink instance (`udp://:14030`, the "camera" link, independent of the Mission Agent's `udp://:14540`), routes updates through a 3-tier `RateClassMux` (critical/event FIFO queue, position and bulk single-slot coalescing buffers), and pushes newline-delimited JSON to the backend over a new dedicated TCP port. The backend gets a new `TelemetryAgentClient` feeding the existing `/ws/telemetry` WebSocket. The Mission Agent's `TelemetryPublisher` is deleted.

**Tech Stack:** C++17, CMake, MAVSDK, nlohmann/json, Catch2 (agent); Python, pytest-asyncio (backend).

**Spec:** `docs/architecture/telemetry-agent-design.md`

## Global Constraints

- C++ standard: 17 (`CMAKE_CXX_STANDARD 17`, `CMAKE_CXX_STANDARD_REQUIRED ON`), matching `onboard/mission-agent/CMakeLists.txt`.
- `-Wall -Wextra` on every C++ target (`MISSION_AGENT_WARNINGS`-style variable), matching the Mission Agent.
- JSON library: nlohmann/json v3.11.3 via `FetchContent`, matching the Mission Agent exactly.
- Test framework: Catch2 v3.6.0 via `FetchContent` for C++; `pytest-asyncio` for Python (already a backend dependency).
- No shared library between `onboard/mission-agent/` and `onboard/telemetry-agent/` -- each owns its own MAVSDK connection code, even where it duplicates a similar shape (see design doc's "No shared library" consequence).
- Default aircraft geofence env vars (`GEOFENCE_CENTER_LAT_DEG`, `GEOFENCE_CENTER_LON_DEG`, `GEOFENCE_RADIUS_M`, `MIN_ALTITUDE_M`, `MAX_ALTITUDE_M`) are NOT needed by the Telemetry Agent -- it doesn't validate anything, only observes. Don't add them.
- No AI-authorship trailers in commit messages (repo-specific convention).
- Every commit's tests must pass before moving to the next task.

---

## Task 1: Scaffold the `onboard/telemetry-agent/` CMake project

**Files:**
- Create: `onboard/telemetry-agent/CMakeLists.txt`
- Create: `onboard/telemetry-agent/tests/CMakeLists.txt`
- Create: `onboard/telemetry-agent/src/main.cpp`
- Create: `onboard/telemetry-agent/tests/test_smoke.cpp`

**Interfaces:**
- Produces: a buildable `telemetry_agent` executable target and a `telemetry_agent_tests` test executable target, both empty/minimal, ready for later tasks to add sources to.

- [ ] **Step 1: Write `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.16)
project(telemetry_agent CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(MAVSDK REQUIRED)
find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

set(TELEMETRY_AGENT_WARNINGS -Wall -Wextra)

add_executable(telemetry_agent src/main.cpp)
target_link_libraries(telemetry_agent PRIVATE nlohmann_json::nlohmann_json Threads::Threads MAVSDK::mavsdk)
target_compile_options(telemetry_agent PRIVATE ${TELEMETRY_AGENT_WARNINGS})

enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 2: Write `tests/CMakeLists.txt`**

```cmake
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.6.0
)
FetchContent_MakeAvailable(Catch2)

find_package(Threads REQUIRED)

add_executable(telemetry_agent_tests test_smoke.cpp)
target_link_libraries(telemetry_agent_tests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json Threads::Threads MAVSDK::mavsdk)
target_compile_options(telemetry_agent_tests PRIVATE ${TELEMETRY_AGENT_WARNINGS})

include(CTest)
include(Catch)
catch_discover_tests(telemetry_agent_tests)
```

- [ ] **Step 3: Write `src/main.cpp`** (placeholder wiring, replaced fully in Task 5)

```cpp
// onboard/telemetry-agent/src/main.cpp
#include <iostream>

int main() {
    std::cout << "telemetry_agent: scaffold\n";
    return 0;
}
```

- [ ] **Step 4: Write `tests/test_smoke.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("smoke", "[smoke]") {
    REQUIRE(1 + 1 == 2);
}
```

- [ ] **Step 5: Configure and build**

Run:
```bash
cd /mnt/c/Users/youruser/git/uav_project/onboard/telemetry-agent
mkdir -p build && cd build
cmake ..
cmake --build . -j4
```
Expected: both `telemetry_agent` and `telemetry_agent_tests` build successfully.

- [ ] **Step 6: Run the smoke test**

Run: `./tests/telemetry_agent_tests`
Expected: `All tests passed (1 assertion in 1 test case)`

- [ ] **Step 7: Commit**

```bash
git add onboard/telemetry-agent/
git commit -m "Scaffold telemetry-agent CMake project"
```

---

## Task 2: `RateClassMux` -- critical/event tier

**Files:**
- Create: `onboard/telemetry-agent/src/rate_class_mux.h`
- Create: `onboard/telemetry-agent/src/rate_class_mux.cpp`
- Test: `onboard/telemetry-agent/tests/test_rate_class_mux.cpp`
- Modify: `onboard/telemetry-agent/tests/CMakeLists.txt` (add the two new sources)

**Interfaces:**
- Produces: `telemetry_agent::RateClassMux` with `push_critical_event(std::string field, nlohmann::json value)` and `std::optional<nlohmann::json> pop_critical_event()`. Later tasks (3, 4) depend on these exact names.

- [ ] **Step 1: Write the header**

```cpp
// onboard/telemetry-agent/src/rate_class_mux.h
#pragma once
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace telemetry_agent {

struct PositionSnapshot {
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    float relative_altitude_m = 0.0f;
    float absolute_altitude_m = 0.0f;
    float roll_deg = 0.0f;
    float pitch_deg = 0.0f;
    float yaw_deg = 0.0f;
};

struct BulkSnapshot {
    float battery_voltage_v = 0.0f;
    float battery_remaining_pct = 0.0f;
    float airspeed_m_s = 0.0f;
    float groundspeed_m_s = 0.0f;
    float heading_deg = 0.0f;
};

// Routes MAVSDK telemetry updates into three independently-behaved tiers:
// critical events are queued (bounded FIFO, nothing coalesced, everything
// individually delivered); position and bulk samples are coalesced into a
// single latest-known snapshot each (a new sample overwrites the pending
// one -- there is no value in ever delivering a stale intermediate position
// or battery reading once a newer one exists). See
// docs/architecture/telemetry-agent-design.md's Rate Classes section.
class RateClassMux {
public:
    explicit RateClassMux(size_t critical_queue_depth = 200);

    // --- Producer side: called from MAVSDK telemetry callbacks ---
    void push_critical_event(const std::string& field, nlohmann::json value);
    void update_position(double latitude_deg, double longitude_deg, float relative_altitude_m,
                         float absolute_altitude_m, float roll_deg, float pitch_deg, float yaw_deg);
    void update_bulk(float battery_voltage_v, float battery_remaining_pct, float airspeed_m_s,
                     float groundspeed_m_s, float heading_deg);

    // --- Consumer side: called from TelemetryServer's drain loop ---
    // FIFO pop of the next queued critical event as a ready-to-send JSON
    // object (already includes "class"/"field"/"value"/"timestamp").
    // nullopt if the queue is empty.
    std::optional<nlohmann::json> pop_critical_event();
    // Always returns the current latest-known snapshot as a ready-to-send
    // JSON object (all zero-valued fields before the first update arrives).
    nlohmann::json latest_position_json() const;
    nlohmann::json latest_bulk_json() const;

private:
    mutable std::mutex mutex_;
    size_t critical_queue_depth_;
    std::deque<nlohmann::json> critical_queue_;
    PositionSnapshot position_;
    BulkSnapshot bulk_;

    static std::string now_iso8601();
};

}  // namespace telemetry_agent
```

- [ ] **Step 2: Write the failing test for critical-tier FIFO behavior**

```cpp
// onboard/telemetry-agent/tests/test_rate_class_mux.cpp
#include <catch2/catch_test_macros.hpp>
#include "../src/rate_class_mux.h"

using namespace telemetry_agent;

TEST_CASE("RateClassMux pops critical events in FIFO order", "[rate_class_mux]") {
    RateClassMux mux;
    mux.push_critical_event("armed", true);
    mux.push_critical_event("flight_mode", "MISSION");

    auto first = mux.pop_critical_event();
    REQUIRE(first.has_value());
    REQUIRE((*first)["field"] == "armed");
    REQUIRE((*first)["value"] == true);
    REQUIRE((*first)["class"] == "critical");
    REQUIRE((*first).contains("timestamp"));

    auto second = mux.pop_critical_event();
    REQUIRE(second.has_value());
    REQUIRE((*second)["field"] == "flight_mode");
    REQUIRE((*second)["value"] == "MISSION");

    REQUIRE_FALSE(mux.pop_critical_event().has_value());
}
```

- [ ] **Step 3: Add the new files to `tests/CMakeLists.txt`**

Update the `add_executable` line:
```cmake
add_executable(telemetry_agent_tests test_smoke.cpp test_rate_class_mux.cpp ../src/rate_class_mux.cpp)
```

- [ ] **Step 4: Run the test to verify it fails**

Run: `cd build && cmake --build . --target telemetry_agent_tests -j4`
Expected: compile failure -- `RateClassMux` has no implementation yet.

- [ ] **Step 5: Write the implementation**

```cpp
// onboard/telemetry-agent/src/rate_class_mux.cpp
#include "rate_class_mux.h"
#include <chrono>
#include <cstdio>
#include <ctime>

namespace telemetry_agent {

namespace {
std::string iso8601_now() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d+00:00", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}
}  // namespace

RateClassMux::RateClassMux(size_t critical_queue_depth) : critical_queue_depth_(critical_queue_depth) {}

void RateClassMux::push_critical_event(const std::string& field, nlohmann::json value) {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json event;
    event["class"] = "critical";
    event["field"] = field;
    event["value"] = std::move(value);
    event["timestamp"] = iso8601_now();

    if (critical_queue_.size() >= critical_queue_depth_) {
        critical_queue_.pop_front();  // drop oldest -- see design doc's Rate Classes section
    }
    critical_queue_.push_back(std::move(event));
}

std::optional<nlohmann::json> RateClassMux::pop_critical_event() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (critical_queue_.empty()) return std::nullopt;
    nlohmann::json event = std::move(critical_queue_.front());
    critical_queue_.pop_front();
    return event;
}

void RateClassMux::update_position(double latitude_deg, double longitude_deg, float relative_altitude_m,
                                   float absolute_altitude_m, float roll_deg, float pitch_deg, float yaw_deg) {
    std::lock_guard<std::mutex> lock(mutex_);
    position_.latitude_deg = latitude_deg;
    position_.longitude_deg = longitude_deg;
    position_.relative_altitude_m = relative_altitude_m;
    position_.absolute_altitude_m = absolute_altitude_m;
    position_.roll_deg = roll_deg;
    position_.pitch_deg = pitch_deg;
    position_.yaw_deg = yaw_deg;
}

nlohmann::json RateClassMux::latest_position_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json j;
    j["class"] = "position";
    j["latitude_deg"] = position_.latitude_deg;
    j["longitude_deg"] = position_.longitude_deg;
    j["relative_altitude_m"] = position_.relative_altitude_m;
    j["absolute_altitude_m"] = position_.absolute_altitude_m;
    j["roll_deg"] = position_.roll_deg;
    j["pitch_deg"] = position_.pitch_deg;
    j["yaw_deg"] = position_.yaw_deg;
    j["timestamp"] = iso8601_now();
    return j;
}

void RateClassMux::update_bulk(float battery_voltage_v, float battery_remaining_pct, float airspeed_m_s,
                               float groundspeed_m_s, float heading_deg) {
    std::lock_guard<std::mutex> lock(mutex_);
    bulk_.battery_voltage_v = battery_voltage_v;
    bulk_.battery_remaining_pct = battery_remaining_pct;
    bulk_.airspeed_m_s = airspeed_m_s;
    bulk_.groundspeed_m_s = groundspeed_m_s;
    bulk_.heading_deg = heading_deg;
}

nlohmann::json RateClassMux::latest_bulk_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json j;
    j["class"] = "bulk";
    j["battery_voltage_v"] = bulk_.battery_voltage_v;
    j["battery_remaining_pct"] = bulk_.battery_remaining_pct;
    j["airspeed_m_s"] = bulk_.airspeed_m_s;
    j["groundspeed_m_s"] = bulk_.groundspeed_m_s;
    j["heading_deg"] = bulk_.heading_deg;
    j["timestamp"] = iso8601_now();
    return j;
}

}  // namespace telemetry_agent
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build . --target telemetry_agent_tests -j4 && ./tests/telemetry_agent_tests "[rate_class_mux]"`
Expected: PASS

- [ ] **Step 7: Add tests for drop-oldest-when-full, position/bulk coalescing, and default values**

```cpp
TEST_CASE("RateClassMux drops the oldest critical event once the queue is full", "[rate_class_mux]") {
    RateClassMux mux(2);  // small depth to make the drop reachable in a test
    mux.push_critical_event("a", 1);
    mux.push_critical_event("b", 2);
    mux.push_critical_event("c", 3);  // queue full at "a","b" -- drops "a"

    auto first = mux.pop_critical_event();
    REQUIRE((*first)["field"] == "b");
    auto second = mux.pop_critical_event();
    REQUIRE((*second)["field"] == "c");
    REQUIRE_FALSE(mux.pop_critical_event().has_value());
}

TEST_CASE("RateClassMux coalesces position updates -- only the latest is ever returned", "[rate_class_mux]") {
    RateClassMux mux;
    mux.update_position(1.0, 2.0, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f);
    mux.update_position(10.0, 20.0, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f);

    auto j = mux.latest_position_json();
    REQUIRE(j["class"] == "position");
    REQUIRE(j["latitude_deg"] == 10.0);
    REQUIRE(j["longitude_deg"] == 20.0);
}

TEST_CASE("RateClassMux coalesces bulk updates -- only the latest is ever returned", "[rate_class_mux]") {
    RateClassMux mux;
    mux.update_bulk(11.0f, 50.0f, 5.0f, 4.0f, 90.0f);
    mux.update_bulk(16.0f, 90.0f, 12.0f, 11.0f, 180.0f);

    auto j = mux.latest_bulk_json();
    REQUIRE(j["class"] == "bulk");
    REQUIRE(j["battery_voltage_v"] == 16.0f);
    REQUIRE(j["battery_remaining_pct"] == 90.0f);
}

TEST_CASE("RateClassMux returns zero-valued position/bulk before any update", "[rate_class_mux]") {
    RateClassMux mux;
    REQUIRE(mux.latest_position_json()["latitude_deg"] == 0.0);
    REQUIRE(mux.latest_bulk_json()["battery_voltage_v"] == 0.0f);
}
```

- [ ] **Step 8: Run all rate_class_mux tests**

Run: `cmake --build . --target telemetry_agent_tests -j4 && ./tests/telemetry_agent_tests "[rate_class_mux]"`
Expected: all PASS (7 test cases)

- [ ] **Step 9: Commit**

```bash
git add onboard/telemetry-agent/src/rate_class_mux.h onboard/telemetry-agent/src/rate_class_mux.cpp onboard/telemetry-agent/tests/test_rate_class_mux.cpp onboard/telemetry-agent/tests/CMakeLists.txt
git commit -m "Add RateClassMux: critical FIFO queue, position/bulk coalescing"
```

---

## Task 3: `TelemetryServer` -- TCP push with per-tier cadence

**Files:**
- Create: `onboard/telemetry-agent/src/telemetry_server.h`
- Create: `onboard/telemetry-agent/src/telemetry_server.cpp`
- Test: `onboard/telemetry-agent/tests/test_telemetry_server.cpp`
- Modify: `onboard/telemetry-agent/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `RateClassMux::pop_critical_event()`, `latest_position_json()`, `latest_bulk_json()` (Task 2).
- Produces: `telemetry_agent::TelemetryServer(int port, RateClassMux& mux, std::chrono::milliseconds position_interval = 100ms, std::chrono::milliseconds bulk_interval = 1000ms)`, with `run()` and `stop()`. Task 5 (`main.cpp`) depends on this exact constructor shape.

- [ ] **Step 1: Write the header**

```cpp
// onboard/telemetry-agent/src/telemetry_server.h
#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include "rate_class_mux.h"

namespace telemetry_agent {

// Single-connection TCP server: accepts one backend connection, then drains
// RateClassMux's three tiers on their own cadence and pushes
// newline-delimited JSON. One-way (agent -> backend) -- there is nothing to
// read from the client. run() blocks; call it from a dedicated thread.
// Socket-hardening (SIGPIPE via MSG_NOSIGNAL, non-blocking + poll()-based
// backpressure) mirrors the Mission Agent's AgentServer directly -- that
// was a real, hard-won fix from its final review.
class TelemetryServer {
public:
    TelemetryServer(int port, RateClassMux& mux, std::chrono::milliseconds position_interval = std::chrono::milliseconds(100),
                    std::chrono::milliseconds bulk_interval = std::chrono::milliseconds(1000));
    ~TelemetryServer();

    void run();
    void stop();

private:
    int port_;
    RateClassMux& mux_;
    std::chrono::milliseconds position_interval_;
    std::chrono::milliseconds bulk_interval_;
    int listen_fd_ = -1;
    int client_fd_ = -1;
    std::atomic<bool> running_{false};
    mutable std::mutex client_mutex_;

    enum class WriteOutcome { Ok, WouldBlock, ClientGone };
    WriteOutcome write_payload(int fd, const std::string& payload, int budget_ms);
    // Sends one line with the zero-wait ("coalescing") budget; drops it
    // silently on WouldBlock, tears down client_fd_ on ClientGone. Returns
    // false only when the client is gone (caller should stop the drain loop
    // for this connection).
    bool send_line(int fd, const std::string& payload);
    void drain_until_disconnected(int client_fd);
};

}  // namespace telemetry_agent
```

- [ ] **Step 2: Write the failing test -- server accepts a connection and pushes a critical event immediately**

```cpp
// onboard/telemetry-agent/tests/test_telemetry_server.cpp
#include <catch2/catch_test_macros.hpp>
#include <arpa/inet.h>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include "../src/telemetry_server.h"

using namespace telemetry_agent;

namespace {

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

TEST_CASE("TelemetryServer pushes a critical event to a connected client promptly", "[telemetry_server]") {
    RateClassMux mux;
    TelemetryServer server(15761, mux, std::chrono::milliseconds(1000), std::chrono::milliseconds(1000));
    std::thread server_thread([&server]() { server.run(); });

    int client_fd = connect_with_retry(15761);
    mux.push_critical_event("armed", true);

    std::string line = read_line(client_fd);
    auto j = nlohmann::json::parse(line);
    REQUIRE(j["class"] == "critical");
    REQUIRE(j["field"] == "armed");
    REQUIRE(j["value"] == true);

    close(client_fd);
    server.stop();
    server_thread.join();
}
```

- [ ] **Step 3: Add the new files to `tests/CMakeLists.txt`**

```cmake
add_executable(telemetry_agent_tests test_smoke.cpp test_rate_class_mux.cpp test_telemetry_server.cpp ../src/rate_class_mux.cpp ../src/telemetry_server.cpp)
target_link_libraries(telemetry_agent_tests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json Threads::Threads MAVSDK::mavsdk)
```

- [ ] **Step 4: Run the test to verify it fails**

Run: `cmake --build . --target telemetry_agent_tests -j4`
Expected: compile failure -- `TelemetryServer` undefined.

- [ ] **Step 5: Write the implementation**

```cpp
// onboard/telemetry-agent/src/telemetry_server.cpp
#include "telemetry_server.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace telemetry_agent {

namespace {
constexpr int kPollSliceMs = 50;
}  // namespace

TelemetryServer::TelemetryServer(int port, RateClassMux& mux, std::chrono::milliseconds position_interval,
                                 std::chrono::milliseconds bulk_interval)
    : port_(port), mux_(mux), position_interval_(position_interval), bulk_interval_(bulk_interval) {}

TelemetryServer::~TelemetryServer() { stop(); }

void TelemetryServer::run() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        throw std::runtime_error("bind() failed on port " + std::to_string(port_));
    }
    if (listen(listen_fd_, 1) != 0) {
        throw std::runtime_error("listen() failed on port " + std::to_string(port_));
    }

    running_ = true;
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
        if (fd < 0) {
            if (!running_) break;
            continue;
        }
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            std::cerr << "telemetry_agent: could not set client socket non-blocking: " << std::strerror(errno) << "\n";
            close(fd);
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            client_fd_ = fd;
        }
        drain_until_disconnected(fd);
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            client_fd_ = -1;
        }
        close(fd);
    }
}

void TelemetryServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

TelemetryServer::WriteOutcome TelemetryServer::write_payload(int fd, const std::string& payload, int budget_ms) {
    size_t sent = 0;
    int waited_ms = 0;
    while (sent < payload.size()) {
        ssize_t n = send(fd, payload.data() + sent, payload.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (waited_ms >= budget_ms) {
                return sent == 0 ? WriteOutcome::WouldBlock : WriteOutcome::ClientGone;
            }
            pollfd pfd{fd, POLLOUT, 0};
            const int slice = std::min(kPollSliceMs, budget_ms - waited_ms);
            const int ready = poll(&pfd, 1, slice);
            if (ready < 0 && errno != EINTR) return WriteOutcome::ClientGone;
            if (ready > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) return WriteOutcome::ClientGone;
            waited_ms += slice;
            continue;
        }
        return WriteOutcome::ClientGone;
    }
    return WriteOutcome::Ok;
}

bool TelemetryServer::send_line(int fd, const std::string& payload_no_newline) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_fd_ < 0) return false;
    std::string payload = payload_no_newline + "\n";

    // Zero-wait budget -- every tier here is either coalescing (a dropped
    // sample is superseded by the next one) or, for critical events,
    // already popped from the queue (re-pushing a dropped critical event
    // back on WouldBlock would reorder it behind newer ones, so instead we
    // simply lose that one delivery attempt and rely on the queue's own
    // retention -- see Step 7 below for why this is still safe).
    switch (write_payload(fd, payload, 0)) {
        case WriteOutcome::Ok:
            return true;
        case WriteOutcome::WouldBlock:
            return true;  // socket busy this tick; not a disconnect
        case WriteOutcome::ClientGone:
            client_fd_ = -1;
            return false;
    }
    return false;
}

void TelemetryServer::drain_until_disconnected(int client_fd) {
    auto last_position = std::chrono::steady_clock::now();
    auto last_bulk = last_position;

    while (running_) {
        // Detect disconnect: poll for POLLIN/POLLHUP even though we never
        // expect the backend to send anything -- a read() of 0 or an error
        // means the client is gone.
        pollfd pfd{client_fd, POLLIN, 0};
        const int ready = poll(&pfd, 1, kPollSliceMs);
        if (ready < 0 && errno != EINTR) return;
        if (ready > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
            char buf[64];
            ssize_t n = read(client_fd, buf, sizeof(buf));
            if (n <= 0 && !(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
                return;  // disconnected
            }
        }

        // Drain every currently-queued critical event this tick -- these
        // are never coalesced, so a burst of events is delivered in full
        // (up to the zero-wait budget per line above).
        while (auto event = mux_.pop_critical_event()) {
            if (!send_line(client_fd, event->dump())) return;
        }

        auto now = std::chrono::steady_clock::now();
        if (now - last_position >= position_interval_) {
            if (!send_line(client_fd, mux_.latest_position_json().dump())) return;
            last_position = now;
        }
        if (now - last_bulk >= bulk_interval_) {
            if (!send_line(client_fd, mux_.latest_bulk_json().dump())) return;
            last_bulk = now;
        }
    }
}

}  // namespace telemetry_agent
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build . --target telemetry_agent_tests -j4 && ./tests/telemetry_agent_tests "[telemetry_server]"`
Expected: PASS

- [ ] **Step 7: Add tests for position/bulk cadence and client disconnect**

```cpp
TEST_CASE("TelemetryServer pushes position and bulk samples at their configured intervals", "[telemetry_server]") {
    RateClassMux mux;
    mux.update_position(47.4, 8.5, 30.0, 500.0, 1.0, 2.0, 3.0);
    mux.update_bulk(16.0f, 80.0f, 10.0f, 9.0f, 90.0f);

    TelemetryServer server(15762, mux, std::chrono::milliseconds(50), std::chrono::milliseconds(50));
    std::thread server_thread([&server]() { server.run(); });

    int client_fd = connect_with_retry(15762);
    std::string first = read_line(client_fd);
    std::string second = read_line(client_fd);

    std::vector<std::string> classes;
    for (auto& line : {first, second}) {
        classes.push_back(nlohmann::json::parse(line)["class"].get<std::string>());
    }
    REQUIRE(std::find(classes.begin(), classes.end(), "position") != classes.end());
    REQUIRE(std::find(classes.begin(), classes.end(), "bulk") != classes.end());

    close(client_fd);
    server.stop();
    server_thread.join();
}

TEST_CASE("TelemetryServer survives a client that vanishes and accepts a new one", "[telemetry_server]") {
    RateClassMux mux;
    TelemetryServer server(15763, mux, std::chrono::milliseconds(1000), std::chrono::milliseconds(1000));
    std::thread server_thread([&server]() { server.run(); });

    int first_client = connect_with_retry(15763);
    close(first_client);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));  // let the server notice the disconnect

    int second_client = connect_with_retry(15763);
    mux.push_critical_event("armed", false);
    std::string line = read_line(second_client);
    REQUIRE(nlohmann::json::parse(line)["field"] == "armed");

    close(second_client);
    server.stop();
    server_thread.join();
}
```

- [ ] **Step 8: Run all telemetry_server tests**

Run: `cmake --build . --target telemetry_agent_tests -j4 && ./tests/telemetry_agent_tests "[telemetry_server]"`
Expected: all PASS (3 test cases)

- [ ] **Step 9: Commit**

```bash
git add onboard/telemetry-agent/src/telemetry_server.h onboard/telemetry-agent/src/telemetry_server.cpp onboard/telemetry-agent/tests/test_telemetry_server.cpp onboard/telemetry-agent/tests/CMakeLists.txt
git commit -m "Add TelemetryServer: per-tier TCP push, reuses Mission Agent's socket hardening"
```

---

## Task 4: `MavlinkTelemetrySource` -- PX4 connection and telemetry routing

**Files:**
- Create: `onboard/telemetry-agent/src/mavlink_telemetry_source.h`
- Create: `onboard/telemetry-agent/src/mavlink_telemetry_source.cpp`
- Create: `onboard/telemetry-agent/tests/manual_mavlink_test.cpp` (live-only harness, not run in CI -- mirrors `onboard/mission-agent/tests/manual_mavlink_test.cpp`)
- Modify: `onboard/telemetry-agent/CMakeLists.txt` (add the `manual_mavlink_test` executable)

**Interfaces:**
- Consumes: `RateClassMux::push_critical_event`, `update_position`, `update_bulk` (Task 2).
- Produces: `telemetry_agent::MavlinkTelemetrySource(const std::string& connection_url, RateClassMux& mux, int connect_timeout_s = 10)`. Task 5 depends on this constructor shape.

**No unit test for this file** -- like the Mission Agent's `MavlinkConnection`, this wraps a real MAVSDK connection and can't be meaningfully tested without live PX4. Verified via the live end-to-end test in Task 10.

- [ ] **Step 1: Write the header**

```cpp
// onboard/telemetry-agent/src/mavlink_telemetry_source.h
#pragma once
#include <memory>
#include <string>
#include <mavsdk/mavsdk.h>
#include <mavsdk/plugins/telemetry/telemetry.h>
#include "rate_class_mux.h"

namespace telemetry_agent {

// Wraps a MAVSDK connection to PX4's second onboard MAVLink instance (the
// "camera" link, udp://:14030 -- see docs/architecture/telemetry-agent-design.md's
// "Why a second, genuinely separate MAVLink instance"). Connects in the
// constructor (blocks until a system with an autopilot is discovered, or
// throws std::runtime_error after connect_timeout_s). Routes every MAVSDK
// telemetry callback into the given RateClassMux for the lifetime of this
// object.
class MavlinkTelemetrySource {
public:
    MavlinkTelemetrySource(const std::string& connection_url, RateClassMux& mux, int connect_timeout_s = 10);

    bool is_connected() const;

private:
    mavsdk::Mavsdk mavsdk_;
    std::shared_ptr<mavsdk::System> system_;
    std::unique_ptr<mavsdk::Telemetry> telemetry_;
    RateClassMux& mux_;

    void subscribe_telemetry();
};

}  // namespace telemetry_agent
```

- [ ] **Step 2: Write the implementation**

```cpp
// onboard/telemetry-agent/src/mavlink_telemetry_source.cpp
#include "mavlink_telemetry_source.h"
#include <atomic>
#include <future>
#include <sstream>
#include <stdexcept>

namespace telemetry_agent {

namespace {
template <typename T>
std::string to_display_string(const T& value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}
}  // namespace

MavlinkTelemetrySource::MavlinkTelemetrySource(const std::string& connection_url, RateClassMux& mux,
                                               int connect_timeout_s)
    : mavsdk_(mavsdk::Mavsdk::Configuration{mavsdk::ComponentType::CompanionComputer}), mux_(mux) {
    auto connection_result = mavsdk_.add_any_connection(connection_url);
    if (connection_result != mavsdk::ConnectionResult::Success) {
        throw std::runtime_error("failed to add MAVSDK connection: " + connection_url);
    }

    auto system_promise = std::promise<std::shared_ptr<mavsdk::System>>{};
    auto system_future = system_promise.get_future();
    std::atomic<bool> discovered{false};

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
    telemetry_ = std::make_unique<mavsdk::Telemetry>(system_);
    subscribe_telemetry();
}

bool MavlinkTelemetrySource::is_connected() const { return system_ != nullptr && system_->is_connected(); }

void MavlinkTelemetrySource::subscribe_telemetry() {
    telemetry_->subscribe_armed([this](bool armed) { mux_.push_critical_event("armed", armed); });

    telemetry_->subscribe_flight_mode([this](mavsdk::Telemetry::FlightMode mode) {
        mux_.push_critical_event("flight_mode", to_display_string(mode));
    });

    telemetry_->subscribe_health([this](mavsdk::Telemetry::Health health) {
        mux_.push_critical_event("is_global_position_ok", health.is_global_position_ok);
        mux_.push_critical_event("is_home_position_ok", health.is_home_position_ok);
        mux_.push_critical_event("is_armable", health.is_armable);
    });

    mux_.push_critical_event("mission_progress", nlohmann::json{{"current", 0}, {"total", 0}});
    telemetry_->subscribe_mission_progress([this](mavsdk::Telemetry::MissionProgress progress) {
        mux_.push_critical_event("mission_progress", nlohmann::json{{"current", progress.current}, {"total", progress.total}});
    });

    telemetry_->subscribe_position([this](mavsdk::Telemetry::Position position) {
        // Position tier bundles position+attitude into one coalescing
        // snapshot -- see RateClassMux::update_position. Attitude is
        // updated independently below; each callback only overwrites the
        // fields it owns.
        mux_.update_position(position.latitude_deg, position.longitude_deg, position.relative_altitude_m,
                             position.absolute_altitude_m, 0.0f, 0.0f, 0.0f);
    });

    telemetry_->subscribe_attitude_euler([this](mavsdk::Telemetry::EulerAngle attitude) {
        auto j = mux_.latest_position_json();
        mux_.update_position(j["latitude_deg"], j["longitude_deg"], j["relative_altitude_m"], j["absolute_altitude_m"],
                             attitude.roll_deg, attitude.pitch_deg, attitude.yaw_deg);
    });

    telemetry_->subscribe_battery([this](mavsdk::Telemetry::Battery battery) {
        auto j = mux_.latest_bulk_json();
        mux_.update_bulk(battery.voltage_v, battery.remaining_percent, j["airspeed_m_s"], j["groundspeed_m_s"],
                         j["heading_deg"]);
    });

    telemetry_->subscribe_fixedwing_metrics([this](mavsdk::Telemetry::FixedwingMetrics metrics) {
        auto j = mux_.latest_bulk_json();
        mux_.update_bulk(j["battery_voltage_v"], j["battery_remaining_pct"], metrics.airspeed_m_s,
                         metrics.groundspeed_m_s, metrics.heading_deg);
    });
}

}  // namespace telemetry_agent
```

**Note on read-modify-write in the position/battery callbacks:** each MAVSDK
callback only owns a subset of `update_position`/`update_bulk`'s parameters,
so it reads the current coalesced snapshot back out to preserve the fields
it doesn't own before writing the merged result. This is safe (not a data
race) because `RateClassMux`'s internal mutex serializes each individual
`latest_*_json()`/`update_*` call, even though the two calls together aren't
atomic as a pair -- a worst-case interleaving loses one field's update for
one tick, which is exactly the class of imprecision the design doc's
coalescing tiers already accept.

- [ ] **Step 3: Write the live-only test harness**

```cpp
// onboard/telemetry-agent/tests/manual_mavlink_test.cpp
// Not run by ctest -- requires a live PX4 SITL instance. Run manually:
//   ./manual_mavlink_test udp://:14030
#include <iostream>
#include <thread>
#include "../src/mavlink_telemetry_source.h"
#include "../src/rate_class_mux.h"

int main(int argc, char** argv) {
    std::string url = argc > 1 ? argv[1] : "udp://:14030";
    telemetry_agent::RateClassMux mux;
    try {
        telemetry_agent::MavlinkTelemetrySource source(url, mux);
        std::cout << "connected, is_connected=" << source.is_connected() << "\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::cout << "latest position: " << mux.latest_position_json().dump() << "\n";
        std::cout << "latest bulk: " << mux.latest_bulk_json().dump() << "\n";
        while (auto event = mux.pop_critical_event()) {
            std::cout << "critical event: " << event->dump() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

- [ ] **Step 4: Add both new sources plus the manual test executable to `CMakeLists.txt`**

```cmake
add_executable(telemetry_agent src/main.cpp src/rate_class_mux.cpp src/telemetry_server.cpp src/mavlink_telemetry_source.cpp)
target_link_libraries(telemetry_agent PRIVATE nlohmann_json::nlohmann_json Threads::Threads MAVSDK::mavsdk)
target_compile_options(telemetry_agent PRIVATE ${TELEMETRY_AGENT_WARNINGS})

add_executable(manual_mavlink_test tests/manual_mavlink_test.cpp src/rate_class_mux.cpp src/mavlink_telemetry_source.cpp)
target_link_libraries(manual_mavlink_test PRIVATE nlohmann_json::nlohmann_json Threads::Threads MAVSDK::mavsdk)
target_compile_options(manual_mavlink_test PRIVATE ${TELEMETRY_AGENT_WARNINGS})
```

Also add `../src/mavlink_telemetry_source.cpp` to `tests/CMakeLists.txt`'s `telemetry_agent_tests` sources (it doesn't get its own unit test, but `main.cpp`-adjacent code in Task 5 may reference the header, so keep the test binary's link set consistent -- skip this if it doesn't end up included anywhere in test code).

- [ ] **Step 5: Build everything**

Run: `cmake .. && cmake --build . -j4`
Expected: `telemetry_agent`, `telemetry_agent_tests`, and `manual_mavlink_test` all build with no warnings.

- [ ] **Step 6: Run the existing unit tests to confirm nothing broke**

Run: `./tests/telemetry_agent_tests`
Expected: all previous tests still pass (this task added no new unit tests).

- [ ] **Step 7: Commit**

```bash
git add onboard/telemetry-agent/src/mavlink_telemetry_source.h onboard/telemetry-agent/src/mavlink_telemetry_source.cpp onboard/telemetry-agent/tests/manual_mavlink_test.cpp onboard/telemetry-agent/CMakeLists.txt
git commit -m "Add MavlinkTelemetrySource: PX4's second onboard link, routes into RateClassMux"
```

---

## Task 5: Wire `main.cpp` and build the real `telemetry_agent` binary

**Files:**
- Modify: `onboard/telemetry-agent/src/main.cpp`

**Interfaces:**
- Consumes: `RateClassMux` (Task 2), `TelemetryServer` (Task 3), `MavlinkTelemetrySource` (Task 4).

- [ ] **Step 1: Replace the placeholder `main.cpp`**

```cpp
// onboard/telemetry-agent/src/main.cpp
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include "mavlink_telemetry_source.h"
#include "rate_class_mux.h"
#include "telemetry_server.h"

namespace {
std::string env_or(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : fallback;
}
}  // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);  // see AgentServer's write_payload comment -- send(MSG_NOSIGNAL) already guards
                                     // this per-call, this is the process-wide belt-and-suspenders match

    std::string mavlink_url = env_or("MAVLINK_URL", "udp://:14030");
    int agent_port = std::stoi(env_or("TELEMETRY_AGENT_PORT", "5761"));

    try {
        telemetry_agent::RateClassMux mux;

        std::cerr << "telemetry_agent: connecting to PX4 at " << mavlink_url << "...\n";
        telemetry_agent::MavlinkTelemetrySource source(mavlink_url, mux);
        std::cerr << "telemetry_agent: connected to PX4\n";

        telemetry_agent::TelemetryServer server(agent_port, mux);
        std::cerr << "telemetry_agent: serving on port " << agent_port << "\n";
        server.run();  // blocks
    } catch (const std::exception& e) {
        std::cerr << "telemetry_agent: fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

- [ ] **Step 2: Build**

Run: `cmake --build . --target telemetry_agent -j4`
Expected: builds successfully with no warnings.

- [ ] **Step 3: Run the full unit suite once more to confirm nothing regressed**

Run: `./tests/telemetry_agent_tests`
Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add onboard/telemetry-agent/src/main.cpp
git commit -m "Wire telemetry_agent main: env-configured MAVLink URL and port"
```

---

## Task 6: Backend `TelemetryAgentClient`

**Files:**
- Create: `ground-station/backend/app/telemetry_agent_client.py`
- Test: `ground-station/backend/tests/test_telemetry_agent_client.py`

**Interfaces:**
- Produces: `TelemetryAgentClient(host, port)` with `connect()`, `disconnect()`, `on_telemetry(callback)`, `on_connection_change(callback)` -- same shape as `MissionAgentClient` but without command sending, acks, or `_pending_acks`. Task 7 depends on this exact interface.

- [ ] **Step 1: Write the failing test**

```python
# ground-station/backend/tests/test_telemetry_agent_client.py
"""Tests TelemetryAgentClient against a real local TCP server (not the C++
binary) -- mirrors the Telemetry Agent's one-way push protocol."""
import asyncio
import json

import pytest

from app.telemetry_agent_client import TelemetryAgentClient


@pytest.mark.asyncio
async def test_telemetry_callback_receives_pushed_messages():
    port = 15861
    received: list[dict] = []

    async def handle(reader, writer):
        for msg in [
            {"class": "critical", "field": "armed", "value": True, "timestamp": "t"},
            {"class": "position", "latitude_deg": 47.4, "timestamp": "t"},
        ]:
            writer.write((json.dumps(msg) + "\n").encode())
            await writer.drain()
        await asyncio.sleep(1)  # keep the connection open long enough for the client to read both lines

    server = await asyncio.start_server(handle, "127.0.0.1", port)
    async with server:
        asyncio.create_task(server.serve_forever())

        client = TelemetryAgentClient("127.0.0.1", port)
        client.on_telemetry(lambda msg: received.append(msg))
        await client.connect()
        await asyncio.sleep(0.2)

        assert len(received) == 2
        assert received[0]["field"] == "armed"
        assert received[1]["class"] == "position"

        await client.disconnect()
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd ground-station/backend && python -m pytest tests/test_telemetry_agent_client.py -v`
Expected: FAIL -- `ModuleNotFoundError: No module named 'app.telemetry_agent_client'`

- [ ] **Step 3: Write the implementation**

```python
# ground-station/backend/app/telemetry_agent_client.py
"""TCP/JSON client for the C++ Telemetry Agent.

See docs/architecture/telemetry-agent-design.md for the protocol this
implements. One-way (agent -> backend) -- unlike MissionAgentClient, there
is no command/ack chain here, just a stream of telemetry messages.
"""

from __future__ import annotations

import asyncio
import json
import logging
from typing import Callable

logger = logging.getLogger(__name__)

_DEFAULT_RECONNECT_INITIAL_DELAY_S = 1.0
_DEFAULT_RECONNECT_MAX_DELAY_S = 30.0


class TelemetryAgentClient:
    """One persistent TCP connection to the Telemetry Agent."""

    def __init__(
        self,
        host: str,
        port: int,
        reconnect_initial_delay: float = _DEFAULT_RECONNECT_INITIAL_DELAY_S,
        reconnect_max_delay: float = _DEFAULT_RECONNECT_MAX_DELAY_S,
    ) -> None:
        self._host = host
        self._port = port
        self._reconnect_initial_delay = reconnect_initial_delay
        self._reconnect_max_delay = reconnect_max_delay
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._telemetry_callback: Callable[[dict], None] | None = None
        self._connection_callback: Callable[[bool], None] | None = None
        self._supervisor_task: asyncio.Task | None = None
        self._closing = False

    async def connect(self) -> None:
        self._closing = False
        connected = await self._open_connection()
        self._supervisor_task = asyncio.create_task(self._supervise(connected))

    async def disconnect(self) -> None:
        self._closing = True
        if self._supervisor_task is not None:
            self._supervisor_task.cancel()
            try:
                await self._supervisor_task
            except (asyncio.CancelledError, Exception):
                pass
            self._supervisor_task = None
        await self._close_writer()
        self._notify_connection(False)

    def on_telemetry(self, callback: Callable[[dict], None]) -> None:
        self._telemetry_callback = callback

    def on_connection_change(self, callback: Callable[[bool], None]) -> None:
        self._connection_callback = callback

    def _notify_connection(self, connected: bool) -> None:
        if self._connection_callback is not None:
            try:
                self._connection_callback(connected)
            except Exception:  # noqa: BLE001 -- a bad subscriber must not break the link
                logger.exception("Telemetry agent connection callback failed")

    async def _open_connection(self) -> bool:
        try:
            self._reader, self._writer = await asyncio.open_connection(self._host, self._port)
        except (OSError, asyncio.TimeoutError) as exc:
            logger.warning("Telemetry agent connection to %s:%s failed: %s", self._host, self._port, exc)
            self._reader = None
            self._writer = None
            return False
        logger.info("Telemetry agent connected at %s:%s", self._host, self._port)
        self._notify_connection(True)
        return True

    async def _close_writer(self) -> None:
        if self._writer is None:
            return
        writer, self._writer = self._writer, None
        self._reader = None
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:  # noqa: BLE001 -- already-broken sockets raise here
            pass

    async def _supervise(self, connected: bool) -> None:
        delay = self._reconnect_initial_delay
        while not self._closing:
            if connected:
                await self._read_until_closed()
                if self._closing:
                    break
                await self._close_writer()
                self._notify_connection(False)
                connected = False
                delay = self._reconnect_initial_delay

            await asyncio.sleep(delay)
            if self._closing:
                break
            connected = await self._open_connection()
            delay = self._reconnect_initial_delay if connected else min(delay * 2, self._reconnect_max_delay)

    async def _read_until_closed(self) -> None:
        reader = self._reader
        if reader is None:
            return
        try:
            while True:
                line = await reader.readline()
                if not line:
                    logger.warning("Telemetry agent connection closed")
                    return
                message = json.loads(line)
                if self._telemetry_callback is not None:
                    self._telemetry_callback(message)
        except asyncio.CancelledError:
            raise
        except Exception as exc:  # noqa: BLE001 -- any read/parse failure means the connection is unusable
            logger.warning("Telemetry agent read loop failed: %s", exc)
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `python -m pytest tests/test_telemetry_agent_client.py -v`
Expected: PASS

- [ ] **Step 5: Add a reconnect-after-drop test**

```python
@pytest.mark.asyncio
async def test_reconnects_after_the_connection_drops():
    port = 15862
    connections = 0
    connected_states: list[bool] = []

    async def handle(reader, writer):
        nonlocal connections
        connections += 1
        if connections == 1:
            writer.close()  # first connection: drop immediately
            return
        writer.write((json.dumps({"class": "critical", "field": "armed", "value": True, "timestamp": "t"}) + "\n").encode())
        await writer.drain()
        await asyncio.sleep(1)

    server = await asyncio.start_server(handle, "127.0.0.1", port)
    async with server:
        asyncio.create_task(server.serve_forever())

        client = TelemetryAgentClient("127.0.0.1", port, reconnect_initial_delay=0.1, reconnect_max_delay=0.2)
        client.on_connection_change(lambda c: connected_states.append(c))
        received: list[dict] = []
        client.on_telemetry(lambda msg: received.append(msg))

        await client.connect()
        await asyncio.sleep(1.0)  # long enough for the first drop + reconnect + second server's push

        assert connections >= 2
        assert True in connected_states
        assert len(received) >= 1

        await client.disconnect()
```

- [ ] **Step 6: Run all telemetry_agent_client tests**

Run: `python -m pytest tests/test_telemetry_agent_client.py -v`
Expected: both tests PASS

- [ ] **Step 7: Commit**

```bash
git add ground-station/backend/app/telemetry_agent_client.py ground-station/backend/tests/test_telemetry_agent_client.py
git commit -m "Add TelemetryAgentClient: one-way TCP/JSON client for the Telemetry Agent"
```

---

## Task 7: Wire the backend to consume Telemetry Agent data

**Files:**
- Modify: `ground-station/backend/app/vehicle.py`
- Modify: `ground-station/backend/app/main.py`
- Modify: `ground-station/backend/tests/test_vehicle.py`

**Interfaces:**
- Consumes: `TelemetryAgentClient` (Task 6).
- Produces: `VehicleConnection(mission_agent_host, mission_agent_port, telemetry_agent_host, telemetry_agent_port)` -- constructor signature changes, `VehicleState` gains `telemetry_connected: bool`.

- [ ] **Step 1: Write the failing test -- telemetry now arrives via the new client, not the Mission Agent's**

```python
# Add to ground-station/backend/tests/test_vehicle.py
import pytest

from app.vehicle import VehicleConnection


def test_telemetry_from_the_telemetry_agent_updates_state():
    vehicle = VehicleConnection("127.0.0.1", 1, "127.0.0.1", 2)  # ports unused -- no real connect() in this test
    vehicle._on_telemetry_agent_message({"class": "critical", "field": "armed", "value": True, "timestamp": "t"})
    assert vehicle.state.armed is True

    vehicle._on_telemetry_agent_message(
        {"class": "position", "latitude_deg": 47.4, "longitude_deg": 8.5, "relative_altitude_m": 30.0,
         "absolute_altitude_m": 500.0, "roll_deg": 1.0, "pitch_deg": 2.0, "yaw_deg": 3.0, "timestamp": "t"}
    )
    assert vehicle.state.latitude_deg == 47.4
    assert vehicle.state.roll_deg == 1.0


def test_telemetry_agent_connection_state_is_tracked_separately_from_mission_agent():
    vehicle = VehicleConnection("127.0.0.1", 1, "127.0.0.1", 2)
    assert vehicle.state.telemetry_connected is False
    vehicle._on_telemetry_agent_connection_change(True)
    assert vehicle.state.telemetry_connected is True
    assert vehicle.state.is_connected is False  # Mission Agent link is a separate flag, untouched here
```

(Existing tests in this file that construct `VehicleConnection(host, port)` with two arguments will need updating to the new four-argument signature as part of this step -- see Step 3.)

- [ ] **Step 2: Run the test to verify it fails**

Run: `python -m pytest tests/test_vehicle.py -v`
Expected: FAIL -- `TypeError: VehicleConnection.__init__() takes 3 positional arguments but 5 were given` (and existing tests fail on the signature change too).

- [ ] **Step 3: Update `app/vehicle.py`**

Replace the file's constructor, telemetry handling, and `VehicleState` with:

```python
from app.mission_agent_client import MissionAgentClient
from app.telemetry_agent_client import TelemetryAgentClient

@dataclass
class VehicleState:
    is_connected: bool = False
    telemetry_connected: bool = False

    # ... (all existing fields unchanged) ...

    def to_dict(self) -> dict:
        return asdict(self)


class VehicleConnection:
    def __init__(self, mission_agent_host: str, mission_agent_port: int,
                 telemetry_agent_host: str, telemetry_agent_port: int) -> None:
        self._client = MissionAgentClient(mission_agent_host, mission_agent_port)
        self._telemetry_client = TelemetryAgentClient(telemetry_agent_host, telemetry_agent_port)
        self.state = VehicleState()
        self._subscribers: set[asyncio.Queue] = set()

    async def connect(self) -> None:
        logger.info("Connecting to mission agent at %s:%s", self._client._host, self._client._port)
        self._client.on_connection_change(self._on_connection_change)
        await self._client.connect()

        logger.info("Connecting to telemetry agent at %s:%s", self._telemetry_client._host, self._telemetry_client._port)
        self._telemetry_client.on_telemetry(self._on_telemetry_agent_message)
        self._telemetry_client.on_connection_change(self._on_telemetry_agent_connection_change)
        await self._telemetry_client.connect()

    async def disconnect(self) -> None:
        self.state.is_connected = False
        self.state.telemetry_connected = False
        await self._client.disconnect()
        await self._telemetry_client.disconnect()

    # ... subscribe/unsubscribe/_notify unchanged ...

    def _on_connection_change(self, connected: bool) -> None:
        if self.state.is_connected == connected:
            return
        logger.info("Mission agent link %s", "up" if connected else "down")
        self.state.is_connected = connected
        self._notify()

    def _on_telemetry_agent_connection_change(self, connected: bool) -> None:
        if self.state.telemetry_connected == connected:
            return
        logger.info("Telemetry agent link %s", "up" if connected else "down")
        self.state.telemetry_connected = connected
        self._notify()

    def _on_telemetry_agent_message(self, message: dict) -> None:
        cls = message.get("class")
        if cls == "critical":
            field = message.get("field")
            value = message.get("value")
            if field == "armed":
                self.state.armed = value
            elif field == "flight_mode":
                self.state.flight_mode = value
            elif field == "is_global_position_ok":
                self.state.is_gps_ok = value
            elif field == "is_armable":
                self.state.is_armable = value
            elif field == "mission_progress" and isinstance(value, dict):
                self.state.mission_current = value.get("current")
                self.state.mission_total = value.get("total")
        elif cls == "position":
            self.state.latitude_deg = message.get("latitude_deg", self.state.latitude_deg)
            self.state.longitude_deg = message.get("longitude_deg", self.state.longitude_deg)
            self.state.relative_altitude_m = message.get("relative_altitude_m", self.state.relative_altitude_m)
            self.state.absolute_altitude_m = message.get("absolute_altitude_m", self.state.absolute_altitude_m)
            self.state.roll_deg = message.get("roll_deg", self.state.roll_deg)
            self.state.pitch_deg = message.get("pitch_deg", self.state.pitch_deg)
            self.state.yaw_deg = message.get("yaw_deg", self.state.yaw_deg)
        elif cls == "bulk":
            self.state.battery_voltage_v = message.get("battery_voltage_v", self.state.battery_voltage_v)
            self.state.battery_remaining_pct = message.get("battery_remaining_pct", self.state.battery_remaining_pct)
            self.state.airspeed_m_s = message.get("airspeed_m_s", self.state.airspeed_m_s)
            self.state.groundspeed_m_s = message.get("groundspeed_m_s", self.state.groundspeed_m_s)
            self.state.heading_deg = message.get("heading_deg", self.state.heading_deg)
        self._notify()

    # ... upload_mission/start_mission/return_to_launch unchanged (still via self._client) ...
```

Remove the old `_on_telemetry` method entirely -- it was the Mission Agent's flat-snapshot handler, now replaced by `_on_telemetry_agent_message`'s class-tagged handling. Update every existing test in `test_vehicle.py` that constructs `VehicleConnection(host, port)` to the new four-argument form (use dummy host/port for the telemetry agent args where the test doesn't exercise it).

- [ ] **Step 4: Update `app/main.py`**

```python
MISSION_AGENT_HOST = os.environ.get("MISSION_AGENT_HOST", "127.0.0.1")
MISSION_AGENT_PORT = int(os.environ.get("MISSION_AGENT_PORT", "5760"))
TELEMETRY_AGENT_HOST = os.environ.get("TELEMETRY_AGENT_HOST", "127.0.0.1")
TELEMETRY_AGENT_PORT = int(os.environ.get("TELEMETRY_AGENT_PORT", "5761"))

@asynccontextmanager
async def lifespan(app: FastAPI):
    vehicle = VehicleConnection(MISSION_AGENT_HOST, MISSION_AGENT_PORT, TELEMETRY_AGENT_HOST, TELEMETRY_AGENT_PORT)
    app.state.vehicle = vehicle
    connect_task = asyncio.create_task(vehicle.connect())
    yield
    connect_task.cancel()
    await vehicle.disconnect()


@app.get("/api/status")
async def status():
    vehicle: VehicleConnection = app.state.vehicle
    return {
        "vehicle_connected": vehicle.state.is_connected,
        "telemetry_connected": vehicle.state.telemetry_connected,
        "mission_agent_address": f"{MISSION_AGENT_HOST}:{MISSION_AGENT_PORT}",
        "telemetry_agent_address": f"{TELEMETRY_AGENT_HOST}:{TELEMETRY_AGENT_PORT}",
    }
```

- [ ] **Step 5: Run the full backend test suite**

Run: `python -m pytest tests/ -v`
Expected: all tests pass, including the two new ones and every existing test updated for the new constructor signature.

- [ ] **Step 6: Commit**

```bash
git add ground-station/backend/app/vehicle.py ground-station/backend/app/main.py ground-station/backend/tests/test_vehicle.py
git commit -m "Wire backend to the Telemetry Agent, add telemetry_connected status"
```

---

## Task 8: Retire the Mission Agent's `TelemetryPublisher`

**Files:**
- Delete: `onboard/mission-agent/src/telemetry_publisher.h`
- Delete: `onboard/mission-agent/src/telemetry_publisher.cpp`
- Delete: `onboard/mission-agent/tests/test_telemetry_publisher.cpp`
- Modify: `onboard/mission-agent/src/agent_server.h` (remove `ITelemetrySink`)
- Modify: `onboard/mission-agent/src/agent_server.cpp` (remove `send_line`, telemetry-specific write budget)
- Modify: `onboard/mission-agent/src/main.cpp` (remove `TelemetryPublisher` wiring)
- Modify: `onboard/mission-agent/CMakeLists.txt`
- Modify: `onboard/mission-agent/tests/CMakeLists.txt`
- Modify: `onboard/mission-agent/tests/test_agent_server.cpp` (remove the `ITelemetrySink`-dependent tests)

- [ ] **Step 1: Delete the three files**

```bash
git rm onboard/mission-agent/src/telemetry_publisher.h onboard/mission-agent/src/telemetry_publisher.cpp onboard/mission-agent/tests/test_telemetry_publisher.cpp
```

- [ ] **Step 2: Remove `ITelemetrySink` from `agent_server.h`**

Delete the `ITelemetrySink` class (lines 13-17) and change `AgentServer` to no longer inherit from it:

```cpp
class AgentServer {
public:
    AgentServer(int port, CommandValidator& validator, IMavlinkConnection& mavlink, StateTracker& state_tracker);
    ~AgentServer();

    void run();
    void stop();

private:
    // ... unchanged, minus send_line's declaration ...
```

Remove `void send_line(const std::string& line) override;` from the public section entirely (no replacement -- nothing calls this any more).

- [ ] **Step 3: Remove telemetry-specific code from `agent_server.cpp`**

Delete the `kTelemetryWriteBudgetMs` constant and the entire `AgentServer::send_line` method. `write_payload` and `send_ack` are unchanged (they're used by the command/ack path, which is unaffected).

- [ ] **Step 4: Remove `TelemetryPublisher` wiring from `main.cpp`**

Read the current file first (`onboard/mission-agent/src/main.cpp`) and remove the `#include "telemetry_publisher.h"`, the `TelemetryPublisher` construction, and its `start()`/`stop()` calls -- the `AgentServer` no longer needs anything telemetry-related passed to it.

- [ ] **Step 5: Update `CMakeLists.txt`**

Remove `src/telemetry_publisher.cpp` from the `mission_agent` executable's source list (both places it appears -- the main target and, if present, `manual_mavlink_test`).

- [ ] **Step 6: Update `tests/CMakeLists.txt`**

Remove `test_telemetry_publisher.cpp` and `../src/telemetry_publisher.cpp` from the `mission_agent_tests` source list.

- [ ] **Step 7: Update `tests/test_agent_server.cpp`**

Remove the two tests that exercise `ITelemetrySink` through `AgentServer`: `"AgentServer send_line reaches a connected client via the telemetry sink interface"` and `"AgentServer survives a client that vanishes mid-telemetry"`. The command/ack-focused tests in that file are unaffected.

- [ ] **Step 8: Rebuild and run the Mission Agent's full test suite**

Run:
```bash
cd /mnt/c/Users/youruser/git/uav_project/onboard/mission-agent/build
cmake --build . --target mission_agent_tests -j4
./tests/mission_agent_tests
```
Expected: all remaining tests pass (2 fewer test cases than before this task, everything else green).

- [ ] **Step 9: Rebuild the production binary**

Run: `cmake --build . --target mission_agent -j4`
Expected: builds successfully, no warnings.

- [ ] **Step 10: Commit**

```bash
git add -u onboard/mission-agent/
git commit -m "Retire Mission Agent's TelemetryPublisher stand-in

Telemetry is now the Telemetry Agent's job (see
docs/architecture/telemetry-agent-design.md). AgentServer goes back to
being purely command/ack -- ITelemetrySink and its write-budget policy
move with TelemetryPublisher's removal."
```

---

## Task 9: Simulation launch scripts

**Files:**
- Create: `simulation/network/launch-telemetry-agent-only.sh`
- Modify: `simulation/network/launch-aircraft.sh`
- Modify: `simulation/network/launch-ground.sh`

**Interfaces:**
- Consumes: the `telemetry_agent` binary (Task 5), `TELEMETRY_AGENT_HOST`/`TELEMETRY_AGENT_PORT` env vars (Task 7).

- [ ] **Step 1: Write `launch-telemetry-agent-only.sh`**

```bash
#!/bin/bash
# Test-only: launches the Telemetry Agent alone inside aircraft-net,
# connecting to PX4's second onboard link. Run in its own terminal window,
# AFTER launch-px4-only.sh is up. Matches the single-process-per-script
# pattern in launch-px4-only.sh/launch-agent-only.sh -- see that script's
# header for why.
#
# Must be run with sudo (ip netns exec requires root).

set -euo pipefail

REAL_USER="${SUDO_USER:-youruser}"
AGENT_BIN="/mnt/c/Users/youruser/git/uav_project/onboard/telemetry-agent/build/telemetry_agent"
LOG_FILE="/tmp/telemetry_agent_aircraft_net.log"

exec ip netns exec aircraft-net sudo -u "$REAL_USER" env \
  MAVLINK_URL="udp://:14030" TELEMETRY_AGENT_PORT="5761" \
  "$AGENT_BIN" > "$LOG_FILE" 2>&1 < /dev/null
```

- [ ] **Step 2: Make it executable**

Run: `chmod +x simulation/network/launch-telemetry-agent-only.sh`

- [ ] **Step 3: Update `launch-aircraft.sh` to also launch the Telemetry Agent**

Read the current file first (`simulation/network/launch-aircraft.sh`). Add a third binary path/log file, launch it the same way the Mission Agent is launched (backgrounded, `&`, its PID tracked), add it to the `trap` cleanup and the `wait -n` set:

```bash
PX4_BIN="/home/$REAL_USER/src/PX4-Autopilot/build/px4_sitl_default/bin/px4"
AGENT_BIN="/mnt/c/Users/youruser/git/uav_project/onboard/mission-agent/build/mission_agent"
TELEMETRY_BIN="/mnt/c/Users/youruser/git/uav_project/onboard/telemetry-agent/build/telemetry_agent"
PX4_LOG_FILE="/tmp/px4_sitl_aircraft_net.log"
AGENT_LOG_FILE="/tmp/mission_agent_aircraft_net.log"
TELEMETRY_LOG_FILE="/tmp/telemetry_agent_aircraft_net.log"

PX4_PID=""
AGENT_PID=""
TELEMETRY_PID=""
trap 'kill $PX4_PID $AGENT_PID $TELEMETRY_PID 2>/dev/null' EXIT

# ... existing PX4 launch + sleep 10 + Mission Agent launch unchanged ...

ip netns exec aircraft-net sudo -u "$REAL_USER" env \
  MAVLINK_URL="udp://:14030" TELEMETRY_AGENT_PORT="5761" \
  "$TELEMETRY_BIN" > "$TELEMETRY_LOG_FILE" 2>&1 < /dev/null &
TELEMETRY_PID=$!

wait -n || true
kill "$PX4_PID" "$AGENT_PID" "$TELEMETRY_PID" 2>/dev/null || true
wait "$PX4_PID" "$AGENT_PID" "$TELEMETRY_PID" 2>/dev/null || true
exit 0
```

- [ ] **Step 4: Update `launch-ground.sh` to add the Telemetry Agent env vars**

Read the current file first. Add `TELEMETRY_AGENT_HOST`/`TELEMETRY_AGENT_PORT` to the `env` block passed to uvicorn:

```bash
exec ip netns exec ground-net sudo -u "$REAL_USER" env \
  MISSION_AGENT_HOST="10.99.0.2" MISSION_AGENT_PORT="5760" \
  TELEMETRY_AGENT_HOST="10.99.0.2" TELEMETRY_AGENT_PORT="5761" \
  GEOFENCE_CENTER_LAT_DEG="$GEOFENCE_CENTER_LAT_DEG" \
  GEOFENCE_CENTER_LON_DEG="$GEOFENCE_CENTER_LON_DEG" \
  GEOFENCE_RADIUS_M="$GEOFENCE_RADIUS_M" \
  MIN_ALTITUDE_M="$MIN_ALTITUDE_M" \
  MAX_ALTITUDE_M="$MAX_ALTITUDE_M" \
  bash -c "cd '$BACKEND_DIR' && exec /home/$REAL_USER/venvs/aerolink-backend/bin/python -m uvicorn app.main:app --host 0.0.0.0 --port 8000" \
  > "$LOG_FILE" 2>&1 < /dev/null
```

- [ ] **Step 5: Syntax-check all three scripts**

Run: `bash -n simulation/network/launch-telemetry-agent-only.sh simulation/network/launch-aircraft.sh simulation/network/launch-ground.sh`
Expected: no output (no syntax errors).

- [ ] **Step 6: Commit**

```bash
git add simulation/network/launch-telemetry-agent-only.sh simulation/network/launch-aircraft.sh simulation/network/launch-ground.sh
git commit -m "Add Telemetry Agent to simulation launch scripts"
```

---

## Task 10: Live end-to-end and fault-injection verification

**Files:**
- Create: `tests/simulation/telemetry_agent_rate_classing.md`
- Modify: `SIMULATION.md`
- Modify: `ARCHITECTURE.md`

**Interfaces:** none (verification task, no new code).

- [ ] **Step 1: Launch the full stack**

Using the decoupled scripts (for fault-testing flexibility, matching `companion_service_restart.md`'s pattern): `launch-px4-only.sh`, `launch-agent-only.sh`, `launch-telemetry-agent-only.sh` (three separate terminals), then `launch-ground.sh`, the `socat` relay, and the frontend dev server.

- [ ] **Step 2: Confirm `/api/status` reports both links up**

Run: `curl http://localhost:8000/api/status`
Expected: `{"vehicle_connected": true, "telemetry_connected": true, ...}`

- [ ] **Step 3: Fly a real mission and confirm live telemetry updates in the frontend**

Place waypoints, upload, start the mission through the frontend UI. Confirm the map/telemetry panel updates smoothly (position tier) and mission-progress/armed-state changes appear (critical tier).

- [ ] **Step 4: Companion-service-restart re-verification for the Telemetry Agent**

Kill only the Telemetry Agent process mid-flight. Confirm via `ps aux` that PX4 and the Mission Agent are unaffected (mission continues/completes normally), and via the backend log that `telemetry_connected` flips to `false` while `vehicle_connected` (Mission Agent link) stays `true` -- proving the two links are genuinely independent. Relaunch the Telemetry Agent and confirm the backend reconnects automatically.

- [ ] **Step 5: Fault-injection re-verification against a degraded link**

Reuse `tests/simulation/latency_injection.md`'s namespace-based latency/packet-loss injection. With the link degraded, confirm via the Telemetry Agent's own log (or a temporary debug counter) that critical events are still delivered (possibly delayed) while position/bulk samples visibly drop/coalesce rather than backing up -- the frontend's telemetry panel should show gaps in position updates, not a growing lag.

- [ ] **Step 6: Write up the test**

Follow the SETUP/ACTION/EXPECTED RESULT/PASS CRITERIA format used throughout `tests/simulation/`, matching `companion_service_restart.md`'s and `command_replay_protection.md`'s style. Include real log excerpts from Steps 2-5.

- [ ] **Step 7: Update `SIMULATION.md` and `ARCHITECTURE.md`**

`SIMULATION.md`: add a "Telemetry Agent Integration" section (mirroring the existing "Mission Agent Integration" section) and update the "What this does NOT yet close" language that currently describes `TelemetryPublisher` as a stand-in -- it's gone now. `ARCHITECTURE.md`: update the component table's Telemetry Agent row and date annotation to reflect it's built and verified, matching the Mission Agent row's existing pattern.

- [ ] **Step 8: Commit**

```bash
git add tests/simulation/telemetry_agent_rate_classing.md SIMULATION.md ARCHITECTURE.md
git commit -m "Document Telemetry Agent live and fault-injection verification"
```

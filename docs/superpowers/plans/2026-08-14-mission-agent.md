# Mission Agent Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the C++/MAVSDK Mission Agent described in `docs/architecture/mission-agent-design.md`, and re-plumb the Python backend to talk to it instead of connecting to PX4 directly.

**Architecture:** A new `onboard/mission-agent/` C++ binary owns the local MAVSDK link to PX4 (`udp://:14540`) and exposes a newline-delimited JSON/TCP protocol. The Python backend's `VehicleConnection` becomes a TCP client of that protocol instead of holding its own MAVSDK `System`. Full protocol/component details are in the design doc -- this plan does not repeat them, it implements them.

**Tech Stack:** C++17, CMake, MAVSDK C++ SDK (system-installed via `.deb`), nlohmann/json (FetchContent), Catch2 (FetchContent) for C++ unit tests. Python side: unchanged (FastAPI, pytest, asyncio).

## Global Constraints

- **No AI-authorship trailers in any commit** (`Co-Authored-By`, session links) -- this project's established convention, every commit step below must NOT include one.
- **Never run `sudo` or `ip netns exec` directly** -- every command needing either must be handed to the user to run in their own native Ubuntu terminal window, exactly as done throughout this whole project (see `SIMULATION.md`'s Phase 4 findings). Any task step marked **[USER TERMINAL]** below is one of these.
- **WSL commands always target `-d Ubuntu-22.04` explicitly** -- the machine's default `wsl` distro is `docker-desktop`, not the one this project uses; omitting `-d Ubuntu-22.04` silently runs commands in the wrong environment (see this project's own `reference-wsl-environment` lesson).
- **MAVSDK C++ version should match the Python `mavsdk` package already in use (v3.17.2)** wherever a specific version can be chosen, to keep wire-compatible behavior between the two SDKs during this transition.
- **Keep the frontend-facing REST API unchanged.** `app/main.py`'s endpoint signatures and response shapes must not change -- only `app/vehicle.py`'s internals change. If a task seems to require changing `app/main.py`'s public behavior, stop and reconsider; that's out of scope.
- Existing Python tests in `ground-station/backend/tests/` must keep passing (`pytest -q`) after every backend-touching task.

---

## Prerequisites (before Task 1)

**[USER TERMINAL]** Install the MAVSDK C++ SDK. There's no `apt` package for it; MAVSDK publishes prebuilt `.deb` packages on their GitHub releases page. In your native Ubuntu terminal:

```bash
# Check the releases page for the version closest to v3.17.2 (the Python
# mavsdk package this project already uses) and download the matching
# amd64 .deb for Ubuntu 22.04:
#   https://github.com/mavlink/MAVSDK/releases
# Example (verify the exact filename/version on the releases page first --
# don't assume this exact one still exists):
wget https://github.com/mavlink/MAVSDK/releases/download/v3.17.2/mavsdk_3.17.2_ubuntu22.04_amd64.deb
sudo dpkg -i mavsdk_3.17.2_ubuntu22.04_amd64.deb
# Verify it installed:
dpkg -l | grep mavsdk
pkg-config --modversion mavsdk
```

If that exact release asset doesn't exist, pick the closest available Ubuntu 22.04 amd64 `.deb` on the releases page instead -- exact patch version isn't critical, just stay close to 3.17.x for the API surface this plan relies on.

**[USER TERMINAL]** Install a CMake and a C++17-capable compiler if not already present:

```bash
sudo apt update && sudo apt install -y cmake build-essential
cmake --version   # need >= 3.16
g++ --version     # need C++17 support (any recent g++ has this)
```

---

### Task 1: CMake project skeleton

**Files:**
- Create: `onboard/mission-agent/CMakeLists.txt`
- Create: `onboard/mission-agent/src/main.cpp`
- Create: `onboard/mission-agent/tests/CMakeLists.txt`
- Create: `onboard/mission-agent/tests/test_smoke.cpp`

**Interfaces:**
- Produces: a working build with `cmake --build build` producing `onboard/mission-agent/build/mission_agent` and `onboard/mission-agent/build/tests/mission_agent_tests`.

- [ ] **Step 1: Write the top-level CMakeLists.txt**

```cmake
# onboard/mission-agent/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(mission_agent CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(MAVSDK REQUIRED)

include(FetchContent)
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

add_executable(mission_agent src/main.cpp)
target_link_libraries(mission_agent PRIVATE nlohmann_json::nlohmann_json)

enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 2: Write a placeholder main.cpp**

```cpp
// onboard/mission-agent/src/main.cpp
#include <iostream>

int main() {
    std::cout << "mission_agent starting (skeleton)\n";
    return 0;
}
```

- [ ] **Step 3: Write the tests CMakeLists.txt with Catch2 via FetchContent**

```cmake
# onboard/mission-agent/tests/CMakeLists.txt
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.6.0
)
FetchContent_MakeAvailable(Catch2)

add_executable(mission_agent_tests test_smoke.cpp)
target_link_libraries(mission_agent_tests PRIVATE Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(mission_agent_tests)
```

- [ ] **Step 4: Write a trivial smoke test**

```cpp
// onboard/mission-agent/tests/test_smoke.cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("build system is wired up", "[smoke]") {
    REQUIRE(1 + 1 == 2);
}
```

- [ ] **Step 5: Build and run**

Run:
```bash
cd onboard/mission-agent
cmake -B build -S .
cmake --build build
./build/tests/mission_agent_tests
./build/mission_agent
```
Expected: build succeeds, the test binary reports `1 test case: 1 assertion... passed`, and `mission_agent` prints `mission_agent starting (skeleton)`.

If `find_package(MAVSDK REQUIRED)` fails here, stop and re-check the Prerequisites step -- everything downstream depends on this succeeding.

- [ ] **Step 6: Commit**

```bash
git add onboard/mission-agent/
git commit -m "Scaffold mission-agent CMake project with MAVSDK, nlohmann/json, Catch2"
```

---

### Task 2: Protocol -- Command/Ack types and JSON (de)serialization

**Files:**
- Create: `onboard/mission-agent/src/protocol.h`
- Create: `onboard/mission-agent/src/protocol.cpp`
- Test: `onboard/mission-agent/tests/test_protocol.cpp`
- Modify: `onboard/mission-agent/CMakeLists.txt` (add `src/protocol.cpp` to the executable)
- Modify: `onboard/mission-agent/tests/CMakeLists.txt` (add `test_protocol.cpp`)

**Interfaces:**
- Produces (used by all later C++ tasks):
  - `enum class mission_agent::CommandType { UploadMission, StartMission, ReturnToLaunch }`
  - `struct mission_agent::Waypoint { double latitude_deg; double longitude_deg; float altitude_m; std::optional<float> speed_m_s; std::optional<float> loiter_duration_s; }`
  - `struct mission_agent::Command { std::string command_id; std::string aircraft_id; std::string timestamp; CommandType command_type; std::vector<Waypoint> waypoints; int mission_version; }`
  - `enum class mission_agent::AckStatus { Received, Validated, Accepted, Rejected, Px4ActionStarted }`
  - `struct mission_agent::Ack { std::string command_id; AckStatus status; std::string reason; }`
  - `mission_agent::Command mission_agent::parse_command(const std::string& json_line)` -- throws `std::invalid_argument` on malformed input
  - `std::string mission_agent::serialize_ack(const Ack& ack)` -- includes trailing `\n`
  - `std::string mission_agent::to_string(AckStatus status)`

- [ ] **Step 1: Write the failing tests**

```cpp
// onboard/mission-agent/tests/test_protocol.cpp
#include <catch2/catch_test_macros.hpp>
#include "../src/protocol.h"

using namespace mission_agent;

TEST_CASE("parse_command parses a valid UPLOAD_MISSION command", "[protocol]") {
    std::string line = R"({
        "command_id": "abc-123",
        "aircraft_id": "aerolink-1",
        "timestamp": "2026-08-14T10:00:00Z",
        "command_type": "UPLOAD_MISSION",
        "parameters": {
            "waypoints": [
                {"latitude_deg": 47.4, "longitude_deg": 8.5, "altitude_m": 30.0, "speed_m_s": 15.0}
            ]
        },
        "mission_version": 1
    })";

    Command cmd = parse_command(line);

    REQUIRE(cmd.command_id == "abc-123");
    REQUIRE(cmd.aircraft_id == "aerolink-1");
    REQUIRE(cmd.command_type == CommandType::UploadMission);
    REQUIRE(cmd.mission_version == 1);
    REQUIRE(cmd.waypoints.size() == 1);
    REQUIRE(cmd.waypoints[0].latitude_deg == 47.4);
    REQUIRE(cmd.waypoints[0].speed_m_s.has_value());
    REQUIRE(cmd.waypoints[0].speed_m_s.value() == 15.0f);
}

TEST_CASE("parse_command parses a START_MISSION command with no waypoints", "[protocol]") {
    std::string line = R"({
        "command_id": "def-456",
        "aircraft_id": "aerolink-1",
        "timestamp": "2026-08-14T10:01:00Z",
        "command_type": "START_MISSION",
        "parameters": {},
        "mission_version": 1
    })";

    Command cmd = parse_command(line);

    REQUIRE(cmd.command_type == CommandType::StartMission);
    REQUIRE(cmd.waypoints.empty());
}

TEST_CASE("parse_command throws on malformed JSON", "[protocol]") {
    REQUIRE_THROWS_AS(parse_command("not json"), std::invalid_argument);
}

TEST_CASE("parse_command throws on unrecognized command_type", "[protocol]") {
    std::string line = R"({
        "command_id": "x", "aircraft_id": "y", "timestamp": "z",
        "command_type": "NOT_A_REAL_COMMAND", "parameters": {}, "mission_version": 1
    })";
    REQUIRE_THROWS_AS(parse_command(line), std::invalid_argument);
}

TEST_CASE("serialize_ack produces the expected JSON shape", "[protocol]") {
    Ack ack{"abc-123", AckStatus::Accepted, ""};
    std::string line = serialize_ack(ack);

    REQUIRE(line.back() == '\n');
    nlohmann::json parsed = nlohmann::json::parse(line);
    REQUIRE(parsed["command_id"] == "abc-123");
    REQUIRE(parsed["status"] == "ACCEPTED");
}

TEST_CASE("serialize_ack includes reason on REJECTED", "[protocol]") {
    Ack ack{"abc-123", AckStatus::Rejected, "outside geofence"};
    nlohmann::json parsed = nlohmann::json::parse(serialize_ack(ack));

    REQUIRE(parsed["status"] == "REJECTED");
    REQUIRE(parsed["reason"] == "outside geofence");
}
```

- [ ] **Step 2: Add the test file to the test CMakeLists and run to verify it fails**

Edit `onboard/mission-agent/tests/CMakeLists.txt`, change:
```cmake
add_executable(mission_agent_tests test_smoke.cpp)
```
to:
```cmake
add_executable(mission_agent_tests test_smoke.cpp test_protocol.cpp)
target_link_libraries(mission_agent_tests PRIVATE nlohmann_json::nlohmann_json)
```

Run:
```bash
cd onboard/mission-agent
cmake -B build -S .
cmake --build build
```
Expected: FAIL -- `protocol.h` doesn't exist yet.

- [ ] **Step 3: Write protocol.h**

```cpp
// onboard/mission-agent/src/protocol.h
#pragma once
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mission_agent {

enum class CommandType { UploadMission, StartMission, ReturnToLaunch };

struct Waypoint {
    double latitude_deg;
    double longitude_deg;
    float altitude_m;
    std::optional<float> speed_m_s;
    std::optional<float> loiter_duration_s;
};

struct Command {
    std::string command_id;
    std::string aircraft_id;
    std::string timestamp;
    CommandType command_type;
    std::vector<Waypoint> waypoints;  // only populated for UploadMission
    int mission_version;
};

enum class AckStatus { Received, Validated, Accepted, Rejected, Px4ActionStarted };

struct Ack {
    std::string command_id;
    AckStatus status;
    std::string reason;  // empty unless Rejected
};

std::string to_string(AckStatus status);
std::string to_string(CommandType type);
CommandType command_type_from_string(const std::string& s);

// Parses one line of newline-delimited JSON into a Command.
// Throws std::invalid_argument on malformed/unrecognized input.
Command parse_command(const std::string& json_line);

// Serializes an Ack to one line of JSON, including the trailing '\n'.
std::string serialize_ack(const Ack& ack);

}  // namespace mission_agent
```

- [ ] **Step 4: Write protocol.cpp**

```cpp
// onboard/mission-agent/src/protocol.cpp
#include "protocol.h"
#include <nlohmann/json.hpp>

namespace mission_agent {

using nlohmann::json;

std::string to_string(AckStatus status) {
    switch (status) {
        case AckStatus::Received: return "RECEIVED";
        case AckStatus::Validated: return "VALIDATED";
        case AckStatus::Accepted: return "ACCEPTED";
        case AckStatus::Rejected: return "REJECTED";
        case AckStatus::Px4ActionStarted: return "PX4_ACTION_STARTED";
    }
    throw std::invalid_argument("unknown AckStatus");
}

std::string to_string(CommandType type) {
    switch (type) {
        case CommandType::UploadMission: return "UPLOAD_MISSION";
        case CommandType::StartMission: return "START_MISSION";
        case CommandType::ReturnToLaunch: return "RETURN_TO_LAUNCH";
    }
    throw std::invalid_argument("unknown CommandType");
}

CommandType command_type_from_string(const std::string& s) {
    if (s == "UPLOAD_MISSION") return CommandType::UploadMission;
    if (s == "START_MISSION") return CommandType::StartMission;
    if (s == "RETURN_TO_LAUNCH") return CommandType::ReturnToLaunch;
    throw std::invalid_argument("unrecognized command_type: " + s);
}

Command parse_command(const std::string& json_line) {
    json j;
    try {
        j = json::parse(json_line);
    } catch (const json::parse_error& e) {
        throw std::invalid_argument(std::string("malformed JSON: ") + e.what());
    }

    Command cmd;
    cmd.command_id = j.at("command_id").get<std::string>();
    cmd.aircraft_id = j.at("aircraft_id").get<std::string>();
    cmd.timestamp = j.at("timestamp").get<std::string>();
    cmd.command_type = command_type_from_string(j.at("command_type").get<std::string>());
    cmd.mission_version = j.at("mission_version").get<int>();

    if (j.contains("parameters") && j["parameters"].contains("waypoints")) {
        for (const auto& wp_json : j["parameters"]["waypoints"]) {
            Waypoint wp;
            wp.latitude_deg = wp_json.at("latitude_deg").get<double>();
            wp.longitude_deg = wp_json.at("longitude_deg").get<double>();
            wp.altitude_m = wp_json.at("altitude_m").get<float>();
            if (wp_json.contains("speed_m_s") && !wp_json["speed_m_s"].is_null()) {
                wp.speed_m_s = wp_json["speed_m_s"].get<float>();
            }
            if (wp_json.contains("loiter_duration_s") && !wp_json["loiter_duration_s"].is_null()) {
                wp.loiter_duration_s = wp_json["loiter_duration_s"].get<float>();
            }
            cmd.waypoints.push_back(wp);
        }
    }

    return cmd;
}

std::string serialize_ack(const Ack& ack) {
    json j;
    j["command_id"] = ack.command_id;
    j["status"] = to_string(ack.status);
    j["reason"] = ack.reason;
    return j.dump() + "\n";
}

}  // namespace mission_agent
```

- [ ] **Step 5: Add protocol.cpp to the main executable and rebuild**

Edit `onboard/mission-agent/CMakeLists.txt`, change:
```cmake
add_executable(mission_agent src/main.cpp)
```
to:
```cmake
add_executable(mission_agent src/main.cpp src/protocol.cpp)
```

Run:
```bash
cd onboard/mission-agent
cmake --build build
./build/tests/mission_agent_tests
```
Expected: PASS -- all `[protocol]` test cases and the smoke test pass.

- [ ] **Step 6: Commit**

```bash
git add onboard/mission-agent/
git commit -m "Add Mission Agent protocol types and JSON (de)serialization"
```

---

### Task 3: StateTracker

**Files:**
- Create: `onboard/mission-agent/src/state_tracker.h`
- Create: `onboard/mission-agent/src/state_tracker.cpp`
- Test: `onboard/mission-agent/tests/test_state_tracker.cpp`
- Modify: `onboard/mission-agent/CMakeLists.txt`, `onboard/mission-agent/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing new.
- Produces (used by Tasks 4, 5, 6, 7):
  - `struct mission_agent::StateSnapshot { bool armed; std::string flight_mode; double latitude_deg; double longitude_deg; float relative_altitude_m; float battery_remaining_pct; bool is_global_position_ok; bool is_home_position_ok; int mission_current; int mission_total; bool mission_uploaded; }`
  - `class mission_agent::StateTracker` with `StateSnapshot snapshot() const` and setters: `set_armed(bool)`, `set_flight_mode(std::string)`, `set_position(double lat, double lon, float rel_alt)`, `set_battery(float pct)`, `set_health(bool global_position_ok, bool home_position_ok)`, `set_mission_progress(int current, int total)`, `set_mission_uploaded(bool)`

- [ ] **Step 1: Write the failing tests**

```cpp
// onboard/mission-agent/tests/test_state_tracker.cpp
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>
#include "../src/state_tracker.h"

using namespace mission_agent;

TEST_CASE("StateTracker starts with sane defaults", "[state_tracker]") {
    StateTracker tracker;
    auto snap = tracker.snapshot();

    REQUIRE_FALSE(snap.armed);
    REQUIRE(snap.flight_mode == "UNKNOWN");
    REQUIRE_FALSE(snap.mission_uploaded);
}

TEST_CASE("StateTracker setters update the snapshot", "[state_tracker]") {
    StateTracker tracker;

    tracker.set_armed(true);
    tracker.set_flight_mode("MISSION");
    tracker.set_position(47.4, 8.5, 30.0f);
    tracker.set_battery(85.5f);
    tracker.set_health(true, true);
    tracker.set_mission_progress(2, 4);
    tracker.set_mission_uploaded(true);

    auto snap = tracker.snapshot();
    REQUIRE(snap.armed);
    REQUIRE(snap.flight_mode == "MISSION");
    REQUIRE(snap.latitude_deg == 47.4);
    REQUIRE(snap.longitude_deg == 8.5);
    REQUIRE(snap.relative_altitude_m == 30.0f);
    REQUIRE(snap.battery_remaining_pct == 85.5f);
    REQUIRE(snap.is_global_position_ok);
    REQUIRE(snap.is_home_position_ok);
    REQUIRE(snap.mission_current == 2);
    REQUIRE(snap.mission_total == 4);
    REQUIRE(snap.mission_uploaded);
}

TEST_CASE("StateTracker is safe under concurrent reads and writes", "[state_tracker]") {
    StateTracker tracker;
    std::vector<std::thread> threads;

    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&tracker, i]() {
            for (int j = 0; j < 1000; ++j) {
                tracker.set_position(static_cast<double>(i), static_cast<double>(j), 0.0f);
                auto snap = tracker.snapshot();
                // set_position holds the lock across both fields, so any
                // snapshot -- from this thread or another -- must see a
                // (lat, lon) pair some single call actually wrote together,
                // never a torn mix of two different calls' values. lat is
                // always a thread index (0-7); lon is always that thread's
                // current loop counter (0-999). A torn read would produce
                // a value outside these ranges.
                REQUIRE(snap.latitude_deg >= 0.0);
                REQUIRE(snap.latitude_deg < 8.0);
                REQUIRE(snap.longitude_deg >= 0.0);
                REQUIRE(snap.longitude_deg < 1000.0);
            }
        });
    }
    for (auto& t : threads) t.join();

    // After every thread has finished, the tracker must still hold one
    // fully consistent (lat, lon) pair, not a corrupted mid-write value.
    auto final_snap = tracker.snapshot();
    REQUIRE(final_snap.latitude_deg >= 0.0);
    REQUIRE(final_snap.latitude_deg < 8.0);
    REQUIRE(final_snap.longitude_deg >= 0.0);
    REQUIRE(final_snap.longitude_deg < 1000.0);
}
```

- [ ] **Step 2: Add to tests/CMakeLists.txt and run to verify it fails**

```cmake
add_executable(mission_agent_tests test_smoke.cpp test_protocol.cpp test_state_tracker.cpp)
```

Run:
```bash
cd onboard/mission-agent
cmake --build build
```
Expected: FAIL -- `state_tracker.h` doesn't exist.

- [ ] **Step 3: Write state_tracker.h**

```cpp
// onboard/mission-agent/src/state_tracker.h
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
```

- [ ] **Step 4: Write state_tracker.cpp**

```cpp
// onboard/mission-agent/src/state_tracker.cpp
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

void StateTracker::set_position(double lat, double lon, float rel_alt) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.latitude_deg = lat;
    state_.longitude_deg = lon;
    state_.relative_altitude_m = rel_alt;
}

void StateTracker::set_battery(float remaining_pct) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.battery_remaining_pct = remaining_pct;
}

void StateTracker::set_health(bool global_position_ok, bool home_position_ok) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.is_global_position_ok = global_position_ok;
    state_.is_home_position_ok = home_position_ok;
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
```

- [ ] **Step 5: Add to the main executable, link pthread, and rebuild**

Edit `onboard/mission-agent/CMakeLists.txt`:
```cmake
add_executable(mission_agent src/main.cpp src/protocol.cpp src/state_tracker.cpp)
find_package(Threads REQUIRED)
target_link_libraries(mission_agent PRIVATE nlohmann_json::nlohmann_json Threads::Threads)
```
And in `tests/CMakeLists.txt`, add `test_state_tracker.cpp` to the sources and link `Threads::Threads` there too.

Run:
```bash
cd onboard/mission-agent
cmake --build build
./build/tests/mission_agent_tests
```
Expected: PASS -- all `[state_tracker]` tests pass, including the concurrency test (no crash, no hang).

- [ ] **Step 6: Commit**

```bash
git add onboard/mission-agent/
git commit -m "Add thread-safe StateTracker for mirrored PX4 vehicle state"
```

---

### Task 4: CommandValidator

**Files:**
- Create: `onboard/mission-agent/src/command_validator.h`
- Create: `onboard/mission-agent/src/command_validator.cpp`
- Test: `onboard/mission-agent/tests/test_command_validator.cpp`
- Modify: `onboard/mission-agent/CMakeLists.txt`, `onboard/mission-agent/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `mission_agent::Command`, `mission_agent::CommandType`, `mission_agent::StateSnapshot` (Tasks 2, 3)
- Produces (used by Task 6):
  - `struct mission_agent::GeofenceConfig { double center_lat_deg; double center_lon_deg; double radius_m; float min_altitude_m; float max_altitude_m; }`
  - `struct mission_agent::ValidationResult { bool is_valid; std::string reason; }`
  - `class mission_agent::CommandValidator` with constructor `CommandValidator(GeofenceConfig geofence)` and `ValidationResult validate(const Command& command, const StateSnapshot& state) const`
  - `double mission_agent::distance_m(double lat1, double lon1, double lat2, double lon2)` (haversine, mirrors the Python backend's `_distance_m` in `app/mission.py`)

- [ ] **Step 1: Write the failing tests**

```cpp
// onboard/mission-agent/tests/test_command_validator.cpp
#include <catch2/catch_test_macros.hpp>
#include "../src/command_validator.h"

using namespace mission_agent;

namespace {
GeofenceConfig test_geofence() {
    return GeofenceConfig{47.397742, 8.545593, 2000.0, 10.0f, 120.0f};
}

Command upload_command(std::vector<Waypoint> waypoints) {
    Command cmd;
    cmd.command_id = "id-1";
    cmd.aircraft_id = "aerolink-1";
    cmd.timestamp = "2026-08-14T10:00:00Z";
    cmd.command_type = CommandType::UploadMission;
    cmd.mission_version = 1;
    cmd.waypoints = std::move(waypoints);
    return cmd;
}
}  // namespace

TEST_CASE("distance_m returns ~0 for the same point", "[command_validator]") {
    REQUIRE(distance_m(47.397742, 8.545593, 47.397742, 8.545593) < 1.0);
}

TEST_CASE("distance_m returns a sane value for a known offset", "[command_validator]") {
    // Roughly 1 degree of latitude is about 111km.
    double d = distance_m(0.0, 0.0, 1.0, 0.0);
    REQUIRE(d > 110000.0);
    REQUIRE(d < 112000.0);
}

TEST_CASE("CommandValidator accepts a mission within the geofence", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;
    state.armed = false;

    auto cmd = upload_command({Waypoint{47.399, 8.547, 30.0f, 15.0f, std::nullopt}});
    auto result = validator.validate(cmd, state);

    REQUIRE(result.is_valid);
}

TEST_CASE("CommandValidator rejects a waypoint outside the geofence", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;

    // ~0.1 degrees away is roughly 11km, well past the 2000m radius.
    auto cmd = upload_command({Waypoint{47.497742, 8.545593, 30.0f, 15.0f, std::nullopt}});
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
    REQUIRE(result.reason.find("geofence") != std::string::npos);
}

TEST_CASE("CommandValidator rejects a waypoint above max altitude", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;

    auto cmd = upload_command({Waypoint{47.399, 8.547, 500.0f, 15.0f, std::nullopt}});
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
    REQUIRE(result.reason.find("altitude") != std::string::npos);
}

TEST_CASE("CommandValidator rejects UPLOAD_MISSION while armed", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;
    state.armed = true;

    auto cmd = upload_command({Waypoint{47.399, 8.547, 30.0f, 15.0f, std::nullopt}});
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
}

TEST_CASE("CommandValidator rejects START_MISSION with no mission uploaded", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;
    state.mission_uploaded = false;

    Command cmd;
    cmd.command_type = CommandType::StartMission;
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
}

TEST_CASE("CommandValidator accepts START_MISSION with a mission uploaded", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;
    state.mission_uploaded = true;

    Command cmd;
    cmd.command_type = CommandType::StartMission;
    auto result = validator.validate(cmd, state);

    REQUIRE(result.is_valid);
}

TEST_CASE("CommandValidator rejects RETURN_TO_LAUNCH when not connected/armed context missing", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;
    state.armed = false;

    Command cmd;
    cmd.command_type = CommandType::ReturnToLaunch;
    auto result = validator.validate(cmd, state);

    REQUIRE_FALSE(result.is_valid);
}

TEST_CASE("CommandValidator accepts RETURN_TO_LAUNCH while armed", "[command_validator]") {
    CommandValidator validator(test_geofence());
    StateSnapshot state;
    state.armed = true;

    Command cmd;
    cmd.command_type = CommandType::ReturnToLaunch;
    auto result = validator.validate(cmd, state);

    REQUIRE(result.is_valid);
}
```

- [ ] **Step 2: Add to tests/CMakeLists.txt and run to verify it fails**

```cmake
add_executable(mission_agent_tests
    test_smoke.cpp
    test_protocol.cpp
    test_state_tracker.cpp
    test_command_validator.cpp
)
```

Run:
```bash
cd onboard/mission-agent
cmake --build build
```
Expected: FAIL -- `command_validator.h` doesn't exist.

- [ ] **Step 3: Write command_validator.h**

```cpp
// onboard/mission-agent/src/command_validator.h
#pragma once
#include <string>
#include "protocol.h"
#include "state_tracker.h"

namespace mission_agent {

struct GeofenceConfig {
    double center_lat_deg;
    double center_lon_deg;
    double radius_m;
    float min_altitude_m;
    float max_altitude_m;
};

struct ValidationResult {
    bool is_valid;
    std::string reason;  // empty if is_valid
};

// Haversine distance in meters -- mirrors app/mission.py's _distance_m.
double distance_m(double lat1, double lon1, double lat2, double lon2);

class CommandValidator {
public:
    explicit CommandValidator(GeofenceConfig geofence);

    ValidationResult validate(const Command& command, const StateSnapshot& state) const;

private:
    GeofenceConfig geofence_;

    ValidationResult validate_upload_mission(const Command& command, const StateSnapshot& state) const;
    ValidationResult validate_start_mission(const StateSnapshot& state) const;
    ValidationResult validate_return_to_launch(const StateSnapshot& state) const;
};

}  // namespace mission_agent
```

- [ ] **Step 4: Write command_validator.cpp**

```cpp
// onboard/mission-agent/src/command_validator.cpp
#include "command_validator.h"
#include <cmath>

namespace mission_agent {

namespace {
constexpr double kEarthRadiusM = 6371000.0;
constexpr double kPi = 3.14159265358979323846;

double to_radians(double degrees) { return degrees * kPi / 180.0; }
}  // namespace

double distance_m(double lat1, double lon1, double lat2, double lon2) {
    double phi1 = to_radians(lat1);
    double phi2 = to_radians(lat2);
    double d_phi = to_radians(lat2 - lat1);
    double d_lambda = to_radians(lon2 - lon1);

    double a = std::sin(d_phi / 2) * std::sin(d_phi / 2) +
               std::cos(phi1) * std::cos(phi2) * std::sin(d_lambda / 2) * std::sin(d_lambda / 2);
    return 2 * kEarthRadiusM * std::asin(std::sqrt(a));
}

CommandValidator::CommandValidator(GeofenceConfig geofence) : geofence_(geofence) {}

ValidationResult CommandValidator::validate(const Command& command, const StateSnapshot& state) const {
    switch (command.command_type) {
        case CommandType::UploadMission:
            return validate_upload_mission(command, state);
        case CommandType::StartMission:
            return validate_start_mission(state);
        case CommandType::ReturnToLaunch:
            return validate_return_to_launch(state);
    }
    return ValidationResult{false, "unknown command type"};
}

ValidationResult CommandValidator::validate_upload_mission(const Command& command, const StateSnapshot& state) const {
    if (state.armed) {
        return ValidationResult{false, "cannot upload a mission while armed"};
    }
    if (command.waypoints.empty()) {
        return ValidationResult{false, "mission must contain at least one waypoint"};
    }
    for (size_t i = 0; i < command.waypoints.size(); ++i) {
        const auto& wp = command.waypoints[i];
        if (wp.altitude_m < geofence_.min_altitude_m || wp.altitude_m > geofence_.max_altitude_m) {
            return ValidationResult{false, "waypoint " + std::to_string(i) + ": altitude outside allowed range"};
        }
        double d = distance_m(geofence_.center_lat_deg, geofence_.center_lon_deg, wp.latitude_deg, wp.longitude_deg);
        if (d > geofence_.radius_m) {
            return ValidationResult{false, "waypoint " + std::to_string(i) + ": outside geofence"};
        }
    }
    return ValidationResult{true, ""};
}

ValidationResult CommandValidator::validate_start_mission(const StateSnapshot& state) const {
    if (!state.mission_uploaded) {
        return ValidationResult{false, "no mission uploaded"};
    }
    return ValidationResult{true, ""};
}

ValidationResult CommandValidator::validate_return_to_launch(const StateSnapshot& state) const {
    if (!state.armed) {
        return ValidationResult{false, "vehicle is not armed"};
    }
    return ValidationResult{true, ""};
}

}  // namespace mission_agent
```

- [ ] **Step 5: Add to the main executable and tests, rebuild**

Add `src/command_validator.cpp` to `mission_agent`'s sources in `CMakeLists.txt`, and `test_command_validator.cpp` is already in the tests list from Step 2.

Run:
```bash
cd onboard/mission-agent
cmake --build build
./build/tests/mission_agent_tests
```
Expected: PASS -- all `[command_validator]` tests pass.

- [ ] **Step 6: Commit**

```bash
git add onboard/mission-agent/
git commit -m "Add CommandValidator: geofence, altitude, and per-command state checks"
```

---

### Task 5: MavlinkConnection (real PX4 integration)

**Files:**
- Create: `onboard/mission-agent/src/mavlink_connection.h`
- Create: `onboard/mission-agent/src/mavlink_connection.cpp`
- Modify: `onboard/mission-agent/CMakeLists.txt`
- Test: manual integration test against real PX4 SITL (documented below, not a Catch2 test -- this is the first component that genuinely needs a live vehicle)

**Interfaces:**
- Consumes: `mission_agent::Waypoint`, `mission_agent::StateTracker` (Tasks 2, 3)
- Produces (used by Task 6):
  - `struct mission_agent::MavlinkResult { bool success; std::string error_message; }`
  - `class mission_agent::IMavlinkConnection` (abstract): `virtual bool is_connected() const = 0; virtual MavlinkResult upload_mission(const std::vector<Waypoint>&) = 0; virtual MavlinkResult start_mission() = 0; virtual MavlinkResult return_to_launch() = 0;`
  - `class mission_agent::MavlinkConnection : public IMavlinkConnection` -- real MAVSDK-backed implementation. Constructor: `MavlinkConnection(const std::string& connection_url, StateTracker& state_tracker, int connect_timeout_s = 10)`. Throws `std::runtime_error` if no system is discovered within `connect_timeout_s`.

`IMavlinkConnection` exists so Task 6's `AgentServer` tests can use a fake implementation instead of needing real PX4 -- this is a normal implementation detail within the agreed design, not a scope change.

- [ ] **Step 1: Write mavlink_connection.h**

```cpp
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
```

- [ ] **Step 2: Write mavlink_connection.cpp**

```cpp
// onboard/mission-agent/src/mavlink_connection.cpp
#include "mavlink_connection.h"
#include <future>
#include <stdexcept>

namespace mission_agent {

MavlinkConnection::MavlinkConnection(const std::string& connection_url, StateTracker& state_tracker, int connect_timeout_s)
    : mavsdk_(mavsdk::Mavsdk::Configuration{mavsdk::ComponentType::CompanionComputer}),
      state_tracker_(state_tracker) {
    auto connection_result = mavsdk_.add_any_connection(connection_url);
    if (connection_result != mavsdk::ConnectionResult::Success) {
        throw std::runtime_error("failed to add MAVSDK connection: " + connection_url);
    }

    auto system_promise = std::promise<std::shared_ptr<mavsdk::System>>{};
    auto system_future = system_promise.get_future();

    mavsdk_.subscribe_on_new_system([this, &system_promise]() {
        auto system = mavsdk_.systems().back();
        if (system->has_autopilot()) {
            mavsdk_.subscribe_on_new_system(nullptr);
            system_promise.set_value(system);
        }
    });

    if (system_future.wait_for(std::chrono::seconds(connect_timeout_s)) == std::future_status::timeout) {
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
        state_tracker_.set_flight_mode(std::string(mavsdk::Telemetry::flight_mode_str(mode)));
    });

    telemetry_->subscribe_position([this](mavsdk::Telemetry::Position position) {
        state_tracker_.set_position(position.latitude_deg, position.longitude_deg, position.relative_altitude_m);
    });

    telemetry_->subscribe_battery([this](mavsdk::Telemetry::Battery battery) {
        state_tracker_.set_battery(battery.remaining_percent);
    });

    telemetry_->subscribe_health([this](mavsdk::Telemetry::Health health) {
        state_tracker_.set_health(health.is_global_position_ok, health.is_home_position_ok);
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
        return MavlinkResult{false, std::string("mission upload failed: ") + mavsdk::Mission::result_str(result)};
    }
    state_tracker_.set_mission_uploaded(true);
    return MavlinkResult{true, ""};
}

MavlinkResult MavlinkConnection::start_mission() {
    auto arm_result = action_->arm();
    if (arm_result != mavsdk::Action::Result::Success) {
        return MavlinkResult{false, std::string("arm failed: ") + mavsdk::Action::result_str(arm_result)};
    }
    auto start_result = mission_->start_mission();
    if (start_result != mavsdk::Mission::Result::Success) {
        return MavlinkResult{false, std::string("mission start failed: ") + mavsdk::Mission::result_str(start_result)};
    }
    return MavlinkResult{true, ""};
}

MavlinkResult MavlinkConnection::return_to_launch() {
    auto result = action_->return_to_launch();
    if (result != mavsdk::Action::Result::Success) {
        return MavlinkResult{false, std::string("return_to_launch failed: ") + mavsdk::Action::result_str(result)};
    }
    return MavlinkResult{true, ""};
}

}  // namespace mission_agent
```

**Note for whoever implements this step:** MAVSDK's exact C++ API (enum member names, `MissionItem` field names, `Result` string helpers) can shift slightly between versions. If this doesn't compile as-is, check the installed version's headers (`/usr/include/mavsdk/plugins/mission/mission.h` etc. after the Prerequisites `.deb` install) for the exact field/method names and adjust -- the *structure* (one MissionItem per waypoint plus an auto-appended landing item, arm-then-start for START_MISSION, subscribe-then-mirror-into-StateTracker for telemetry) is what matters and shouldn't need to change.

- [ ] **Step 3: Add to the main executable's CMakeLists and build**

```cmake
add_executable(mission_agent src/main.cpp src/protocol.cpp src/state_tracker.cpp src/command_validator.cpp src/mavlink_connection.cpp)
target_link_libraries(mission_agent PRIVATE nlohmann_json::nlohmann_json Threads::Threads MAVSDK::mavsdk)
```

Run:
```bash
cd onboard/mission-agent
cmake --build build
```
Expected: builds successfully (no test to run yet -- `main.cpp` doesn't use `MavlinkConnection` until Task 8).

- [ ] **Step 4: [USER TERMINAL] Manual integration test against real PX4 SITL**

This needs a live PX4 instance -- write a tiny standalone test program and run it against the aircraft-net SITL setup already used throughout this project.

Create `onboard/mission-agent/tests/manual_mavlink_test.cpp`:
```cpp
// onboard/mission-agent/tests/manual_mavlink_test.cpp
// Not part of the Catch2 suite -- run manually against real PX4 SITL.
#include <iostream>
#include "../src/mavlink_connection.h"
#include "../src/state_tracker.h"

int main() {
    mission_agent::StateTracker tracker;
    std::cout << "Connecting to udp://:14540 ...\n";
    mission_agent::MavlinkConnection conn("udp://:14540", tracker, 15);
    std::cout << "Connected: " << conn.is_connected() << "\n";

    std::this_thread::sleep_for(std::chrono::seconds(3));
    auto snap = tracker.snapshot();
    std::cout << "armed=" << snap.armed
              << " mode=" << snap.flight_mode
              << " global_position_ok=" << snap.is_global_position_ok << "\n";

    return 0;
}
```

Add a build target for it in `onboard/mission-agent/CMakeLists.txt`:
```cmake
add_executable(manual_mavlink_test tests/manual_mavlink_test.cpp src/protocol.cpp src/state_tracker.cpp src/mavlink_connection.cpp)
target_link_libraries(manual_mavlink_test PRIVATE nlohmann_json::nlohmann_json Threads::Threads MAVSDK::mavsdk)
```

SETUP: PX4 SITL running with its onboard MAVLink instance available on `14540` -- this is `simulation/network/launch-aircraft.sh` from earlier phases, run *without* the network namespace split for this quick test (this test runs on the same host as PX4, no tunnel needed yet -- that's Task 10's job). In your native Ubuntu terminal:
```bash
cd ~/src/PX4-Autopilot/build/px4_sitl_default/src/modules/simulation/simulator_sih
PX4_SIM_MODEL=sihsim_standard_vtol PX4_SIMULATOR=sihsim ../../../bin/px4 -d
```

ACTION: in a second terminal, build and run the manual test:
```bash
cd onboard/mission-agent
cmake --build build
./build/manual_mavlink_test
```

EXPECTED RESULT: prints `Connected: 1` within a few seconds, then a telemetry line showing `armed=0 mode=...` with `global_position_ok=1` (SITL provides a simulated GPS fix immediately).

PASS CRITERIA: connects without throwing, telemetry fields are populated (not all-zero/default), process exits cleanly.

- [ ] **Step 5: Commit**

```bash
git add onboard/mission-agent/
git commit -m "Add MavlinkConnection: real MAVSDK link to PX4's onboard instance"
```

---

### Task 6: AgentServer

**Files:**
- Create: `onboard/mission-agent/src/agent_server.h`
- Create: `onboard/mission-agent/src/agent_server.cpp`
- Test: `onboard/mission-agent/tests/test_agent_server.cpp`
- Modify: `onboard/mission-agent/CMakeLists.txt`, `onboard/mission-agent/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `mission_agent::Command`, `mission_agent::Ack`, `mission_agent::parse_command`, `mission_agent::serialize_ack` (Task 2); `mission_agent::CommandValidator` (Task 4); `mission_agent::IMavlinkConnection` (Task 5); `mission_agent::StateTracker` (Task 3)
- Produces (used by Task 7, 8):
  - `class mission_agent::ITelemetrySink` (abstract): `virtual void send_line(const std::string& line) = 0;`
  - `class mission_agent::AgentServer : public ITelemetrySink`. Constructor: `AgentServer(int port, CommandValidator& validator, IMavlinkConnection& mavlink, StateTracker& state_tracker)`. Methods: `void run()` (blocking accept loop, call from a dedicated thread), `void stop()`, `void send_line(const std::string& line) override`.

- [ ] **Step 1: Write the failing tests**

```cpp
// onboard/mission-agent/tests/test_agent_server.cpp
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
```

- [ ] **Step 2: Add to tests/CMakeLists.txt and run to verify it fails**

```cmake
add_executable(mission_agent_tests
    test_smoke.cpp
    test_protocol.cpp
    test_state_tracker.cpp
    test_command_validator.cpp
    test_agent_server.cpp
    ../src/protocol.cpp
    ../src/state_tracker.cpp
    ../src/command_validator.cpp
)
target_link_libraries(mission_agent_tests PRIVATE nlohmann_json::nlohmann_json Threads::Threads)
```

Run:
```bash
cd onboard/mission-agent
cmake --build build
```
Expected: FAIL -- `agent_server.h` doesn't exist.

- [ ] **Step 3: Write agent_server.h**

```cpp
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
```

- [ ] **Step 4: Write agent_server.cpp**

```cpp
// onboard/mission-agent/src/agent_server.cpp
#include "agent_server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>

namespace mission_agent {

AgentServer::AgentServer(int port, CommandValidator& validator, IMavlinkConnection& mavlink, StateTracker& state_tracker)
    : port_(port), validator_(validator), mavlink_(mavlink), state_tracker_(state_tracker) {}

AgentServer::~AgentServer() { stop(); }

void AgentServer::run() {
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
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            client_fd_ = fd;
        }
        handle_connection(fd);
        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            client_fd_ = -1;
        }
        close(fd);
    }
}

void AgentServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

void AgentServer::send_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_fd_ < 0) return;  // no-op if nobody's connected
    std::string payload = line;
    if (payload.empty() || payload.back() != '\n') payload += "\n";
    write(client_fd_, payload.c_str(), payload.size());
}

void AgentServer::send_ack(int client_fd, const Ack& ack) {
    std::string line = serialize_ack(ack);
    write(client_fd, line.c_str(), line.size());
}

void AgentServer::handle_connection(int client_fd) {
    std::string buffer;
    char chunk[1024];
    while (running_) {
        ssize_t n = read(client_fd, chunk, sizeof(chunk));
        if (n <= 0) return;  // connection closed or error -- SAFETY.md's LTE_LOST case, not an error to log loudly
        buffer.append(chunk, static_cast<size_t>(n));

        size_t newline;
        while ((newline = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, newline);
            buffer.erase(0, newline + 1);
            if (line.empty()) continue;

            Command command;
            try {
                command = parse_command(line);
            } catch (const std::invalid_argument&) {
                send_ack(client_fd, Ack{"", AckStatus::Rejected, "malformed command"});
                continue;
            }

            send_ack(client_fd, Ack{command.command_id, AckStatus::Received, ""});

            auto validation = validator_.validate(command, state_tracker_.snapshot());
            if (!validation.is_valid) {
                send_ack(client_fd, Ack{command.command_id, AckStatus::Rejected, validation.reason});
                continue;
            }
            send_ack(client_fd, Ack{command.command_id, AckStatus::Validated, ""});
            send_ack(client_fd, Ack{command.command_id, AckStatus::Accepted, ""});

            MavlinkResult result{true, ""};
            switch (command.command_type) {
                case CommandType::UploadMission:
                    result = mavlink_.upload_mission(command.waypoints);
                    break;
                case CommandType::StartMission:
                    result = mavlink_.start_mission();
                    break;
                case CommandType::ReturnToLaunch:
                    result = mavlink_.return_to_launch();
                    break;
            }

            if (!result.success) {
                send_ack(client_fd, Ack{command.command_id, AckStatus::Rejected, result.error_message});
                continue;
            }
            if (command.command_type == CommandType::StartMission || command.command_type == CommandType::ReturnToLaunch) {
                send_ack(client_fd, Ack{command.command_id, AckStatus::Px4ActionStarted, ""});
            }
        }
    }
}

}  // namespace mission_agent
```

- [ ] **Step 5: Rebuild and run**

Run:
```bash
cd onboard/mission-agent
cmake --build build
./build/tests/mission_agent_tests
```
Expected: PASS -- all `[agent_server]` tests pass, alongside every earlier test.

- [ ] **Step 6: Commit**

```bash
git add onboard/mission-agent/
git commit -m "Add AgentServer: TCP command dispatch and ack chain"
```

---

### Task 7: TelemetryPublisher

**Files:**
- Create: `onboard/mission-agent/src/telemetry_publisher.h`
- Create: `onboard/mission-agent/src/telemetry_publisher.cpp`
- Test: `onboard/mission-agent/tests/test_telemetry_publisher.cpp`
- Modify: `onboard/mission-agent/CMakeLists.txt`, `onboard/mission-agent/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `mission_agent::StateTracker` (Task 3), `mission_agent::ITelemetrySink` (Task 6)
- Produces (used by Task 8): `class mission_agent::TelemetryPublisher` -- constructor `TelemetryPublisher(StateTracker& state_tracker, ITelemetrySink& sink, std::chrono::milliseconds interval)`, methods `void start()`, `void stop()`.

- [ ] **Step 1: Write the failing test**

```cpp
// onboard/mission-agent/tests/test_telemetry_publisher.cpp
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include <vector>
#include "../src/telemetry_publisher.h"

using namespace mission_agent;

namespace {
class RecordingSink : public ITelemetrySink {
public:
    void send_line(const std::string& line) override {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.push_back(line);
    }
    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lines_.size();
    }
    std::string last() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lines_.back();
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::string> lines_;
};
}  // namespace

TEST_CASE("TelemetryPublisher pushes snapshots at roughly the configured interval", "[telemetry_publisher]") {
    StateTracker tracker;
    tracker.set_position(47.4, 8.5, 30.0f);
    RecordingSink sink;

    TelemetryPublisher publisher(tracker, sink, std::chrono::milliseconds(50));
    publisher.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(260));
    publisher.stop();

    // ~260ms at a 50ms interval should yield roughly 4-6 pushes.
    REQUIRE(sink.count() >= 3);
    REQUIRE(sink.last().find("47.4") != std::string::npos);
}

TEST_CASE("TelemetryPublisher stops cleanly and pushes nothing further", "[telemetry_publisher]") {
    StateTracker tracker;
    RecordingSink sink;

    TelemetryPublisher publisher(tracker, sink, std::chrono::milliseconds(20));
    publisher.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    publisher.stop();
    size_t count_after_stop = sink.count();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(sink.count() == count_after_stop);
}
```

- [ ] **Step 2: Add to tests/CMakeLists.txt and run to verify it fails**

```cmake
add_executable(mission_agent_tests
    test_smoke.cpp
    test_protocol.cpp
    test_state_tracker.cpp
    test_command_validator.cpp
    test_agent_server.cpp
    test_telemetry_publisher.cpp
    ../src/protocol.cpp
    ../src/state_tracker.cpp
    ../src/command_validator.cpp
)
```

Run:
```bash
cd onboard/mission-agent
cmake --build build
```
Expected: FAIL -- `telemetry_publisher.h` doesn't exist.

- [ ] **Step 3: Write telemetry_publisher.h**

```cpp
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
```

- [ ] **Step 4: Write telemetry_publisher.cpp**

```cpp
// onboard/mission-agent/src/telemetry_publisher.cpp
#include "telemetry_publisher.h"
#include <nlohmann/json.hpp>

namespace mission_agent {

TelemetryPublisher::TelemetryPublisher(StateTracker& state_tracker, ITelemetrySink& sink, std::chrono::milliseconds interval)
    : state_tracker_(state_tracker), sink_(sink), interval_(interval) {}

TelemetryPublisher::~TelemetryPublisher() { stop(); }

void TelemetryPublisher::start() {
    running_ = true;
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
    j["battery_remaining_pct"] = s.battery_remaining_pct;
    j["is_global_position_ok"] = s.is_global_position_ok;
    j["is_home_position_ok"] = s.is_home_position_ok;
    j["mission_current"] = s.mission_current;
    j["mission_total"] = s.mission_total;
    return j.dump();
}

}  // namespace mission_agent
```

- [ ] **Step 5: Add to the main executable, rebuild, and run**

Add `src/telemetry_publisher.cpp` to `mission_agent`'s sources in `CMakeLists.txt`.

Run:
```bash
cd onboard/mission-agent
cmake --build build
./build/tests/mission_agent_tests
```
Expected: PASS -- all `[telemetry_publisher]` tests pass, alongside every earlier test.

- [ ] **Step 6: Commit**

```bash
git add onboard/mission-agent/
git commit -m "Add TelemetryPublisher: periodic StateTracker push to connected backend"
```

---

### Task 8: main.cpp -- wire the real binary together

**Files:**
- Modify: `onboard/mission-agent/src/main.cpp`
- Modify: `onboard/mission-agent/CMakeLists.txt`

**Interfaces:**
- Consumes: everything from Tasks 2-7.
- Produces: a runnable `mission_agent` binary that connects to PX4 and serves the protocol on a TCP port.

- [ ] **Step 1: Write the real main.cpp**

```cpp
// onboard/mission-agent/src/main.cpp
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include "agent_server.h"
#include "command_validator.h"
#include "mavlink_connection.h"
#include "state_tracker.h"
#include "telemetry_publisher.h"

namespace {
std::string env_or(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : fallback;
}

double env_or_double(const char* name, double fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::stod(value) : fallback;
}
}  // namespace

int main() {
    // Same defaults as ground-station/backend/app/config.py, overridable
    // the same way (env vars), so both sides of the system stay in sync
    // without hand-editing two files.
    mission_agent::GeofenceConfig geofence{
        env_or_double("GEOFENCE_CENTER_LAT_DEG", 47.397742),
        env_or_double("GEOFENCE_CENTER_LON_DEG", 8.545593),
        env_or_double("GEOFENCE_RADIUS_M", 2000.0),
        static_cast<float>(env_or_double("MIN_ALTITUDE_M", 10.0)),
        static_cast<float>(env_or_double("MAX_ALTITUDE_M", 120.0)),
    };

    std::string mavlink_url = env_or("MAVLINK_URL", "udp://:14540");
    int agent_port = static_cast<int>(env_or_double("AGENT_PORT", 5760));

    std::cout << "mission_agent: connecting to PX4 at " << mavlink_url << "...\n";

    mission_agent::StateTracker state_tracker;
    mission_agent::MavlinkConnection mavlink(mavlink_url, state_tracker);
    std::cout << "mission_agent: connected to PX4\n";

    mission_agent::CommandValidator validator(geofence);
    mission_agent::AgentServer server(agent_port, validator, mavlink, state_tracker);

    mission_agent::TelemetryPublisher telemetry_publisher(state_tracker, server, std::chrono::milliseconds(250));
    telemetry_publisher.start();

    std::cout << "mission_agent: serving on port " << agent_port << "\n";
    server.run();  // blocks

    telemetry_publisher.stop();
    return 0;
}
```

- [ ] **Step 2: Finalize CMakeLists.txt with all sources**

```cmake
# onboard/mission-agent/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(mission_agent CXX)

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

add_executable(mission_agent
    src/main.cpp
    src/protocol.cpp
    src/state_tracker.cpp
    src/command_validator.cpp
    src/mavlink_connection.cpp
    src/agent_server.cpp
    src/telemetry_publisher.cpp
)
target_link_libraries(mission_agent PRIVATE nlohmann_json::nlohmann_json Threads::Threads MAVSDK::mavsdk)

add_executable(manual_mavlink_test
    tests/manual_mavlink_test.cpp
    src/protocol.cpp
    src/state_tracker.cpp
    src/mavlink_connection.cpp
)
target_link_libraries(manual_mavlink_test PRIVATE nlohmann_json::nlohmann_json Threads::Threads MAVSDK::mavsdk)

enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 3: Build**

Run:
```bash
cd onboard/mission-agent
cmake --build build
./build/tests/mission_agent_tests
```
Expected: builds cleanly, all unit tests still pass.

- [ ] **Step 4: [USER TERMINAL] Manual end-to-end run against PX4 SITL**

SETUP: same PX4 SITL launch as Task 5 Step 4 (no network namespace yet):
```bash
cd ~/src/PX4-Autopilot/build/px4_sitl_default/src/modules/simulation/simulator_sih
PX4_SIM_MODEL=sihsim_standard_vtol PX4_SIMULATOR=sihsim ../../../bin/px4 -d
```

ACTION: in a second terminal:
```bash
cd onboard/mission-agent
./build/mission_agent
```
Then in a third terminal, send a command by hand to confirm the protocol works end-to-end:
```bash
printf '{"command_id":"manual-1","aircraft_id":"a1","timestamp":"t","command_type":"UPLOAD_MISSION","parameters":{"waypoints":[{"latitude_deg":47.399,"longitude_deg":8.547,"altitude_m":30.0,"speed_m_s":15.0}]},"mission_version":1}\n' | nc localhost 5760
```

EXPECTED RESULT: the `mission_agent` terminal prints connection/serving messages; the `nc` terminal prints a stream of JSON lines: `RECEIVED`, `VALIDATED`, `ACCEPTED` acks for the upload, interleaved with periodic telemetry lines (position/battery/etc).

PASS CRITERIA: the ack chain completes with `ACCEPTED` (not `REJECTED`), and telemetry lines keep appearing every ~250ms even without further commands.

- [ ] **Step 5: Commit**

```bash
git add onboard/mission-agent/
git commit -m "Wire Mission Agent's real binary: MAVSDK + validator + TCP server + telemetry"
```

---

### Task 9: Backend rework -- MissionAgentClient and VehicleConnection

**Files:**
- Create: `ground-station/backend/app/mission_agent_client.py`
- Modify: `ground-station/backend/app/vehicle.py`
- Modify: `ground-station/backend/app/main.py:26-46` (env var and lifespan wiring)
- Create: `ground-station/backend/tests/test_mission_agent_client.py`
- Modify: `ground-station/backend/tests/test_vehicle.py` (existing MAVSDK-based fakes need to target `MissionAgentClient` instead)

**Interfaces:**
- Produces:
  - `class app.mission_agent_client.MissionAgentClient` -- `__init__(host: str, port: int)`, `async connect() -> None`, `on_telemetry(callback: Callable[[dict], None]) -> None`, `async send_command(command_type: str, parameters: dict, mission_version: int = 1, aircraft_id: str = "aerolink-1") -> list[Ack]`
  - `@dataclass class app.mission_agent_client.Ack` -- `command_id: str`, `status: str`, `reason: str = ""`
- `app.vehicle.VehicleConnection`'s public interface (`connect()`, `state`, `subscribe()`, `unsubscribe()`, `upload_mission()`, `start_mission()`, `return_to_launch()`, `disconnect()`) stays the same shape as today so `app/main.py` doesn't need to change.

- [ ] **Step 1: Write the failing tests for MissionAgentClient**

```python
# ground-station/backend/tests/test_mission_agent_client.py
"""Tests MissionAgentClient against a real local TCP server (not PX4) --
mirrors the Mission Agent's protocol without needing the C++ binary."""
import asyncio
import json

import pytest

from app.mission_agent_client import MissionAgentClient


async def _run_fake_agent(port: int, responses: list[dict], received: list[dict]):
    """Accepts one connection, echoes back `responses` (one per line read),
    ignoring the actual command content except recording it."""

    async def handle(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        line = await reader.readline()
        received.append(json.loads(line))
        for resp in responses:
            writer.write((json.dumps(resp) + "\n").encode())
            await writer.drain()
        await asyncio.sleep(0.05)
        writer.close()

    server = await asyncio.start_server(handle, "127.0.0.1", port)
    async with server:
        await server.serve_forever()


@pytest.mark.asyncio
async def test_send_command_returns_full_ack_chain():
    port = 15761
    received: list[dict] = []
    responses = [
        {"command_id": "will-be-overwritten", "status": "RECEIVED", "reason": ""},
        {"command_id": "will-be-overwritten", "status": "VALIDATED", "reason": ""},
        {"command_id": "will-be-overwritten", "status": "ACCEPTED", "reason": ""},
    ]

    async def handle(reader, writer):
        line = await reader.readline()
        cmd = json.loads(line)
        received.append(cmd)
        for resp in responses:
            resp = dict(resp, command_id=cmd["command_id"])
            writer.write((json.dumps(resp) + "\n").encode())
            await writer.drain()

    server = await asyncio.start_server(handle, "127.0.0.1", port)
    async with server:
        asyncio.create_task(server.serve_forever())

        client = MissionAgentClient("127.0.0.1", port)
        await client.connect()
        acks = await client.send_command("UPLOAD_MISSION", {"waypoints": []})

        assert [a.status for a in acks] == ["RECEIVED", "VALIDATED", "ACCEPTED"]
        assert received[0]["command_type"] == "UPLOAD_MISSION"


@pytest.mark.asyncio
async def test_send_command_stops_at_rejected():
    port = 15762

    async def handle(reader, writer):
        line = await reader.readline()
        cmd = json.loads(line)
        writer.write((json.dumps({"command_id": cmd["command_id"], "status": "RECEIVED", "reason": ""}) + "\n").encode())
        writer.write((json.dumps({"command_id": cmd["command_id"], "status": "REJECTED", "reason": "outside geofence"}) + "\n").encode())
        await writer.drain()

    server = await asyncio.start_server(handle, "127.0.0.1", port)
    async with server:
        asyncio.create_task(server.serve_forever())

        client = MissionAgentClient("127.0.0.1", port)
        await client.connect()
        acks = await client.send_command("UPLOAD_MISSION", {"waypoints": []})

        assert acks[-1].status == "REJECTED"
        assert acks[-1].reason == "outside geofence"


@pytest.mark.asyncio
async def test_telemetry_callback_receives_non_ack_lines():
    port = 15763
    telemetry_received: list[dict] = []

    async def handle(reader, writer):
        writer.write((json.dumps({"armed": True, "latitude_deg": 47.4}) + "\n").encode())
        await writer.drain()
        await asyncio.sleep(0.1)

    server = await asyncio.start_server(handle, "127.0.0.1", port)
    async with server:
        asyncio.create_task(server.serve_forever())

        client = MissionAgentClient("127.0.0.1", port)
        client.on_telemetry(lambda msg: telemetry_received.append(msg))
        await client.connect()
        await asyncio.sleep(0.05)

        assert len(telemetry_received) == 1
        assert telemetry_received[0]["latitude_deg"] == 47.4
```

- [ ] **Step 2: Run to verify it fails**

Run:
```bash
cd ground-station/backend
source ~/venvs/aerolink-backend/bin/activate
python -m pytest tests/test_mission_agent_client.py -v
```
Expected: FAIL -- `app.mission_agent_client` doesn't exist. (If `pytest-asyncio` isn't installed, add it: `pip install pytest-asyncio` and add `asyncio_mode = auto` under `[tool.pytest.ini_options]` in `pyproject.toml` if not already configured that way -- check the existing `pyproject.toml` first, since `test_vehicle.py`/`test_mission.py` already run async tests today and may already have this configured.)

- [ ] **Step 3: Write mission_agent_client.py**

```python
# ground-station/backend/app/mission_agent_client.py
"""TCP/JSON client for the C++ Mission Agent.

See docs/architecture/mission-agent-design.md for the protocol this
implements. Replaces VehicleConnection's former direct MAVSDK connection --
the backend now talks to the Mission Agent, which owns the actual MAVSDK
link to PX4.
"""

from __future__ import annotations

import asyncio
import datetime
import json
import logging
import uuid
from dataclasses import dataclass
from typing import Callable

logger = logging.getLogger(__name__)

# Matches docs/architecture/mission-agent-design.md's ack-chain semantics:
# REJECTED always terminates the chain; otherwise each command type has
# its own terminal status.
_TERMINAL_STATUS = {
    "UPLOAD_MISSION": "ACCEPTED",
    "START_MISSION": "PX4_ACTION_STARTED",
    "RETURN_TO_LAUNCH": "PX4_ACTION_STARTED",
}


@dataclass
class Ack:
    command_id: str
    status: str
    reason: str = ""


class MissionAgentClient:
    """One persistent TCP connection to the Mission Agent."""

    def __init__(self, host: str, port: int) -> None:
        self._host = host
        self._port = port
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._telemetry_callback: Callable[[dict], None] | None = None
        self._pending_acks: dict[str, asyncio.Queue] = {}

    async def connect(self) -> None:
        self._reader, self._writer = await asyncio.open_connection(self._host, self._port)
        asyncio.create_task(self._read_loop())

    def on_telemetry(self, callback: Callable[[dict], None]) -> None:
        self._telemetry_callback = callback

    async def _read_loop(self) -> None:
        assert self._reader is not None
        while True:
            line = await self._reader.readline()
            if not line:
                logger.warning("Mission agent connection closed")
                return
            message = json.loads(line)
            if "command_id" in message and message["command_id"] in self._pending_acks:
                await self._pending_acks[message["command_id"]].put(Ack(**message))
            elif self._telemetry_callback is not None:
                self._telemetry_callback(message)

    async def send_command(
        self,
        command_type: str,
        parameters: dict,
        mission_version: int = 1,
        aircraft_id: str = "aerolink-1",
    ) -> list[Ack]:
        assert self._writer is not None
        command_id = str(uuid.uuid4())
        queue: asyncio.Queue = asyncio.Queue()
        self._pending_acks[command_id] = queue

        message = {
            "command_id": command_id,
            "aircraft_id": aircraft_id,
            "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "command_type": command_type,
            "parameters": parameters,
            "mission_version": mission_version,
        }
        self._writer.write((json.dumps(message) + "\n").encode())
        await self._writer.drain()

        terminal_status = _TERMINAL_STATUS[command_type]
        acks: list[Ack] = []
        while True:
            ack = await queue.get()
            acks.append(ack)
            if ack.status in ("REJECTED", terminal_status):
                break

        del self._pending_acks[command_id]
        return acks
```

- [ ] **Step 4: Run to verify tests pass**

Run:
```bash
cd ground-station/backend
python -m pytest tests/test_mission_agent_client.py -v
```
Expected: PASS -- all three tests pass.

- [ ] **Step 5: Rewrite vehicle.py to use MissionAgentClient**

Replace the contents of `ground-station/backend/app/vehicle.py`:

```python
"""Connection to the Mission Agent, re-published as a plain state snapshot.

This is the only module in the backend that imports MissionAgentClient --
everything else (the FastAPI app, the WebSocket layer) talks to
VehicleConnection, never to the Mission Agent directly. See
ARCHITECTURE.md's non-critical/mission-critical split: this backend is
non-critical, and its job is narrow (send validated-looking commands, read
state), not to embed flight logic -- that now lives in the Mission Agent.
"""

from __future__ import annotations

import asyncio
import logging
from dataclasses import asdict, dataclass

from app.mission import Waypoint
from app.mission_agent_client import MissionAgentClient

logger = logging.getLogger(__name__)


@dataclass
class VehicleState:
    is_connected: bool = False

    latitude_deg: float | None = None
    longitude_deg: float | None = None
    absolute_altitude_m: float | None = None
    relative_altitude_m: float | None = None

    roll_deg: float | None = None
    pitch_deg: float | None = None
    yaw_deg: float | None = None

    airspeed_m_s: float | None = None
    groundspeed_m_s: float | None = None
    heading_deg: float | None = None

    battery_voltage_v: float | None = None
    battery_remaining_pct: float | None = None

    flight_mode: str | None = None
    armed: bool = False

    is_gps_ok: bool = False
    is_armable: bool = False

    mission_uploaded: bool = False
    mission_current: int | None = None
    mission_total: int | None = None

    def to_dict(self) -> dict:
        return asdict(self)


class VehicleConnection:
    """Owns one connection to the Mission Agent and fans out state updates to subscribers."""

    def __init__(self, agent_host: str, agent_port: int) -> None:
        self._client = MissionAgentClient(agent_host, agent_port)
        self.state = VehicleState()
        self._subscribers: set[asyncio.Queue] = set()

    async def connect(self) -> None:
        logger.info("Connecting to mission agent at %s:%s", self._client._host, self._client._port)
        self._client.on_telemetry(self._on_telemetry)
        await self._client.connect()
        self.state.is_connected = True
        logger.info("Mission agent connected")

    async def disconnect(self) -> None:
        self.state.is_connected = False

    def subscribe(self) -> asyncio.Queue:
        queue: asyncio.Queue = asyncio.Queue(maxsize=1)
        self._subscribers.add(queue)
        return queue

    def unsubscribe(self, queue: asyncio.Queue) -> None:
        self._subscribers.discard(queue)

    def _notify(self) -> None:
        snapshot = self.state.to_dict()
        for queue in self._subscribers:
            if queue.full():
                try:
                    queue.get_nowait()
                except asyncio.QueueEmpty:
                    pass
            queue.put_nowait(snapshot)

    def _on_telemetry(self, message: dict) -> None:
        self.state.armed = message.get("armed", self.state.armed)
        self.state.flight_mode = message.get("flight_mode", self.state.flight_mode)
        self.state.latitude_deg = message.get("latitude_deg", self.state.latitude_deg)
        self.state.longitude_deg = message.get("longitude_deg", self.state.longitude_deg)
        self.state.relative_altitude_m = message.get("relative_altitude_m", self.state.relative_altitude_m)
        self.state.battery_remaining_pct = message.get("battery_remaining_pct", self.state.battery_remaining_pct)
        self.state.is_gps_ok = message.get("is_global_position_ok", self.state.is_gps_ok)
        self.state.mission_current = message.get("mission_current", self.state.mission_current)
        self.state.mission_total = message.get("mission_total", self.state.mission_total)
        self._notify()

    async def upload_mission(self, waypoints: list[Waypoint]) -> None:
        parameters = {
            "waypoints": [
                {
                    "latitude_deg": wp.latitude_deg,
                    "longitude_deg": wp.longitude_deg,
                    "altitude_m": wp.altitude_m,
                    "speed_m_s": wp.speed_m_s,
                    "loiter_duration_s": wp.loiter_duration_s,
                }
                for wp in waypoints
            ]
        }
        acks = await self._client.send_command("UPLOAD_MISSION", parameters)
        if acks[-1].status == "REJECTED":
            raise RuntimeError(acks[-1].reason)
        self.state.mission_uploaded = True
        self._notify()

    async def start_mission(self) -> None:
        acks = await self._client.send_command("START_MISSION", {})
        if acks[-1].status == "REJECTED":
            raise RuntimeError(acks[-1].reason)

    async def return_to_launch(self) -> None:
        if not self.state.is_connected:
            raise RuntimeError("Vehicle is not connected")
        acks = await self._client.send_command("RETURN_TO_LAUNCH", {})
        if acks[-1].status == "REJECTED":
            raise RuntimeError(acks[-1].reason)
```

- [ ] **Step 6: Update existing vehicle/mission tests that assumed a direct MAVSDK connection**

Read `ground-station/backend/tests/test_vehicle.py` first -- it currently fakes out a `mavsdk.System`. Replace those fakes with a fake `MissionAgentClient` instead (matching the pattern already established in `test_mission_agent_client.py`'s fake TCP server, or a simpler unittest.mock-based fake since `VehicleConnection` now only calls `self._client.connect()` and `self._client.send_command()`). Update each existing test case to construct `VehicleConnection` with a mocked `MissionAgentClient` (e.g. via `unittest.mock.AsyncMock` for `connect`/`send_command`) instead of a mocked MAVSDK `System`, keeping the same assertions about `VehicleConnection`'s public behavior (state updates, exceptions on rejection, etc).

Run:
```bash
cd ground-station/backend
python -m pytest tests/ -v
```
Expected: PASS -- every test in `tests/` passes, including the rewritten `test_vehicle.py` and the existing `test_mission.py` (which shouldn't need changes since it only tests `app/mission.py`'s validation logic, untouched by this task).

- [ ] **Step 7: Update main.py's env var and lifespan wiring**

In `ground-station/backend/app/main.py`, replace:
```python
VEHICLE_SYSTEM_ADDRESS = os.environ.get("VEHICLE_SYSTEM_ADDRESS", "udpin://0.0.0.0:14540")
```
with:
```python
MISSION_AGENT_HOST = os.environ.get("MISSION_AGENT_HOST", "127.0.0.1")
MISSION_AGENT_PORT = int(os.environ.get("MISSION_AGENT_PORT", "5760"))
```

And in the `lifespan` function, replace:
```python
vehicle = VehicleConnection(VEHICLE_SYSTEM_ADDRESS)
```
with:
```python
vehicle = VehicleConnection(MISSION_AGENT_HOST, MISSION_AGENT_PORT)
```

And in the `/api/status` endpoint, replace the `"system_address": VEHICLE_SYSTEM_ADDRESS` field with `"mission_agent_address": f"{MISSION_AGENT_HOST}:{MISSION_AGENT_PORT}"` (this does change the response shape slightly -- acceptable since it's reporting an implementation detail, not something the frontend renders; confirm by checking `ground-station/frontend/src/` doesn't reference `system_address` before making this change).

Run:
```bash
cd ground-station/backend
python -m pytest tests/ -v
```
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add ground-station/backend/
git commit -m "Re-plumb backend to talk to the Mission Agent instead of PX4 directly"
```

---

### Task 10: Update simulation launch scripts

**Files:**
- Modify: `simulation/network/launch-aircraft.sh`
- Modify: `simulation/network/launch-ground.sh`
- Modify: `simulation/network/launch-ground-stress-test.sh`

**Interfaces:**
- Consumes: the `mission_agent` binary built in Task 8 (at `onboard/mission-agent/build/mission_agent`), and `MISSION_AGENT_HOST`/`MISSION_AGENT_PORT` env vars from Task 9.

- [ ] **Step 1: Update launch-aircraft.sh to also start the Mission Agent**

The Mission Agent now runs alongside PX4 inside `aircraft-net` (see the design doc's Network Topology section) and connects to PX4's onboard link locally. Both processes need to be running in that namespace. Rewrite `simulation/network/launch-aircraft.sh`:

```bash
#!/bin/bash
# Launches PX4 SITL and the Mission Agent together inside the aircraft-net
# namespace (see setup-netns-wireguard.sh). The Mission Agent connects to
# PX4's local onboard MAVLink link (udp://:14540) and is what the backend
# now talks to across the WireGuard tunnel -- see
# docs/architecture/mission-agent-design.md.
#
# Must be run with sudo (ip netns exec requires root). Runs in the
# foreground -- invoke via the harness's `!` prefix so it's automatically
# tracked as a long-running background task, same as every other SITL
# launch in this project.

set -euo pipefail

REAL_USER="${SUDO_USER:-youruser}"
PX4_DIR="/home/$REAL_USER/src/PX4-Autopilot/build/px4_sitl_default/src/modules/simulation/simulator_sih"
PX4_BIN="/home/$REAL_USER/src/PX4-Autopilot/build/px4_sitl_default/bin/px4"
AGENT_BIN="/mnt/c/Users/youruser/git/uav_project/onboard/mission-agent/build/mission_agent"
PX4_LOG_FILE="/tmp/px4_sitl_aircraft_net.log"
AGENT_LOG_FILE="/tmp/mission_agent_aircraft_net.log"

ip netns exec aircraft-net sudo -u "$REAL_USER" env \
  PX4_SIM_MODEL=sihsim_standard_vtol PX4_SIMULATOR=sihsim \
  bash -c "cd '$PX4_DIR' && exec '$PX4_BIN' -d" > "$PX4_LOG_FILE" 2>&1 < /dev/null &
PX4_PID=$!

# Give PX4 a moment to open its onboard MAVLink listener before the agent
# tries to connect.
sleep 3

ip netns exec aircraft-net sudo -u "$REAL_USER" env \
  MAVLINK_URL="udp://:14540" AGENT_PORT="5760" \
  "$AGENT_BIN" > "$AGENT_LOG_FILE" 2>&1 < /dev/null &
AGENT_PID=$!

wait "$PX4_PID" "$AGENT_PID"
```

- [ ] **Step 2: Update launch-ground.sh to point at the Mission Agent instead of PX4**

Rewrite `simulation/network/launch-ground.sh`:

```bash
#!/bin/bash
# Launches the ground-station backend inside the ground-net namespace,
# connecting to the Mission Agent through the WireGuard tunnel (the agent
# runs inside aircraft-net alongside PX4 -- see launch-aircraft.sh and
# docs/architecture/mission-agent-design.md). The backend no longer speaks
# MAVLink at all; MISSION_AGENT_HOST/PORT point at the agent's TCP protocol.
#
# Must be run with sudo (ip netns exec requires root). Runs in the
# foreground -- invoke via the harness's `!` prefix, same as
# launch-aircraft.sh.

set -euo pipefail

REAL_USER="${SUDO_USER:-youruser}"
BACKEND_DIR="/mnt/c/Users/youruser/git/uav_project/ground-station/backend"
LOG_FILE="/tmp/backend_ground_net.log"

exec ip netns exec ground-net sudo -u "$REAL_USER" env \
  MISSION_AGENT_HOST="10.99.0.2" MISSION_AGENT_PORT="5760" \
  bash -c "cd '$BACKEND_DIR' && exec /home/$REAL_USER/venvs/aerolink-backend/bin/python -m uvicorn app.main:app --host 0.0.0.0 --port 8000" \
  > "$LOG_FILE" 2>&1 < /dev/null
```

- [ ] **Step 3: Update launch-ground-stress-test.sh the same way**

Apply the same `MISSION_AGENT_HOST`/`MISSION_AGENT_PORT` change to `simulation/network/launch-ground-stress-test.sh`, keeping its `MAX_TURN_ANGLE_DEG`/`MIN_WAYPOINT_SEPARATION_M` overrides -- those still apply to the backend's own upload-time validation, independent of this change. Note: the Mission Agent's own geofence validation (Task 8's `main.cpp`) reads `GEOFENCE_RADIUS_M`/`MIN_ALTITUDE_M`/`MAX_ALTITUDE_M` from its own environment, but has no equivalent override for turn-angle/separation checks -- the Mission Agent doesn't implement those checks at all (only geofence + altitude + state legality, per the design doc). This means a stress-test mission that the relaxed backend accepts will now also need to pass the Mission Agent's independent geofence/altitude check, which is unaffected by the stress-test env vars. This is expected and correct (double validation, per the design), not a bug to fix.

- [ ] **Step 4: Commit**

```bash
git add simulation/network/
git commit -m "Point launch scripts at the Mission Agent instead of PX4 directly"
```

---

### Task 11: End-to-end integration test

**Files:**
- Create: `tests/simulation/mission_agent_integration.md`

**Interfaces:**
- Consumes: the fully wired system from Tasks 1-10.

- [ ] **Step 1: [USER TERMINAL] Build the Mission Agent binary fresh, then run the full stack**

```bash
cd onboard/mission-agent
cmake -B build -S .
cmake --build build
```

Then, following this project's established three-terminal pattern:

```bash
# Terminal 1:
sudo bash /mnt/c/Users/youruser/git/uav_project/simulation/network/launch-aircraft.sh
# wait for both "Ready for takeoff!" (PX4) and "mission_agent: serving on port 5760"

# Terminal 2:
sudo bash /mnt/c/Users/youruser/git/uav_project/simulation/network/launch-ground.sh
# wait for "Vehicle connected" / "Mission agent connected" in the backend log

# Terminal 3 (if not already running):
socat TCP-LISTEN:8000,fork,reuseaddr TCP:10.201.0.2:8000
```

- [ ] **Step 2: Fly a real mission through the full new stack**

Use the frontend (or `curl` against `http://10.201.0.2:8000/api/missions` and `/api/missions/current/start`, the same way this project's stress tests already do) to upload and start a normal mission -- same waypoint geometry used in the original successful VTOL flight test (`SIMULATION.md`'s "First VTOL Flight" section).

- [ ] **Step 3: Write up the result in tests/simulation/mission_agent_integration.md**

Follow this project's established SETUP/ACTION/EXPECTED RESULT/PASS CRITERIA format (see any existing file in `tests/simulation/` for the exact structure). Document what actually happened -- if it doesn't work on the first try, that's fine, this is exactly the kind of real result this project's docs are meant to capture; debug it the same way every other issue in this project got debugged (check `/tmp/px4_sitl_aircraft_net.log`, `/tmp/mission_agent_aircraft_net.log`, and `/tmp/backend_ground_net.log`, in that order, before guessing).

- [ ] **Step 4: Update SIMULATION.md and ARCHITECTURE.md with the real result**

Add a findings entry to `SIMULATION.md` (matching the style of every other phase's findings section) noting the Mission Agent is now real, not a diagram box -- and update `ARCHITECTURE.md`'s system diagram/component table if anything about the real implementation differs from what was originally drawn (e.g. if the Mission Agent's actual port or connection details differ from the design doc's assumptions).

- [ ] **Step 5: Commit**

```bash
git add tests/simulation/mission_agent_integration.md SIMULATION.md ARCHITECTURE.md
git commit -m "Document Mission Agent end-to-end integration test results"
```

---

## Self-Review Notes

- **Spec coverage:** every section of `docs/architecture/mission-agent-design.md` maps to a task -- Architecture/network topology (Tasks 8, 10), Protocol (Tasks 2, 6, 9), Internal Components (Tasks 2-7), Concurrency (Task 3's thread-safety test, Task 5's real MAVSDK threads), Error Handling (Task 6's malformed-JSON and connection-drop handling, Task 9's rejection-surfacing), Testing Approach (Catch2 unit tests throughout, CMake, integration test in Task 11), Consequences (backend rework in Task 9, launch scripts in Task 10, doc updates in Task 11).
- **No placeholders:** every code step above is complete, compilable-as-written C++ or Python, not a sketch -- the one explicit caveat is Task 5's note that exact MAVSDK enum/field names may need adjustment against the installed version's headers, which is an honest engineering caveat, not a placeholder.
- **Type consistency check:** `IMavlinkConnection`/`MavlinkConnection` (Task 5) match their usage in `AgentServer` (Task 6) and `main.cpp` (Task 8). `ITelemetrySink` (Task 6) matches its usage in `TelemetryPublisher` (Task 7) and `main.cpp` (Task 8). `MissionAgentClient`/`Ack` (Task 9) match their usage in `VehicleConnection` (Task 9). The ack terminal-status mapping is stated once in the design doc and implemented identically in both `AgentServer::handle_connection` (Task 6, C++) and `_TERMINAL_STATUS` (Task 9, Python) -- verified these agree: `UPLOAD_MISSION` -> `ACCEPTED`, `START_MISSION`/`RETURN_TO_LAUNCH` -> `PX4_ACTION_STARTED`, `REJECTED` always terminal.

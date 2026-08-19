// onboard/mission-agent/tests/test_telemetry_publisher.cpp
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
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

TEST_CASE("serialize_snapshot includes every field the frontend renders", "[telemetry_publisher]") {
    // The frontend's TelemetryPanel.tsx and MapView.tsx read these exact
    // names off VehicleState; anything missing here renders as "--" forever
    // (and MapView stops rotating the aircraft marker).
    StateSnapshot s;
    s.armed = true;
    s.flight_mode = "MISSION";
    s.latitude_deg = 47.4;
    s.longitude_deg = 8.5;
    s.relative_altitude_m = 30.0f;
    s.absolute_altitude_m = 518.0f;
    s.roll_deg = 1.5f;
    s.pitch_deg = -2.5f;
    s.yaw_deg = 91.0f;
    s.airspeed_m_s = 16.5f;
    s.groundspeed_m_s = 18.25f;
    s.heading_deg = 274.0f;
    s.battery_remaining_pct = 85.5f;
    s.battery_voltage_v = 22.1f;
    s.is_global_position_ok = true;
    s.is_home_position_ok = true;
    s.is_armable = true;
    s.mission_current = 2;
    s.mission_total = 4;

    auto j = nlohmann::json::parse(TelemetryPublisher::serialize_snapshot(s));

    REQUIRE(j["armed"] == true);
    REQUIRE(j["flight_mode"] == "MISSION");
    REQUIRE(j["latitude_deg"] == 47.4);
    REQUIRE(j["longitude_deg"] == 8.5);
    REQUIRE(j["relative_altitude_m"] == 30.0f);
    REQUIRE(j["absolute_altitude_m"] == 518.0f);
    REQUIRE(j["roll_deg"] == 1.5f);
    REQUIRE(j["pitch_deg"] == -2.5f);
    REQUIRE(j["yaw_deg"] == 91.0f);
    REQUIRE(j["airspeed_m_s"] == 16.5f);
    REQUIRE(j["groundspeed_m_s"] == 18.25f);
    REQUIRE(j["heading_deg"] == 274.0f);
    REQUIRE(j["battery_remaining_pct"] == 85.5f);
    REQUIRE(j["battery_voltage_v"] == 22.1f);
    REQUIRE(j["is_global_position_ok"] == true);
    REQUIRE(j["is_home_position_ok"] == true);
    REQUIRE(j["is_armable"] == true);
    REQUIRE(j["mission_current"] == 2);
    REQUIRE(j["mission_total"] == 4);
}

TEST_CASE("TelemetryPublisher pushes snapshots at roughly the configured interval", "[telemetry_publisher]") {
    StateTracker tracker;
    tracker.set_position(47.4, 8.5, 30.0f, 518.0f);
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

TEST_CASE("TelemetryPublisher handles double start() safely as a no-op", "[telemetry_publisher]") {
    StateTracker tracker;
    RecordingSink sink;

    TelemetryPublisher publisher(tracker, sink, std::chrono::milliseconds(30));
    publisher.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    size_t count_after_first = sink.count();

    // Second start() should be a no-op and not crash
    publisher.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    size_t count_after_second = sink.count();

    publisher.stop();

    // Publisher should still be pushing after double start()
    REQUIRE(count_after_first >= 1);
    REQUIRE(count_after_second > count_after_first);
}

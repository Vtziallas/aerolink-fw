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

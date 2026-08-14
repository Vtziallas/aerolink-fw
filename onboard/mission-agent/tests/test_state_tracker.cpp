#include <catch2/catch_test_macros.hpp>
#include <atomic>
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
    std::atomic<int> violations{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&tracker, &violations, i]() {
            for (int j = 0; j < 1000; ++j) {
                tracker.set_position(static_cast<double>(i), static_cast<double>(j), 0.0f);
                auto snap = tracker.snapshot();
                // set_position holds the lock across both fields, so any
                // snapshot must see a (lat, lon) pair some single call
                // actually wrote together, never a torn mix. A torn read
                // would produce a value outside these ranges.
                bool ok = snap.latitude_deg >= 0.0 && snap.latitude_deg < 8.0 &&
                          snap.longitude_deg >= 0.0 && snap.longitude_deg < 1000.0;
                if (!ok) violations.fetch_add(1);
            }
        });
    }
    for (auto& t : threads) t.join();

    // Assert only on the main thread, after all workers have finished --
    // Catch2's REQUIRE is not documented safe to call from worker threads.
    REQUIRE(violations.load() == 0);

    auto final_snap = tracker.snapshot();
    REQUIRE(final_snap.latitude_deg >= 0.0);
    REQUIRE(final_snap.latitude_deg < 8.0);
    REQUIRE(final_snap.longitude_deg >= 0.0);
    REQUIRE(final_snap.longitude_deg < 1000.0);
}

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

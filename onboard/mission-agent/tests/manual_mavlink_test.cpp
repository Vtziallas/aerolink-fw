// onboard/mission-agent/tests/manual_mavlink_test.cpp
// Not part of the Catch2 suite -- run manually against real PX4 SITL.
#include <chrono>
#include <iostream>
#include <thread>
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

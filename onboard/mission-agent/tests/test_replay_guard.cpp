#include <catch2/catch_test_macros.hpp>
#include "../src/replay_guard.h"
#include <cstdio>
#include <ctime>
#include <functional>

using namespace mission_agent;

namespace {

std::string iso8601_utc(std::chrono::system_clock::time_point tp, const char* suffix = "Z") {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string(buf) + suffix;
}

// A controllable fake clock: ReplayGuard::check() invokes now_fn once per
// call, so tests can advance "now" between calls to exercise aging/pruning.
class FakeClock {
public:
    explicit FakeClock(std::chrono::system_clock::time_point start) : now_(start) {}
    std::chrono::system_clock::time_point operator()() const { return now_; }
    void advance(std::chrono::seconds delta) { now_ += delta; }

private:
    std::chrono::system_clock::time_point now_;
};

}  // namespace

TEST_CASE("ReplayGuard accepts a fresh, unseen command", "[replay_guard]") {
    auto now = std::chrono::system_clock::now();
    ReplayGuard guard(std::chrono::seconds(30), [now] { return now; });

    REQUIRE_FALSE(guard.check("cmd-1", iso8601_utc(now)).has_value());
}

TEST_CASE("ReplayGuard rejects a duplicate command_id within the window", "[replay_guard]") {
    auto now = std::chrono::system_clock::now();
    ReplayGuard guard(std::chrono::seconds(30), [now] { return now; });

    REQUIRE_FALSE(guard.check("cmd-1", iso8601_utc(now)).has_value());
    auto second = guard.check("cmd-1", iso8601_utc(now));
    REQUIRE(second.has_value());
    REQUIRE(second->find("already processed") != std::string::npos);
}

TEST_CASE("ReplayGuard rejects a timestamp older than the window", "[replay_guard]") {
    auto now = std::chrono::system_clock::now();
    ReplayGuard guard(std::chrono::seconds(30), [now] { return now; });

    auto result = guard.check("cmd-1", iso8601_utc(now - std::chrono::seconds(31)));
    REQUIRE(result.has_value());
    REQUIRE(result->find("freshness window") != std::string::npos);
}

TEST_CASE("ReplayGuard rejects a timestamp from the future beyond the window", "[replay_guard]") {
    auto now = std::chrono::system_clock::now();
    ReplayGuard guard(std::chrono::seconds(30), [now] { return now; });

    auto result = guard.check("cmd-1", iso8601_utc(now + std::chrono::seconds(31)));
    REQUIRE(result.has_value());
}

TEST_CASE("ReplayGuard accepts a timestamp comfortably inside the window", "[replay_guard]") {
    auto now = std::chrono::system_clock::now();
    ReplayGuard guard(std::chrono::seconds(30), [now] { return now; });

    REQUIRE_FALSE(guard.check("cmd-1", iso8601_utc(now - std::chrono::seconds(10))).has_value());
}

TEST_CASE("ReplayGuard rejects a malformed timestamp", "[replay_guard]") {
    auto now = std::chrono::system_clock::now();
    ReplayGuard guard(std::chrono::seconds(30), [now] { return now; });

    auto result = guard.check("cmd-1", "not-a-timestamp");
    REQUIRE(result.has_value());
    REQUIRE(result->find("could not be parsed") != std::string::npos);
}

TEST_CASE("ReplayGuard accepts a numeric UTC offset suffix, not just Z", "[replay_guard]") {
    // Python's datetime.now(timezone.utc).isoformat() -- the real backend's
    // format -- produces "+00:00", not "Z".
    auto now = std::chrono::system_clock::now();
    ReplayGuard guard(std::chrono::seconds(30), [now] { return now; });

    REQUIRE_FALSE(guard.check("cmd-1", iso8601_utc(now, "+00:00")).has_value());
}

TEST_CASE("ReplayGuard allows the same command_id again once it has aged out of the window", "[replay_guard]") {
    auto now = std::chrono::system_clock::now();
    FakeClock clock(now);
    ReplayGuard guard(std::chrono::seconds(30), std::ref(clock));

    REQUIRE_FALSE(guard.check("cmd-1", iso8601_utc(now)).has_value());

    clock.advance(std::chrono::seconds(31));
    auto later = clock();
    REQUIRE_FALSE(guard.check("cmd-1", iso8601_utc(later)).has_value());
}

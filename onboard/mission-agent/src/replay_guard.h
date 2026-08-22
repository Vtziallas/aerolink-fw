// onboard/mission-agent/src/replay_guard.h
#pragma once
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace mission_agent {

// Rejects commands whose timestamp has fallen outside a sliding freshness
// window (in either direction -- a forged far-future timestamp must not be
// able to stay "fresh" forever) or whose command_id has already been seen
// within that window. See NETWORKING.md's replay-protection threat-model
// entry -- this is the mission agent's half of "command_id nonce cache +
// timestamp window".
class ReplayGuard {
public:
    // now_fn exists purely so tests can inject a fixed clock; production
    // code should use the default.
    explicit ReplayGuard(std::chrono::seconds window = std::chrono::seconds(30),
                          std::function<std::chrono::system_clock::time_point()> now_fn =
                              &std::chrono::system_clock::now);

    // Returns a rejection reason if the command should be rejected as stale
    // or previously seen. Otherwise records command_id as seen (at the
    // current time, per now_fn) and returns nullopt.
    std::optional<std::string> check(const std::string& command_id, const std::string& timestamp);

private:
    std::chrono::seconds window_;
    std::function<std::chrono::system_clock::time_point()> now_fn_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> seen_;

    // Drops entries older than window_ so seen_ can't grow without bound
    // over a long-running agent process.
    void prune(std::chrono::system_clock::time_point now);
};

}  // namespace mission_agent

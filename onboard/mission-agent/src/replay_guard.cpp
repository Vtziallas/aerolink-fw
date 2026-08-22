// onboard/mission-agent/src/replay_guard.cpp
#include "replay_guard.h"
#include <cctype>
#include <cstdio>
#include <ctime>

namespace mission_agent {

namespace {

// Parses "YYYY-MM-DDTHH:MM:SS[.ffffff](Z|+HH:MM|-HH:MM)" into a UTC time
// point. Fractional seconds are consumed but discarded -- irrelevant at a
// 30s freshness granularity. A naive timestamp with no "Z"/offset is
// rejected rather than guessed at, matching this codebase's fail-closed
// posture (see CommandValidator's check_position_settled).
std::optional<std::chrono::system_clock::time_point> parse_iso8601_utc(const std::string& s) {
    int year, month, day, hour, min, sec;
    int consumed = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d%n", &year, &month, &day, &hour, &min, &sec, &consumed) != 6) {
        return std::nullopt;
    }

    size_t i = static_cast<size_t>(consumed);
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    }

    long offset_seconds = 0;
    if (i < s.size() && s[i] == 'Z') {
        ++i;
        if (i != s.size()) return std::nullopt;
    } else if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        char sign = s[i];
        int off_h = 0, off_m = 0, n = 0;
        if (std::sscanf(s.c_str() + i, "%*c%2d:%2d%n", &off_h, &off_m, &n) != 2) {
            return std::nullopt;
        }
        if (i + static_cast<size_t>(n) != s.size()) return std::nullopt;
        offset_seconds = (sign == '+' ? 1 : -1) * (off_h * 3600 + off_m * 60);
    } else {
        return std::nullopt;
    }

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    std::time_t utc_epoch = timegm(&tm);
    if (utc_epoch == static_cast<std::time_t>(-1)) return std::nullopt;
    utc_epoch -= offset_seconds;
    return std::chrono::system_clock::from_time_t(utc_epoch);
}

}  // namespace

ReplayGuard::ReplayGuard(std::chrono::seconds window,
                          std::function<std::chrono::system_clock::time_point()> now_fn)
    : window_(window), now_fn_(std::move(now_fn)) {}

void ReplayGuard::prune(std::chrono::system_clock::time_point now) {
    for (auto it = seen_.begin(); it != seen_.end();) {
        if (now - it->second > window_) {
            it = seen_.erase(it);
        } else {
            ++it;
        }
    }
}

std::optional<std::string> ReplayGuard::check(const std::string& command_id, const std::string& timestamp) {
    auto now = now_fn_();
    prune(now);

    auto parsed = parse_iso8601_utc(timestamp);
    if (!parsed) {
        return "command timestamp '" + timestamp + "' could not be parsed";
    }

    auto age = now - *parsed;
    if (age > window_ || age < -window_) {
        return "command timestamp is outside the freshness window";
    }

    if (seen_.count(command_id) != 0) {
        return "command_id '" + command_id + "' was already processed (possible replay)";
    }

    seen_[command_id] = now;
    return std::nullopt;
}

}  // namespace mission_agent

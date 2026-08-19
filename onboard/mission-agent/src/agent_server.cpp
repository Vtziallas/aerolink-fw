// onboard/mission-agent/src/agent_server.cpp
#include "agent_server.h"
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

namespace mission_agent {

namespace {

// How long send_ack is willing to wait for a backed-up socket to drain.
// Acks are the operator-visible half of the protocol -- a dropped ack
// leaves the backend waiting on a command it will never hear about -- so
// they get a real (if bounded) retry budget, unlike telemetry samples.
constexpr int kAckWriteBudgetMs = 2000;

// Telemetry gets no waiting budget at all: if the socket is full, the
// newest sample is dropped rather than blocking the command path behind a
// degraded link. A dropped telemetry sample is stale data nobody misses;
// a blocked write holds client_mutex_ and wedges acks too.
constexpr int kTelemetryWriteBudgetMs = 0;

// Once part of a line is on the wire we can't just give up -- a truncated
// JSON line corrupts the framing for everything after it. Wait this long
// to finish the line, then treat the client as gone.
constexpr int kPartialLineFlushBudgetMs = 500;

constexpr int kPollSliceMs = 50;

}  // namespace

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
        // Non-blocking so a backend that has stopped reading (a degraded
        // tunnel rather than a clean disconnect) can never park a write --
        // and with it client_mutex_, and with it the whole command path --
        // inside the kernel's send buffer.
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            std::cerr << "mission_agent: could not set client socket non-blocking: "
                      << std::strerror(errno) << "\n";
            close(fd);
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

AgentServer::WriteOutcome AgentServer::write_payload(int fd, const std::string& payload,
                                                    int initial_budget_ms) {
    size_t sent = 0;
    int waited_ms = 0;

    while (sent < payload.size()) {
        // send(..., MSG_NOSIGNAL) rather than write(): a peer that has gone
        // away must surface as EPIPE here, never as a SIGPIPE. main() also
        // ignores SIGPIPE process-wide, but this keeps the class safe on its
        // own (and testable without main()'s signal setup).
        ssize_t n = send(fd, payload.data() + sent, payload.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            const int budget = (sent == 0) ? initial_budget_ms : kPartialLineFlushBudgetMs;
            if (waited_ms >= budget) {
                // Nothing on the wire yet -- the caller can drop this
                // payload and the stream stays well-formed. If we'd already
                // written part of the line, the framing is broken and the
                // only honest thing left is to drop the client.
                return sent == 0 ? WriteOutcome::WouldBlock : WriteOutcome::ClientGone;
            }
            pollfd pfd{fd, POLLOUT, 0};
            const int slice = std::min(kPollSliceMs, budget - waited_ms);
            const int ready = poll(&pfd, 1, slice);
            if (ready < 0 && errno != EINTR) return WriteOutcome::ClientGone;
            if (ready > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
                return WriteOutcome::ClientGone;
            }
            waited_ms += slice;
            continue;
        }
        // EPIPE / ECONNRESET / EBADF: the peer is gone. SIGPIPE is ignored
        // process-wide (see main.cpp), so this surfaces as an error return
        // rather than killing the agent.
        return WriteOutcome::ClientGone;
    }
    return WriteOutcome::Ok;
}

void AgentServer::send_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_fd_ < 0) return;  // no-op if nobody's connected
    std::string payload = line;
    if (payload.empty() || payload.back() != '\n') payload += "\n";

    switch (write_payload(client_fd_, payload, kTelemetryWriteBudgetMs)) {
        case WriteOutcome::Ok:
            break;
        case WriteOutcome::WouldBlock:
            // Backpressure: the backend isn't draining fast enough. Dropping
            // the newest telemetry sample is the correct trade -- it's a
            // sampled stream, and blocking here would stall acks too.
            break;
        case WriteOutcome::ClientGone:
            // Don't leave client_fd_ pointing at a dead descriptor; the next
            // telemetry push (250ms away, on another thread) would otherwise
            // write to it again.
            client_fd_ = -1;
            break;
    }
}

bool AgentServer::send_ack(int client_fd, const Ack& ack) {
    const std::string line = serialize_ack(ack);
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_fd_ < 0) return false;

    const WriteOutcome outcome = write_payload(client_fd, line, kAckWriteBudgetMs);
    if (outcome == WriteOutcome::Ok) return true;

    if (outcome == WriteOutcome::WouldBlock) {
        // Backed up for the whole ack budget. Nothing was written, so the
        // stream is still well-formed, but the backend is not keeping up
        // with a link this degraded -- log it loudly and keep the
        // connection; the backend's own ack timeout will surface it.
        std::cerr << "mission_agent: dropped " << to_string(ack.status)
                  << " ack for command_id=" << ack.command_id
                  << " (client not reading for " << kAckWriteBudgetMs << "ms)\n";
        return true;
    }

    if (client_fd_ == client_fd) client_fd_ = -1;
    return false;
}

void AgentServer::handle_connection(int client_fd) {
    std::string buffer;
    char chunk[1024];
    while (running_) {
        // The client socket is non-blocking (see run()), so poll for
        // readability instead of parking in read(). The timeout also keeps
        // running_ checked regularly so stop() is observed promptly.
        pollfd pfd{client_fd, POLLIN, 0};
        const int ready = poll(&pfd, 1, 200);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (ready == 0) continue;

        ssize_t n = read(client_fd, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            return;
        }
        if (n == 0) return;  // connection closed -- SAFETY.md's LTE_LOST case, not an error to log loudly
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
                // command_id is unknowable when the JSON itself didn't parse,
                // so this ack can't be routed to an in-flight command on the
                // backend -- it's logged there as an unmatched ack instead
                // (see mission_agent_client.py's _read_until_closed).
                if (!send_ack(client_fd, Ack{"", AckStatus::Rejected, "malformed command"})) return;
                continue;
            }

            if (!send_ack(client_fd, Ack{command.command_id, AckStatus::Received, ""})) return;

            auto validation = validator_.validate(command, state_tracker_.snapshot());
            if (!validation.is_valid) {
                if (!send_ack(client_fd, Ack{command.command_id, AckStatus::Rejected, validation.reason})) return;
                continue;
            }
            if (!send_ack(client_fd, Ack{command.command_id, AckStatus::Validated, ""})) return;

            // The design doc's "PX4 connection lost/not yet established"
            // case: reject clearly rather than handing the command to a
            // MAVSDK plugin with no system behind it and hanging on its
            // internal timeout.
            if (!mavlink_.is_connected()) {
                if (!send_ack(client_fd, Ack{command.command_id, AckStatus::Rejected, "PX4 link down"})) return;
                continue;
            }

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
                if (!send_ack(client_fd, Ack{command.command_id, AckStatus::Rejected, result.error_message})) return;
                continue;
            }
            if (!send_ack(client_fd, Ack{command.command_id, AckStatus::Accepted, ""})) return;
            if (command.command_type == CommandType::StartMission || command.command_type == CommandType::ReturnToLaunch) {
                if (!send_ack(client_fd, Ack{command.command_id, AckStatus::Px4ActionStarted, ""})) return;
            }
        }
    }
}

}  // namespace mission_agent

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
    std::lock_guard<std::mutex> lock(client_mutex_);
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
            send_ack(client_fd, Ack{command.command_id, AckStatus::Accepted, ""});
            if (command.command_type == CommandType::StartMission || command.command_type == CommandType::ReturnToLaunch) {
                send_ack(client_fd, Ack{command.command_id, AckStatus::Px4ActionStarted, ""});
            }
        }
    }
}

}  // namespace mission_agent

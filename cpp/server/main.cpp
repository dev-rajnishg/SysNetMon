#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.hpp"
#include "platform.hpp"

namespace {

using sysnetmon::AlertEvent;
using sysnetmon::AlertRule;
using sysnetmon::ChatMessage;
using sysnetmon::MetricSnapshot;

volatile std::sig_atomic_t g_running = 1;

enum class ClientRole {
    Unknown,
    Agent,
    Dashboard,
};

struct ClientConnection {
    sysnetmon::socket_t fd = sysnetmon::invalid_socket;
    ClientRole role = ClientRole::Unknown;
    std::string peer_label;
    std::string declared_host;
    std::string buffer;
};

void signal_handler(int) {
    g_running = 0;
}

std::string endpoint_label(const sockaddr_storage &storage, socklen_t length) {
    (void) length;
    const sockaddr_in *address = reinterpret_cast<const sockaddr_in *>(&storage);
    const char *host = inet_ntoa(address->sin_addr);
    if (host != nullptr) {
        return std::string(host) + ":" + std::to_string(ntohs(address->sin_port));
    }
    return "unknown-peer";
}

class MonitorServer {
public:
    explicit MonitorServer(int port) : port_(port) {}

    bool start() {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (!sysnetmon::is_socket_valid(listen_fd_)) {
            std::cerr << "socket failed: " << sysnetmon::last_socket_error() << std::endl;
            return false;
        }

        const int reuse = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
            reinterpret_cast<const char *>(&reuse), sizeof(reuse));

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(static_cast<unsigned short>(port_));
        if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
            std::cerr << "bind failed: " << sysnetmon::last_socket_error() << std::endl;
            sysnetmon::close_socket(listen_fd_);
            listen_fd_ = sysnetmon::invalid_socket;
            return false;
        }

        if (listen(listen_fd_, 32) != 0) {
            std::cerr << "listen failed: " << sysnetmon::last_socket_error() << std::endl;
            sysnetmon::close_socket(listen_fd_);
            listen_fd_ = sysnetmon::invalid_socket;
            return false;
        }

        std::cout << "SysNetMon server listening on port " << port_ << std::endl;
        return true;
    }

    void run() {
        while (g_running) {
            fd_set read_set;
            fd_set error_set;
            FD_ZERO(&read_set);
            FD_ZERO(&error_set);

            FD_SET(listen_fd_, &read_set);
            FD_SET(listen_fd_, &error_set);
            sysnetmon::socket_t highest_fd = listen_fd_;
            for (const auto &client : clients_) {
                FD_SET(client.fd, &read_set);
                FD_SET(client.fd, &error_set);
                if (client.fd > highest_fd) {
                    highest_fd = client.fd;
                }
            }

            timeval timeout {};
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            const int ready = select(sysnetmon::socket_select_width(highest_fd), &read_set, nullptr, &error_set, &timeout);
            if (ready < 0) {
#ifndef _WIN32
                if (errno == EINTR) {
                    continue;
                }
#endif
                std::cerr << "select failed: " << sysnetmon::last_socket_error() << std::endl;
                break;
            }

            if (FD_ISSET(listen_fd_, &read_set)) {
                accept_connection();
            }

            std::vector<std::size_t> disconnect_indices;
            for (std::size_t index = 0; index < clients_.size(); ++index) {
                if (FD_ISSET(clients_[index].fd, &error_set)) {
                    disconnect_indices.push_back(index);
                    continue;
                }
                if (FD_ISSET(clients_[index].fd, &read_set)) {
                    if (!read_client(index)) {
                        disconnect_indices.push_back(index);
                    }
                }
            }

            for (auto it = disconnect_indices.rbegin(); it != disconnect_indices.rend(); ++it) {
                disconnect_client(*it);
            }
        }

        shutdown_all();
    }

private:
    void accept_connection() {
        sockaddr_storage client_addr {};
        socklen_t client_len = sizeof(client_addr);
        const sysnetmon::socket_t client_fd = accept(listen_fd_, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        if (!sysnetmon::is_socket_valid(client_fd)) {
            std::cerr << "accept failed: " << sysnetmon::last_socket_error() << std::endl;
            return;
        }
        ClientConnection connection;
        connection.fd = client_fd;
        connection.peer_label = endpoint_label(client_addr, client_len);
        clients_.push_back(connection);
        std::cout << "Accepted connection from " << connection.peer_label << std::endl;
    }

    bool read_client(std::size_t index) {
        char buffer[4096];
        const int bytes_read = static_cast<int>(recv(clients_[index].fd, buffer, sizeof(buffer), 0));
        if (bytes_read <= 0) {
            return false;
        }
        clients_[index].buffer.append(buffer, static_cast<std::size_t>(bytes_read));

        auto &pending = clients_[index].buffer;
        std::size_t newline = pending.find('\n');
        while (newline != std::string::npos) {
            std::string line = sysnetmon::trim(pending.substr(0, newline));
            pending.erase(0, newline + 1);
            if (!line.empty()) {
                handle_line(index, line);
            }
            newline = pending.find('\n');
        }

        return true;
    }

    void handle_line(std::size_t index, const std::string &line) {
        auto &client = clients_[index];
        const std::string type = sysnetmon::json_get_string(line, "type").value_or("");
        if (type == "register") {
            const std::string role = sysnetmon::lower_copy(sysnetmon::json_get_string(line, "role").value_or(""));
            client.declared_host = sysnetmon::json_get_string(line, "host").value_or(client.peer_label);
            if (role == "agent") {
                client.role = ClientRole::Agent;
            } else if (role == "dashboard") {
                client.role = ClientRole::Dashboard;
            }
            sysnetmon::send_line(client.fd, sysnetmon::ack_event_to_json("registered as " + role));
            if (client.role == ClientRole::Dashboard) {
                sysnetmon::send_line(client.fd, sysnetmon::snapshot_event_to_json(latest_metrics_));
                sysnetmon::send_line(client.fd, sysnetmon::rules_event_to_json(alert_rules_));
            }
            return;
        }

        if (type == "metrics" && client.role == ClientRole::Agent) {
            MetricSnapshot metric;
            metric.host = sysnetmon::json_get_string(line, "host").value_or(client.declared_host);
            metric.timestamp = sysnetmon::json_get_string(line, "timestamp").value_or(sysnetmon::iso_timestamp());
            metric.cpu_percent = sysnetmon::json_get_number(line, "cpu_percent").value_or(0.0);
            metric.memory_percent = sysnetmon::json_get_number(line, "memory_percent").value_or(0.0);
            metric.disk_percent = sysnetmon::json_get_number(line, "disk_percent").value_or(0.0);
            metric.network_rx_kbps = sysnetmon::json_get_number(line, "network_rx_kbps").value_or(0.0);
            metric.network_tx_kbps = sysnetmon::json_get_number(line, "network_tx_kbps").value_or(0.0);
            metric.memory_used_mb = sysnetmon::json_get_integer(line, "memory_used_mb").value_or(0);
            metric.memory_total_mb = sysnetmon::json_get_integer(line, "memory_total_mb").value_or(0);
            metric.disk_used_mb = sysnetmon::json_get_integer(line, "disk_used_mb").value_or(0);
            metric.disk_total_mb = sysnetmon::json_get_integer(line, "disk_total_mb").value_or(0);
            latest_metrics_[metric.host] = metric;
            broadcast_to_dashboards(sysnetmon::metric_event_to_json(metric));
            evaluate_rules(metric);
            return;
        }

        if (type == "command") {
            const std::string text = sysnetmon::json_get_string(line, "text").value_or("");
            handle_command(client, text);
            return;
        }

        if (type == "chat") {
            ChatMessage message;
            message.sender = sysnetmon::json_get_string(line, "from").value_or(client.declared_host.empty() ? client.peer_label : client.declared_host);
            message.text = sysnetmon::json_get_string(line, "text").value_or("");
            message.timestamp = sysnetmon::iso_timestamp();
            broadcast_to_dashboards(sysnetmon::chat_event_to_json(message));
            return;
        }

        sysnetmon::send_line(client.fd, sysnetmon::error_event_to_json("unsupported message"));
    }

    void handle_command(ClientConnection &client, const std::string &text) {
        const std::string lowered = sysnetmon::lower_copy(sysnetmon::trim(text));
        if (lowered.rfind("/alert", 0) == 0) {
            const auto rule = sysnetmon::parse_alert_command(text);
            if (!rule.has_value()) {
                sysnetmon::send_line(client.fd, sysnetmon::error_event_to_json("expected /alert CPU>80% or /alert MEMORY>70%"));
                return;
            }
            const AlertRule parsed_rule = rule.value();
            alert_rules_.push_back(parsed_rule);
            const ChatMessage system_message{"system", "Registered alert rule: " + parsed_rule.raw_command, sysnetmon::iso_timestamp()};
            broadcast_to_dashboards(sysnetmon::chat_event_to_json(system_message));
            broadcast_to_dashboards(sysnetmon::rules_event_to_json(alert_rules_));
            sysnetmon::send_line(client.fd, sysnetmon::ack_event_to_json("rule added: " + parsed_rule.raw_command));
            return;
        }

        if (lowered == "/listalerts") {
            sysnetmon::send_line(client.fd, sysnetmon::rules_event_to_json(alert_rules_));
            return;
        }

        if (lowered == "/clients") {
            sysnetmon::send_line(client.fd, sysnetmon::ack_event_to_json("connected agents: " + std::to_string(latest_metrics_.size())));
            return;
        }

        sysnetmon::send_line(client.fd, sysnetmon::error_event_to_json("unknown command"));
    }

    void evaluate_rules(const MetricSnapshot &metric) {
        for (const auto &rule : alert_rules_) {
            const std::string dedupe_key = metric.host + "|" + rule.raw_command;
            const bool matched = sysnetmon::rule_matches(rule, metric);
            const bool previous_match = active_alerts_[dedupe_key];
            if (matched && !previous_match) {
                AlertEvent alert;
                alert.host = metric.host;
                alert.timestamp = sysnetmon::iso_timestamp();
                alert.metric_key = rule.metric_key;
                alert.display_metric = rule.display_metric;
                alert.metric_value = sysnetmon::metric_value_for_key(metric, rule.metric_key);
                alert.threshold = rule.threshold;
                alert.rule = rule.raw_command;
                alert.message = rule.display_metric + " on " + metric.host + " crossed threshold at " + sysnetmon::format_double(alert.metric_value);
                broadcast_to_dashboards(sysnetmon::alert_event_to_json(alert));
            }
            active_alerts_[dedupe_key] = matched;
        }
    }

    void broadcast_to_dashboards(const std::string &payload) {
        for (auto &client : clients_) {
            if (client.role == ClientRole::Dashboard) {
                sysnetmon::send_line(client.fd, payload);
            }
        }
    }

    void disconnect_client(std::size_t index) {
        std::cout << "Disconnecting " << clients_[index].peer_label << std::endl;
        sysnetmon::close_socket(clients_[index].fd);
        clients_.erase(clients_.begin() + static_cast<long long>(index));
    }

    void shutdown_all() {
        for (auto &client : clients_) {
            sysnetmon::close_socket(client.fd);
        }
        clients_.clear();
        if (sysnetmon::is_socket_valid(listen_fd_)) {
            sysnetmon::close_socket(listen_fd_);
            listen_fd_ = sysnetmon::invalid_socket;
        }
    }

    int port_ = 9090;
    sysnetmon::socket_t listen_fd_ = sysnetmon::invalid_socket;
    std::vector<ClientConnection> clients_;
    std::unordered_map<std::string, MetricSnapshot> latest_metrics_;
    std::vector<AlertRule> alert_rules_;
    std::unordered_map<std::string, bool> active_alerts_;
};

}  // namespace

int main(int argc, char **argv) {
    int port = 9090;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    sysnetmon::SocketRuntime socket_runtime;
    if (!socket_runtime.ok()) {
        std::cerr << "Socket runtime initialization failed" << std::endl;
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif

    MonitorServer server(port);
    if (!server.start()) {
        return 1;
    }
    server.run();
    return 0;
}
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#ifndef _WIN32
#include <thread>
#endif

#include "common.hpp"
#include "metrics.hpp"
#include "platform.hpp"

namespace {

volatile std::sig_atomic_t g_running = 1;

void signal_handler(int) {
    g_running = 0;
}

void sleep_seconds(int seconds) {
#ifdef _WIN32
    Sleep(static_cast<DWORD>(seconds * 1000));
#else
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
#endif
}

struct AgentConfig {
    std::string server_host = "127.0.0.1";
    int server_port = 9090;
    std::string agent_name = sysnetmon::hostname_or("agent-node");
    int interval_seconds = 5;
    bool daemonize = false;
};

bool connect_to_server(const AgentConfig &config, sysnetmon::socket_t &socket_fd) {
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!sysnetmon::is_socket_valid(socket_fd)) {
        std::cerr << "socket failed: " << sysnetmon::last_socket_error() << std::endl;
        return false;
    }

    sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<unsigned short>(config.server_port));

    const unsigned long direct_address = inet_addr(config.server_host.c_str());
    if (direct_address == INADDR_NONE) {
        hostent *resolved = gethostbyname(config.server_host.c_str());
        if (resolved == nullptr || resolved->h_addr == nullptr) {
            std::cerr << "Unable to resolve server host " << config.server_host << std::endl;
            sysnetmon::close_socket(socket_fd);
            socket_fd = sysnetmon::invalid_socket;
            return false;
        }
        std::memcpy(&server_addr.sin_addr, resolved->h_addr, resolved->h_length);
    } else {
        server_addr.sin_addr.s_addr = direct_address;
    }

    if (connect(socket_fd, reinterpret_cast<sockaddr *>(&server_addr), sizeof(server_addr)) != 0) {
        std::cerr << "connect failed: " << sysnetmon::last_socket_error() << std::endl;
        sysnetmon::close_socket(socket_fd);
        socket_fd = sysnetmon::invalid_socket;
        return false;
    }

    const std::string register_message = "{\"type\":\"register\",\"role\":\"agent\",\"host\":\"" +
                                         sysnetmon::escape_json(config.agent_name) + "\"}";
    if (!sysnetmon::send_line(socket_fd, register_message)) {
        sysnetmon::close_socket(socket_fd);
        return false;
    }

    std::cout << "Connected agent " << config.agent_name << " to " << config.server_host << ":" << config.server_port << std::endl;
    return true;
}

std::string metrics_payload(const sysnetmon::MetricSnapshot &metric) {
    return std::string("{\"type\":\"metrics\",") + sysnetmon::metric_data_to_json(metric).substr(1);
}

AgentConfig parse_args(int argc, char **argv) {
    AgentConfig config;
    int positional_index = 0;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--daemon") {
            config.daemonize = true;
            continue;
        }
        ++positional_index;
        if (positional_index == 1) {
            config.server_host = argument;
        } else if (positional_index == 2) {
            config.server_port = std::atoi(argument.c_str());
        } else if (positional_index == 3) {
            config.agent_name = argument;
        } else if (positional_index == 4) {
            config.interval_seconds = std::max(1, std::atoi(argument.c_str()));
        }
    }
    return config;
}

}  // namespace

int main(int argc, char **argv) {
    const AgentConfig config = parse_args(argc, argv);
    sysnetmon::SocketRuntime socket_runtime;
    if (!socket_runtime.ok()) {
        std::cerr << "Socket runtime initialization failed" << std::endl;
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

#ifndef _WIN32
    if (config.daemonize) {
        if (daemon(1, 0) != 0) {
            std::perror("daemon");
            return 1;
        }
    }
#endif

    sysnetmon::CpuSample previous_cpu = sysnetmon::read_cpu_sample();
    sysnetmon::NetSample previous_net = sysnetmon::read_net_sample();
    auto previous_tick = std::chrono::steady_clock::now();

    while (g_running) {
        sysnetmon::socket_t socket_fd = sysnetmon::invalid_socket;
        if (!connect_to_server(config, socket_fd)) {
            sleep_seconds(3);
            continue;
        }

        while (g_running) {
            sleep_seconds(config.interval_seconds);
            const auto current_tick = std::chrono::steady_clock::now();
            const std::chrono::duration<double> elapsed = current_tick - previous_tick;
            previous_tick = current_tick;

            const sysnetmon::MetricSnapshot metric = sysnetmon::collect_metrics(
                config.agent_name, previous_cpu, previous_net, elapsed.count());
            if (!sysnetmon::send_line(socket_fd, metrics_payload(metric))) {
                std::cerr << "Disconnected from server, retrying" << std::endl;
                sysnetmon::close_socket(socket_fd);
                socket_fd = sysnetmon::invalid_socket;
                break;
            }
        }

        if (sysnetmon::is_socket_valid(socket_fd)) {
            sysnetmon::close_socket(socket_fd);
        }
        if (g_running) {
            sleep_seconds(2);
        }
    }

    return 0;
}
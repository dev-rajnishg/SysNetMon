#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "platform.hpp"

namespace sysnetmon {

template <typename T>
class Maybe {
public:
    Maybe() : has_value_(false), value_() {}

    explicit Maybe(const T &value) : has_value_(true), value_(value) {}

    explicit Maybe(T &&value) : has_value_(true), value_(std::move(value)) {}

    bool has_value() const {
        return has_value_;
    }

    T value_or(const T &fallback) const {
        return has_value_ ? value_ : fallback;
    }

    const T &value() const {
        return value_;
    }

private:
    bool has_value_;
    T value_;
};

struct MetricSnapshot {
    std::string host;
    std::string timestamp;
    double cpu_percent = 0.0;
    double memory_percent = 0.0;
    double disk_percent = 0.0;
    double network_rx_kbps = 0.0;
    double network_tx_kbps = 0.0;
    long long memory_used_mb = 0;
    long long memory_total_mb = 0;
    long long disk_used_mb = 0;
    long long disk_total_mb = 0;
};

struct ChatMessage {
    std::string sender;
    std::string text;
    std::string timestamp;
};

struct AlertRule {
    std::string raw_command;
    std::string metric_key;
    std::string display_metric;
    double threshold = 0.0;
    char comparison = '>';
};

struct AlertEvent {
    std::string host;
    std::string timestamp;
    std::string metric_key;
    std::string display_metric;
    double metric_value = 0.0;
    double threshold = 0.0;
    std::string rule;
    std::string message;
};

inline std::string trim(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());
    return value;
}

inline std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline std::string escape_json(const std::string &value) {
    std::ostringstream out;
    for (char ch : value) {
        switch (ch) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << ch;
                break;
        }
    }
    return out.str();
}

inline std::string iso_timestamp() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const std::time_t now_time = clock::to_time_t(now);
    std::tm utc_time {};
#ifdef _WIN32
#if defined(__MINGW32__)
    std::tm *tmp = std::gmtime(&now_time);
    if (tmp != nullptr) {
        utc_time = *tmp;
    }
#else
    gmtime_s(&utc_time, &now_time);
#endif
#else
    gmtime_r(&now_time, &utc_time);
#endif
    std::ostringstream out;
    out << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

inline std::string format_double(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

inline Maybe<std::string> json_get_string(const std::string &payload, const std::string &key) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (std::regex_search(payload, match, pattern) && match.size() > 1) {
        return Maybe<std::string>(match[1].str());
    }
    return Maybe<std::string>();
}

inline Maybe<double> json_get_number(const std::string &payload, const std::string &key) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch match;
    if (std::regex_search(payload, match, pattern) && match.size() > 1) {
        return Maybe<double>(std::stod(match[1].str()));
    }
    return Maybe<double>();
}

inline Maybe<long long> json_get_integer(const std::string &payload, const std::string &key) {
    const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*(-?[0-9]+)");
    std::smatch match;
    if (std::regex_search(payload, match, pattern) && match.size() > 1) {
        return Maybe<long long>(std::stoll(match[1].str()));
    }
    return Maybe<long long>();
}

inline std::string metric_data_to_json(const MetricSnapshot &metric) {
    std::ostringstream out;
    out << "{";
    out << "\"host\":\"" << escape_json(metric.host) << "\",";
    out << "\"timestamp\":\"" << escape_json(metric.timestamp) << "\",";
    out << "\"cpu_percent\":" << format_double(metric.cpu_percent) << ",";
    out << "\"memory_percent\":" << format_double(metric.memory_percent) << ",";
    out << "\"disk_percent\":" << format_double(metric.disk_percent) << ",";
    out << "\"network_rx_kbps\":" << format_double(metric.network_rx_kbps) << ",";
    out << "\"network_tx_kbps\":" << format_double(metric.network_tx_kbps) << ",";
    out << "\"memory_used_mb\":" << metric.memory_used_mb << ",";
    out << "\"memory_total_mb\":" << metric.memory_total_mb << ",";
    out << "\"disk_used_mb\":" << metric.disk_used_mb << ",";
    out << "\"disk_total_mb\":" << metric.disk_total_mb;
    out << "}";
    return out.str();
}

inline std::string metric_event_to_json(const MetricSnapshot &metric) {
    return std::string("{\"type\":\"metric\",\"data\":") + metric_data_to_json(metric) + "}";
}

inline std::string snapshot_event_to_json(const std::unordered_map<std::string, MetricSnapshot> &metrics) {
    std::ostringstream out;
    out << "{\"type\":\"snapshot\",\"timestamp\":\"" << escape_json(iso_timestamp()) << "\",\"metrics\":[";
    bool first = true;
    std::map<std::string, MetricSnapshot> ordered(metrics.begin(), metrics.end());
    for (std::map<std::string, MetricSnapshot>::const_iterator it = ordered.begin(); it != ordered.end(); ++it) {
        const MetricSnapshot &metric = it->second;
        if (!first) {
            out << ",";
        }
        first = false;
        out << metric_data_to_json(metric);
    }
    out << "]}";
    return out.str();
}

inline std::string chat_event_to_json(const ChatMessage &chat) {
    std::ostringstream out;
    out << "{\"type\":\"chat\",\"from\":\"" << escape_json(chat.sender) << "\",\"text\":\""
        << escape_json(chat.text) << "\",\"timestamp\":\"" << escape_json(chat.timestamp) << "\"}";
    return out.str();
}

inline std::string ack_event_to_json(const std::string &message) {
    return std::string("{\"type\":\"ack\",\"message\":\"") + escape_json(message) + "\"}";
}

inline std::string error_event_to_json(const std::string &message) {
    return std::string("{\"type\":\"error\",\"message\":\"") + escape_json(message) + "\"}";
}

inline std::string alert_event_to_json(const AlertEvent &alert) {
    std::ostringstream out;
    out << "{\"type\":\"alert\",\"data\":{";
    out << "\"host\":\"" << escape_json(alert.host) << "\",";
    out << "\"timestamp\":\"" << escape_json(alert.timestamp) << "\",";
    out << "\"metric_key\":\"" << escape_json(alert.metric_key) << "\",";
    out << "\"display_metric\":\"" << escape_json(alert.display_metric) << "\",";
    out << "\"metric_value\":" << format_double(alert.metric_value) << ",";
    out << "\"threshold\":" << format_double(alert.threshold) << ",";
    out << "\"rule\":\"" << escape_json(alert.rule) << "\",";
    out << "\"message\":\"" << escape_json(alert.message) << "\"}}";
    return out.str();
}

inline std::string rules_event_to_json(const std::vector<AlertRule> &rules) {
    std::ostringstream out;
    out << "{\"type\":\"rules\",\"items\":[";
    bool first = true;
    for (const auto &rule : rules) {
        if (!first) {
            out << ",";
        }
        first = false;
        out << "{\"command\":\"" << escape_json(rule.raw_command) << "\",\"metric_key\":\""
            << escape_json(rule.metric_key) << "\",\"threshold\":" << format_double(rule.threshold) << "}";
    }
    out << "]}";
    return out.str();
}

inline bool send_line(socket_t fd, const std::string &payload) {
    const std::string data = payload + "\n";
    std::size_t total_sent = 0;
    while (total_sent < data.size()) {
        const int sent = static_cast<int>(send(fd, data.data() + total_sent,
            static_cast<int>(data.size() - total_sent), 0));
        if (sent <= 0) {
            return false;
        }
        total_sent += static_cast<std::size_t>(sent);
    }
    return true;
}

inline Maybe<AlertRule> parse_alert_command(const std::string &text) {
    const std::regex pattern(R"(^\s*/alert\s+([A-Za-z_]+)\s*([><])\s*([0-9]+(?:\.[0-9]+)?)\s*%?\s*$)", std::regex::icase);
    std::smatch match;
    if (!std::regex_match(text, match, pattern) || match.size() < 4) {
        return Maybe<AlertRule>();
    }

    const std::string token = lower_copy(match[1].str());
    AlertRule rule;
    rule.raw_command = trim(text);
    rule.comparison = match[2].str()[0];
    rule.threshold = std::stod(match[3].str());

    if (token == "cpu") {
        rule.metric_key = "cpu_percent";
        rule.display_metric = "CPU";
    } else if (token == "memory" || token == "mem") {
        rule.metric_key = "memory_percent";
        rule.display_metric = "Memory";
    } else if (token == "disk") {
        rule.metric_key = "disk_percent";
        rule.display_metric = "Disk";
    } else if (token == "rx" || token == "netrx" || token == "net_rx" || token == "networkrx" || token == "network_rx") {
        rule.metric_key = "network_rx_kbps";
        rule.display_metric = "Network RX";
    } else if (token == "tx" || token == "nettx" || token == "net_tx" || token == "networktx" || token == "network_tx") {
        rule.metric_key = "network_tx_kbps";
        rule.display_metric = "Network TX";
    } else {
        return Maybe<AlertRule>();
    }

    return Maybe<AlertRule>(rule);
}

inline double metric_value_for_key(const MetricSnapshot &metric, const std::string &key) {
    if (key == "cpu_percent") {
        return metric.cpu_percent;
    }
    if (key == "memory_percent") {
        return metric.memory_percent;
    }
    if (key == "disk_percent") {
        return metric.disk_percent;
    }
    if (key == "network_rx_kbps") {
        return metric.network_rx_kbps;
    }
    if (key == "network_tx_kbps") {
        return metric.network_tx_kbps;
    }
    return 0.0;
}

inline bool rule_matches(const AlertRule &rule, const MetricSnapshot &metric) {
    const double value = metric_value_for_key(metric, rule.metric_key);
    if (rule.comparison == '>') {
        return value > rule.threshold;
    }
    return value < rule.threshold;
}

}  // namespace sysnetmon
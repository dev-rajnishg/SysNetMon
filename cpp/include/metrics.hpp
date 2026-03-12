#pragma once

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "common.hpp"
#include "platform.hpp"

#ifdef _WIN32
#include <iphlpapi.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <ifaddrs.h>
#include <mach/host_info.h>
#include <mach/mach.h>
#include <mach/vm_statistics.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <sys/sysctl.h>
#include <unistd.h>
#else
#include <sys/statvfs.h>
#include <unistd.h>
#endif

namespace sysnetmon {

struct CpuSample {
    unsigned long long idle = 0;
    unsigned long long total = 0;
};

struct NetSample {
    unsigned long long rx_bytes = 0;
    unsigned long long tx_bytes = 0;
};

inline std::string hostname_or(const std::string &fallback) {
#ifdef _WIN32
    char buffer[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameA(buffer, &size) != 0) {
        return std::string(buffer, size);
    }
#else
    char buffer[256] = {0};
    if (gethostname(buffer, sizeof(buffer) - 1) == 0) {
        return std::string(buffer);
    }
#endif
    return fallback;
}

inline CpuSample read_cpu_sample() {
#ifdef _WIN32
    FILETIME idle_time {};
    FILETIME kernel_time {};
    FILETIME user_time {};
    CpuSample sample;
    typedef BOOL (WINAPI *GetSystemTimesFn)(LPFILETIME, LPFILETIME, LPFILETIME);
    static GetSystemTimesFn get_system_times = reinterpret_cast<GetSystemTimesFn>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetSystemTimes"));
    if (get_system_times != nullptr && get_system_times(&idle_time, &kernel_time, &user_time) != 0) {
        const auto to_u64 = [](const FILETIME &value) {
            ULARGE_INTEGER combined {};
            combined.LowPart = value.dwLowDateTime;
            combined.HighPart = value.dwHighDateTime;
            return static_cast<unsigned long long>(combined.QuadPart);
        };
        sample.idle = to_u64(idle_time);
        sample.total = to_u64(kernel_time) + to_u64(user_time);
    }
    return sample;
#elif defined(__APPLE__)
    host_cpu_load_info_data_t load {};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    CpuSample sample;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        reinterpret_cast<host_info_t>(&load), &count) == KERN_SUCCESS) {
        sample.idle = static_cast<unsigned long long>(load.cpu_ticks[CPU_STATE_IDLE]);
        sample.total = static_cast<unsigned long long>(load.cpu_ticks[CPU_STATE_USER]) +
                       static_cast<unsigned long long>(load.cpu_ticks[CPU_STATE_SYSTEM]) +
                       static_cast<unsigned long long>(load.cpu_ticks[CPU_STATE_NICE]) +
                       static_cast<unsigned long long>(load.cpu_ticks[CPU_STATE_IDLE]);
    }
    return sample;
#else
    std::ifstream proc_stat("/proc/stat");
    std::string label;
    CpuSample sample;
    unsigned long long user = 0;
    unsigned long long nice = 0;
    unsigned long long system = 0;
    unsigned long long idle = 0;
    unsigned long long iowait = 0;
    unsigned long long irq = 0;
    unsigned long long softirq = 0;
    unsigned long long steal = 0;
    proc_stat >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    sample.idle = idle + iowait;
    sample.total = user + nice + system + idle + iowait + irq + softirq + steal;
    return sample;
#endif
}

inline NetSample read_net_sample() {
#ifdef _WIN32
    ULONG size = 0;
    GetIfTable(NULL, &size, FALSE);
    std::unique_ptr<unsigned char[]> buffer(new unsigned char[size]);
    auto *table = reinterpret_cast<MIB_IFTABLE *>(buffer.get());
    NetSample sample;
    if (GetIfTable(table, &size, FALSE) != 0) {
        return sample;
    }
    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
        const MIB_IFROW &row = table->table[index];
        if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        sample.rx_bytes += static_cast<unsigned long long>(row.dwInOctets);
        sample.tx_bytes += static_cast<unsigned long long>(row.dwOutOctets);
    }
    return sample;
#elif defined(__APPLE__)
    NetSample sample;
    ifaddrs *interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) {
        return sample;
    }
    for (ifaddrs *entry = interfaces; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_data == nullptr) {
            continue;
        }
        if (entry->ifa_addr->sa_family != AF_LINK) {
            continue;
        }
        if ((entry->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        const auto *data = reinterpret_cast<if_data *>(entry->ifa_data);
        sample.rx_bytes += static_cast<unsigned long long>(data->ifi_ibytes);
        sample.tx_bytes += static_cast<unsigned long long>(data->ifi_obytes);
    }
    freeifaddrs(interfaces);
    return sample;
#else
    std::ifstream proc_net("/proc/net/dev");
    std::string line;
    NetSample sample;
    int line_index = 0;
    while (std::getline(proc_net, line)) {
        ++line_index;
        if (line_index <= 2) {
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string interface_name = trim(line.substr(0, colon));
        if (interface_name == "lo") {
            continue;
        }
        std::istringstream values(line.substr(colon + 1));
        unsigned long long rx_bytes = 0;
        unsigned long long rx_packets = 0;
        unsigned long long rx_errs = 0;
        unsigned long long rx_drop = 0;
        unsigned long long rx_fifo = 0;
        unsigned long long rx_frame = 0;
        unsigned long long rx_compressed = 0;
        unsigned long long rx_multicast = 0;
        unsigned long long tx_bytes = 0;
        unsigned long long tx_packets = 0;
        unsigned long long tx_errs = 0;
        unsigned long long tx_drop = 0;
        unsigned long long tx_fifo = 0;
        unsigned long long tx_colls = 0;
        unsigned long long tx_carrier = 0;
        unsigned long long tx_compressed = 0;
        values >> rx_bytes >> rx_packets >> rx_errs >> rx_drop >> rx_fifo >> rx_frame >> rx_compressed >> rx_multicast
               >> tx_bytes >> tx_packets >> tx_errs >> tx_drop >> tx_fifo >> tx_colls >> tx_carrier >> tx_compressed;
        sample.rx_bytes += rx_bytes;
        sample.tx_bytes += tx_bytes;
    }
    return sample;
#endif
}

inline MetricSnapshot collect_metrics(const std::string &host, CpuSample &previous_cpu,
                                      NetSample &previous_net, double elapsed_seconds) {
    MetricSnapshot metric;
    metric.host = host;
    metric.timestamp = iso_timestamp();

    const CpuSample current_cpu = read_cpu_sample();
    const unsigned long long total_delta = current_cpu.total - previous_cpu.total;
    const unsigned long long idle_delta = current_cpu.idle - previous_cpu.idle;
    if (previous_cpu.total != 0 && total_delta > 0) {
        metric.cpu_percent = 100.0 * static_cast<double>(total_delta - idle_delta) / static_cast<double>(total_delta);
    }
    previous_cpu = current_cpu;

#ifdef _WIN32
    MEMORYSTATUSEX memory_status {};
    memory_status.dwLength = sizeof(memory_status);
    if (GlobalMemoryStatusEx(&memory_status) != 0) {
        const unsigned long long total_bytes = memory_status.ullTotalPhys;
        const unsigned long long available_bytes = memory_status.ullAvailPhys;
        const unsigned long long used_bytes = total_bytes - available_bytes;
        metric.memory_total_mb = static_cast<long long>(total_bytes / (1024ULL * 1024ULL));
        metric.memory_used_mb = static_cast<long long>(used_bytes / (1024ULL * 1024ULL));
        if (total_bytes > 0) {
            metric.memory_percent = 100.0 * static_cast<double>(used_bytes) / static_cast<double>(total_bytes);
        }
    }

    ULARGE_INTEGER available_bytes {};
    ULARGE_INTEGER total_bytes {};
    ULARGE_INTEGER free_bytes {};
    if (GetDiskFreeSpaceExA(nullptr, &available_bytes, &total_bytes, &free_bytes) != 0) {
        const unsigned long long used_bytes = total_bytes.QuadPart - free_bytes.QuadPart;
        metric.disk_total_mb = static_cast<long long>(total_bytes.QuadPart / (1024ULL * 1024ULL));
        metric.disk_used_mb = static_cast<long long>(used_bytes / (1024ULL * 1024ULL));
        if (total_bytes.QuadPart > 0) {
            metric.disk_percent = 100.0 * static_cast<double>(used_bytes) / static_cast<double>(total_bytes.QuadPart);
        }
    }
#elif defined(__APPLE__)
    int64_t memory_total_bytes = 0;
    size_t length = sizeof(memory_total_bytes);
    if (sysctlbyname("hw.memsize", &memory_total_bytes, &length, nullptr, 0) == 0) {
        vm_statistics64_data_t vm_stats {};
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                              reinterpret_cast<host_info64_t>(&vm_stats), &count) == KERN_SUCCESS) {
            vm_size_t page_size = 0;
            host_page_size(mach_host_self(), &page_size);
            const unsigned long long available_bytes_local =
                static_cast<unsigned long long>(vm_stats.free_count + vm_stats.inactive_count) * page_size;
            const unsigned long long total_bytes_local = static_cast<unsigned long long>(memory_total_bytes);
            const unsigned long long used_bytes = total_bytes_local > available_bytes_local
                ? total_bytes_local - available_bytes_local : 0ULL;
            metric.memory_total_mb = static_cast<long long>(total_bytes_local / (1024ULL * 1024ULL));
            metric.memory_used_mb = static_cast<long long>(used_bytes / (1024ULL * 1024ULL));
            if (total_bytes_local > 0) {
                metric.memory_percent = 100.0 * static_cast<double>(used_bytes) / static_cast<double>(total_bytes_local);
            }
        }
    }

    struct statvfs disk_stats {};
    if (statvfs("/", &disk_stats) == 0) {
        const unsigned long long total_bytes_local = static_cast<unsigned long long>(disk_stats.f_blocks) * disk_stats.f_frsize;
        const unsigned long long available_bytes_local = static_cast<unsigned long long>(disk_stats.f_bavail) * disk_stats.f_frsize;
        const unsigned long long used_bytes = total_bytes_local - available_bytes_local;
        metric.disk_total_mb = static_cast<long long>(total_bytes_local / (1024ULL * 1024ULL));
        metric.disk_used_mb = static_cast<long long>(used_bytes / (1024ULL * 1024ULL));
        if (total_bytes_local > 0) {
            metric.disk_percent = 100.0 * static_cast<double>(used_bytes) / static_cast<double>(total_bytes_local);
        }
    }
#else
    std::ifstream meminfo("/proc/meminfo");
    std::string key;
    long long value = 0;
    std::string unit;
    long long mem_total_kb = 0;
    long long mem_available_kb = 0;
    while (meminfo >> key >> value >> unit) {
        if (key == "MemTotal:") {
            mem_total_kb = value;
        } else if (key == "MemAvailable:") {
            mem_available_kb = value;
        }
        if (mem_total_kb > 0 && mem_available_kb > 0) {
            break;
        }
    }
    const long long mem_used_kb = mem_total_kb - mem_available_kb;
    metric.memory_total_mb = mem_total_kb / 1024;
    metric.memory_used_mb = mem_used_kb / 1024;
    if (mem_total_kb > 0) {
        metric.memory_percent = 100.0 * static_cast<double>(mem_used_kb) / static_cast<double>(mem_total_kb);
    }

    struct statvfs disk_stats {};
    if (statvfs("/", &disk_stats) == 0) {
        const unsigned long long total_bytes_local = static_cast<unsigned long long>(disk_stats.f_blocks) * disk_stats.f_frsize;
        const unsigned long long available_bytes_local = static_cast<unsigned long long>(disk_stats.f_bavail) * disk_stats.f_frsize;
        const unsigned long long used_bytes = total_bytes_local - available_bytes_local;
        metric.disk_total_mb = static_cast<long long>(total_bytes_local / (1024ULL * 1024ULL));
        metric.disk_used_mb = static_cast<long long>(used_bytes / (1024ULL * 1024ULL));
        if (total_bytes_local > 0) {
            metric.disk_percent = 100.0 * static_cast<double>(used_bytes) / static_cast<double>(total_bytes_local);
        }
    }
#endif

    const NetSample current_net = read_net_sample();
    if (previous_net.rx_bytes != 0 && elapsed_seconds > 0.0) {
        const double rx_delta = static_cast<double>(current_net.rx_bytes - previous_net.rx_bytes);
        const double tx_delta = static_cast<double>(current_net.tx_bytes - previous_net.tx_bytes);
        metric.network_rx_kbps = rx_delta / 1024.0 / elapsed_seconds;
        metric.network_tx_kbps = tx_delta / 1024.0 / elapsed_seconds;
    }
    previous_net = current_net;

    return metric;
}

}  // namespace sysnetmon
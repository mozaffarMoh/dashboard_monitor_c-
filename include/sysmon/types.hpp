#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sysmon {

struct CpuTimes {
    std::uint64_t user{0};
    std::uint64_t nice{0};
    std::uint64_t system{0};
    std::uint64_t idle{0};
    std::uint64_t iowait{0};
    std::uint64_t irq{0};
    std::uint64_t softirq{0};
    std::uint64_t steal{0};
    std::uint64_t guest{0};
    std::uint64_t guest_nice{0};

    std::uint64_t total() const;
    std::uint64_t busy() const;
};

struct CpuCore {
    int index{0};
    double usage{0.0};
    double frequency_mhz{0.0};
};

struct CpuStats {
    std::string model;
    int logical_cores{0};
    double usage{0.0};
    double frequency_mhz{0.0};
    std::vector<CpuCore> cores;
};

struct MemoryStats {
    std::uint64_t total{0};
    std::uint64_t available{0};
    std::uint64_t used{0};
    std::uint64_t free{0};
    std::uint64_t buffers{0};
    std::uint64_t cached{0};
    std::uint64_t swap_total{0};
    std::uint64_t swap_free{0};
    std::uint64_t swap_used{0};
};

struct LoadStats {
    double one{0.0};
    double five{0.0};
    double fifteen{0.0};
    std::uint64_t tasks_running{0};
    std::uint64_t tasks_total{0};
    double uptime_seconds{0.0};
};

struct DiskStats {
    std::string device;
    std::string mountpoint;
    std::string filesystem;
    std::uint64_t total{0};
    std::uint64_t used{0};
    std::uint64_t available{0};
    double usage{0.0};
};

struct NetworkStats {
    std::string interface;
    std::uint64_t rx_bytes{0};
    std::uint64_t tx_bytes{0};
    double rx_rate{0.0};
    double tx_rate{0.0};
};

struct ThermalZone {
    std::string name;
    double celsius{0.0};
};

struct Snapshot {
    CpuStats cpu;
    MemoryStats memory;
    LoadStats load;
    std::vector<DiskStats> disks;
    std::vector<NetworkStats> network;
    std::vector<ThermalZone> thermal;
    std::string hostname;
    std::string kernel;
    std::string timestamp;
};

}  // namespace sysmon

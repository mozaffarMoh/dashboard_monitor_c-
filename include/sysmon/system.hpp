#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sysmon/types.hpp"

namespace sysmon {

class MemoryReader {
public:
    MemoryStats read() const;
};

class LoadReader {
public:
    LoadStats read() const;
};

class DiskReader {
public:
    std::vector<DiskStats> read() const;
};

class NetworkMonitor {
public:
    std::vector<NetworkStats> sample();

private:
    struct Counters {
        std::uint64_t rx{0};
        std::uint64_t tx{0};
    };

    std::vector<NetworkStats> previous_;
    std::chrono::steady_clock::time_point previous_time_{};
    bool initialized_{false};
};

class ThermalReader {
public:
    std::vector<ThermalZone> read() const;
};

}  // namespace sysmon

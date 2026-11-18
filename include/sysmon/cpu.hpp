#pragma once

#include <cstdint>
#include <vector>

#include "sysmon/types.hpp"

namespace sysmon {

class CpuMonitor {
public:
    CpuMonitor();

    CpuStats sample();

private:
    struct CoreState {
        CpuTimes previous;
        bool initialized{false};
    };

    static bool parse_times(const std::vector<std::string>& fields, CpuTimes& out);
    static double usage_between(const CpuTimes& previous, const CpuTimes& current);
    static double read_frequency_mhz(int index);

    std::vector<CoreState> core_states_;
    CpuTimes previous_total_;
    bool total_initialized_{false};
    std::string model_;
    int logical_cores_{0};
};

}  // namespace sysmon

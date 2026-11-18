#include "sysmon/cpu.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <string>
#include <thread>

#include "sysmon/util.hpp"

namespace sysmon {
namespace {

constexpr const char* kProcStat = "/proc/stat";
constexpr const char* kCpuInfo = "/proc/cpuinfo";

std::string detect_model() {
    for (const auto& line : read_lines(kCpuInfo)) {
        if (line.starts_with("model name")) {
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                return trim(std::string_view{line}.substr(colon + 1));
            }
        }
    }
    return "unknown CPU";
}

std::vector<std::string> parse_cpu_lines() {
    std::vector<std::string> entries;
    for (auto& line : read_lines(kProcStat)) {
        if (line.starts_with("cpu") && line.size() > 3 && line[3] >= '0' && line[3] <= '9') {
            entries.push_back(std::move(line));
        }
    }
    return entries;
}

}  // namespace

CpuMonitor::CpuMonitor() : model_(detect_model()) {
    const auto detected = static_cast<int>(std::thread::hardware_concurrency());
    const auto reported = static_cast<int>(parse_cpu_lines().size());
    logical_cores_ = std::max({detected, reported, 1});
    core_states_.resize(static_cast<std::size_t>(logical_cores_));
}

bool CpuMonitor::parse_times(const std::vector<std::string>& fields, CpuTimes& out) {
    if (fields.size() < 5) {
        return false;
    }
    std::uint64_t values[10] = {};
    for (std::size_t index = 0; index < std::size(values); ++index) {
        if (index + 1 >= fields.size()) {
            break;
        }
        auto parsed = to_u64(fields[index + 1]);
        if (!parsed) {
            return false;
        }
        values[index] = *parsed;
    }
    out.user = values[0];
    out.nice = values[1];
    out.system = values[2];
    out.idle = values[3];
    out.iowait = values[4];
    out.irq = values[5];
    out.softirq = values[6];
    out.steal = values[7];
    out.guest = values[8];
    out.guest_nice = values[9];
    return true;
}

double CpuMonitor::usage_between(const CpuTimes& previous, const CpuTimes& current) {
    const auto total_delta = static_cast<double>(current.total() - previous.total());
    const auto busy_delta = static_cast<double>(current.busy() - previous.busy());
    if (total_delta <= 0.0) {
        return 0.0;
    }
    return std::clamp(busy_delta * 100.0 / total_delta, 0.0, 100.0);
}

double CpuMonitor::read_frequency_mhz(int index) {
    const auto path = std::format("/sys/devices/system/cpu/cpu{}/cpufreq/scaling_cur_freq", index);
    if (auto value = read_file(path)) {
        if (auto khz = to_u64(trim(*value))) {
            return static_cast<double>(*khz) / 1000.0;
        }
    }
    return 0.0;
}

CpuStats CpuMonitor::sample() {
    CpuStats stats;
    stats.model = model_;
    stats.logical_cores = logical_cores_;
    stats.cores.reserve(core_states_.size());

    CpuTimes aggregate{};
    bool have_aggregate = false;

    for (auto& line : read_lines(kProcStat)) {
        if (!line.starts_with("cpu")) {
            continue;
        }
        auto fields = split_whitespace(line);
        if (fields.empty()) {
            continue;
        }
        if (fields[0] == "cpu") {
            CpuTimes times;
            if (parse_times(fields, times)) {
                if (total_initialized_) {
                    stats.usage = usage_between(previous_total_, times);
                }
                previous_total_ = times;
                total_initialized_ = true;
                aggregate = times;
                have_aggregate = true;
            }
            continue;
        }
        if (fields[0].size() <= 3 || fields[0][3] < '0' || fields[0][3] > '9') {
            continue;
        }
        auto index_value = to_u64(std::string_view{fields[0]}.substr(3));
        if (!index_value || *index_value >= core_states_.size()) {
            continue;
        }
        const auto index = static_cast<int>(*index_value);
        CpuTimes times;
        if (!parse_times(fields, times)) {
            continue;
        }
        auto& state = core_states_[static_cast<std::size_t>(index)];
        CpuCore core;
        core.index = index;
        if (state.initialized) {
            core.usage = usage_between(state.previous, times);
        }
        state.previous = times;
        state.initialized = true;
        core.frequency_mhz = read_frequency_mhz(index);
        stats.cores.push_back(core);
    }

    if (!have_aggregate && !stats.cores.empty()) {
        double sum = 0.0;
        for (const auto& core : stats.cores) {
            sum += core.usage;
        }
        stats.usage = sum / static_cast<double>(stats.cores.size());
    }

    double frequency_sum = 0.0;
    std::size_t frequency_count = 0;
    for (const auto& core : stats.cores) {
        if (core.frequency_mhz > 0.0) {
            frequency_sum += core.frequency_mhz;
            ++frequency_count;
        }
    }
    if (frequency_count > 0) {
        stats.frequency_mhz = frequency_sum / static_cast<double>(frequency_count);
    }

    if (aggregate.total() == 0 && !stats.cores.empty()) {
        stats.logical_cores = static_cast<int>(stats.cores.size());
    }
    return stats;
}

}  // namespace sysmon

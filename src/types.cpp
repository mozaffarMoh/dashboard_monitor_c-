#include "sysmon/types.hpp"

namespace sysmon {

std::uint64_t CpuTimes::total() const {
    return user + nice + system + idle + iowait + irq + softirq + steal;
}

std::uint64_t CpuTimes::busy() const {
    return total() - idle - iowait;
}

}  // namespace sysmon

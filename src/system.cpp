#include "sysmon/system.hpp"

#include <sys/statvfs.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <unordered_map>

#include "sysmon/util.hpp"

namespace sysmon {
namespace {

constexpr const char* kMemInfo = "/proc/meminfo";
constexpr const char* kLoadAvg = "/proc/loadavg";
constexpr const char* kUptime = "/proc/uptime";
constexpr const char* kMounts = "/proc/mounts";
constexpr const char* kNetDev = "/proc/net/dev";
constexpr const char* kThermalRoot = "/sys/class/thermal";

std::unordered_map<std::string, std::uint64_t> parse_meminfo() {
    std::unordered_map<std::string, std::uint64_t> values;
    for (const auto& line : read_lines(kMemInfo)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const auto key = line.substr(0, colon);
        auto fields = split_whitespace(std::string_view{line}.substr(colon + 1));
        if (fields.empty()) {
            continue;
        }
        if (auto value = to_u64(fields[0])) {
            values[key] = *value * 1024;
        }
    }
    return values;
}

bool is_relevant_filesystem(const std::string& type) {
    static const std::map<std::string, bool> types{
        {"ext2", true},    {"ext3", true},    {"ext4", true},  {"xfs", true},
        {"btrfs", true},   {"zfs", true},     {"f2fs", true},  {"vfat", true},
        {"ntfs", true},    {"ntfs3", true},   {"fuseblk", true},
        {"overlay", true}, {"virtiofs", true}, {"9p", true},   {"drvfs", true},
    };
    return types.contains(type);
}

std::string base_device(std::string_view device) {
    const auto slash = device.find_last_of('/');
    if (slash == std::string_view::npos) {
        return std::string{device};
    }
    return std::string{device.substr(slash + 1)};
}

}  // namespace

MemoryStats MemoryReader::read() const {
    const auto values = parse_meminfo();
    const auto lookup = [&values](std::string_view key) -> std::uint64_t {
        const auto it = values.find(std::string{key});
        return it == values.end() ? 0 : it->second;
    };

    MemoryStats stats;
    stats.total = lookup("MemTotal");
    stats.free = lookup("MemFree");
    stats.buffers = lookup("Buffers");
    stats.cached = lookup("Cached");
    stats.available = lookup("MemAvailable");
    if (stats.available == 0 && stats.total > 0) {
        stats.available = stats.free + stats.buffers + stats.cached;
    }
    stats.used = stats.total > stats.available ? stats.total - stats.available : 0;
    stats.swap_total = lookup("SwapTotal");
    stats.swap_free = lookup("SwapFree");
    stats.swap_used = stats.swap_total > stats.swap_free ? stats.swap_total - stats.swap_free : 0;
    return stats;
}

LoadStats LoadReader::read() const {
    LoadStats stats;
    if (auto contents = read_file(kLoadAvg)) {
        auto fields = split_whitespace(*contents);
        if (fields.size() >= 4) {
            if (auto value = to_double(fields[0])) {
                stats.one = *value;
            }
            if (auto value = to_double(fields[1])) {
                stats.five = *value;
            }
            if (auto value = to_double(fields[2])) {
                stats.fifteen = *value;
            }
            const auto slash = fields[3].find('/');
            if (slash != std::string::npos) {
                if (auto running = to_u64(std::string_view{fields[3]}.substr(0, slash))) {
                    stats.tasks_running = *running;
                }
                if (auto total = to_u64(std::string_view{fields[3]}.substr(slash + 1))) {
                    stats.tasks_total = *total;
                }
            }
        }
    }

    if (auto contents = read_file(kUptime)) {
        auto fields = split_whitespace(*contents);
        if (!fields.empty()) {
            if (auto value = to_double(fields[0])) {
                stats.uptime_seconds = *value;
            }
        }
    }
    return stats;
}

std::vector<DiskStats> DiskReader::read() const {
    std::vector<DiskStats> disks;
    std::map<std::string, bool> seen;

    for (const auto& line : read_lines(kMounts)) {
        auto fields = split_whitespace(line);
        if (fields.size() < 3) {
            continue;
        }
        const auto& device = fields[0];
        const auto mountpoint = unescape_mount_path(fields[1]);
        const auto& type = fields[2];
        if (!is_relevant_filesystem(type)) {
            continue;
        }
        const auto key = device + " -> " + mountpoint;
        if (seen.contains(key)) {
            continue;
        }
        seen[key] = true;

        struct statvfs info {};
        if (statvfs(mountpoint.c_str(), &info) != 0) {
            continue;
        }
        const auto block_size = static_cast<std::uint64_t>(info.f_frsize != 0 ? info.f_frsize : info.f_bsize);
        if (block_size == 0) {
            continue;
        }

        DiskStats disk;
        disk.device = base_device(device);
        disk.mountpoint = mountpoint;
        disk.filesystem = type;
        disk.total = static_cast<std::uint64_t>(info.f_blocks) * block_size;
        const auto free_blocks = static_cast<std::uint64_t>(info.f_bfree) * block_size;
        disk.available = static_cast<std::uint64_t>(info.f_bavail) * block_size;
        disk.used = disk.total > free_blocks ? disk.total - free_blocks : 0;
        const auto usable = disk.used + disk.available;
        disk.usage = usable > 0 ? static_cast<double>(disk.used) * 100.0 / static_cast<double>(usable) : 0.0;
        if (disk.total == 0) {
            continue;
        }
        disks.push_back(std::move(disk));
    }

    std::sort(disks.begin(), disks.end(), [](const DiskStats& left, const DiskStats& right) {
        return left.mountpoint < right.mountpoint;
    });
    return disks;
}

std::vector<NetworkStats> NetworkMonitor::sample() {
    std::vector<NetworkStats> current;
    const auto now = std::chrono::steady_clock::now();

    for (const auto& line : read_lines(kNetDev)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        auto fields = split_whitespace(std::string_view{line}.substr(colon + 1));
        if (fields.size() < 9) {
            continue;
        }
        auto rx = to_u64(fields[0]);
        auto tx = to_u64(fields[8]);
        if (!rx || !tx) {
            continue;
        }
        NetworkStats stats;
        stats.interface = trim(std::string_view{line}.substr(0, colon));
        if (stats.interface == "lo") {
            continue;
        }
        stats.rx_bytes = *rx;
        stats.tx_bytes = *tx;
        const auto virtual_interface = stats.interface.starts_with("veth") ||
                                       stats.interface.starts_with("br-") ||
                                       stats.interface == "docker0";
        if (virtual_interface && stats.rx_bytes == 0 && stats.tx_bytes == 0) {
            continue;
        }
        current.push_back(std::move(stats));
    }

    if (initialized_) {
        std::unordered_map<std::string, Counters> previous;
        for (const auto& item : previous_) {
            previous[item.interface] = Counters{item.rx_bytes, item.tx_bytes};
        }
        const auto elapsed = std::max(
            std::chrono::duration<double>{now - previous_time_}.count(), 0.001);
        for (auto& item : current) {
            const auto it = previous.find(item.interface);
            if (it == previous.end()) {
                continue;
            }
            const auto rx_delta = static_cast<double>(item.rx_bytes - it->second.rx);
            const auto tx_delta = static_cast<double>(item.tx_bytes - it->second.tx);
            item.rx_rate = rx_delta < 0.0 ? 0.0 : rx_delta / elapsed;
            item.tx_rate = tx_delta < 0.0 ? 0.0 : tx_delta / elapsed;
        }
    }

    previous_ = current;
    previous_time_ = now;
    initialized_ = true;
    return current;
}

std::vector<ThermalZone> ThermalReader::read() const {
    std::vector<ThermalZone> zones;
    std::error_code error;
    const std::filesystem::path root{kThermalRoot};
    if (!std::filesystem::exists(root, error)) {
        return zones;
    }

    for (const auto& entry : std::filesystem::directory_iterator{root, error}) {
        if (error) {
            break;
        }
        const auto filename = entry.path().filename().string();
        if (!filename.starts_with("thermal_zone")) {
            continue;
        }
        auto raw_temp = read_file(entry.path() / "temp");
        if (!raw_temp) {
            continue;
        }
        auto value = to_u64(trim(*raw_temp));
        if (!value) {
            continue;
        }
        ThermalZone zone;
        if (auto type = read_file(entry.path() / "type")) {
            zone.name = trim(*type);
        }
        if (zone.name.empty()) {
            zone.name = filename;
        }
        zone.celsius = static_cast<double>(*value) / 1000.0;
        if (zone.celsius <= 0.0 || zone.celsius > 200.0) {
            continue;
        }
        zones.push_back(std::move(zone));
    }

    std::sort(zones.begin(), zones.end(), [](const ThermalZone& left, const ThermalZone& right) {
        return left.name < right.name;
    });
    return zones;
}

}  // namespace sysmon

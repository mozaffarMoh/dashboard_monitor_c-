#include "sysmon/util.hpp"

#include <charconv>
#include <fstream>
#include <format>
#include <iterator>
#include <system_error>

namespace sysmon {

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return std::nullopt;
    }
    std::string contents{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    if (!stream.good() && !stream.eof()) {
        return std::nullopt;
    }
    return contents;
}

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::vector<std::string> lines;
    auto contents = read_file(path);
    if (!contents) {
        return lines;
    }
    std::size_t start = 0;
    while (start <= contents->size()) {
        auto end = contents->find('\n', start);
        if (end == std::string::npos) {
            end = contents->size();
        }
        if (end > start) {
            lines.push_back(contents->substr(start, end - start));
        }
        start = end + 1;
    }
    return lines;
}

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string{text.substr(first, last - first + 1)};
}

std::vector<std::string> split_whitespace(std::string_view text) {
    std::vector<std::string> parts;
    std::size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() && (text[index] == ' ' || text[index] == '\t')) {
            ++index;
        }
        const std::size_t start = index;
        while (index < text.size() && text[index] != ' ' && text[index] != '\t') {
            ++index;
        }
        if (index > start) {
            parts.emplace_back(text.substr(start, index - start));
        }
    }
    return parts;
}

std::optional<std::uint64_t> to_u64(std::string_view text) {
    std::uint64_t value{0};
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<double> to_double(std::string_view text) {
    double value{0.0};
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::string format_bytes(std::uint64_t bytes) {
    static constexpr std::string_view units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        ++unit;
    }
    return std::format("{:.1f} {}", value, units[unit]);
}

std::string format_rate(double bytes_per_second) {
    return std::format("{}/s", format_bytes(static_cast<std::uint64_t>(bytes_per_second < 0.0 ? 0.0 : bytes_per_second)));
}

std::string format_duration(double seconds) {
    auto total = static_cast<std::uint64_t>(seconds);
    const auto days = total / 86400;
    total %= 86400;
    const auto hours = total / 3600;
    total %= 3600;
    const auto minutes = total / 60;

    if (days > 0) {
        return std::format("{}d {:02}h {:02}m", days, hours, minutes);
    }
    if (hours > 0) {
        return std::format("{}h {:02}m", hours, minutes);
    }
    return std::format("{}m", minutes);
}

std::string unescape_mount_path(std::string_view path) {
    std::string result;
    result.reserve(path.size());
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (path[index] == '\\' && index + 3 < path.size()) {
            const auto octal = path.substr(index + 1, 3);
            auto decoded = to_u64(octal);
            if (decoded && *decoded >= 8 && *decoded < 256) {
                result.push_back(static_cast<char>(*decoded));
                index += 3;
                continue;
            }
        }
        result.push_back(path[index]);
    }
    return result;
}

}  // namespace sysmon

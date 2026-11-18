#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sysmon {

std::optional<std::string> read_file(const std::filesystem::path& path);
std::vector<std::string> read_lines(const std::filesystem::path& path);
std::string trim(std::string_view text);
std::vector<std::string> split_whitespace(std::string_view text);
std::optional<std::uint64_t> to_u64(std::string_view text);
std::optional<double> to_double(std::string_view text);

std::string format_bytes(std::uint64_t bytes);
std::string format_rate(double bytes_per_second);
std::string format_duration(double seconds);

std::string unescape_mount_path(std::string_view path);

}  // namespace sysmon

#include "sysmon/dashboard.hpp"

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <poll.h>
#include <string>

#include "sysmon/util.hpp"

namespace sysmon {

class Terminal::RawState {
public:
    termios saved{};
    bool active{false};
};

Terminal::Terminal(bool color)
    : is_tty_(::isatty(STDOUT_FILENO) == 1),
      color_(color && is_tty_),
      raw_state_(new RawState()) {}

Terminal::~Terminal() {
    leave_raw();
    delete raw_state_;
}

bool Terminal::enter_raw() {
    if (raw_state_->active || ::isatty(STDIN_FILENO) != 1) {
        return false;
    }
    if (::tcgetattr(STDIN_FILENO, &raw_state_->saved) != 0) {
        return false;
    }
    termios raw = raw_state_->saved;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        return false;
    }
    raw_state_->active = true;
    if (is_tty_) {
        std::fputs(std::string{ansi::hide_cursor}.c_str(), stdout);
        std::fflush(stdout);
    }
    return true;
}

void Terminal::leave_raw() {
    if (!raw_state_->active) {
        return;
    }
    ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_state_->saved);
    raw_state_->active = false;
    if (is_tty_) {
        std::fputs(std::string{ansi::show_cursor}.c_str(), stdout);
        std::fflush(stdout);
    }
}

bool Terminal::key_pressed() {
    if (::isatty(STDIN_FILENO) != 1) {
        return false;
    }
    pollfd descriptor{STDIN_FILENO, POLLIN, 0};
    return ::poll(&descriptor, 1, 0) > 0;
}

int Terminal::read_key() {
    if (::isatty(STDIN_FILENO) != 1) {
        return -1;
    }
    unsigned char buffer = 0;
    const auto count = ::read(STDIN_FILENO, &buffer, 1);
    if (count <= 0) {
        return -1;
    }
    return static_cast<int>(buffer);
}

int Terminal::columns() const {
    winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return static_cast<int>(size.ws_col);
    }
    return 100;
}

int Terminal::rows() const {
    winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0) {
        return static_cast<int>(size.ws_row);
    }
    return 30;
}

std::string Terminal::paint(std::string_view text, std::string_view color) const {
    if (!color_) {
        return std::string{text};
    }
    return std::format("{}{}{}", color, text, ansi::reset);
}

namespace {

std::string repeat(std::string_view unit, int count) {
    std::string output;
    if (count <= 0) {
        return output;
    }
    output.reserve(unit.size() * static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        output.append(unit);
    }
    return output;
}

std::string fit_line(std::string_view line, int width) {
    if (width <= 0) {
        return {};
    }
    std::string output;
    output.reserve(line.size());
    int column = 0;
    bool saw_escape = false;
    bool truncated = false;

    std::size_t index = 0;
    while (index < line.size()) {
        const auto byte = static_cast<unsigned char>(line[index]);
        if (byte == 0x1b && index + 1 < line.size() && line[index + 1] == '[') {
            saw_escape = true;
            std::size_t end = index + 2;
            while (end < line.size()) {
                const auto code = static_cast<unsigned char>(line[end]);
                ++end;
                if (code >= 0x40 && code <= 0x7e) {
                    break;
                }
            }
            output.append(line.substr(index, end - index));
            index = end;
            continue;
        }

        std::size_t bytes = 1;
        if ((byte & 0x80U) != 0) {
            if ((byte & 0xE0U) == 0xC0U) {
                bytes = 2;
            } else if ((byte & 0xF0U) == 0xE0U) {
                bytes = 3;
            } else if ((byte & 0xF8U) == 0xF0U) {
                bytes = 4;
            }
        }
        if (column + 1 > width) {
            truncated = true;
            break;
        }
        output.append(line.substr(index, bytes));
        index += bytes;
        ++column;
    }

    if (truncated && saw_escape) {
        output.append(ansi::reset);
    }
    return output;
}

}  // namespace

Dashboard::Dashboard(bool color, bool show_footer) : color_(color), show_footer_(show_footer) {}

void Dashboard::reset() { history_.clear(); }

std::string Dashboard::color_for(double fraction) const {
    if (!color_) {
        return {};
    }
    if (fraction >= 0.85) {
        return std::string{ansi::red};
    }
    if (fraction >= 0.60) {
        return std::string{ansi::yellow};
    }
    return std::string{ansi::green};
}

std::string Dashboard::section(std::string_view title, int width) const {
    std::string heading;
    if (color_) {
        heading = std::format("{}▌ {}{}", ansi::cyan, title, ansi::reset);
    } else {
        heading = std::format("| {}", title);
    }
    const auto visible = static_cast<int>(title.size()) + 2;
    const auto rule = std::max(0, width - visible);
    heading.append(static_cast<std::size_t>(rule), ' ');
    return heading + "\n";
}

std::string Dashboard::bar(double fraction, int width, std::string_view label, int max_bar) const {
    fraction = std::clamp(fraction, 0.0, 1.0);
    const auto bar_width = std::clamp(width - 34, 8, std::max(8, max_bar));
    const auto filled = static_cast<int>(std::lround(fraction * static_cast<double>(bar_width)));
    const auto color = color_for(fraction);

    std::string output = "[";
    const std::string used = repeat("\u2588", std::clamp(filled, 0, bar_width));
    const std::string rest = repeat("\u2591", std::max(0, bar_width - filled));
    if (color.empty()) {
        output += used + rest;
    } else {
        output += std::format("{}{}{}{}", color, used, ansi::reset, rest);
    }
    output += std::format("] {:5.1f}%  ", fraction * 100.0);
    output += label;
    return output;
}

std::string Dashboard::sparkline(int width) const {
    static constexpr std::string_view blocks[] = {"\u2581", "\u2582", "\u2583", "\u2584",
                                                  "\u2585", "\u2586", "\u2587", "\u2588"};
    const auto count = std::min<std::size_t>(history_.size(), static_cast<std::size_t>(std::max(1, width - 4)));
    if (count == 0) {
        return {};
    }
    std::string line;
    for (std::size_t index = history_.size() - count; index < history_.size(); ++index) {
        const auto value = std::clamp(history_[index], 0.0, 100.0);
        const auto bucket = static_cast<std::size_t>(value / 100.0 * 7.0);
        line += blocks[std::min(bucket, std::size_t{7})];
    }
    return line;
}

std::string Dashboard::render(const Snapshot& snapshot, int width) {
    width = std::clamp(width, 56, 160);
    history_.push_back(snapshot.cpu.usage);
    if (history_.size() > 120) {
        history_.pop_front();
    }

    std::string output;
    output += section("SYSTEM", width);
    const auto header = std::format("  {}  {}  up {}  {}", snapshot.hostname, snapshot.kernel,
                                    format_duration(snapshot.load.uptime_seconds),
                                    snapshot.timestamp);
    output += header + "\n\n";

    output += render_cpu(snapshot, width);
    output += "\n";
    output += render_memory(snapshot, width);
    output += "\n";
    output += render_load(snapshot, width);
    output += "\n";
    output += render_disks(snapshot, width);
    output += "\n";
    output += render_network(snapshot, width);
    if (!snapshot.thermal.empty()) {
        output += "\n";
        output += render_thermal(snapshot, width);
    }

    output += "\n";
    if (show_footer_) {
        const auto footer = std::string{"  q quit   r reset history   +/- interval"};
        if (color_) {
            output += std::format("{}{}{}", ansi::dim, footer, ansi::reset);
        } else {
            output += footer;
        }
        output += "\n";
    }

    std::string fitted;
    fitted.reserve(output.size());
    std::size_t start = 0;
    while (start < output.size()) {
        const auto end = output.find('\n', start);
        const auto length = (end == std::string::npos ? output.size() : end) - start;
        fitted += fit_line(std::string_view{output}.substr(start, length), width);
        if (end == std::string::npos) {
            break;
        }
        fitted.push_back('\n');
        start = end + 1;
    }
    return fitted;
}

std::string Dashboard::render_cpu(const Snapshot& snapshot, int width) {
    const auto& cpu = snapshot.cpu;
    std::string output;

    auto title = std::string{"CPU"};
    if (!cpu.model.empty()) {
        title += std::format("  {}", cpu.model);
    }
    title += std::format("  ({} threads)", cpu.logical_cores);
    if (cpu.frequency_mhz > 0.0) {
        title += std::format("  {:.2f} GHz", cpu.frequency_mhz / 1000.0);
    }
    output += section(title, width);

    output += "  " + bar(cpu.usage / 100.0, width, "") + "\n";
    if (const auto spark = sparkline(width); !spark.empty()) {
        if (color_) {
            output += std::format("  {}{}{}\n", ansi::dim, spark, ansi::reset);
        } else {
            output += "  " + spark + "\n";
        }
    }

    if (cpu.cores.empty()) {
        return output;
    }

    const auto cell_width = std::clamp(width / 4, 16, 24);
    const auto columns = std::max(1, (width - 2) / cell_width);
    for (std::size_t index = 0; index < cpu.cores.size(); index += static_cast<std::size_t>(columns)) {
        std::string row = "  ";
        for (int column = 0; column < columns; ++column) {
            const auto core_index = index + static_cast<std::size_t>(column);
            if (core_index >= cpu.cores.size()) {
                break;
            }
            const auto& core = cpu.cores[core_index];
            const auto mini_width = std::max(4, cell_width - 12);
            const auto filled = static_cast<int>(std::lround(core.usage / 100.0 * static_cast<double>(mini_width)));
            const auto color = color_for(core.usage / 100.0);
            std::string cell = std::format("c{:<2} [", core.index);
            const std::string used = repeat("\u2588", std::clamp(filled, 0, mini_width));
            const std::string rest = repeat("\u2591", std::max(0, mini_width - filled));
            if (color.empty()) {
                cell += used + rest;
            } else {
                cell += std::format("{}{}{}{}", color, used, ansi::reset, rest);
            }
            cell += std::format("] {:3.0f}%", core.usage);
            row += cell;
            const auto visible_width = mini_width + 11;
            row.append(static_cast<std::size_t>(std::max(0, cell_width - visible_width)), ' ');
        }
        output += row + "\n";
    }
    return output;
}

std::string Dashboard::render_memory(const Snapshot& snapshot, int width) {
    const auto& memory = snapshot.memory;
    std::string output = section("MEMORY", width);

    const auto total = static_cast<double>(memory.total);
    const auto used = static_cast<double>(memory.used);
    const auto fraction = total > 0.0 ? used / total : 0.0;
    output += "  RAM   " +
              bar(fraction, width, std::format("{} / {}  (available {})", format_bytes(memory.used),
                                                format_bytes(memory.total), format_bytes(memory.available))) +
              "\n";

    if (memory.swap_total > 0) {
        const auto swap_fraction = static_cast<double>(memory.swap_used) / static_cast<double>(memory.swap_total);
        output += "  Swap  " +
                  bar(swap_fraction, width,
                      std::format("{} / {}", format_bytes(memory.swap_used), format_bytes(memory.swap_total))) +
                  "\n";
    } else {
        output += "  Swap  disabled\n";
    }

    output += std::format("  Buffers {}   Cached {}   Free {}\n", format_bytes(memory.buffers),
                          format_bytes(memory.cached), format_bytes(memory.free));
    return output;
}

std::string Dashboard::render_load(const Snapshot& snapshot, int width) {
    const auto& load = snapshot.load;
    (void)width;
    std::string output = section("LOAD", width);
    output += std::format("  Load  {:.2f}  {:.2f}  {:.2f}      Tasks {} running / {} total\n", load.one,
                          load.five, load.fifteen, load.tasks_running, load.tasks_total);
    return output;
}

std::string Dashboard::render_disks(const Snapshot& snapshot, int width) {
    std::string output = section("DISKS", width);
    if (snapshot.disks.empty()) {
        output += "  no mounted block filesystems detected\n";
        return output;
    }

    for (const auto& disk : snapshot.disks) {
        auto label = std::format("{} / {}", format_bytes(disk.used), format_bytes(disk.total));
        if (width >= 96) {
            label += std::format("  ({} free)", format_bytes(disk.available));
        }
        output += std::format("  {:<14} {}  {}\n", disk.mountpoint.substr(0, 14),
                              bar(disk.usage / 100.0, width - 15, label, 24), disk.filesystem);
    }
    return output;
}

std::string Dashboard::render_network(const Snapshot& snapshot, int width) {
    std::string output = section("NETWORK", width);
    if (snapshot.network.empty()) {
        output += "  no non-loopback interfaces detected\n";
        return output;
    }

    for (const auto& item : snapshot.network) {
        auto line = std::format("  {:<12} down {:>10}/s  up {:>10}/s", item.interface,
                                format_bytes(static_cast<std::uint64_t>(item.rx_rate)),
                                format_bytes(static_cast<std::uint64_t>(item.tx_rate)));
        if (width >= 92) {
            line += std::format("   total down {}  up {}", format_bytes(item.rx_bytes),
                                format_bytes(item.tx_bytes));
        }
        output += line + "\n";
    }
    return output;
}

std::string Dashboard::render_thermal(const Snapshot& snapshot, int width) {
    std::string output = section("THERMAL", width);
    std::string line = "  ";
    int visible = 2;

    for (const auto& zone : snapshot.thermal) {
        const auto reading = std::format("{:.0f}C", zone.celsius);
        const auto item_width = static_cast<int>(zone.name.size()) + 1 + static_cast<int>(reading.size()) + 5;
        if (visible > 2 && visible + item_width > width) {
            output += line + "\n";
            line = "  ";
            visible = 2;
        }
        line += zone.name + " ";
        if (color_) {
            const auto color = zone.celsius >= 85.0   ? ansi::red
                               : zone.celsius >= 70.0 ? ansi::yellow
                                                      : ansi::green;
            line += std::format("{}{}{}     ", color, reading, ansi::reset);
        } else {
            line += reading + "     ";
        }
        visible += item_width;
    }
    output += line + "\n";
    return output;
}

}  // namespace sysmon

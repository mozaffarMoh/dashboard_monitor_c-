#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>

#include "sysmon/types.hpp"

namespace sysmon {

namespace ansi {
inline constexpr std::string_view reset{"\x1b[0m"};
inline constexpr std::string_view bold{"\x1b[1m"};
inline constexpr std::string_view dim{"\x1b[2m"};
inline constexpr std::string_view red{"\x1b[31m"};
inline constexpr std::string_view green{"\x1b[32m"};
inline constexpr std::string_view yellow{"\x1b[33m"};
inline constexpr std::string_view blue{"\x1b[34m"};
inline constexpr std::string_view magenta{"\x1b[35m"};
inline constexpr std::string_view cyan{"\x1b[36m"};
inline constexpr std::string_view clear{"\x1b[2J\x1b[H"};
inline constexpr std::string_view hide_cursor{"\x1b[?25l"};
inline constexpr std::string_view show_cursor{"\x1b[?25h"};
}  // namespace ansi

class Terminal {
public:
    explicit Terminal(bool color);
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    bool enter_raw();
    void leave_raw();
    bool key_pressed();
    int read_key();
    int columns() const;
    int rows() const;
    bool interactive() const { return is_tty_; }

    std::string paint(std::string_view text, std::string_view color) const;

private:
    bool is_tty_{false};
    bool raw_{false};
    bool color_{false};
    class RawState;
    RawState* raw_state_;
};

class Dashboard {
public:
    explicit Dashboard(bool color, bool show_footer = true);

    std::string render(const Snapshot& snapshot, int width);
    void reset();

private:
    std::string render_cpu(const Snapshot& snapshot, int width);
    std::string render_memory(const Snapshot& snapshot, int width);
    std::string render_load(const Snapshot& snapshot, int width);
    std::string render_disks(const Snapshot& snapshot, int width);
    std::string render_network(const Snapshot& snapshot, int width);
    std::string render_thermal(const Snapshot& snapshot, int width);

    std::string section(std::string_view title, int width) const;
    std::string bar(double fraction, int width, std::string_view label, int max_bar = 36) const;
    std::string color_for(double fraction) const;
    std::string sparkline(int width) const;

    bool color_{true};
    bool show_footer_{true};
    std::deque<double> history_;
};

}  // namespace sysmon

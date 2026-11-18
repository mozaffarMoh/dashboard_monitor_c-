#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#include "sysmon/cpu.hpp"
#include "sysmon/dashboard.hpp"
#include "sysmon/system.hpp"
#include "sysmon/types.hpp"
#include "sysmon/util.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;

void handle_signal(int) { g_stop = 1; }

struct Options {
    double interval{1.0};
    bool once{false};
    bool color{true};
    bool help{false};
    bool version{false};
};

void print_usage(std::string_view program) {
    std::cout << std::format(
        "sysmon - real-time Linux system resource monitor\n\n"
        "Usage: {} [options]\n\n"
        "Options:\n"
        "  -n, --interval <seconds>  refresh interval (0.2 - 10.0, default 1.0)\n"
        "      --once                print one snapshot and exit\n"
        "      --no-color            disable ANSI colors\n"
        "  -h, --help                show this help\n"
        "  -v, --version             show version\n\n"
        "Keys: q quit, r reset history, +/- change interval\n",
        program);
}

bool parse_options(int argc, char** argv, Options& options, std::string& error) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "-h" || argument == "--help") {
            options.help = true;
        } else if (argument == "-v" || argument == "--version") {
            options.version = true;
        } else if (argument == "--once") {
            options.once = true;
        } else if (argument == "--no-color") {
            options.color = false;
        } else if (argument == "-n" || argument == "--interval") {
            if (index + 1 >= argc) {
                error = "missing value for " + std::string{argument};
                return false;
            }
            auto parsed = sysmon::to_double(argv[++index]);
            if (!parsed) {
                error = "invalid interval value";
                return false;
            }
            options.interval = std::clamp(*parsed, 0.2, 10.0);
        } else {
            error = "unknown option: " + std::string{argument};
            return false;
        }
    }
    return true;
}

std::string hostname() {
    char buffer[256] = {};
    if (::gethostname(buffer, sizeof(buffer) - 1) != 0) {
        return "unknown";
    }
    return buffer;
}

std::string kernel_release() {
    utsname info{};
    if (::uname(&info) != 0) {
        return "unknown";
    }
    return info.release;
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    return std::format("{:%H:%M:%S}", std::chrono::floor<std::chrono::seconds>(now));
}

sysmon::Snapshot collect(sysmon::CpuMonitor& cpu, sysmon::MemoryReader& memory, sysmon::LoadReader& load,
                         sysmon::DiskReader& disks, sysmon::NetworkMonitor& network,
                         sysmon::ThermalReader& thermal) {
    sysmon::Snapshot snapshot;
    snapshot.cpu = cpu.sample();
    snapshot.memory = memory.read();
    snapshot.load = load.read();
    snapshot.disks = disks.read();
    snapshot.network = network.sample();
    snapshot.thermal = thermal.read();
    snapshot.hostname = hostname();
    snapshot.kernel = kernel_release();
    snapshot.timestamp = timestamp();
    return snapshot;
}

bool use_color(bool requested) { return requested && ::isatty(STDOUT_FILENO) == 1; }

int run_once(bool color) {
    sysmon::CpuMonitor cpu;
    sysmon::MemoryReader memory;
    sysmon::LoadReader load;
    sysmon::DiskReader disks;
    sysmon::NetworkMonitor network;
    sysmon::ThermalReader thermal;

    (void)collect(cpu, memory, load, disks, network, thermal);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const auto snapshot = collect(cpu, memory, load, disks, network, thermal);

    sysmon::Dashboard dashboard{use_color(color), false};
    std::cout << dashboard.render(snapshot, 100);
    return 0;
}

int run_interactive(const Options& options) {
    sysmon::CpuMonitor cpu;
    sysmon::MemoryReader memory;
    sysmon::LoadReader load;
    sysmon::DiskReader disks;
    sysmon::NetworkMonitor network;
    sysmon::ThermalReader thermal;
    const auto color = use_color(options.color);
    sysmon::Terminal terminal{color};
    sysmon::Dashboard dashboard{color};

    auto interval = options.interval;
    terminal.enter_raw();
    std::cout << sysmon::ansi::clear << std::flush;

    (void)collect(cpu, memory, load, disks, network, thermal);

    auto next_refresh = std::chrono::steady_clock::now();
    while (g_stop == 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_refresh) {
            const auto snapshot = collect(cpu, memory, load, disks, network, thermal);
            std::cout << sysmon::ansi::clear;
            std::cout << dashboard.render(snapshot, terminal.columns());
            if (terminal.rows() > 0) {
                std::cout << "\x1b[" << terminal.rows() << ";1H";
            }
            std::cout << std::flush;
            next_refresh = now + std::chrono::milliseconds(static_cast<long long>(interval * 1000.0));
        }

        while (terminal.key_pressed()) {
            const auto key = terminal.read_key();
            if (key < 0) {
                break;
            }
            if (key == 'q' || key == 'Q') {
                g_stop = 1;
                break;
            }
            if (key == 'r' || key == 'R') {
                dashboard.reset();
            }
            if (key == '+' || key == '=') {
                interval = std::clamp(interval * 2.0, 0.2, 10.0);
            }
            if (key == '-' || key == '_') {
                interval = std::clamp(interval / 2.0, 0.2, 10.0);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    terminal.leave_raw();
    std::cout << sysmon::ansi::clear << std::flush;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        std::cerr << std::format("sysmon: {}\n", error);
        std::cerr << "Try '--help' for more information.\n";
        return 2;
    }
    if (options.help) {
        print_usage(argv[0]);
        return 0;
    }
    if (options.version) {
        std::cout << "sysmon " << SYSMON_VERSION << "\n";
        return 0;
    }

    if (std::getenv("NO_COLOR") != nullptr) {
        options.color = false;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    try {
        if (options.once) {
            return run_once(options.color);
        }
        return run_interactive(options);
    } catch (const std::exception& exception) {
        std::cerr << std::format("sysmon: fatal: {}\n", exception.what());
        return 1;
    }
}

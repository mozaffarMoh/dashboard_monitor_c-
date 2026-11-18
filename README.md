# sysmon

A dependency-free, real-time Linux system resource monitor written in modern C++20.
It reads kernel interfaces directly (`/proc`, `/sys`) and renders a responsive
terminal dashboard with CPU, memory, load, disk, network and thermal telemetry.

## Features

- Per-core CPU utilization, average frequency and total-usage sparkline
- RAM / swap usage with available, cached and buffer breakdown
- Load averages, task counts and uptime
- Per-mount disk usage via `statvfs`
- Per-interface RX/TX throughput and lifetime counters
- Thermal zones from `/sys/class/thermal`
- Responsive layout, ANSI colors (respects `NO_COLOR`) and UTF-8 aware truncation
- Single-snapshot mode for scripts: `--once`
- No third-party libraries; RAII-managed terminal state

## Requirements

- Linux (uses `/proc` and `/sys`)
- CMake >= 3.20
- A C++20 compiler (GCC 12+ or Clang 15+)

## Build and run

```bash
make            # configure + build
make run        # interactive dashboard
make once       # print one snapshot
```

Or directly with CMake:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/sysmon
```

Install system-wide:

```bash
sudo cmake --install build
```

## Usage

```
sysmon [options]

  -n, --interval <seconds>  refresh interval (0.2 - 10.0, default 1.0)
      --once                print one snapshot and exit
      --no-color            disable ANSI colors
  -h, --help                show help
  -v, --version             show version
```

Interactive keys:

| Key | Action             |
|-----|--------------------|
| `q` | quit               |
| `r` | reset CPU history  |
| `+` | faster refresh     |
| `-` | slower refresh     |

## Data sources

| Metric      | Source                                                        |
|-------------|---------------------------------------------------------------|
| CPU times   | `/proc/stat` (delta sampled per refresh)                      |
| CPU model   | `/proc/cpuinfo`                                               |
| CPU clock   | `/sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq`       |
| Memory      | `/proc/meminfo`                                               |
| Load / uptime | `/proc/loadavg`, `/proc/uptime`                             |
| Disks       | `/proc/mounts` + `statvfs(2)`                                 |
| Network     | `/proc/net/dev` (delta / elapsed time)                        |
| Temperatures | `/sys/class/thermal/thermal_zone*`                           |

## Layout

```
include/sysmon/
  cpu.hpp        CPU sampling with delta state
  system.hpp     memory, load, disk, network, thermal readers
  dashboard.hpp  terminal + renderer
  types.hpp      plain data models
  util.hpp       file/parse/format helpers
src/             implementations
```

The core is a static library (`sysmon_core`); `src/main.cpp` contains only
argument handling and the update loop, so the readers and renderer can be reused
or unit-tested independently.

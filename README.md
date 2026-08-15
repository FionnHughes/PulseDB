# PulseDB

A self-hosted system observability daemon for Windows. It watches your CPU,
RAM, disk, network, and running processes every second, stores the history
in a custom binary format, and serves it over a local REST API and a live
WebSocket feed.

There's no GUI yet as this is the backend. Query it with curl, a script, or
build your own frontend against the API.

## Status

Collector, storage engine, and API/WebSocket layer are done and tested.
No GUI, no alert engine, no config file yet so see "What's not here yet"
below.

## Features

- Collects CPU (per-core), RAM/swap, disk throughput per drive, network
  throughput per adapter, and process info every second
- Uses the lowest-level Windows API available per metric with no unnecessary
  overhead
- Custom binary storage format (`.pulse`), LZ4-compressed, one file per
  metric per day
- Automatic downsampling into 1-minute and 1-hour summaries, with
  configurable retention
- Local REST API and a live WebSocket feed
- Crash-safe writes via a per-metric write-ahead log

## Installation

**If you just want to run it**

1. Get the latest `pulsedb_daemon.exe` from the [Releases](../../releases) page
2. Make sure the [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe)
   is installed. Most Windows machines already have it. If the exe won't
   launch, this is almost certainly why.
3. Run `pulsedb_daemon.exe`

No installer, no extra dependencies to set up as it's a single executable.

**Building from source**

Needs CMake 3.25+, Conan 2, and Visual Studio (MSVC) on Windows.

```
git clone https://github.com/FionnHughes/PulseDB.git
cd PulseDB
conan install . --build=missing --output-folder=conan
```

Then open the project in Visual Studio (or build via CMake directly) and
build the `pulsedb_daemon` and `pulsedb_tests` targets. The daemon exe lands
at `conan\daemon\Release\pulsedb_daemon.exe`, tests at
`conan\daemon\Release\pulsedb_tests.exe`.

## Usage

Run the daemon:

```
pulsedb_daemon.exe
```

It starts collecting immediately and serves the API on
`http://localhost:7700`. Leave the window open, Ctrl+C shuts it down
cleanly, flushing any pending writes first.

Query it from another terminal:

```
curl http://127.0.0.1:7700/api/status
curl http://127.0.0.1:7700/api/latest
curl "http://127.0.0.1:7700/api/query?metric=cpu_total&from=0&to=9999999999999"
```

## API reference

Base URL: `http://localhost:7700`

| Endpoint | Description |
|---|---|
| `GET /api/status` | Daemon uptime and version |
| `GET /api/metrics` | Names of all metrics currently being written |
| `GET /api/latest` | Most recent snapshot, read from an in-memory ring buffer |
| `GET /api/query?metric=X&from=T&to=T` | Raw historical readings between two Unix ms timestamps, with min/max/mean/p95 |
| `GET /api/processes/latest` | Every running process, not just the top 25 |
| `WS /ws/live` | Pushes one live snapshot per second to any connected client |

## Architecture

```
Client (curl, script, future GUI)
    |  HTTP REST + WebSocket (localhost:7700)
Drogon API layer
    |
Collectors (WinAPI / NT / PDH) -> SPSC queue -> Storage engine (.pulse files)
                                -> ring buffer (last 5 min, in memory)
                                                  |
                                          SQLite (1-min / 1-hr summaries)
```

Collectors use the lowest-level Windows API available per metric —
`GetSystemTimes` and `NtQuerySystemInformation` for CPU, `GlobalMemoryStatusEx`
for RAM, `GetIfTable2` for network, `GetSystemPowerStatus` for battery. PDH is
used only for disk, since named per-drive enumeration is cleanest through it.

## Storage format

One `.pulse` file per metric per day. Data is written in chunks of 60
readings, LZ4-compressed, with delta-encoded timestamps.

```
[File Header - 64 bytes]
[Chunk Index - 16 bytes x chunk_count]
[Chunks - variable size, LZ4-compressed]
```

Magic bytes: `PULS`. Version: 1. Everything little-endian.

Each chunk decompresses to a 16-byte header plus `N x 10` bytes of readings
(2-byte timestamp delta + 8-byte float64 value).

## Data retention

| Resolution | Storage | Default retention |
|---|---|---|
| Raw (1s) | `.pulse` files | 7 days |
| 1-minute summaries | SQLite | 30 days |
| 1-hour summaries | SQLite | 365 days |

## Roadmap

- [ ] Config file (currently everything is hardcoded in `main.cpp`)
- [ ] Full process list wired into a live query endpoint alongside top-25
- [ ] Alert engine, threshold, sustained threshold, and rate of change
      rules, evaluated on a timer, with Windows toast notifications
- [ ] Desktop GUI (Tauri + React)
- [ ] Windows service packaging

## What's not here yet

- No GUI. API-only right now.
- No alert engine. It is designed but not implemented.
- No config file. Collection interval, retention, and port are all hardcoded.
- No API authentication. Only binds to 127.0.0.1 so don't expose it to a
  network.
- No Windows service packaging. It's a console app you run and leave open.
- Admin rights are needed for the process collector to see every process,
  not just your own.

## Goals

- Collector CPU overhead under 2%
- 30 days of 1-second data under 500 MB on disk
- 24-hour range query under 100ms
- Live update latency under 500ms end to end
- Stable for 30+ days without a restart

## License

Not yet decided.
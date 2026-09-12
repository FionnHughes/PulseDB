# PulseDB

A self-hosted system observability tool for Windows. It collects CPU, RAM,
disk, network, and process metrics every second, stores the history in a
custom binary format, and provides a desktop app plus a REST API for
viewing live and historical data.

## Contents

- [Features](#features)
- [Installation](#installation)
- [Usage](#usage)
- [API reference](#api-reference)
- [Architecture](#architecture)
- [Storage format](#storage-format)
- [Data retention](#data-retention)
- [Roadmap](#roadmap)
- [Development notes](#development-notes)
- [License](#license)

## Features

- Collects CPU (per-core), RAM/swap, disk throughput per drive, network
  throughput per adapter, and full process info every second
- Custom binary storage format (`.pulse`), LZ4-compressed, one file per
  metric per day, with a write-ahead log for crash recovery
- Automatic downsampling into 1-minute and 1-hour summaries in SQLite,
  with configurable retention per resolution
- Alert engine: threshold, sustained-threshold, and rate-of-change rules,
  evaluated every 5 seconds, with a full history log
- Desktop app (Tauri + React): live dashboard, process monitor
  (filter/sort/kill), historical explorer with zoomable charts and a
  point-in-time inspector, alert manager, and settings
- Local REST API and live WebSocket feed for scripting or building your
  own frontend against it
- Exclusive lock on the data directory, so a second daemon instance can't
  run against the same data and corrupt it

## Installation

Pick one: the installer if you want the full desktop app, or the daemon exe
alone if you only want the API/CLI. You don't need both.

**Option A: Windows installer (daemon + desktop app)**

1. Download the latest `.exe` installer from the [Releases](../../releases) page
2. Run it and follow the prompts
3. Launch PulseDB from the Start Menu

The installer sets up both the daemon and the desktop app together, no
separate steps needed. Closing the window minimizes PulseDB to the system
tray rather than quitting; right-click the tray icon to reopen the window
or quit for real.

**Option B: Daemon exe only (API/CLI, no desktop app)**

1. Get the latest `pulsedb_daemon.exe` from the [Releases](../../releases) page
2. Make sure the [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe)
   is installed (most Windows machines already have it)
3. Run `pulsedb_daemon.exe`

**Running the GUI from source (dev mode)**

Only needed if you're developing the GUI itself, not for normal use.
Requires Node.js and the Tauri CLI. From `gui/`:

```
npm install
npm run tauri dev
```

The daemon must be running for the GUI to show data.

**Build from source**

Requires CMake 3.25+, Conan 2, and Visual Studio (MSVC).

```
git clone https://github.com/FionnHughes/PulseDB.git
cd PulseDB
conan install . --build=missing --output-folder=conan
```

Open in Visual Studio or build via CMake directly. Build the
`pulsedb_daemon` and `pulsedb_tests` targets. The daemon binary lands at
`conan\daemon\Release\pulsedb_daemon.exe`.

## Usage

Run the daemon and leave the window open. Ctrl+C shuts it down cleanly and
flushes pending writes first.

```
pulsedb_daemon.exe
```

It starts collecting immediately and serves the API on
`http://localhost:7700`.

```
curl http://127.0.0.1:7700/api/status
curl http://127.0.0.1:7700/api/latest
curl "http://127.0.0.1:7700/api/query?metric=cpu_total&from=0&to=9999999999999"
```

## API reference

Base URL: `http://localhost:7700`

| Endpoint                                                          | Description                                                    |
| ----------------------------------------------------------------- | -------------------------------------------------------------- |
| `GET /api/status`                                                 | Daemon uptime and version                                      |
| `GET /api/metrics`                                                | Names of all metrics currently being written                   |
| `GET /api/latest`                                                 | Most recent snapshot, from an in-memory ring buffer            |
| `GET /api/query?metric=X&from=T&to=T&resolution=raw\|1min\|1hr`   | Historical readings, raw or downsampled, with min/max/mean/p95 |
| `GET /api/processes/latest`                                       | Every running process                                          |
| `POST /api/processes/{pid}/kill`                                  | Terminates a process                                           |
| `GET/POST /api/alerts/rules`, `PUT/DELETE /api/alerts/rules/{id}` | Alert rule CRUD                                                |
| `GET /api/alerts/active`                                          | Rules currently pending or firing                              |
| `GET /api/alerts/history`, `DELETE /api/alerts/history/{id}`      | Alert history, with cleanup                                    |
| `GET/PUT /api/config`                                             | Read or update the config file                                 |
| `WS /ws/live`                                                     | Pushes one live snapshot per second                            |

## Architecture

- Desktop GUI (Tauri + React) talks to the Drogon API layer over HTTP REST and WebSocket on `localhost:7700`
- Collectors (WinAPI / NT / PDH) push into an SPSC queue, which feeds both the storage engine (`.pulse` files) and an in-memory ring buffer (last 5 min)
- SQLite holds the 1-min/1-hr summaries, alert rules and history, and config
- The alert engine runs its own 5-second evaluation loop off the ring buffer

Collectors use the lowest-level Windows API available per metric:
`GetSystemTimes` and `NtQuerySystemInformation` for CPU, `GlobalMemoryStatusEx`
for RAM, `GetIfTable2` for network, `GetSystemPowerStatus` for battery. PDH
is used for disk, since named per-drive enumeration is cleanest through it.

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

| Resolution         | Storage        | Default retention |
| ------------------ | -------------- | ----------------- |
| Raw (1s)           | `.pulse` files | 7 days            |
| 1-minute summaries | SQLite         | 30 days           |
| 1-hour summaries   | SQLite         | 365 days          |

Configurable via `pulsedb.json` or the Settings screen in the GUI. Restart
the daemon after changing.

## Roadmap

- Linux support
- Hand-rewritten frontend

## Development notes

I built the backend (collectors, storage engine, alert engine) myself, with
AI (Claude) helping out here and there along the way, mostly for debugging
and troubleshooting. I leaned on it more for the GUI, since I mainly use
PulseDB through the API/CLI myself and didn't want to spend my own time on a
frontend I wouldn't use much.



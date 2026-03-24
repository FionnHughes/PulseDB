# PulseDB — Full Project Specification & Development Timeline
# Windows-Targeted Build | C++20 + Tauri + React

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Goals & Success Criteria](#2-goals--success-criteria)
3. [System Architecture](#3-system-architecture)
4. [Technology Stack](#4-technology-stack)
5. [Module 1 — Collector Daemon](#5-module-1--collector-daemon)
6. [Module 2 — Time-Series Storage Engine](#6-module-2--time-series-storage-engine)
7. [Module 3 — REST API & WebSocket Layer](#7-module-3--rest-api--websocket-layer)
8. [Module 4 — Alert Engine](#8-module-4--alert-engine)
9. [Module 5 — Desktop GUI](#9-module-5--desktop-gui)
10. [Database Schema](#10-database-schema)
11. [File Format Specification (.pulse)](#11-file-format-specification-pulse)
12. [API Reference](#12-api-reference)
13. [Configuration System](#13-configuration-system)
14. [Testing Strategy](#14-testing-strategy)
15. [Development Timeline](#15-development-timeline)
16. [Directory Structure](#16-directory-structure)
17. [Third-Party Dependencies](#17-third-party-dependencies)
18. [Known Limitations & Future Work](#18-known-limitations--future-work)

---

## 1. Project Overview

PulseDB is a self-hosted, lightweight system observability platform targeting Windows machines. It consists of a C++20 background daemon that continuously collects system performance metrics, a custom binary time-series storage engine that persists those metrics efficiently on disk, a REST/WebSocket API that exposes the data, and a Tauri-based desktop application (React frontend) that visualises everything in real time and historically.

The core thesis of PulseDB is that commercial observability tools (Datadog, New Relic, Grafana Agent) are resource-heavy, require cloud connectivity, and are opaque in how they work. PulseDB is fully local, fully open, and intentionally minimal in its own resource footprint — it monitors your system without meaningfully impacting it.

### What it does in plain terms

- Runs silently in the background as a Windows service
- Every second (configurable), it samples CPU usage per core, total and per-process RAM, disk read/write throughput per drive, network bytes in/out per adapter, and a ranked list of the top processes by resource consumption
- Writes all of that to a compressed binary file on disk using a custom storage format
- Older data is automatically downsampled (1s resolution -> 1min averages -> 1hr averages) and eventually pruned based on configurable retention policies
- Exposes a local HTTP API and WebSocket stream so the desktop GUI can query historical data and receive live updates
- The desktop app shows live charts, a process monitor, a historical query explorer, and an alert manager
- Users can define alert rules (e.g. "if CPU > 85% for 30 seconds, show a notification") that the alert engine evaluates continuously

---

## 2. Goals & Success Criteria

### Primary Goals

- Collect CPU, RAM, disk, network, and process metrics with sub-2% CPU overhead on the collector daemon itself
- Store 30 days of 1-second resolution data for all metrics in under 500MB on disk
- Serve a historical range query covering 24 hours of data in under 100ms
- Deliver live metric updates to the GUI with under 500ms latency from collection to render
- Run stably for 30+ days without restart, memory leak, or file corruption

### Secondary Goals

- Produce a clean, documented REST API that a third-party tool could consume
- Support at least 3 configurable alert rule types (threshold, sustained threshold, rate-of-change)
- Package as a single Windows installer (.exe) using NSIS
- GUI works at 1080p and 1440p resolutions without layout breakage

### Out of Scope for v1

- Remote/multi-machine monitoring
- Linux or macOS support
- Authentication on the API
- Cloud sync or remote storage
- Custom plugin SDK

---

## 3. System Architecture

```
+---------------------------------------------------------------+
|                     TAURI DESKTOP APP                         |
|                  (React + Recharts + Tailwind)                |
|                                                               |
|  +---------------+  +-------------------+  +--------------+  |
|  | Live Dashboard|  | Historical Query  |  | Alert Manager|  |
|  +---------------+  +-------------------+  +--------------+  |
|  +---------------+  +-------------------+                    |
|  | Process Monitor|  | Settings          |                    |
|  +---------------+  +-------------------+                    |
+-----------------------------+---------------------------------+
                              | HTTP REST + WebSocket (localhost)
+-----------------------------v---------------------------------+
|                   C++ REST API LAYER                          |
|              (Drogon Framework, async, coroutines)            |
+-----------------------------+---------------------------------+
                              |
+-----------------------------v---------------------------------+
|                   CORE C++ DAEMON                             |
|                                                               |
|  +---------------------+    +-----------------------------+   |
|  |   COLLECTOR MODULE  |    |  TIME-SERIES STORAGE ENGINE |   |
|  |                     |    |                             |   |
|  | WMI / PDH / WinAPI  |    |  .pulse binary file format  |   |
|  |                     |    |  LZ4 chunk compression      |   |
|  | - CPU (per core)    |    |  Delta timestamp encoding   |   |
|  | - RAM (total/proc)  |    |  Retention policy engine    |   |
|  | - Disk (per drive)  |    |  Downsampling scheduler     |   |
|  | - Network (per NIC) |    |  mmap-based reads           |   |
|  | - Process list      |    |  Write-ahead log (WAL)      |   |
|  +----------+----------+    +-----------------------------+   |
|             |                                                 |
|             +-----------> Lock-free SPSC Queue                |
|                            (collector -> storage)             |
|                                                               |
|  +---------------------+                                      |
|  |   ALERT ENGINE      |                                      |
|  |                     |                                      |
|  | Rule evaluator      |                                      |
|  | Windows Toast API   |                                      |
|  | Alert history log   |                                      |
|  +---------------------+                                      |
+---------------------------------------------------------------+
                              |
               +--------------v-----------+
               |    SQLite3 Database      |
               |  (alert rules, config,   |
               |   alert history,         |
               |   downsampled summaries) |
               +--------------------------+
               |    .pulse binary files   |
               |  (raw time-series data,  |
               |   one file per metric    |
               |   per day)               |
               +--------------------------+
```

### Data flow summary

1. Collector runs on a 1-second timer via Boost.Asio
2. Each tick, it reads all metrics from Windows APIs
3. Metric readings are pushed onto a lock-free single-producer single-consumer (SPSC) queue
4. A storage writer thread drains the queue and appends to the active .pulse files
5. Every 60 seconds, a downsampler reads the last 60 raw readings, computes averages/min/max, and writes a 1-minute summary to SQLite
6. Every 60 minutes, 1-minute summaries are aggregated into 1-hour summaries
7. The alert engine runs on its own timer (configurable, default 5s), reads the latest metric values from an in-memory ring buffer, evaluates all active alert rules, and fires notifications if rules are triggered
8. The API layer reads from both the .pulse files (for raw data) and SQLite (for summaries and config) to serve requests
9. The WebSocket pushes the latest snapshot from the in-memory buffer to all connected clients every 1 second

---

## 4. Technology Stack

| Layer                    | Technology                        | Reason                                                      |
|--------------------------|-----------------------------------|-------------------------------------------------------------|
| Core language            | C++20                             | Coroutines, concepts, ranges — modern and CV-relevant       |
| Build system             | CMake 3.25+                       | Industry standard                                           |
| Package manager          | Conan 2.x                         | Handles all C++ deps cleanly on Windows                     |
| Async runtime            | Boost.Asio                        | Battle-tested, cross-platform async I/O and timers          |
| HTTP/WebSocket server    | Drogon                            | Fastest C++ web framework, native async, WebSocket support  |
| Compression              | LZ4                               | Extremely fast compression/decompression, low CPU overhead  |
| Serialisation            | FlatBuffers                       | Zero-copy deserialisation, schema-defined, fast             |
| Config & metadata        | nlohmann/json                     | Simple, header-only JSON                                    |
| Relational storage       | SQLite3 (sqlite_modern_cpp)       | Alerts, config, summaries                                   |
| Windows metrics APIs     | PDH, WMI, WinAPI, IPHLPAPI        | Official Windows performance data interfaces                |
| Hashing                  | xxHash                            | Very fast non-cryptographic hash for dedup and integrity    |
| Testing                  | Google Test + Google Benchmark    | Unit tests + performance benchmarks                         |
| Desktop shell            | Tauri 2.x                         | Native desktop app, tiny binary, uses React frontend        |
| Frontend language        | TypeScript + React 18             | Type safety, component model                                |
| UI framework             | Tailwind CSS v3                   | Utility-first, fast to build with                           |
| Charting                 | Recharts                          | React-native charts, good for time-series                   |
| State management         | Zustand                           | Lightweight, no boilerplate                                 |
| Installer                | NSIS                              | Windows installer packaging                                 |

---

## 5. Module 1 — Collector Daemon

### Overview

The collector is a set of platform-specific readers that each expose a common interface. Every second (configurable via collection_interval_ms in config), the collector scheduler fires, calls each reader, assembles a MetricSnapshot struct, and enqueues it for the storage engine.

### Common Interface

Every collector plugin implements:

```cpp
class IMetricCollector {
public:
    virtual std::string name() const = 0;
    virtual bool initialize() = 0;
    virtual MetricReading collect() = 0;
    virtual void shutdown() = 0;
    virtual ~IMetricCollector() = default;
};
```

### MetricSnapshot Structure

```cpp
struct MetricSnapshot {
    int64_t timestamp_ms;           // Unix timestamp in milliseconds

    // CPU
    float cpu_total_percent;
    std::vector<float> cpu_per_core_percent;
    float cpu_frequency_mhz;

    // Memory
    uint64_t ram_total_bytes;
    uint64_t ram_used_bytes;
    uint64_t ram_available_bytes;
    uint64_t swap_total_bytes;
    uint64_t swap_used_bytes;

    // Per-disk
    struct DiskStats {
        std::string device_name;
        uint64_t read_bytes_per_sec;
        uint64_t write_bytes_per_sec;
        float utilization_percent;
        uint64_t queue_depth;
    };
    std::vector<DiskStats> disks;

    // Per-network adapter
    struct NetworkStats {
        std::string adapter_name;
        uint64_t bytes_in_per_sec;
        uint64_t bytes_out_per_sec;
        uint64_t packets_in_per_sec;
        uint64_t packets_out_per_sec;
    };
    std::vector<NetworkStats> network_adapters;

    // Top processes
    struct ProcessInfo {
        uint32_t pid;
        std::string name;
        float cpu_percent;
        uint64_t ram_bytes;
        uint64_t disk_read_bytes_per_sec;
        uint64_t disk_write_bytes_per_sec;
    };
    std::vector<ProcessInfo> top_processes;  // top 25 by CPU
};
```

### CPU Collector (Windows)

Uses the Windows Performance Data Helper (PDH) library.

- Opens a PDH query on startup
- Adds counters for \Processor(_Total)\% Processor Time and \Processor(N)\% Processor Time for each core
- Calls PdhCollectQueryData twice per reading (PDH requires two samples to compute a rate)
- First call happens at initialisation, second at each collection tick
- CPU frequency read from HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0\~MHz

Error handling:
- If PDH query fails to open, collector marks itself as degraded and returns zeroed readings
- PDH counter removal and re-addition on counter staleness errors

### RAM Collector (Windows)

Uses GlobalMemoryStatusEx from WinAPI.

- Fills a MEMORYSTATUSEX struct
- Extracts ullTotalPhys, ullAvailPhys, ullTotalPageFile, ullAvailPageFile
- Swap = page file minus physical RAM (approximation)
- No PDH needed, single WinAPI call per tick

### Disk Collector (Windows)

Uses PDH counters:

- \PhysicalDisk(*)\Disk Read Bytes/sec
- \PhysicalDisk(*)\Disk Write Bytes/sec
- \PhysicalDisk(*)\% Disk Time
- \PhysicalDisk(*)\Current Disk Queue Length

Enumerates all physical disks at initialisation. Wildcards expand per-disk automatically.

### Network Collector (Windows)

Uses GetIfTable2 from IPHLPAPI to enumerate all network adapters. On each tick:

- Calls GetIfTable2 again
- Diffs InOctets and OutOctets against previous reading
- Divides by elapsed time to get bytes/sec
- Filters out loopback and tunnel adapters by IfType

### Process Collector (Windows)

Uses EnumProcesses + OpenProcess + GetProcessMemoryInfo + PDH per-process CPU counters.

- Enumerates all PIDs
- Opens each process with PROCESS_QUERY_LIMITED_INFORMATION
- Reads working set from PROCESS_MEMORY_COUNTERS
- CPU per process via PDH \Process(*)\% Processor Time wildcard counter
- Sorts by CPU descending, returns top 25
- Caches process name lookups to avoid repeated QueryFullProcessImageName calls

### Scheduler

```
Boost.Asio io_context with a steady_timer
  -> fires every collection_interval_ms
  -> calls each IMetricCollector::collect() sequentially
  -> assembles MetricSnapshot
  -> pushes to SPSC queue
  -> updates in-memory ring buffer (last 300 readings = 5 minutes at 1s)
```

The ring buffer is read by the alert engine and the WebSocket broadcaster without going to disk.

---

## 6. Module 2 — Time-Series Storage Engine

### Overview

The storage engine is the most technically significant component. It writes metric data to a custom binary file format (.pulse) optimised for sequential writes and range reads. It also manages a downsampling pipeline that reduces data resolution over time.

### Design Decisions

- One file per metric per day. e.g. data/cpu_total/2024-11-15.pulse. This keeps files small, makes pruning trivial (delete file = delete day), and allows parallel reads.
- Append-only writes. No in-place mutation. This makes writes fast and the file easy to reason about.
- Chunked storage. Data is grouped into chunks of 60 readings each. A chunk is compressed as a unit with LZ4. This balances compression ratio against random-access cost.
- Delta encoding for timestamps. Instead of storing full 64-bit timestamps for every reading, only the base timestamp is stored per chunk and subsequent readings store a 16-bit delta in milliseconds. At 1s intervals, the delta is always 1000ms, so this compresses extremely well.
- Write-Ahead Log (WAL). Before any chunk is committed to the .pulse file, it is written to a small WAL file. On startup, the WAL is replayed to recover any uncommitted chunk from a crash.

### Writer Thread

- Drains the SPSC queue
- Accumulates readings into an in-memory chunk buffer (60 readings)
- When the buffer is full, compresses it with LZ4, writes to WAL, appends to the active .pulse file, clears WAL
- On midnight rollover, closes the current day's file and opens a new one
- File handles are kept open for the duration of the day (no open/close per write)

### Reader

- Given a metric name and a time range [start_ms, end_ms]:
  - Identifies which daily files overlap the range
  - For each file, reads the file header to get the chunk index
  - Seeks to only the chunks whose time range overlaps the query range
  - Decompresses each relevant chunk with LZ4
  - Filters individual readings to the exact requested range
  - Returns a vector of (timestamp_ms, value) pairs
- Uses mmap (CreateFileMapping + MapViewOfFile on Windows) for file reads to avoid read() syscall overhead on large files

### Downsampling Pipeline

Runs on a background timer:

1-minute summaries (every 60 seconds):
- Reads the last 60 raw readings for each metric from the .pulse file or ring buffer
- Computes: min, max, mean, p95
- Writes to SQLite table metric_summaries_1min

1-hour summaries (every 60 minutes):
- Reads the last 60 one-minute summaries from SQLite
- Computes: min, max, mean, p95
- Writes to SQLite table metric_summaries_1hr

Retention enforcement (daily at 03:00):
- Deletes .pulse files older than raw_retention_days (default: 7)
- Deletes SQLite rows from metric_summaries_1min older than min_retention_days (default: 30)
- Deletes SQLite rows from metric_summaries_1hr older than hr_retention_days (default: 365)

### Storage Estimates

At 1 second resolution with LZ4 compression and delta encoding:
- CPU total: ~8 bytes/reading raw -> ~2 bytes after compression = 172KB/day
- RAM: ~16 bytes/reading raw -> ~3 bytes after compression = 259KB/day
- Disk (2 drives): ~32 bytes/reading raw -> ~5 bytes after compression = 432KB/day
- Network (2 adapters): ~32 bytes/reading raw -> ~5 bytes after compression = 432KB/day
- Processes (top 25): ~600 bytes/reading raw -> ~80 bytes after compression = 6.9MB/day

Total raw data estimate: ~8-10MB per day. 7 days raw = ~70MB. Well within target.

---

## 7. Module 3 — REST API & WebSocket Layer

### Overview

Built with the Drogon C++ framework. Runs on localhost only (not exposed to network by default). Handles all queries from the GUI and any external tools.

### Threading Model

Drogon uses an event-loop model with a configurable number of I/O threads. All API handlers are async — they dispatch storage reads to a thread pool and return results via coroutines without blocking the event loop.

```cpp
// Example handler pattern using Drogon coroutines
Task<> MetricController::queryRange(HttpRequestPtr req,
                                     std::function<void(HttpResponsePtr)> callback) {
    auto metric = req->getParameter("metric");
    auto from = std::stoll(req->getParameter("from"));
    auto to = std::stoll(req->getParameter("to"));

    auto readings = co_await storageEngine_->queryRangeAsync(metric, from, to);

    auto resp = HttpResponse::newHttpJsonResponse(toJson(readings));
    callback(resp);
}
```

### CORS Configuration

All responses include Access-Control-Allow-Origin: http://localhost:* to allow the Tauri webview to make requests.

### WebSocket Live Feed

- Client connects to ws://localhost:7700/ws/live
- Server pushes a JSON snapshot every 1 second from the in-memory ring buffer
- If no client is connected, the broadcast is skipped (no wasted work)
- On disconnect, server cleans up the connection handler immediately
- Snapshot payload is the full MetricSnapshot serialised as JSON

---

## 8. Module 4 — Alert Engine

### Overview

Runs on a dedicated thread with a configurable evaluation interval (default 5 seconds). Reads from the in-memory ring buffer (not from disk) for low latency. Persists triggered alerts to SQLite. Fires Windows toast notifications via the Windows Runtime (WinRT) API.

### Alert Rule Types

Type 1: Threshold
Fires if a metric value exceeds a threshold on any single reading.
```json
{
  "type": "threshold",
  "metric": "cpu_total_percent",
  "operator": "gt",
  "value": 90.0
}
```

Type 2: Sustained Threshold
Fires if a metric value exceeds a threshold for a consecutive number of readings.
```json
{
  "type": "sustained_threshold",
  "metric": "cpu_total_percent",
  "operator": "gt",
  "value": 85.0,
  "duration_readings": 30
}
```

Type 3: Rate of Change
Fires if a metric changes by more than X% within a rolling window of N readings.
```json
{
  "type": "rate_of_change",
  "metric": "ram_used_bytes",
  "change_percent": 20.0,
  "window_readings": 60
}
```

### Supported Metrics for Alerts

- cpu_total_percent
- cpu_core_N_percent (where N is core index)
- ram_used_percent
- ram_used_bytes
- disk_DEVICE_read_bytes_per_sec
- disk_DEVICE_write_bytes_per_sec
- network_ADAPTER_bytes_in_per_sec
- network_ADAPTER_bytes_out_per_sec

### Alert State Machine

Each rule has a state: INACTIVE, PENDING, ACTIVE, COOLDOWN.

```
INACTIVE -> PENDING   (threshold first crossed)
PENDING  -> ACTIVE    (condition held for required duration)
ACTIVE   -> COOLDOWN  (condition no longer met; notification fired once on ACTIVE transition)
COOLDOWN -> INACTIVE  (cooldown period elapsed, default 5 minutes)
```

This prevents alert spam on oscillating metrics.

### Windows Toast Notification

Uses WinRT ToastNotificationManager to send a native Windows notification with:
- Title: "PulseDB Alert"
- Body: human-readable description of the triggered rule
- App icon

Requires a registered AUMID (App User Model ID) set during installation.

---

## 9. Module 5 — Desktop GUI

### Overview

Tauri 2.x provides the native window shell and system tray integration. The frontend is a React 18 TypeScript application served inside the Tauri webview. It communicates with the C++ backend via standard HTTP and WebSocket.

### Application Screens

#### Screen 1: Live Dashboard
- Header bar: current time, daemon status indicator (green/red), system hostname
- Grid of metric cards:
  - CPU gauge (donut chart) + sparkline of last 60 seconds
  - RAM usage bar + used/total labels
  - Disk read/write throughput bars per drive
  - Network in/out throughput per adapter
- Process table: PID, name, CPU%, RAM, disk R/W, sortable by column, updates every 5 seconds
- All data comes from the WebSocket /ws/live feed

#### Screen 2: Historical Explorer
- Metric selector dropdown (all available metrics)
- Date/time range picker (from/to, supports presets: last 1h, 6h, 24h, 7d)
- Resolution selector: raw (1s), 1-minute averages, 1-hour averages
- Line chart (Recharts LineChart) with zoom + pan
- Stats panel: min, max, mean, p95 for the selected range
- Export button: downloads the queried data as CSV

#### Screen 3: Process Monitor
- Full process list (not just top 25) — queries /api/processes/latest
- Columns: PID, Name, CPU%, RAM (MB), Disk R/W, Status
- Search/filter by name
- Click a process to see a mini history chart (last 5 minutes from ring buffer)

#### Screen 4: Alert Manager
- List of configured alert rules with enable/disable toggle
- "New Alert" button opens a form: metric, rule type, thresholds, name
- Alert history table: timestamp, rule name, metric value at trigger, duration
- Clear history button

#### Screen 5: Settings
- Collection interval (ms) — slider, 500ms to 10000ms
- Retention policies — raw days, 1min days, 1hr days
- Data directory path
- API port
- Theme toggle (dark/light)
- "Open Data Directory" button
- Daemon restart button

### System Tray

Tauri registers a system tray icon. Right-click menu:
- Show/Hide window
- Pause/Resume collection
- Exit

### State Management

Zustand store with slices:
- liveMetrics — latest snapshot from WebSocket
- alerts — alert rules + history
- settings — local copy of config (synced from API on startup)
- queryCache — simple in-memory cache of recent historical queries (keyed by metric+range)

### WebSocket Client

- Connects on app load
- Auto-reconnects with exponential backoff if connection drops
- Updates liveMetrics store on every message
- Shows "Disconnected" banner if reconnection fails after 5 attempts

---

## 10. Database Schema

SQLite database: pulsedb.sqlite in the data directory.

```sql
-- Alert rules defined by the user
CREATE TABLE alert_rules (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    name              TEXT NOT NULL,
    metric            TEXT NOT NULL,
    rule_type         TEXT NOT NULL CHECK(rule_type IN ('threshold','sustained_threshold','rate_of_change')),
    operator          TEXT CHECK(operator IN ('gt','lt','gte','lte')),
    value             REAL,
    duration_readings INTEGER,
    change_percent    REAL,
    window_readings   INTEGER,
    cooldown_seconds  INTEGER NOT NULL DEFAULT 300,
    enabled           INTEGER NOT NULL DEFAULT 1,
    created_at        INTEGER NOT NULL
);

-- History of triggered alerts
CREATE TABLE alert_history (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    rule_id          INTEGER NOT NULL REFERENCES alert_rules(id),
    triggered_at     INTEGER NOT NULL,
    resolved_at      INTEGER,
    peak_value       REAL,
    duration_seconds INTEGER
);

-- 1-minute downsampled summaries
CREATE TABLE metric_summaries_1min (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    metric    TEXT NOT NULL,
    bucket_ts INTEGER NOT NULL,   -- unix timestamp of the minute start
    min_val   REAL NOT NULL,
    max_val   REAL NOT NULL,
    mean_val  REAL NOT NULL,
    p95_val   REAL NOT NULL
);
CREATE INDEX idx_summaries_1min_metric_ts ON metric_summaries_1min(metric, bucket_ts);

-- 1-hour downsampled summaries
CREATE TABLE metric_summaries_1hr (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    metric    TEXT NOT NULL,
    bucket_ts INTEGER NOT NULL,
    min_val   REAL NOT NULL,
    max_val   REAL NOT NULL,
    mean_val  REAL NOT NULL,
    p95_val   REAL NOT NULL
);
CREATE INDEX idx_summaries_1hr_metric_ts ON metric_summaries_1hr(metric, bucket_ts);

-- Configuration key-value store
CREATE TABLE config (
    key        TEXT PRIMARY KEY,
    value      TEXT NOT NULL,
    updated_at INTEGER NOT NULL
);
```

---

## 11. File Format Specification (.pulse)

All multi-byte integers are little-endian.

### File Layout

```
[File Header]
[Chunk Index]
[Chunk 0]
[Chunk 1]
...
[Chunk N]
```

### File Header (64 bytes)

```
Offset  Size  Type    Field
0       4     u8[4]   Magic bytes: 0x50 0x55 0x4C 0x53 ("PULS")
4       2     u16     Format version (currently 1)
6       1     u8      Metric type ID (see table below)
7       1     u8      Reserved (0x00)
8       8     i64     File creation timestamp (Unix ms)
16      8     i64     Day start timestamp (midnight Unix ms)
24      4     u32     Number of chunks in file
28      4     u32     Offset of chunk index from file start
32      4     u32     Expected readings per chunk (default 60)
36      4     u32     Collection interval ms
40      24    u8[24]  Metric name string (null-padded, UTF-8)
```

### Metric Type IDs

```
0x01  cpu_total
0x02  cpu_core (core index in reserved header byte)
0x03  ram_used
0x04  ram_available
0x05  disk_read (device index)
0x06  disk_write (device index)
0x07  net_in (adapter index)
0x08  net_out (adapter index)
```

### Chunk Index Entry (16 bytes each)

```
Offset  Size  Type    Field
0       8     i64     Chunk start timestamp (Unix ms, absolute)
8       4     u32     Byte offset of chunk from file start
12      4     u32     Compressed chunk size in bytes
```

### Chunk Layout (variable size, LZ4-compressed)

Decompressed layout:

```
[Chunk Header: 16 bytes]
  - base_timestamp_ms (i64): timestamp of first reading
  - reading_count (u16):     number of readings in chunk
  - reserved (u16): 0x0000
  - uncompressed_size (u32): size before LZ4 compression

[Readings: reading_count * 10 bytes each]
  - timestamp_delta_ms (u16): ms since base_timestamp
  - value (f64):              the metric value
```

### WAL File Layout

WAL file (pulsedb.wal) in data directory:

```
[WAL Header]
  - magic (4 bytes): 0x57 0x41 0x4C 0x00 ("WAL\0")
  - entry_count (u32)

[WAL Entry]
  - target_file_path (256 bytes, null-padded)
  - chunk_data_size (u32)
  - chunk_data (variable): the exact bytes to append to target file
  - checksum (u32): xxHash32 of chunk_data
```

On startup, the engine:
1. Opens the WAL file if it exists
2. Verifies each entry's checksum
3. Appends valid entries to their target files
4. Deletes the WAL file

---

## 12. API Reference

Base URL: http://localhost:7700

All responses are Content-Type: application/json unless noted.

---

### GET /api/status

Returns daemon health.

Response:
```json
{
  "status": "ok",
  "uptime_seconds": 3600,
  "version": "1.0.0",
  "collection_active": true,
  "metrics_collected": 12500,
  "storage_used_bytes": 45234123
}
```

---

### GET /api/metrics

Returns a list of all available metric names.

Response:
```json
{
  "metrics": [
    "cpu_total_percent",
    "cpu_core_0_percent",
    "cpu_core_1_percent",
    "ram_used_bytes",
    "ram_available_bytes",
    "disk_0_read_bytes_per_sec",
    "disk_0_write_bytes_per_sec",
    "net_0_bytes_in_per_sec",
    "net_0_bytes_out_per_sec"
  ]
}
```

---

### GET /api/query

Query historical data for a metric.

Query parameters:
- metric (required): metric name
- from (required): Unix timestamp ms
- to (required): Unix timestamp ms
- resolution: raw | 1min | 1hr (default: raw)

Response:
```json
{
  "metric": "cpu_total_percent",
  "resolution": "raw",
  "from": 1700000000000,
  "to": 1700003600000,
  "count": 3600,
  "data": [
    { "ts": 1700000000000, "value": 12.5 },
    { "ts": 1700000001000, "value": 13.1 }
  ],
  "stats": {
    "min": 2.1,
    "max": 98.4,
    "mean": 18.7,
    "p95": 67.3
  }
}
```

---

### GET /api/latest

Returns the most recent snapshot from the in-memory ring buffer.
Response: full MetricSnapshot as JSON.

---

### GET /api/processes/latest

Returns the current full process list (not just top 25).

Response:
```json
{
  "timestamp": 1700000000000,
  "processes": [
    {
      "pid": 1234,
      "name": "chrome.exe",
      "cpu_percent": 8.4,
      "ram_bytes": 524288000,
      "disk_read_bytes_per_sec": 0,
      "disk_write_bytes_per_sec": 4096
    }
  ]
}
```

---

### GET /api/alerts/rules

Returns all alert rules.

### POST /api/alerts/rules

Creates a new alert rule. Body: alert rule JSON object (see Section 8).

### PUT /api/alerts/rules/:id

Updates an existing alert rule.

### DELETE /api/alerts/rules/:id

Deletes an alert rule.

### GET /api/alerts/history

Returns triggered alert history.
Query params: limit (default 100), offset (default 0), rule_id (optional filter).

### GET /api/config

Returns current configuration as JSON.

### PUT /api/config

Updates configuration. Daemon applies changes immediately where possible.

---

### WebSocket: ws://localhost:7700/ws/live

Connects to the live metric stream. Server pushes one message per second:

```json
{
  "type": "snapshot",
  "ts": 1700000000000,
  "cpu_total": 15.4,
  "cpu_cores": [12.1, 18.7, 9.4, 21.1],
  "ram_used_bytes": 8589934592,
  "ram_total_bytes": 17179869184,
  "disks": [],
  "network": [],
  "top_processes": []
}
```

Alert events are also pushed over the same WebSocket:
```json
{
  "type": "alert",
  "rule_id": 3,
  "rule_name": "High CPU",
  "metric": "cpu_total_percent",
  "value": 91.2,
  "triggered_at": 1700000000000
}
```

---

## 13. Configuration System

Config file: pulsedb.json in the data directory. Created with defaults on first run.

```json
{
  "collection_interval_ms": 1000,
  "api_port": 7700,
  "api_host": "127.0.0.1",
  "data_directory": "C:\\ProgramData\\PulseDB\\data",
  "log_directory": "C:\\ProgramData\\PulseDB\\logs",
  "log_level": "info",
  "retention": {
    "raw_days": 7,
    "summary_1min_days": 30,
    "summary_1hr_days": 365
  },
  "collector": {
    "enable_cpu": true,
    "enable_ram": true,
    "enable_disk": true,
    "enable_network": true,
    "enable_processes": true,
    "top_process_count": 25
  },
  "alerts": {
    "default_cooldown_seconds": 300,
    "evaluation_interval_seconds": 5
  },
  "downsampling": {
    "run_at_startup": true,
    "retention_check_hour": 3
  }
}
```

---

## 14. Testing Strategy

### Unit Tests (Google Test)

Modules to test:

Storage Engine:
- Write 60 readings, verify chunk is correctly formed
- Compress/decompress round-trip produces identical data
- Range query returns correct subset
- WAL replay after simulated crash
- Midnight rollover creates new file
- Retention deletion removes correct files

Downsampler:
- 60 raw readings produce correct min/max/mean/p95 in 1-min summary
- Edge case: fewer than 60 readings in window
- Edge case: readings spanning midnight boundary

Alert Engine:
- Threshold rule fires on correct reading
- Sustained threshold does not fire until duration met
- Cooldown prevents re-firing within cooldown window
- Rate-of-change rule fires on correct delta

API:
- All endpoints return correct HTTP status codes
- Malformed parameters return 400 with error body
- Query with no data returns empty array, not error

### Integration Tests

- Full pipeline: collector -> queue -> storage -> API -> query returns same data
- Alert triggered over full pipeline: metric spike -> alert engine -> history record written

### Benchmarks (Google Benchmark)

- Storage write throughput: target >50,000 readings/second
- Range query: target <100ms for 24-hour range at 1s resolution
- LZ4 compression ratio on real CPU data: target >4:1
- API endpoint latency under concurrent load: target <10ms p99 for /api/latest

### Manual Testing Checklist

- Daemon survives 7-day continuous run
- GUI renders correctly at 1080p and 1440p
- Midnight rollover does not drop readings
- Process list updates when a process starts/exits
- Alert notification appears in Windows Action Centre
- Config change via GUI persists after daemon restart
- Installer installs cleanly on a fresh Windows machine and uninstaller removes all files

---

## 15. Development Timeline

Total estimated duration: 16-18 weeks for a complete, polished v1.

---

### Phase 1 — Project Setup & Storage Engine (Weeks 1-4)

Week 1: Toolchain & Scaffolding
- Install and configure: CMake 3.25+, Conan 2.x, MSVC 2022 or Clang-CL
- Create CMakeLists.txt with submodule structure
- Set up Conan conanfile.txt with all C++ dependencies
- Create directory structure (see Section 16)
- Set up Google Test harness with a single passing dummy test
- Set up GitHub repository, .gitignore, README stub
- Configure VS 2022 solution or cmake --preset for Windows

Week 2: .pulse File Format & Writer
- Implement file header write/read
- Implement chunk index write/read
- Implement chunk writer: accumulate readings, compress with LZ4, append to file
- Implement delta timestamp encoding
- Write unit tests for all of the above
- Do not worry about WAL yet

Week 3: Storage Reader & Range Queries
- Implement chunk index reader (mmap via CreateFileMapping/MapViewOfFile)
- Implement range query: given metric + time range, return readings
- Implement multi-file range query (spanning multiple daily files)
- Write unit tests including edge cases (empty range, range spanning midnight)

Week 4: WAL & Downsampler
- Implement WAL write before chunk commit, clear after
- Implement WAL replay on startup
- Implement 1-minute downsampler: reads from .pulse, writes to SQLite
- Implement 1-hour downsampler: reads 1-min from SQLite, writes 1-hr rows
- Implement retention deletion
- Write unit tests and benchmark storage write throughput

Milestone: Working storage engine with tests. No UI, no collection — just verified read/write.

---

### Phase 2 — Collector Daemon (Weeks 5-7)

Week 5: CPU & RAM Collectors
- Implement PDH query wrapper class
- Implement CPU collector (total + per-core via PDH)
- Implement RAM collector (GlobalMemoryStatusEx)
- Write unit tests (mock PDH responses)
- Print readings to stdout to verify correctness

Week 6: Disk, Network & Process Collectors
- Implement disk collector (PDH PhysicalDisk counters)
- Implement network collector (GetIfTable2 diff)
- Implement process collector (EnumProcesses + PDH per-process)
- Wire all collectors into a scheduler using Boost.Asio steady_timer
- Assemble MetricSnapshot struct on each tick

Week 7: Queue, Ring Buffer & Storage Integration
- Implement lock-free SPSC queue (use folly or write simple array-based one)
- Wire scheduler -> queue -> storage writer thread
- Implement in-memory ring buffer (circular array, last 300 readings)
- Verify end-to-end: metrics collected -> stored -> readable via range query
- Run for 24 hours, verify no memory growth, no file corruption

Milestone: Daemon collects and stores real metrics. Readable from disk. Stable over 24h.

---

### Phase 3 — REST API & WebSocket (Weeks 8-9)

Week 8: REST API
- Integrate Drogon into CMake
- Implement all REST endpoints from Section 12
- Wire endpoints to storage engine read calls
- Implement config read/write endpoint
- Test all endpoints with curl and Postman

Week 9: WebSocket & Alert Engine
- Implement WebSocket handler in Drogon
- Wire ring buffer -> WebSocket broadcast (1s timer)
- Implement SQLite schema (Section 10)
- Implement alert rule CRUD (storage + API endpoints)
- Implement alert engine: rule evaluator + state machine
- Implement WinRT toast notification
- Test: create a rule, trigger it by stressing CPU, verify notification fires

Milestone: Full backend working. Can be queried from any HTTP client. Alerts fire correctly.

---

### Phase 4 — Desktop GUI (Weeks 10-14)

Week 10: Tauri Setup & App Shell
- Install Tauri CLI, scaffold Tauri 2 project with React + TypeScript template
- Configure Tauri to allow localhost HTTP and WebSocket connections
- Set up Tailwind CSS v3
- Create app shell: sidebar navigation, screen routing (React Router)
- Implement Settings screen (reads/writes config from API)
- Verify that Tauri app can call the C++ API successfully

Week 11: Live Dashboard
- Implement WebSocket client in React (auto-reconnect)
- Set up Zustand liveMetrics store
- Build CPU card: donut chart + sparkline (Recharts)
- Build RAM card: usage bar
- Build disk and network cards: throughput bars per device
- All data live from WebSocket

Week 12: Process Monitor
- Build process table component (sortable columns)
- Poll /api/processes/latest every 5 seconds
- Add search/filter input
- Add mini history chart on row click

Week 13: Historical Explorer
- Build metric selector dropdown
- Build date/time range picker with presets
- Build resolution selector
- Wire to /api/query endpoint
- Render results in Recharts LineChart with zoom/pan
- Build stats panel (min/max/mean/p95)
- Add CSV export button

Week 14: Alert Manager & System Tray
- Build alert rules list with enable/disable toggles
- Build "New Alert" form
- Wire to alert rule CRUD API endpoints
- Build alert history table
- Implement Tauri system tray icon and right-click menu
- Show alert toast in GUI when WebSocket alert event is received

Milestone: Full GUI. All screens working. App looks polished.

---

### Phase 5 — Polish, Testing & Packaging (Weeks 15-17)

Week 15: Testing
- Write all remaining unit tests (target: 80% coverage on C++ core)
- Run 7-day stability test
- Check for memory leaks (Dr. Memory or Visual Studio Diagnostic Tools)
- Profile collector CPU usage (must be <2% on target machine)
- Fix any layout issues in GUI at 1080p and 1440p

Week 16: Installer & Documentation
- Write NSIS installer script
  - Installs daemon as Windows Service (sc create)
  - Installs Tauri app to Program Files
  - Registers AUMID for toast notifications
  - Creates Start Menu shortcut
  - Uninstaller removes all files and the Windows service
- Write README.md: overview, installation, usage, API docs
- Write ARCHITECTURE.md: detailed component breakdown with diagrams
- Write CONTRIBUTING.md

Week 17: Final Polish
- Record a 3-minute demo video (screen capture of GUI running)
- Fix any bugs found during demo recording
- Tag v1.0.0 release on GitHub
- Upload installer .exe as GitHub release asset
- Write a brief technical write-up explaining design decisions (good for CV and interviews)

---

### Phase 6 — v2 Roadmap (Post-v1)

These are growth areas to implement after v1 is stable and tested:

- Multi-machine support: agents on remote machines report to a central PulseDB server over gRPC
- Custom collectors: TOML-defined collectors that read from arbitrary Windows PDH counter paths
- SQL-like query language: SELECT avg(cpu_total) LAST 1h GROUP BY 1min
- Anomaly detection: z-score based automatic flagging of unusual metric patterns
- Plugin SDK: C API for third-party collectors compiled as DLLs
- Docker container: server mode, browser-only UI (no Tauri)
- Prometheus exposition format: /metrics endpoint so Prometheus can scrape PulseDB
- Mobile companion app: React Native reading from your REST API

---

## 16. Directory Structure

```
pulsedb/
├── CMakeLists.txt
├── conanfile.txt
├── README.md
├── ARCHITECTURE.md
├── CONTRIBUTING.md
│
├── daemon/                         # C++ backend
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp
│   │   ├── config/
│   │   │   ├── Config.h
│   │   │   └── Config.cpp
│   │   ├── collector/
│   │   │   ├── IMetricCollector.h
│   │   │   ├── CollectorScheduler.h
│   │   │   ├── CollectorScheduler.cpp
│   │   │   ├── CpuCollector.h
│   │   │   ├── CpuCollector.cpp
│   │   │   ├── RamCollector.h
│   │   │   ├── RamCollector.cpp
│   │   │   ├── DiskCollector.h
│   │   │   ├── DiskCollector.cpp
│   │   │   ├── NetworkCollector.h
│   │   │   ├── NetworkCollector.cpp
│   │   │   ├── ProcessCollector.h
│   │   │   └── ProcessCollector.cpp
│   │   ├── storage/
│   │   │   ├── StorageEngine.h
│   │   │   ├── StorageEngine.cpp
│   │   │   ├── PulseFileWriter.h
│   │   │   ├── PulseFileWriter.cpp
│   │   │   ├── PulseFileReader.h
│   │   │   ├── PulseFileReader.cpp
│   │   │   ├── WalManager.h
│   │   │   ├── WalManager.cpp
│   │   │   ├── Downsampler.h
│   │   │   ├── Downsampler.cpp
│   │   │   └── RetentionManager.h
│   │   ├── queue/
│   │   │   ├── SpscQueue.h
│   │   │   └── RingBuffer.h
│   │   ├── api/
│   │   │   ├── ApiServer.h
│   │   │   ├── ApiServer.cpp
│   │   │   ├── controllers/
│   │   │   │   ├── MetricController.h
│   │   │   │   ├── MetricController.cpp
│   │   │   │   ├── AlertController.h
│   │   │   │   ├── AlertController.cpp
│   │   │   │   └── ConfigController.h
│   │   │   └── websocket/
│   │   │       ├── LiveFeedHandler.h
│   │   │       └── LiveFeedHandler.cpp
│   │   ├── alerts/
│   │   │   ├── AlertEngine.h
│   │   │   ├── AlertEngine.cpp
│   │   │   ├── AlertRule.h
│   │   │   └── NotificationManager.h
│   │   └── db/
│   │       ├── Database.h
│   │       └── Database.cpp
│   └── tests/
│       ├── storage/
│       │   ├── PulseFileWriterTest.cpp
│       │   ├── PulseFileReaderTest.cpp
│       │   └── WalManagerTest.cpp
│       ├── alerts/
│       │   └── AlertEngineTest.cpp
│       ├── collector/
│       │   └── CollectorTest.cpp
│       └── benchmarks/
│           ├── StorageBenchmark.cpp
│           └── QueryBenchmark.cpp
│
├── gui/                            # Tauri + React frontend
│   ├── src-tauri/
│   │   ├── Cargo.toml
│   │   ├── tauri.conf.json
│   │   └── src/
│   │       └── main.rs
│   ├── src/
│   │   ├── main.tsx
│   │   ├── App.tsx
│   │   ├── api/
│   │   │   ├── client.ts
│   │   │   └── websocket.ts
│   │   ├── store/
│   │   │   ├── liveMetrics.ts
│   │   │   ├── alerts.ts
│   │   │   └── settings.ts
│   │   ├── screens/
│   │   │   ├── Dashboard.tsx
│   │   │   ├── HistoricalExplorer.tsx
│   │   │   ├── ProcessMonitor.tsx
│   │   │   ├── AlertManager.tsx
│   │   │   └── Settings.tsx
│   │   ├── components/
│   │   │   ├── CpuCard.tsx
│   │   │   ├── RamCard.tsx
│   │   │   ├── DiskCard.tsx
│   │   │   ├── NetworkCard.tsx
│   │   │   ├── ProcessTable.tsx
│   │   │   ├── MetricChart.tsx
│   │   │   ├── AlertRuleForm.tsx
│   │   │   └── ConnectionBanner.tsx
│   │   └── types/
│   │       ├── metrics.ts
│   │       └── alerts.ts
│   ├── package.json
│   ├── tsconfig.json
│   └── tailwind.config.js
│
└── installer/
    └── pulsedb_installer.nsi
```

---

## 17. Third-Party Dependencies

### C++ (via Conan)

| Library              | Version  | Purpose                          |
|----------------------|----------|----------------------------------|
| boost                | 1.84.0   | Asio async runtime, timers       |
| drogon               | 1.9.3    | HTTP server and WebSocket        |
| lz4                  | 1.9.4    | Fast compression                 |
| flatbuffers          | 23.5.26  | Binary serialisation             |
| nlohmann_json        | 3.11.3   | JSON config and API responses    |
| sqlite3              | 3.44.2   | Relational storage               |
| sqlite_modern_cpp    | 3.2      | C++ wrapper for SQLite3          |
| xxhash               | 0.8.2    | Fast non-crypto hashing          |
| gtest                | 1.14.0   | Unit testing                     |
| benchmark            | 1.8.3    | Performance benchmarking         |

### Windows System Libraries (linked directly)

| Library        | Purpose                            |
|----------------|------------------------------------|
| pdh.lib        | Performance Data Helper API        |
| iphlpapi.lib   | Network interface enumeration      |
| psapi.lib      | Process memory info                |
| wbemuuid.lib   | WMI (process CPU, optional)        |
| windowsapp.lib | WinRT toast notifications          |

### Frontend (npm)

| Package          | Version  | Purpose                     |
|------------------|----------|-----------------------------|
| react            | 18.x     | UI framework                |
| react-router-dom | 6.x      | Screen routing              |
| recharts         | 2.x      | Chart components            |
| zustand          | 4.x      | State management            |
| tailwindcss      | 3.x      | Styling                     |
| date-fns         | 3.x      | Date/time formatting        |
| @tauri-apps/api  | 2.x      | Tauri JS bindings           |

---

## 18. Known Limitations & Future Work

### Limitations in v1

- Windows only. No Linux or macOS support until v2.
- Single machine only. No remote agent support.
- No authentication on the API. Do not expose port 7700 to a network.
- Process CPU attribution via PDH has ~1-2 second lag on fast-starting processes.
- WinRT toast notifications require Windows 10 1607 or later.
- The process collector requires the daemon to run as Administrator to read other users' processes. Non-admin runs will only see processes owned by the current user.
- Storage engine is single-writer only. The daemon takes a file lock on startup to prevent accidental dual-instance corruption.
- Virtual network adapters (VPN tunnels, WSL virtual switches) are filtered out by adapter type, not by user configuration.

### Known Design Trade-offs

- One .pulse file per metric per day means a machine with many cores and many adapters creates many files. On a 16-core machine with 3 NICs and 4 drives this is ~40 files per day. Fine for the filesystem but worth noting.
- Downsampling is lossy by design. Once raw data is past the retention window, only summary statistics remain. This is intentional.
- Using Tauri means the GUI requires a Rust toolchain to build. This is a build-time dependency only — the packaged installer does not require Rust on the end user's machine.

### Future Work (v2+)

Detailed in Phase 6 of the Development Timeline above.

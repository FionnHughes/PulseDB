# PulseDB

A self-hosted Windows system observability platform. Collects CPU, RAM, disk, network, and process metrics, stores them in a custom binary format, and exposes them via a local REST API and desktop GUI.

---

## Stack

| Layer | Technology |
|---|---|
| Core | C++20, CMake, Conan 2.x |
| Async | Boost.Asio |
| HTTP/WS | Drogon |
| Compression | LZ4 |
| Config/API responses | nlohmann/json |
| Relational storage | SQLite3 |
| Testing | Google Test + Benchmark |
| Desktop | Tauri 2.x + React 18 + Tailwind |

---

## Architecture

```
Tauri GUI (React + Recharts)
    ↕ HTTP REST + WebSocket (localhost:7700)
Drogon API Layer
    ↕
Collector (PDH/WMI/WinAPI) → SPSC Queue → Storage Engine (.pulse files)
                                         ↓
                                  Alert Engine → WinRT Toast
                                         ↓
                                  SQLite (alerts, config, summaries)
```

---

## Modules

**Collector** — 1s timer via Boost.Asio. Reads CPU (PDH), RAM (GlobalMemoryStatusEx), Disk (PDH PhysicalDisk), Network (GetIfTable2), Processes (EnumProcesses). Pushes `MetricSnapshot` onto a lock-free SPSC queue and an in-memory ring buffer (last 300 readings).

**Storage Engine** — Appends to `.pulse` binary files (one per metric per day). Chunks of 60 readings, LZ4-compressed, delta-encoded timestamps. Chunk index at file header for fast seeks. WAL for crash recovery. Downsamples to SQLite (1-min, 1-hr summaries) on a background timer.

**API** — All endpoints under `http://localhost:7700`. Key endpoints: `GET /api/query`, `GET /api/latest`, `GET /api/alerts/rules`, `WS /ws/live`. Fully async via Drogon coroutines.

**Alert Engine** — Evaluates rules every 5s from ring buffer. Three rule types: threshold, sustained threshold, rate-of-change. State machine: `INACTIVE → PENDING → ACTIVE → COOLDOWN`. Fires Windows toast notifications via WinRT.

**GUI** — Five screens: Live Dashboard, Historical Explorer, Process Monitor, Alert Manager, Settings. WebSocket client auto-reconnects. Zustand for state.

---

## .pulse File Format

```
[File Header — 64 bytes]
[Chunk Index — 16 bytes × chunk_count]
[Chunks — variable, LZ4-compressed]
```

Magic: `PULS` (0x50 0x55 0x4C 0x53) · Version: 1 · Little-endian.

Each chunk decompresses to a 16-byte `ChunkHeader` + `N × 10` bytes of readings (2-byte timestamp delta + 8-byte f64 value).

---

## Data Retention

| Resolution | Storage | Default retention |
|---|---|---|
| Raw (1s) | .pulse files | 7 days |
| 1-min summaries | SQLite | 30 days |
| 1-hr summaries | SQLite | 365 days |

Estimated disk usage: ~8–10 MB/day raw. 7 days ≈ 70 MB.

---

## Goals

- Collector CPU overhead < 2%
- 30 days of 1s data < 500 MB
- 24-hour range query < 100 ms
- Live metric latency < 500 ms end-to-end
- Stable for 30+ days without restart

import { useEffect, useMemo, useRef, useState } from "react";
import { LineChart, Line, XAxis, YAxis, Tooltip, Brush, CartesianGrid, ReferenceLine, ResponsiveContainer } from "recharts";
import { useLiveStore } from "../store/liveStore";
import { useBucketWorker } from "../hooks/useBucketWorker";
import type { QueryPoint, ComboPoint } from "../lib/bucketing";

type QueryResponse = {
  metric: string;
  resolution: string;
  from: number;
  to: number;
  count: number;
  data: QueryPoint[];
  stats: { min: number; max: number; mean: number; p95: number } | null;
};

const BASE = "http://localhost:7700";
const MAX_CHART_POINTS = 500;
const SLIDE_MS = 220;

const RANGE_PRESETS = [
  { label: "5m", ms: 5 * 60 * 1000 },
  { label: "15m", ms: 15 * 60 * 1000 },
  { label: "1h", ms: 60 * 60 * 1000 },
  { label: "6h", ms: 6 * 60 * 60 * 1000 },
  { label: "24h", ms: 24 * 60 * 60 * 1000 },
  { label: "7d", ms: 7 * 24 * 60 * 60 * 1000 },
];

const REAL_PATTERNS = ["Wi-Fi", "WiFi", "Ethernet", "Bluetooth"];
const EXCLUDE_PATTERNS = ["Filter Driver", "Kernel Debugger", "WFP", "QoS", "Loopback"];
function isRealAdapter(name: string) {
  const looksReal = REAL_PATTERNS.some((p) => name.includes(p));
  const looksVirtual = EXCLUDE_PATTERNS.some((p) => name.includes(p));
  return looksReal && !looksVirtual;
}

function formatTime(ts: number) {
  return new Date(ts).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}

function getUnitInfo(metricOrLabel: string): { divisor: number; unit: string; decimals: number; tickDecimals: number } {
  const s = metricOrLabel;
  const isRam = s.includes("ram") || s.includes("RAM");
  const isDisk = s.includes("disk") || s.includes("Disk");
  const isNet = s.includes("net") || s.includes("Adapter") || s.includes("Wi-Fi") || s.includes("Ethernet") || s.includes("Bluetooth");
  const isCpu = s.includes("cpu") || s.includes("CPU");

  if (isCpu) return { divisor: 1, unit: "%", decimals: 1, tickDecimals: 0 };
  if (isRam) return { divisor: 1024 * 1024 * 1024, unit: "GB", decimals: 2, tickDecimals: 0 };
  if (isDisk) return { divisor: 1024 * 1024, unit: "MB/s", decimals: 2, tickDecimals: 1 };
  if (isNet) return { divisor: 125000, unit: "Mbps", decimals: 2, tickDecimals: 1 };
  return { divisor: 1, unit: "", decimals: 1, tickDecimals: 1 };
}

function formatForMetric(rawValue: number, metricOrLabel: string): { text: string; unit: string } {
  const { divisor, unit, decimals } = getUnitInfo(metricOrLabel);
  return { text: (rawValue / divisor).toFixed(decimals), unit };
}

function computeTicks(domainMin: number, domainMax: number, divisor: number, baseDecimals: number, steps = 6) {
  let decimals = baseDecimals;
  for (let attempt = 0; attempt < 4; attempt++) {
    const step = (domainMax - domainMin) / (steps - 1);
    const rawTicks = Array.from({ length: steps }, (_, i) => domainMin + step * i);
    const labels = rawTicks.map((v) => (v / divisor).toFixed(decimals));
    if (new Set(labels).size === labels.length) return { rawTicks, decimals };
    decimals++;
  }
  const step = (domainMax - domainMin) / (steps - 1);
  return { rawTicks: Array.from({ length: steps }, (_, i) => domainMin + step * i), decimals };
}

type Selection =
  | { kind: "single"; metric: string; label: string }
  | { kind: "disk-combo"; label: string; readMetric: string; writeMetric: string };

type PickerItem = { label: string; selection: Selection; isAggregate?: boolean };
type MetricGroup = { name: string; items: PickerItem[] };

function buildGroups(metrics: string[], liveDiskNames: string[], liveNetNames: string[]): MetricGroup[] {
  const cpuCores: PickerItem[] = [];
  const ramItems: PickerItem[] = [];
  const netReal: PickerItem[] = [];
  const netOther: PickerItem[] = [];
  let cpuTotal: PickerItem | null = null;
  const diskIndices = new Set<number>();

  for (const m of metrics) {
    if (m === "cpu_total") {
      cpuTotal = { label: "CPU Total", selection: { kind: "single", metric: m, label: "CPU Total" }, isAggregate: true };
      continue;
    }
    if (m === "ram_used") {
      ramItems.push({ label: "RAM Used", selection: { kind: "single", metric: m, label: "RAM Used" } });
      continue;
    }
    if (m === "ram_available") {
      ramItems.push({ label: "RAM Available", selection: { kind: "single", metric: m, label: "RAM Available" } });
      continue;
    }
    const coreMatch = m.match(/^cpu_core_(\d+)$/);
    if (coreMatch) {
      cpuCores.push({ label: `CPU Core ${coreMatch[1]}`, selection: { kind: "single", metric: m, label: `CPU Core ${coreMatch[1]}` } });
      continue;
    }
    const diskMatch = m.match(/^disk_(\d+)_(read|write)$/);
    if (diskMatch) {
      diskIndices.add(parseInt(diskMatch[1], 10));
      continue;
    }
    const netMatch = m.match(/^net_(\d+)_(in|out)$/);
    if (netMatch) {
      const idx = parseInt(netMatch[1], 10);
      const dir = netMatch[2] === "in" ? "In" : "Out";
      const realName = liveNetNames[idx];
      const item: PickerItem = realName
        ? { label: `${realName} ${dir}`, selection: { kind: "single", metric: m, label: `${realName} ${dir}` } }
        : { label: `Adapter ${idx} ${dir}`, selection: { kind: "single", metric: m, label: `Adapter ${idx} ${dir}` } };
      if (realName && isRealAdapter(realName)) netReal.push(item);
      else netOther.push(item);
      continue;
    }
  }

  const numFromLabel = (item: PickerItem) => parseInt(item.label.match(/\d+/)?.[0] ?? "0", 10);
  cpuCores.sort((a, b) => numFromLabel(a) - numFromLabel(b));
  netReal.sort((a, b) => numFromLabel(a) - numFromLabel(b));
  netOther.sort((a, b) => numFromLabel(a) - numFromLabel(b));

  const diskItems: PickerItem[] = [];
  const sortedDiskIndices = Array.from(diskIndices).sort((a, b) => a - b);
  for (const n of sortedDiskIndices) {
    const name = liveDiskNames[n];
    const tag = name ? `Disk ${n} (${name})` : `Disk ${n}`;
    diskItems.push({
      label: `${tag} Combined`,
      isAggregate: true,
      selection: { kind: "disk-combo", label: `${tag} Combined`, readMetric: `disk_${n}_read`, writeMetric: `disk_${n}_write` },
    });
    diskItems.push({ label: `${tag} Read`, selection: { kind: "single", metric: `disk_${n}_read`, label: `${tag} Read` } });
    diskItems.push({ label: `${tag} Write`, selection: { kind: "single", metric: `disk_${n}_write`, label: `${tag} Write` } });
  }

  const groups: MetricGroup[] = [];
  if (cpuTotal || cpuCores.length) groups.push({ name: "CPU", items: [...(cpuTotal ? [cpuTotal] : []), ...cpuCores] });
  if (ramItems.length) groups.push({ name: "RAM", items: ramItems });
  if (diskItems.length) groups.push({ name: "Disk", items: diskItems });
  if (netReal.length) groups.push({ name: "Network", items: netReal });
  if (netOther.length) groups.push({ name: "Network (other)", items: netOther });

  return groups;
}

function MetricPicker({ groups, value, onChange }: { groups: MetricGroup[]; value: Selection | null; onChange: (s: Selection) => void }) {
  const [open, setOpen] = useState(false);
  const [expanded, setExpanded] = useState<Set<string>>(new Set(["CPU"]));

  function toggleGroup(name: string) {
    setExpanded((prev) => {
      const next = new Set(prev);
      if (next.has(name)) next.delete(name);
      else next.add(name);
      return next;
    });
  }

  function isSelected(item: PickerItem) {
    if (!value) return false;
    if (item.selection.kind === "single" && value.kind === "single") return item.selection.metric === value.metric;
    if (item.selection.kind === "disk-combo" && value.kind === "disk-combo") return item.selection.readMetric === value.readMetric;
    return false;
  }

  return (
    <div className="relative">
      <button
        onClick={() => setOpen((o) => !o)}
        className="bg-panel border border-line px-2 py-1 text-sm w-52 text-left flex justify-between items-center"
      >
        <span className="truncate">{value?.label ?? "select metric"}</span>
        <span className="text-muted">{open ? "▴" : "▾"}</span>
      </button>

      {open && (
        <div className="absolute z-20 mt-1 w-60 border border-line bg-panel max-h-96 overflow-auto">
          {groups.map((g) => (
            <div key={g.name}>
              <button
                onClick={() => toggleGroup(g.name)}
                className="w-full text-left px-2 py-1.5 text-xs text-muted hover:text-text border-b border-line/50 flex justify-between"
              >
                <span>{g.name}</span>
                <span>{expanded.has(g.name) ? "▴" : "▾"}</span>
              </button>
              {expanded.has(g.name) &&
                g.items.map((item) => (
                  <button
                    key={item.label}
                    onClick={() => { onChange(item.selection); setOpen(false); }}
                    className={`w-full text-left pl-4 pr-2 py-1 text-xs border-b border-line/30 ${
                      isSelected(item) ? "text-cpu" : item.isAggregate ? "text-text font-bold" : "text-muted hover:text-text"
                    }`}
                  >
                    {item.label}
                  </button>
                ))}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

function ChartTooltip({ active, payload, label, metricKey }: any) {
  if (!active || !payload?.length) return null;
  return (
    <div className="bg-panel border border-line px-2 py-1.5 text-xs">
      <div className="text-muted mb-1">{new Date(label).toLocaleString()}</div>
      {payload.map((p: any) => {
        const { text, unit } = formatForMetric(p.value, p.name === "value" ? metricKey : p.name);
        return (
          <div key={p.dataKey} style={{ color: p.color }}>
            {p.name}: {text} {unit}
          </div>
        );
      })}
    </div>
  );
}

function toLocalInputValue(ts: number) {
  const d = new Date(ts);
  const pad = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

type InspectRow = { text: string; unit: string; loading: boolean };
type AnimPhase = "out-left" | "out-right" | "in-left" | "in-right" | null;

export default function HistoricalExplorer() {
  const liveSnap = useLiveStore((s) => s.latest);
  const { bucketSingle, bucketPair } = useBucketWorker();

  const [metrics, setMetrics] = useState<string[]>([]);
  const [selection, setSelection] = useState<Selection | null>(null);

  const [rangeMode, setRangeMode] = useState<"preset" | "custom">("preset");
  const [presetMs, setPresetMs] = useState(RANGE_PRESETS[2].ms);
  const [customFrom, setCustomFrom] = useState(toLocalInputValue(Date.now() - 60 * 60 * 1000));
  const [customTo, setCustomTo] = useState(toLocalInputValue(Date.now()));

  const [yMin, setYMin] = useState("");
  const [yMax, setYMax] = useState("");

  const [chartData, setChartData] = useState<QueryPoint[]>([]);
  const [comboData, setComboData] = useState<ComboPoint[]>([]);
  const [stats, setStats] = useState<QueryResponse["stats"]>(null);
  const [rawCount, setRawCount] = useState(0);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);

  const [selectedTs, setSelectedTs] = useState<number | null>(null);
  const [inspectData, setInspectData] = useState<Record<string, InspectRow>>({});
  const [animPhase, setAnimPhase] = useState<AnimPhase>(null);
  const chartWrapRef = useRef<HTMLDivElement>(null);
  const inspectAbortRef = useRef<AbortController | null>(null);
  const queryIdRef = useRef(0);

  const liveDiskNames = (liveSnap?.disks ?? []).map((d) => d.name);
  const liveNetNames = (liveSnap?.network ?? []).map((n) => n.name);
  const coreCount = liveSnap?.cpu_cores?.length ?? 0;

  useEffect(() => {
    fetch(`${BASE}/api/metrics`)
      .then((r) => r.json())
      .then((d) => setMetrics(d.metrics ?? []))
      .catch(() => setError("daemon unreachable"));
  }, []);

  useEffect(() => {
    if (!selection && metrics.includes("cpu_total")) {
      setSelection({ kind: "single", metric: "cpu_total", label: "CPU Total" });
    }
  }, [metrics, selection]);

  function activeRange(): { from: number; to: number } {
    if (rangeMode === "custom") {
      return { from: new Date(customFrom).getTime(), to: new Date(customTo).getTime() };
    }
    const to = Date.now();
    return { from: to - presetMs, to };
  }

  async function fetchMetric(metric: string, from: number, to: number, signal?: AbortSignal): Promise<QueryResponse> {
    const res = await fetch(`${BASE}/api/query?metric=${encodeURIComponent(metric)}&from=${from}&to=${to}`, { signal });
    if (!res.ok) throw new Error(`status ${res.status}`);
    return res.json();
  }

  async function runQueryForRange(from: number, to: number) {
    if (!selection) return;
    if (!(from < to)) { setError("from must be before to"); return; }

    const myId = ++queryIdRef.current;
    setLoading(true);
    try {
      if (selection.kind === "single") {
        const res = await fetchMetric(selection.metric, from, to);
        const bucketed = await bucketSingle(res.data, from, to, MAX_CHART_POINTS);
        if (myId !== queryIdRef.current) return;
        setChartData(bucketed);
        setStats(res.stats);
        setRawCount(res.count);
        setComboData([]);
      } else {
        const [readRes, writeRes] = await Promise.all([
          fetchMetric(selection.readMetric, from, to),
          fetchMetric(selection.writeMetric, from, to),
        ]);
        const bucketed = await bucketPair(readRes.data, writeRes.data, from, to, MAX_CHART_POINTS);
        if (myId !== queryIdRef.current) return;
        setComboData(bucketed);
        setStats(null);
        setRawCount(readRes.count + writeRes.count);
        setChartData([]);
      }
      setError(null);
    } catch {
      if (myId === queryIdRef.current) setError("query failed");
    } finally {
      if (myId === queryIdRef.current) setLoading(false);
    }
  }

  function runQuery() {
    const { from, to } = activeRange();
    return runQueryForRange(from, to);
  }

  useEffect(() => {
    if (selection) runQuery();
    setSelectedTs(null);
    setInspectData({});
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selection, presetMs, rangeMode]);

  const activePoints: (QueryPoint | ComboPoint)[] = selection?.kind === "disk-combo" ? comboData : chartData;

  // sequenced: slide fully out (pure visual, data untouched) -> swap data while offscreen -> slide back in from the other side. boundaryTs stays selected in the new view.
  function zoomWithSlide(from: number, to: number, direction: "left" | "right", boundaryTs: number) {
    setAnimPhase(direction === "left" ? "out-left" : "out-right");
    setTimeout(async () => {
      await runQueryForRange(from, to);
      setSelectedTs(boundaryTs);
      setAnimPhase(direction === "left" ? "in-left" : "in-right");
      setTimeout(() => setAnimPhase(null), SLIDE_MS);
    }, SLIDE_MS);
    setRangeMode("custom");
    setCustomFrom(toLocalInputValue(from));
    setCustomTo(toLocalInputValue(to));
  }

  function handleChartClick(e: any) {
    if (!e || e.activeLabel === undefined) return;
    setSelectedTs(Number(e.activeLabel));
  }

  function stepPoint(delta: number) {
    if (selectedTs === null || activePoints.length === 0) return;
    const currentIdx = activePoints.findIndex((p) => p.ts === selectedTs);
    const baseIdx = currentIdx >= 0 ? currentIdx : 0;
    const nextIdx = baseIdx + delta;
    if (nextIdx < 0 || nextIdx >= activePoints.length) return;
    setSelectedTs(activePoints[nextIdx].ts);
  }

  useEffect(() => {
    if (selectedTs === null) { setInspectData({}); return; }
    inspectMoment(selectedTs);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selectedTs]);

  async function inspectMoment(ts: number) {
    inspectAbortRef.current?.abort();
    const controller = new AbortController();
    inspectAbortRef.current = controller;

    const window = 15000;
    const quick: { key: string; label: string }[] = [
      { key: "cpu_total", label: "CPU Total" },
      { key: "ram_used", label: "RAM Used" },
      { key: "ram_available", label: "RAM Available" },
    ];
    for (let i = 0; i < coreCount; i++) quick.push({ key: `cpu_core_${i}`, label: `Core ${i}` });
    liveDiskNames.forEach((name, i) => {
      quick.push({ key: `disk_${i}_read`, label: `${name} Read` });
      quick.push({ key: `disk_${i}_write`, label: `${name} Write` });
    });
    liveNetNames.forEach((name, i) => {
      if (!isRealAdapter(name)) return;
      quick.push({ key: `net_${i}_in`, label: `${name} In` });
      quick.push({ key: `net_${i}_out`, label: `${name} Out` });
    });

    const seeded: Record<string, InspectRow> = {};
    for (const m of quick) seeded[m.label] = { text: "…", unit: "", loading: true };
    setInspectData(seeded);

    const pending: Record<string, InspectRow> = {};
    let flushScheduled = false;
    function scheduleFlush() {
      if (flushScheduled) return;
      flushScheduled = true;
      requestAnimationFrame(() => {
        setInspectData((prev) => ({ ...prev, ...pending }));
        flushScheduled = false;
      });
    }

    await Promise.all(
      quick.map(async (m) => {
        try {
          const res = await fetchMetric(m.key, ts - window, ts + window, controller.signal);
          if (controller.signal.aborted) return;
          if (res.data.length) {
            const closest = res.data.reduce((a, b) => (Math.abs(a.ts - ts) < Math.abs(b.ts - ts) ? a : b));
            const { text, unit } = formatForMetric(closest.value, m.key);
            pending[m.label] = { text, unit, loading: false };
          } else {
            pending[m.label] = { text: "no data", unit: "", loading: false };
          }
        } catch {
          if (!controller.signal.aborted) pending[m.label] = { text: "err", unit: "", loading: false };
        }
        scheduleFlush();
      })
    );
  }

  function downloadCsv(rows: string[], name: string) {
    const blob = new Blob([rows.join("\n")], { type: "text/csv" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `${name}.csv`;
    a.click();
    URL.revokeObjectURL(url);
  }

  function exportCsv() {
    if (selection?.kind === "single" && chartData.length) {
      const rows = ["timestamp,value", ...chartData.map((p) => `${p.ts},${p.value}`)];
      downloadCsv(rows, selection.metric);
    } else if (selection?.kind === "disk-combo" && comboData.length) {
      const rows = ["timestamp,read,write", ...comboData.map((p) => `${p.ts},${p.read ?? ""},${p.write ?? ""}`)];
      downloadCsv(rows, "disk_combined");
    }
  }

  function jumpToNow() {
    if (rangeMode === "preset") {
      runQuery();
    } else {
      setCustomTo(toLocalInputValue(Date.now()));
      runQueryForRange(new Date(customFrom).getTime(), Date.now());
    }
  }

  const groups = buildGroups(metrics, liveDiskNames, liveNetNames);
  const rawLabel = selection?.kind === "single" ? selection.metric : selection ? `${selection.readMetric}, ${selection.writeMetric}` : "";
  const unitInfo = getUnitInfo(rawLabel);

  const seriesValues =
    selection?.kind === "disk-combo"
      ? comboData.flatMap((p) => [p.read, p.write]).filter((v): v is number => v !== undefined)
      : chartData.map((p) => p.value);
  const dataMin = seriesValues.length ? Math.min(...seriesValues) : 0;
  const dataMax = seriesValues.length ? Math.max(...seriesValues) : 1;
  const domainMin = yMin !== "" ? Number(yMin) * unitInfo.divisor : dataMin;
  const domainMax = yMax !== "" ? Number(yMax) * unitInfo.divisor : dataMax;
  const { rawTicks, decimals: tickDecimals } = useMemo(
    () => computeTicks(domainMin, domainMax, unitInfo.divisor, unitInfo.tickDecimals),
    [domainMin, domainMax, unitInfo.divisor, unitInfo.tickDecimals]
  );
  const yAxisTick = (raw: number) => {
    const val = (raw / unitInfo.divisor).toFixed(tickDecimals);
    return unitInfo.unit ? `${val} ${unitInfo.unit}` : val;
  };

  // out-left/out-right push the chart offscreen and fade it; in-left/in-right start from the opposite offscreen side and animate back to rest, css transition drives the motion
  const chartAnimClass =
    animPhase === "out-left" ? "-translate-x-10 opacity-0" :
    animPhase === "out-right" ? "translate-x-10 opacity-0" :
    animPhase === "in-left" ? "animate-slide-in-from-left" :
    animPhase === "in-right" ? "animate-slide-in-from-right" :
    "translate-x-0 opacity-100";

  return (
    <div className="bg-bg min-h-full p-4 font-mono-ui text-text text-sm">
      <div className="flex items-center gap-2 mb-2 flex-wrap">
        <MetricPicker groups={groups} value={selection} onChange={setSelection} />

        <div className="flex border border-line">
          {RANGE_PRESETS.map((p) => (
            <button
              key={p.label}
              onClick={() => { setRangeMode("preset"); setPresetMs(p.ms); }}
              className={`px-3 py-1 text-xs ${
                rangeMode === "preset" && presetMs === p.ms ? "bg-panel text-cpu" : "text-muted hover:text-text"
              }`}
            >
              {p.label}
            </button>
          ))}
          <button
            onClick={() => setRangeMode("custom")}
            className={`px-3 py-1 text-xs border-l border-line ${rangeMode === "custom" ? "bg-panel text-cpu" : "text-muted hover:text-text"}`}
          >
            custom
          </button>
        </div>

        <button onClick={jumpToNow} title="refresh to the latest data" className="px-3 py-1 border border-line text-muted hover:text-cpu text-xs">
          ↻ refresh
        </button>

        <button
          onClick={exportCsv}
          disabled={!chartData.length && !comboData.length}
          className="px-3 py-1 border border-line text-muted hover:text-text text-xs disabled:opacity-40"
        >
          export csv
        </button>

        <span className="text-muted text-xs ml-auto">
          {error ?? (loading ? "loading..." : rawCount ? `${rawCount} raw points` : "")}
        </span>
      </div>

      {rangeMode === "custom" && (
        <div className="flex items-center gap-2 mb-3 text-xs">
          <span className="text-muted">from</span>
          <input type="datetime-local" value={customFrom} onChange={(e) => setCustomFrom(e.target.value)} className="bg-panel border border-line px-2 py-1" />
          <span className="text-muted">to</span>
          <input type="datetime-local" value={customTo} onChange={(e) => setCustomTo(e.target.value)} className="bg-panel border border-line px-2 py-1" />
          <button onClick={() => setCustomTo(toLocalInputValue(Date.now()))} title="set end to right now" className="px-2 py-1 border border-line text-muted hover:text-cpu">
            now
          </button>
          <button onClick={runQuery} className="px-3 py-1 border border-cpu text-cpu">go</button>
        </div>
      )}

      <div className="flex items-center gap-2 mb-3 text-xs">
        <span className="text-muted">y-axis range ({unitInfo.unit || "raw"})</span>
        <input type="number" placeholder="min (auto)" value={yMin} onChange={(e) => setYMin(e.target.value)} className="bg-panel border border-line px-2 py-1 w-24" />
        <input type="number" placeholder="max (auto)" value={yMax} onChange={(e) => setYMax(e.target.value)} className="bg-panel border border-line px-2 py-1 w-24" />
      </div>

      <div className="text-muted text-xs mb-3">{selection?.label ?? rawLabel}</div>

      {stats && (
        <div className="flex gap-4 mb-3 text-xs text-muted">
          <span title="lowest value in this range">min <span className="text-text">{formatForMetric(stats.min, rawLabel).text} {formatForMetric(stats.min, rawLabel).unit}</span></span>
          <span title="highest value in this range">max <span className="text-text">{formatForMetric(stats.max, rawLabel).text} {formatForMetric(stats.max, rawLabel).unit}</span></span>
          <span title="average across this range">mean <span className="text-text">{formatForMetric(stats.mean, rawLabel).text} {formatForMetric(stats.mean, rawLabel).unit}</span></span>
          <span title="95th percentile — value this metric was below 95% of the time">p95 <span className="text-text">{formatForMetric(stats.p95, rawLabel).text} {formatForMetric(stats.p95, rawLabel).unit}</span></span>
        </div>
      )}

      <div className="border border-line bg-panel p-3 relative overflow-hidden" style={{ height: 440 }} ref={chartWrapRef}>
        {selectedTs !== null && (
          <div className="absolute z-10 top-1 left-1/2 -translate-x-1/2 border border-cpu bg-panel px-2 py-1 text-xs flex items-center gap-2">
            <button onClick={() => zoomWithSlide(activeRange().from, selectedTs, "left", selectedTs)} title="zoom to everything before this point" className="hover:text-cpu">◀</button>
            <button onClick={() => stepPoint(-1)} title="previous point" className="hover:text-cpu">‹</button>
            <span className="text-muted">{new Date(selectedTs).toLocaleTimeString()}</span>
            <button onClick={() => stepPoint(1)} title="next point" className="hover:text-cpu">›</button>
            <button onClick={() => zoomWithSlide(selectedTs, activeRange().to, "right", selectedTs)} title="zoom to everything after this point" className="hover:text-cpu">▶</button>
            <button onClick={() => setSelectedTs(null)} title="clear selection" className="text-muted hover:text-text ml-1">×</button>
          </div>
        )}

        <div className={`h-full transition-transform transition-opacity duration-200 ease-in-out ${chartAnimClass}`}>
          {selection?.kind === "disk-combo" && comboData.length ? (
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={comboData} onClick={handleChartClick}>
                <CartesianGrid stroke="#1E2430" strokeOpacity={0.5} vertical={false} />
                <XAxis dataKey="ts" tickFormatter={formatTime} stroke="#5C6773" tick={{ fontSize: 11, fontFamily: "ui-monospace" }} />
                <YAxis stroke="#5C6773" tick={{ fontSize: 11, fontFamily: "ui-monospace" }} tickFormatter={yAxisTick} ticks={rawTicks} domain={[domainMin, domainMax]} />
                <Tooltip content={<ChartTooltip metricKey={rawLabel} />} />
                {selectedTs !== null && <ReferenceLine x={selectedTs} stroke="#5FD3BC" strokeDasharray="3 3" />}
                <Line type="monotone" dataKey="read" name="read" stroke="#5FD3BC" dot={false} strokeWidth={1.5} connectNulls isAnimationActive={false} />
                <Line type="monotone" dataKey="write" name="write" stroke="#E8B85C" dot={false} strokeWidth={1.5} connectNulls isAnimationActive={false} />
                <Brush dataKey="ts" tickFormatter={formatTime} stroke="#5FD3BC" fill="#10141C" height={24} />
              </LineChart>
            </ResponsiveContainer>
          ) : chartData.length ? (
            <ResponsiveContainer width="100%" height="100%">
              <LineChart data={chartData} onClick={handleChartClick}>
                <CartesianGrid stroke="#1E2430" strokeOpacity={0.5} vertical={false} />
                <XAxis dataKey="ts" tickFormatter={formatTime} stroke="#5C6773" tick={{ fontSize: 11, fontFamily: "ui-monospace" }} />
                <YAxis stroke="#5C6773" tick={{ fontSize: 11, fontFamily: "ui-monospace" }} tickFormatter={yAxisTick} ticks={rawTicks} domain={[domainMin, domainMax]} />
                <Tooltip content={<ChartTooltip metricKey={rawLabel} />} />
                {selectedTs !== null && <ReferenceLine x={selectedTs} stroke="#5FD3BC" strokeDasharray="3 3" />}
                <Line type="monotone" dataKey="value" stroke="#5FD3BC" dot={false} strokeWidth={1.5} isAnimationActive={false} />
                <Brush dataKey="ts" tickFormatter={formatTime} stroke="#5FD3BC" fill="#10141C" height={24} />
              </LineChart>
            </ResponsiveContainer>
          ) : (
            <div className="flex items-center justify-center h-full text-muted text-xs">no data for this range</div>
          )}
        </div>
      </div>

      {selectedTs !== null && (
        <div className="border border-line bg-panel p-3 mt-2">
          <div className="text-muted text-xs mb-2">state at {new Date(selectedTs).toLocaleString()}</div>
          <div className="grid grid-cols-3 gap-x-4 gap-y-1 text-xs">
            {Object.entries(inspectData).map(([label, v]) => (
              <div key={label} className="flex justify-between">
                <span className="text-muted">{label}</span>
                <span className={v.loading ? "text-muted" : ""}>{v.text} {v.unit}</span>
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}
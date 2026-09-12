import { useEffect, useState } from "react";

type Rule = {
  id: number;
  name: string;
  metric: string;
  rule_type: "threshold" | "sustained_threshold" | "rate_of_change";
  operator: "gt" | "lt" | "gte" | "lte";
  value: number;
  duration_readings: number;
  change_percent: number;
  window_readings: number;
  cooldown_seconds: number;
  enabled: boolean;
};

type HistoryEntry = {
  id: number;
  rule_id: number;
  triggered_at: number;
  resolved_at: number;
  peak_value: number;
  duration_seconds: number;
  note: string;
};

type ActiveState = {
  rule_id: number;
  name: string;
  state: "pending" | "active";
  peak_value: number;
  state_entered_at: number;
};

const BASE = "http://localhost:7700";
const OP_SYMBOLS: Record<Rule["operator"], string> = { gt: ">", lt: "<", gte: "≥", lte: "≤" };

function humanizeAlertMetric(metric: string): string {
  if (metric === "cpu_total_percent") return "CPU Total";
  if (metric === "ram_used_bytes") return "RAM Used";
  if (metric === "ram_available_bytes") return "RAM Available";
  if (metric === "swap_used_bytes") return "Swap Used";
  const core = metric.match(/^cpu_core_(\d+)$/);
  if (core) return `CPU Core ${core[1]}`;
  const disk = metric.match(/^disk_(\d+)_(read|write)$/);
  if (disk) return `Disk ${disk[1]} ${disk[2] === "read" ? "Read" : "Write"}`;
  const net = metric.match(/^net_(\d+)_(in|out)$/);
  if (net) return `Adapter ${net[1]} ${net[2] === "in" ? "In" : "Out"}`;
  return metric;
}

function unitForMetric(metric: string): "percent" | "gb" | "mb-per-s" | "mbps" | "raw" {
  if (metric.includes("percent")) return "percent";
  if (metric.includes("bytes")) return "gb";
  if (metric.startsWith("disk_")) return "mb-per-s";
  if (metric.startsWith("net_")) return "mbps";
  return "raw";
}

function toRawValue(metric: string, displayValue: number): number {
  const unit = unitForMetric(metric);
  if (unit === "gb") return displayValue * 1024 * 1024 * 1024;
  if (unit === "mb-per-s") return displayValue * 1024 * 1024;
  if (unit === "mbps") return (displayValue * 1_000_000) / 8;
  return displayValue;
}

function toDisplayValue(metric: string, rawValue: number): number {
  const unit = unitForMetric(metric);
  if (unit === "gb") return rawValue / 1024 / 1024 / 1024;
  if (unit === "mb-per-s") return rawValue / 1024 / 1024;
  if (unit === "mbps") return (rawValue * 8) / 1_000_000;
  return rawValue;
}

function unitLabel(metric: string): string {
  const unit = unitForMetric(metric);
  if (unit === "percent") return "%";
  if (unit === "gb") return "GB";
  if (unit === "mb-per-s") return "MB/s";
  if (unit === "mbps") return "Mbps";
  return "";
}

function humanizeValue(metric: string, value: number): string {
  if (metric.includes("percent")) return `${value.toFixed(0)}%`;
  if (metric.includes("bytes")) return `${(value / 1024 / 1024 / 1024).toFixed(1)} GB`;
  if (metric.startsWith("disk_")) return `${(value / 1024 / 1024).toFixed(1)} MB/s`;
  if (metric.startsWith("net_")) return `${((value * 8) / 1_000_000).toFixed(1)} Mbps`;
  return String(value);
}

function formatPeak(metric: string, value: number): string {
  if (metric.includes("percent")) return `${value.toFixed(1)}%`;
  if (metric.includes("bytes")) return `${(value / 1024 / 1024 / 1024).toFixed(2)} GB`;
  if (metric.startsWith("disk_")) return `${(value / 1024 / 1024).toFixed(1)} MB/s`;
  if (metric.startsWith("net_")) return `${((value * 8) / 1_000_000).toFixed(1)} Mbps`;
  return value.toFixed(1);
}

function ruleDescription(r: Rule): string {
  const metric = humanizeAlertMetric(r.metric);
  const op = OP_SYMBOLS[r.operator];
  const val = humanizeValue(r.metric, r.value);
  if (r.rule_type === "sustained_threshold") {
    return `${metric} ${op} ${val} for ${r.duration_readings * 5}s straight`;
  }
  if (r.rule_type === "rate_of_change") {
    return `${metric} changes by ${r.change_percent}% over ${r.window_readings} readings`;
  }
  return `${metric} ${op} ${val}`;
}

const emptyRule: Omit<Rule, "id"> = {
  name: "",
  metric: "cpu_total_percent",
  rule_type: "threshold",
  operator: "gt",
  value: 90,
  duration_readings: 6,
  change_percent: 0,
  window_readings: 0,
  cooldown_seconds: 300,
  enabled: true,
};

export default function AlertManager() {
  const [rules, setRules] = useState<Rule[]>([]);
  const [history, setHistory] = useState<HistoryEntry[]>([]);
  const [active, setActive] = useState<ActiveState[]>([]);
  const [showForm, setShowForm] = useState(false);
  const [editingId, setEditingId] = useState<number | null>(null);
  const [form, setForm] = useState(emptyRule);
  const [displayValue, setDisplayValue] = useState(90);
  const [justToggledId, setJustToggledId] = useState<number | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [historySearch, setHistorySearch] = useState("");

  async function loadAll() {
    try {
      const [rulesRes, historyRes, activeRes] = await Promise.all([
        fetch(`${BASE}/api/alerts/rules`),
        fetch(`${BASE}/api/alerts/history`),
        fetch(`${BASE}/api/alerts/active`),
      ]);
      setRules(await rulesRes.json());
      setHistory(await historyRes.json());
      setActive(await activeRes.json());
      setError(null);
    } catch {
      setError("daemon unreachable");
    }
  }

  useEffect(() => {
    loadAll();
    const interval = setInterval(loadAll, 5000);
    return () => clearInterval(interval);
  }, []);

  async function writeRequest(url: string, method: string, body?: unknown) {
    try {
      const res = await fetch(url, {
        method,
        headers: body ? { "Content-Type": "application/json" } : undefined,
        body: body ? JSON.stringify(body) : undefined,
      });
      if (!res.ok) {
        alert(`request failed: ${res.status}. check console for CORS errors if this keeps happening.`);
        return false;
      }
      return true;
    } catch {
      alert("could not reach daemon, check console for a CORS error");
      return false;
    }
  }

  async function toggleEnabled(rule: Rule) {
    const ok = await writeRequest(`${BASE}/api/alerts/rules/${rule.id}`, "PUT", { ...rule, enabled: !rule.enabled });
    if (!ok) return;
    setJustToggledId(rule.id);
    await loadAll();
    setTimeout(loadAll, 2500);
    setTimeout(() => { loadAll(); setJustToggledId(null); }, 5500);
  }

  async function deleteRule(id: number) {
    const firing = active.find((a) => a.rule_id === id);
    if (firing) {
      alert(`can't delete "${firing.name}" right now — it's currently ${firing.state}. wait for it to resolve first, or it'll be discarded with no record.`);
      return;
    }
    if (!confirm("delete this rule?")) return;
    const ok = await writeRequest(`${BASE}/api/alerts/rules/${id}`, "DELETE");
    if (ok) loadAll();
  }

  async function deleteHistoryEntry(id: number) {
    if (!confirm("delete this history entry?")) return;
    const ok = await writeRequest(`${BASE}/api/alerts/history/${id}`, "DELETE");
    if (ok) loadAll();
  }

  async function clearOlderThanWeek() {
    const cutoff = Date.now() - 7 * 24 * 60 * 60 * 1000;
    if (!confirm("clear all history older than 1 week?")) return;
    const ok = await writeRequest(`${BASE}/api/alerts/history?older_than_ms=${cutoff}`, "DELETE");
    if (ok) loadAll();
  }

  function startEdit(rule: Rule) {
    setEditingId(rule.id);
    setForm({ ...rule });
    setDisplayValue(toDisplayValue(rule.metric, rule.value));
    setShowForm(true);
  }

  function startCreate() {
    setEditingId(null);
    setForm(emptyRule);
    setDisplayValue(toDisplayValue(emptyRule.metric, emptyRule.value));
    setShowForm((s) => !s);
  }

  async function submitForm() {
    const payload = { ...form, value: toRawValue(form.metric, displayValue) };
    const ok =
      editingId !== null
        ? await writeRequest(`${BASE}/api/alerts/rules/${editingId}`, "PUT", payload)
        : await writeRequest(`${BASE}/api/alerts/rules`, "POST", payload);
    if (!ok) return;
    setForm(emptyRule);
    setEditingId(null);
    setShowForm(false);
    loadAll();
  }

  const ruleNameById = new Map(rules.map((r) => [r.id, r.name]));
  const filteredHistory = history.filter((h) =>
    (ruleNameById.get(h.rule_id) ?? `rule #${h.rule_id}`).toLowerCase().includes(historySearch.toLowerCase())
  );
  const totalDuration = filteredHistory.reduce((s, h) => s + h.duration_seconds, 0);

  return (
    <div className="bg-bg min-h-full p-4 font-mono-ui text-text text-sm">
      <div className="flex items-center justify-between mb-3">
        <span className="text-muted text-xs">{error ?? `${rules.length} rules`}</span>
        <button onClick={startCreate} className="px-3 py-1 border border-line text-muted hover:text-text text-xs">
          {showForm && editingId === null ? "cancel" : "+ new rule"}
        </button>
      </div>

      {active.length > 0 && (
        <div className="border border-cpu bg-panel mb-4">
          <div className="px-3 py-2 border-b border-cpu text-cpu text-xs">currently firing</div>
          {active.map((a) => {
            const rule = rules.find((r) => r.id === a.rule_id);
            return (
              <div key={a.rule_id} className="flex items-center justify-between px-3 py-2 border-b border-line/50 text-xs">
                <div>
                  <span className="text-text">{a.name}</span>
                  <span className={`ml-2 ${a.state === "active" ? "text-cpu" : "text-ram"}`}>
                    {a.state === "active" ? "active" : "pending"}
                  </span>
                </div>
                <div className="text-muted tabular-nums">
                  peak {rule ? formatPeak(rule.metric, a.peak_value) : a.peak_value.toFixed(1)} · since {new Date(a.state_entered_at).toLocaleTimeString()}
                </div>
              </div>
            );
          })}
        </div>
      )}

      {showForm && (
        <div className="border border-line bg-panel p-3 mb-3 grid grid-cols-2 gap-2">
          <div className="col-span-2 text-xs text-muted">{editingId !== null ? `editing rule #${editingId}` : "new rule"}</div>
          <input
            className="bg-bg border border-line px-2 py-1 text-sm col-span-2"
            placeholder="rule name"
            value={form.name}
            onChange={(e) => setForm({ ...form, name: e.target.value })}
          />
          <MetricSearchPicker
            value={form.metric}
            onChange={(metric) => {
              const rawEquivalent = toRawValue(form.metric, displayValue);
              setForm({ ...form, metric });
              setDisplayValue(toDisplayValue(metric, rawEquivalent));
            }}
          />
          <select
            className="bg-bg border border-line px-2 py-1 text-sm"
            value={form.rule_type}
            onChange={(e) => setForm({ ...form, rule_type: e.target.value as Rule["rule_type"] })}
          >
            <option value="threshold">threshold</option>
            <option value="sustained_threshold">sustained threshold</option>
            <option value="rate_of_change">rate of change</option>
          </select>
          <select
            className="bg-bg border border-line px-2 py-1 text-sm"
            value={form.operator}
            onChange={(e) => setForm({ ...form, operator: e.target.value as Rule["operator"] })}
          >
            <option value="gt">greater than</option>
            <option value="lt">less than</option>
            <option value="gte">greater or equal</option>
            <option value="lte">less or equal</option>
          </select>
          <div className="flex items-center gap-1">
            <input
              type="number"
              className="bg-bg border border-line px-2 py-1 text-sm flex-1"
              placeholder="threshold value"
              value={displayValue}
              onChange={(e) => setDisplayValue(Number(e.target.value))}
            />
            <span className="text-muted text-xs w-12">{unitLabel(form.metric)}</span>
          </div>
          {form.rule_type === "sustained_threshold" && (
            <input
              type="number"
              className="bg-bg border border-line px-2 py-1 text-sm"
              placeholder="duration readings (5s ticks)"
              value={form.duration_readings}
              onChange={(e) => setForm({ ...form, duration_readings: Number(e.target.value) })}
            />
          )}
          {form.rule_type === "rate_of_change" && (
            <>
              <input
                type="number"
                className="bg-bg border border-line px-2 py-1 text-sm"
                placeholder="change percent"
                value={form.change_percent}
                onChange={(e) => setForm({ ...form, change_percent: Number(e.target.value) })}
              />
              <input
                type="number"
                className="bg-bg border border-line px-2 py-1 text-sm"
                placeholder="window readings"
                value={form.window_readings}
                onChange={(e) => setForm({ ...form, window_readings: Number(e.target.value) })}
              />
            </>
          )}
          <button onClick={submitForm} className="col-span-2 border border-cpu text-cpu py-1 text-xs hover:bg-cpu/10">
            {editingId !== null ? "save changes" : "create rule"}
          </button>
        </div>
      )}

      <div className="border border-line bg-panel mb-4">
        <div className="px-3 py-2 border-b border-line text-muted text-xs">rules</div>
        {rules.map((r) => (
          <div key={r.id} className="flex items-center justify-between px-3 py-2 border-b border-line/50">
            <div>
              <span className={r.enabled ? "text-text" : "text-muted line-through"}>{r.name}</span>
              <div className="text-muted text-xs">{ruleDescription(r)}</div>
            </div>
            <div className="flex gap-3 items-center">
              <button onClick={() => startEdit(r)} className="text-muted hover:text-text text-xs">edit</button>
              <button
                  onClick={() => toggleEnabled(r)}
                  className={`text-xs ${r.enabled ? "text-cpu" : "text-muted"} ${justToggledId === r.id ? "animate-pulse" : ""}`}
                >
                  {r.enabled ? "enabled" : "disabled"}
                </button>
              <button onClick={() => deleteRule(r.id)} className="text-muted hover:text-red-400 text-xs">×</button>
            </div>
          </div>
        ))}
      </div>

      <div className="border border-line bg-panel">
        <div className="px-3 py-2 border-b border-line flex items-center justify-between gap-2">
          <span className="text-muted text-xs">history</span>
          <div className="flex items-center gap-2">
            <input
              className="bg-bg border border-line px-2 py-0.5 text-xs w-40"
              placeholder="search by rule..."
              value={historySearch}
              onChange={(e) => setHistorySearch(e.target.value)}
            />
            <button onClick={clearOlderThanWeek} className="text-muted hover:text-red-400 text-xs">
              clear older than 1w
            </button>
          </div>
        </div>
        {filteredHistory.length > 0 && (
          <div className="px-3 py-1.5 text-xs text-muted border-b border-line/50">
            {filteredHistory.length} alerts, {totalDuration}s total duration
          </div>
        )}
        {filteredHistory.length === 0 && <p className="text-muted text-xs p-3">no alerts found</p>}
        {filteredHistory.map((h) => {
          const rule = rules.find((r) => r.id === h.rule_id);
          return (
            <div key={h.id} className="px-3 py-1.5 border-b border-line/50 text-xs">
              <div className="flex justify-between items-center tabular-nums">
                <span className="text-muted">{ruleNameById.get(h.rule_id) ?? `rule #${h.rule_id}`}</span>
                <span>peak {rule ? formatPeak(rule.metric, h.peak_value) : h.peak_value.toFixed(1)}</span>
                <span className="text-muted">{h.duration_seconds}s</span>
                <span className="text-muted">{new Date(h.triggered_at).toLocaleTimeString()}</span>
                <button onClick={() => deleteHistoryEntry(h.id)} className="text-muted hover:text-red-400 ml-2">×</button>
              </div>
              {h.note && <div className="text-muted mt-1">{h.note}</div>}
            </div>
          );
        })}
      </div>
    </div>
  );
}

type MetricPickerItem = { label: string; metric: string };
type MetricPickerGroup = { name: string; items: MetricPickerItem[] };

function buildAlertMetricGroups(): MetricPickerGroup[] {
  return [
    {
      name: "CPU",
      items: [
        { label: "CPU Total", metric: "cpu_total_percent" },
        ...Array.from({ length: 16 }, (_, i) => ({ label: `CPU Core ${i}`, metric: `cpu_core_${i}` })),
      ],
    },
    {
      name: "RAM",
      items: [
        { label: "RAM Used", metric: "ram_used_bytes" },
        { label: "RAM Available", metric: "ram_available_bytes" },
        { label: "Swap Used", metric: "swap_used_bytes" },
      ],
    },
    {
      name: "Disk",
      items: Array.from({ length: 4 }, (_, i) => [
        { label: `Disk ${i} Read`, metric: `disk_${i}_read` },
        { label: `Disk ${i} Write`, metric: `disk_${i}_write` },
      ]).flat(),
    },
    {
      name: "Network",
      items: Array.from({ length: 4 }, (_, i) => [
        { label: `Adapter ${i} In`, metric: `net_${i}_in` },
        { label: `Adapter ${i} Out`, metric: `net_${i}_out` },
      ]).flat(),
    },
  ];
}

function MetricSearchPicker({ value, onChange }: { value: string; onChange: (metric: string) => void }) {
  const [open, setOpen] = useState(false);
  const [search, setSearch] = useState("");
  const groups = buildAlertMetricGroups();

  const filtered = search
    ? groups
        .map((g) => ({
          name: g.name,
          items: g.items.filter(
            (i) => i.label.toLowerCase().includes(search.toLowerCase()) || i.metric.toLowerCase().includes(search.toLowerCase())
          ),
        }))
        .filter((g) => g.items.length > 0)
    : groups;

  const selectedLabel = groups.flatMap((g) => g.items).find((i) => i.metric === value)?.label;

  return (
    <div className="relative col-span-1">
      <button
        type="button"
        onClick={() => setOpen((o) => !o)}
        className="bg-bg border border-line px-2 py-1 text-sm w-full text-left flex flex-col"
      >
        <span>{selectedLabel ?? "select metric"}</span>
        {value && <span className="text-muted text-xs">{value}</span>}
      </button>

      {open && (
        <div className="absolute z-20 mt-1 w-64 border border-line bg-panel max-h-72 overflow-auto">
          <input
            autoFocus
            className="w-full bg-bg border-b border-line px-2 py-1 text-xs outline-none"
            placeholder="search metrics..."
            value={search}
            onChange={(e) => setSearch(e.target.value)}
          />
          {filtered.map((g) => (
            <div key={g.name}>
              <div className="px-2 py-1 text-xs text-muted border-b border-line/50">{g.name}</div>
              {g.items.map((item) => (
                <button
                  key={item.metric}
                  type="button"
                  onClick={() => { onChange(item.metric); setOpen(false); setSearch(""); }}
                  className={`w-full text-left px-3 py-1 text-xs flex flex-col ${
                    item.metric === value ? "text-cpu" : "text-muted hover:text-text"
                  }`}
                >
                  <span>{item.label}</span>
                  <span className="text-muted text-xs">{item.metric}</span>
                </button>
              ))}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
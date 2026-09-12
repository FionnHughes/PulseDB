import { useEffect, useRef, useState } from "react";

type ProcessEntry = {
  pid: number;
  name: string;
  cpu_pct: number;
  ram_bytes: number;
  threads: number;
  handles: number;
};

const CRITICAL_NAMES = ["system", "csrss.exe", "wininit.exe", "winlogon.exe", "services.exe", "lsass.exe", "smss.exe"];

type SortKey = "cpu" | "ram" | "threads" | "handles" | "name" | "processes";

function bytesToMB(bytes: number) {
  return (bytes / 1024 / 1024).toFixed(0);
}

function groupByName(list: ProcessEntry[]) {
  const groups = new Map<string, ProcessEntry[]>();
  for (const p of list) {
    const arr = groups.get(p.name) ?? [];
    arr.push(p);
    groups.set(p.name, arr);
  }
  return Array.from(groups.entries()).map(([name, procs]) => ({
    name,
    procs,
    totalCpu: procs.reduce((sum, p) => sum + p.cpu_pct, 0),
    totalRam: procs.reduce((sum, p) => sum + p.ram_bytes, 0),
    totalThreads: procs.reduce((sum, p) => sum + p.threads, 0),
    totalHandles: procs.reduce((sum, p) => sum + p.handles, 0),
  }));
}

export default function ProcessMonitor() {
  const [processes, setProcesses] = useState<ProcessEntry[]>([]);
  const [search, setSearch] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [paused, setPaused] = useState(false);
  const [expanded, setExpanded] = useState<Set<string>>(new Set());
  const [sortKey, setSortKey] = useState<SortKey>("cpu");
  const [minCpu, setMinCpu] = useState("");
  const [minRamMB, setMinRamMB] = useState("");
  const [minThreads, setMinThreads] = useState("");

  const latestRef = useRef<ProcessEntry[]>([]);
  const pausedRef = useRef(paused);
  pausedRef.current = paused;

  async function fetchLatest() {
    const res = await fetch("http://localhost:7700/api/processes/latest");
    if (!res.ok) throw new Error(`status ${res.status}`);
    const data = await res.json();
    return data.processes ?? [];
  }

  useEffect(() => {
    async function poll() {
      try {
        const list = await fetchLatest();
        latestRef.current = list;
        setError(null);
        if (!pausedRef.current) setProcesses(list);
      } catch {
        setError("daemon unreachable");
      }
    }
    poll();
    const interval = setInterval(poll, 5000);
    return () => clearInterval(interval);
  }, []);

  function togglePause() {
    setPaused((p) => {
      const next = !p;
      if (!next) setProcesses(latestRef.current);
      return next;
    });
  }

  function toggleGroup(name: string) {
    setExpanded((prev) => {
      const next = new Set(prev);
      if (next.has(name)) next.delete(name);
      else next.add(name);
      return next;
    });
  }

  async function killProcess(pid: number, name: string) {
    if (CRITICAL_NAMES.includes(name.toLowerCase())) {
      alert(`refusing to kill ${name} — this would likely crash your system`);
      return;
    }
    if (!confirm(`kill ${name} (pid ${pid})? this can't be undone.`)) return;
    try {
      const res = await fetch(`http://localhost:7700/api/processes/${pid}/kill`, { method: "POST" });
      const data = await res.json();
      if (!data.success) {
        alert(`failed: ${data.error ?? "unknown error"}`);
        return;
      }
      const fresh = await fetchLatest();
      latestRef.current = fresh;
      setProcesses(fresh);
    } catch {
      alert("failed: could not reach daemon");
    }
  }

  let groups = groupByName(processes).filter((g) => g.name.toLowerCase().includes(search.toLowerCase()));

  if (minCpu !== "") groups = groups.filter((g) => g.totalCpu >= Number(minCpu));
  if (minRamMB !== "") groups = groups.filter((g) => g.totalRam >= Number(minRamMB) * 1024 * 1024);
  if (minThreads !== "") groups = groups.filter((g) => g.totalThreads >= Number(minThreads));

  if (sortKey === "name") {
    groups = groups.sort((a, b) => a.name.localeCompare(b.name));
  } else {
    const sortFns: Record<Exclude<SortKey, "name">, (g: (typeof groups)[number]) => number> = {
      cpu: (g) => g.totalCpu,
      ram: (g) => g.totalRam,
      threads: (g) => g.totalThreads,
      handles: (g) => g.totalHandles,
      processes: (g) => g.procs.length,
    };
    groups = groups.sort((a, b) => sortFns[sortKey](b) - sortFns[sortKey](a));
  }

  return (
    <div className="bg-bg min-h-full p-4 font-mono-ui text-text text-sm">
      <div className="flex items-center gap-2 mb-2 flex-wrap">
        <input
          className="bg-panel border border-line px-2 py-1 text-sm text-text placeholder:text-muted outline-none focus:border-cpu flex-1 min-w-32"
          placeholder="filter by name..."
          value={search}
          onChange={(e) => setSearch(e.target.value)}
        />
        <select
          className="bg-panel border border-line px-2 py-1 text-xs"
          value={sortKey}
          onChange={(e) => setSortKey(e.target.value as SortKey)}
        >
          <option value="cpu">sort: cpu</option>
          <option value="ram">sort: ram</option>
          <option value="threads">sort: threads</option>
          <option value="handles">sort: handles</option>
          <option value="name">sort: name</option>
          <option value="processes">sort: instance count</option>
        </select>
        <button
          onClick={togglePause}
          className={`px-3 py-1 border text-xs ${paused ? "border-ram text-ram" : "border-line text-muted hover:text-text"}`}
        >
          {paused ? "paused" : "pause"}
        </button>
        <span className="text-muted text-xs whitespace-nowrap">
          {error ?? `${groups.length} groups, ${processes.length} procs`}
        </span>
      </div>

      <div className="flex items-center gap-2 mb-3 text-xs">
        <span className="text-muted">filter:</span>
        <input
          type="number"
          placeholder="min cpu %"
          value={minCpu}
          onChange={(e) => setMinCpu(e.target.value)}
          className="bg-panel border border-line px-2 py-1 w-24"
        />
        <input
          type="number"
          placeholder="min ram MB"
          value={minRamMB}
          onChange={(e) => setMinRamMB(e.target.value)}
          className="bg-panel border border-line px-2 py-1 w-24"
        />
        <input
          type="number"
          placeholder="min threads"
          value={minThreads}
          onChange={(e) => setMinThreads(e.target.value)}
          className="bg-panel border border-line px-2 py-1 w-24"
        />
        <span className="text-muted" title="per-process disk I/O isn't collected in this version, so there's no disk filter">
          disk: not tracked yet
        </span>
      </div>

      <div className="border border-line bg-panel">
        <div className="grid grid-cols-[1fr_55px_60px_50px_60px_36px] gap-2 px-3 py-2 border-b border-line text-muted text-xs">
          <span>name</span>
          <span className="text-right">pid</span>
          <span className="text-right">cpu</span>
          <span className="text-right">ram</span>
          <span className="text-right" title="threads / handles">thr/hnd</span>
          <span></span>
        </div>
        <div className="max-h-[calc(100vh-190px)] overflow-auto">
          {groups.map((g) => {
            const isGroup = g.procs.length > 1;
            const isOpen = expanded.has(g.name);
            return (
              <div key={g.name}>
                <div
                  onClick={() => isGroup && toggleGroup(g.name)}
                  className={`grid grid-cols-[1fr_55px_60px_50px_60px_36px] gap-2 px-3 py-1.5 border-b border-line hover:bg-line/40 tabular-nums items-center ${isGroup ? "cursor-pointer" : ""}`}
                >
                  <span className="truncate" title={g.name}>
                    {isGroup ? (isOpen ? "▾ " : "▸ ") : ""}{g.name}
                    {isGroup ? <span className="text-muted"> ({g.procs.length})</span> : ""}
                  </span>
                  <span className="text-right text-muted">{isGroup ? "—" : g.procs[0].pid}</span>
                  <span className="text-right text-cpu">{g.totalCpu.toFixed(1)}%</span>
                  <span className="text-right">{bytesToMB(g.totalRam)}M</span>
                  <span className="text-right text-muted">{isGroup ? "—" : `${g.procs[0].threads}/${g.procs[0].handles}`}</span>
                  {!isGroup && (
                    <button
                      onClick={(e) => { e.stopPropagation(); killProcess(g.procs[0].pid, g.procs[0].name); }}
                      className="text-right text-muted hover:text-red-400 text-xs"
                    >
                      ×
                    </button>
                  )}
                </div>

                {isGroup && isOpen && g.procs.map((p) => (
                  <div
                    key={p.pid}
                    className="grid grid-cols-[1fr_55px_60px_50px_60px_36px] gap-2 px-3 py-1 border-b border-line/50 tabular-nums items-center bg-bg/40"
                  >
                    <span className="truncate pl-4 text-muted" title={p.name}>↳ {p.name}</span>
                    <span className="text-right text-muted">{p.pid}</span>
                    <span className="text-right text-cpu">{p.cpu_pct.toFixed(1)}%</span>
                    <span className="text-right">{bytesToMB(p.ram_bytes)}M</span>
                    <span className="text-right text-muted">{p.threads}/{p.handles}</span>
                    <button onClick={() => killProcess(p.pid, p.name)} className="text-right text-muted hover:text-red-400 text-xs">×</button>
                  </div>
                ))}
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}
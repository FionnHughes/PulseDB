import { useLiveStore } from "../store/liveStore";

function bytesToGB(bytes?: number) {
  if (bytes === undefined) return "?";
  return (bytes / 1024 / 1024 / 1024).toFixed(1);
}

function bpsToMbps(bps?: number) {
  if (bps === undefined) return "?";
  return ((bps * 8) / 1_000_000).toFixed(1);
}

const REAL_PATTERNS = ["Wi-Fi", "WiFi", "Ethernet", "Bluetooth"];
const EXCLUDE_PATTERNS = ["Filter Driver", "Kernel Debugger", "WFP", "QoS", "Loopback"];

function isRealAdapter(name: string) {
  const looksReal = REAL_PATTERNS.some((p) => name.includes(p));
  const looksVirtual = EXCLUDE_PATTERNS.some((p) => name.includes(p));
  return looksReal && !looksVirtual;
}

function Panel({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="border border-line bg-panel">
      <div className="border-b border-line px-3 py-2 text-muted text-xs">{title}</div>
      <div className="p-3">{children}</div>
    </div>
  );
}

// tiny inline sparkline, no charting lib needed for something this small
function Sparkline({ data, color }: { data: number[]; color: string }) {
  if (data.length < 2) return <div className="h-10" />;
  const max = Math.max(...data, 1);
  const points = data
    .map((v, i) => `${(i / (data.length - 1)) * 100},${100 - (v / max) * 100}`)
    .join(" ");
  return (
    <svg viewBox="0 0 100 100" preserveAspectRatio="none" className="h-10 w-full">
      <polyline points={points} fill="none" stroke={color} strokeWidth="2" vectorEffect="non-scaling-stroke" />
    </svg>
  );
}

export default function Dashboard() {
  const latest = useLiveStore((s) => s.latest);
  const connected = useLiveStore((s) => s.connected);
  const cpuHistory = useLiveStore((s) => s.cpuHistory);

  if (!connected) {
    return <div className="p-4 text-muted font-mono-ui text-sm">connecting to daemon...</div>;
  }
  if (!latest) {
    return <div className="p-4 text-muted font-mono-ui text-sm">waiting for first snapshot...</div>;
  }

  const ramPct = latest.ram_total_bytes
    ? (latest.ram_used_bytes / latest.ram_total_bytes) * 100
    : 0;
  const visibleNetwork = (latest.network ?? []).filter((n) => isRealAdapter(n.name));

  return (
    <div className="bg-bg min-h-full p-4 font-mono-ui text-text text-sm grid grid-cols-2 gap-3">
      <Panel title="cpu">
        <div className="flex items-baseline justify-between">
          <div className="text-3xl text-cpu tabular-nums">{latest.cpu_total?.toFixed(1) ?? "?"}%</div>
          <div className="text-muted text-xs">pulsedb {latest.pulsedb_cpu_pct?.toFixed(1) ?? "?"}%</div>
        </div>
        <Sparkline data={cpuHistory} color="#5FD3BC" />
        <div className="grid grid-cols-4 gap-x-3 gap-y-1 mt-2">
          {(latest.cpu_cores ?? []).map((pct, i) => (
            <div key={i} className="flex items-center gap-2">
              <div className="h-1 flex-1 bg-line overflow-hidden">
                <div className="h-full bg-cpu" style={{ width: `${pct}%` }} />
              </div>
              <span className="text-muted text-xs tabular-nums w-8 text-right">{pct.toFixed(0)}%</span>
            </div>
          ))}
        </div>
      </Panel>

      <Panel title="ram">
        <div className="flex items-baseline justify-between">
          <div className="text-3xl text-ram tabular-nums">
            {bytesToGB(latest.ram_used_bytes)} <span className="text-muted text-lg">/ {bytesToGB(latest.ram_total_bytes)} GB</span>
          </div>
          <div className="text-muted text-xs">pulsedb {bytesToGB(latest.pulsedb_ram_bytes)} GB</div>
        </div>
        <div className="h-1 bg-line mt-3 overflow-hidden">
          <div className="h-full bg-ram" style={{ width: `${ramPct}%` }} />
        </div>
        <div className="flex justify-between mt-3 text-muted text-xs">
          <span>available</span>
          <span className="tabular-nums">{bytesToGB(latest.ram_available_bytes)} GB</span>
        </div>
      </Panel>

      <Panel title="disks">
        {(latest.disks ?? []).length === 0 && <p className="text-muted">no disk data</p>}
        <div className="space-y-2">
          {(latest.disks ?? []).map((d) => (
            <div key={d.name}>
              <div className="flex justify-between">
                <span className="text-muted">{d.name}</span>
                <span className="tabular-nums">r {bpsToMbps(d.read_bps)} · w {bpsToMbps(d.write_bps)} mbps</span>
              </div>
              <div className="flex justify-between text-muted text-xs mt-0.5">
                <span>util {d.util_pct?.toFixed(0) ?? "0"}%</span>
                <span>queue {d.queue ?? 0}</span>
              </div>
            </div>
          ))}
        </div>
      </Panel>

      <Panel title="network">
        {visibleNetwork.length === 0 && <p className="text-muted">no network data</p>}
        <div className="space-y-2">
          {visibleNetwork.map((n) => (
            <div key={n.name}>
              <div className="flex justify-between">
                <span className="text-muted">{n.name}</span>
                <span className="tabular-nums">↓{bpsToMbps(n.in_bps)} ↑{bpsToMbps(n.out_bps)} mbps</span>
              </div>
              <div className="flex justify-between text-muted text-xs mt-0.5">
                <span>pkts in {n.packets_in ?? 0}</span>
                <span>pkts out {n.packets_out ?? 0}</span>
              </div>
            </div>
          ))}
        </div>
      </Panel>
    </div>
  );
}
import { useEffect, useState } from "react";

const BASE = "http://localhost:7700";

type ConfigShape = {
  api_port: number;
  data_directory: string;
  collection_interval_ms: number;
  retention: {
    raw_days: number;
    summary_1min_days: number;
    summary_1hr_days: number;
  };
};

export default function Settings() {
  const [config, setConfig] = useState<ConfigShape | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [savedMsg, setSavedMsg] = useState<string | null>(null);

  useEffect(() => {
    fetch(`${BASE}/api/config`)
      .then((r) => r.json())
      .then(setConfig)
      .catch(() => setError("daemon unreachable"));
  }, []);

  async function save() {
    if (!config) return;
    try {
      const res = await fetch(`${BASE}/api/config`, {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(config),
      });
      const data = await res.json();
      setSavedMsg(data.restart_required ? "saved — restart the daemon for this to take effect" : "saved");
      setTimeout(() => setSavedMsg(null), 4000);
    } catch {
      setSavedMsg("save failed, check console");
    }
  }

  if (error) return <div className="p-4 text-muted font-mono-ui text-sm">{error}</div>;
  if (!config) return <div className="p-4 text-muted font-mono-ui text-sm">loading config...</div>;

  return (
    <div className="bg-bg min-h-full p-4 font-mono-ui text-text text-sm max-w-lg">
      <div className="border border-line bg-panel p-1 mb-4 text-xs text-muted">
        nothing here hot-reloads yet — changes are saved to pulsedb.json but only take effect after restarting the daemon
      </div>

      <div className="border border-line bg-panel">
        <div className="px-3 py-2 border-b border-line text-muted text-xs">collection</div>
        <div className="p-3 flex flex-col gap-3">
          <label className="flex flex-col gap-1">
            <span className="text-muted text-xs">collection interval (ms)</span>
            <input
              type="number"
              value={config.collection_interval_ms}
              onChange={(e) => setConfig({ ...config, collection_interval_ms: Number(e.target.value) })}
              className="bg-bg border border-line px-2 py-1 w-32"
            />
          </label>
          <label className="flex flex-col gap-1">
            <span className="text-muted text-xs">api port</span>
            <input
              type="number"
              value={config.api_port}
              onChange={(e) => setConfig({ ...config, api_port: Number(e.target.value) })}
              className="bg-bg border border-line px-2 py-1 w-32"
            />
          </label>
          <label className="flex flex-col gap-1">
            <span className="text-muted text-xs">data directory</span>
            <input
              value={config.data_directory}
              onChange={(e) => setConfig({ ...config, data_directory: e.target.value })}
              className="bg-bg border border-line px-2 py-1"
            />
          </label>
        </div>
      </div>

      <div className="border border-line bg-panel mt-4">
        <div className="px-3 py-2 border-b border-line text-muted text-xs">retention</div>
        <div className="p-3 flex flex-col gap-3">
          <label className="flex flex-col gap-1">
            <span className="text-muted text-xs">raw data (days)</span>
            <input
              type="number"
              value={config.retention.raw_days}
              onChange={(e) => setConfig({ ...config, retention: { ...config.retention, raw_days: Number(e.target.value) } })}
              className="bg-bg border border-line px-2 py-1 w-32"
            />
          </label>
          <label className="flex flex-col gap-1">
            <span className="text-muted text-xs">1-minute summaries (days)</span>
            <input
              type="number"
              value={config.retention.summary_1min_days}
              onChange={(e) => setConfig({ ...config, retention: { ...config.retention, summary_1min_days: Number(e.target.value) } })}
              className="bg-bg border border-line px-2 py-1 w-32"
            />
          </label>
          <label className="flex flex-col gap-1">
            <span className="text-muted text-xs">1-hour summaries (days)</span>
            <input
              type="number"
              value={config.retention.summary_1hr_days}
              onChange={(e) => setConfig({ ...config, retention: { ...config.retention, summary_1hr_days: Number(e.target.value) } })}
              className="bg-bg border border-line px-2 py-1 w-32"
            />
          </label>
        </div>
      </div>

      <div className="flex items-center gap-3 mt-4">
        <button onClick={save} className="px-3 py-1 border border-cpu text-cpu hover:bg-cpu/10 text-xs">
          save
        </button>
        {savedMsg && <span className="text-muted text-xs">{savedMsg}</span>}
      </div>
    </div>
  );
}
import { useState } from "react";

export default function Settings() {
  // defaults below are copied from Config.h, keep them in sync until this actually reads from the daemon
  const [collectionInterval, setCollectionInterval] = useState(1000);
  const [apiPort, setApiPort] = useState(7700);
  const [dataDirectory, setDataDirectory] = useState("C:\\ProgramData\\PulseDB\\data");
  const [rawDays, setRawDays] = useState(7);
  const [summary1minDays, setSummary1minDays] = useState(30);
  const [summary1hrDays, setSummary1hrDays] = useState(365);
  const [savedMsg, setSavedMsg] = useState<string | null>(null);

  // not wired to the daemon yet, just proves the layout and interactions
  function fakeSave() {
    setSavedMsg("saved (not actually wired up yet, this is a placeholder)");
    setTimeout(() => setSavedMsg(null), 3000);
  }

  return (
    <div className="bg-bg min-h-full p-4 font-mono-ui text-text text-sm max-w-lg">
      <div className="border border-line bg-panel p-1 mb-4 text-xs text-muted">
        this screen isn't connected to the daemon yet, nothing here does anything real
      </div>

      <div className="border border-line bg-panel">
        <div className="px-3 py-2 border-b border-line text-muted text-xs">collection</div>
        <div className="p-3 flex flex-col gap-3">
          <label className="flex flex-col gap-1">
            <span className="text-muted text-xs">collection interval (ms)</span>
            <input
              type="number"
              value={collectionInterval}
              onChange={(e) => setCollectionInterval(Number(e.target.value))}
              className="bg-bg border border-line px-2 py-1 w-32"
            />
          </label>
          <label className="flex flex-col gap-1">
            <span className="text-muted text-xs">api port</span>
            <input
              type="number"
              value={apiPort}
              onChange={(e) => setApiPort(Number(e.target.value))}
              className="bg-bg border border-line px-2 py-1 w-32"
            />
          </label>
          <label className="flex flex-col gap-1">
            <span className="text-muted text-xs">data directory</span>
            <input
              value={dataDirectory}
              onChange={(e) => setDataDirectory(e.target.value)}
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
              value={rawDays}
              onChange={(e) => setRawDays(Number(e.target.value))}
              className="bg-bg border border-line px-2 py-1 w-32"
            />
          </label>
          <label className="flex flex-col gap-1">
            <span className="text-muted text-xs">1-minute summaries (days)</span>
            <input
              type="number"
              value={summary1minDays}
              onChange={(e) => setSummary1minDays(Number(e.target.value))}
              className="bg-bg border border-line px-2 py-1 w-32"
            />
          </label>
          <label className="flex flex-col gap-1">
            <span className="text-muted text-xs">1-hour summaries (days)</span>
            <input
              type="number"
              value={summary1hrDays}
              onChange={(e) => setSummary1hrDays(Number(e.target.value))}
              className="bg-bg border border-line px-2 py-1 w-32"
            />
          </label>
        </div>
      </div>

      <div className="flex items-center gap-3 mt-4">
        <button onClick={fakeSave} className="px-3 py-1 border border-cpu text-cpu hover:bg-cpu/10 text-xs">
          save
        </button>
        {savedMsg && <span className="text-muted text-xs">{savedMsg}</span>}
      </div>

      <p className="text-muted text-xs mt-4">
        changes to collection interval and retention will need a daemon restart to take effect once this is actually wired up
      </p>
    </div>
  );
}
import { useEffect } from "react";
import { HashRouter, Routes, Route, NavLink } from "react-router-dom";
import { useLiveStore } from "./store/liveStore";
import Dashboard from "./screens/Dashboard";
import HistoricalExplorer from "./screens/HistoricalExplorer";
import ProcessMonitor from "./screens/ProcessMonitor";
import AlertManager from "./screens/AlertManager";
import Settings from "./screens/Settings";
import "./App.css";

const NAV_ITEMS = [
  { to: "/", label: "dashboard" },
  { to: "/history", label: "historical" },
  { to: "/processes", label: "processes" },
  { to: "/alerts", label: "alerts" },
  { to: "/settings", label: "settings" },
];

function App() {
  const connect = useLiveStore((s) => s.connect);
  const connected = useLiveStore((s) => s.connected);
  useEffect(() => { connect(); }, [connect]);

  return (
    <HashRouter>
      <div className="flex h-screen bg-bg font-mono-ui text-sm">
        <nav className="w-44 border-r border-line flex flex-col">
          <div className="px-3 py-3 border-b border-line text-text">
            pulsedb
            <span className={`ml-2 inline-block h-1.5 w-1.5 rounded-full ${connected ? "bg-cpu" : "bg-muted"}`} />
          </div>
          <div className="flex flex-col">
            {NAV_ITEMS.map((item) => (
              <NavLink
                key={item.to}
                to={item.to}
                end={item.to === "/"}
                className={({ isActive }) =>
                  `px-3 py-2 border-l-2 ${
                    isActive
                      ? "border-cpu text-text bg-panel"
                      : "border-transparent text-muted hover:text-text"
                  }`
                }
              >
                {item.label}
              </NavLink>
            ))}
          </div>
        </nav>
        <main className="flex-1 overflow-auto">
          <Routes>
            <Route path="/" element={<Dashboard />} />
            <Route path="/history" element={<HistoricalExplorer />} />
            <Route path="/processes" element={<ProcessMonitor />} />
            <Route path="/alerts" element={<AlertManager />} />
            <Route path="/settings" element={<Settings />} />
          </Routes>
        </main>
      </div>
    </HashRouter>
  );
}

export default App;
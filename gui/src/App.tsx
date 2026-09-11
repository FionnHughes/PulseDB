import { HashRouter, Routes, Route, Link } from "react-router-dom";
import Dashboard from "./screens/Dashboard";
import HistoricalExplorer from "./screens/HistoricalExplorer";
import ProcessMonitor from "./screens/ProcessMonitor";
import AlertManager from "./screens/AlertManager";
import Settings from "./screens/Settings";
import "./App.css";

function App() {
  return (
    <HashRouter>
      <div className="flex h-screen bg-gray-900">
        <nav className="w-48 bg-gray-800 p-4 flex flex-col gap-2">
          <Link className="text-white" to="/">Dashboard</Link>
          <Link className="text-white" to="/history">Historical</Link>
          <Link className="text-white" to="/processes">Processes</Link>
          <Link className="text-white" to="/alerts">Alerts</Link>
          <Link className="text-white" to="/settings">Settings</Link>
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
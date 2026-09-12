import { create } from "zustand";

export type LiveSnapshot = {
  type: string;
  ts: number;
  cpu_total: number;
  cpu_cores: number[];
  ram_used_bytes: number;
  ram_available_bytes: number;
  ram_total_bytes: number;
  disks: { name: string; read_bps: number; write_bps: number; util_pct: number; queue: number }[];
  network: { name: string; in_bps: number; out_bps: number; packets_in: number; packets_out: number }[];
  pulsedb_pid: number;
  pulsedb_cpu_pct: number;
  pulsedb_ram_bytes: number;
};

const HISTORY_LEN = 60; // 60 ticks at 1/sec = last minute

type LiveStore = {
  latest: LiveSnapshot | null;
  cpuHistory: number[];
  connected: boolean;
  connect: () => void;
};

let socket: WebSocket | null = null;

export const useLiveStore = create<LiveStore>((set, get) => ({
  latest: null,
  cpuHistory: [],
  connected: false,

  connect: () => {
    if (socket) return;

    socket = new WebSocket("ws://localhost:7700/ws/live");

    socket.onopen = () => set({ connected: true });
    socket.onclose = () => {
      set({ connected: false });
      socket = null;
      setTimeout(() => useLiveStore.getState().connect(), 2000);
    };
    socket.onerror = () => socket?.close();

    socket.onmessage = (event) => {
      const snap = JSON.parse(event.data) as LiveSnapshot;
      const nextHistory = [...get().cpuHistory, snap.cpu_total].slice(-HISTORY_LEN);
      set({ latest: snap, cpuHistory: nextHistory });
    };
  },
}));
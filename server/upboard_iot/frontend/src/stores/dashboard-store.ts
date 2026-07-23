import { create } from "zustand";
import type { ConnectionStatus, DemoMode, DeviceStatus } from "@/types/telemetry";

type DeviceFilter = "all" | DeviceStatus;
type NtcMode = "raw" | "temperature";

interface DashboardState {
  selectedDeviceId: string;
  deviceFilter: DeviceFilter;
  searchQuery: string;
  voltageRange: string;
  mapRange: string;
  ntcMode: NtcMode;
  connectionStatus: ConnectionStatus;
  autoRefresh: boolean;
  demoMode: DemoMode;
  setSelectedDeviceId: (deviceId: string) => void;
  setDeviceFilter: (filter: DeviceFilter) => void;
  setSearchQuery: (query: string) => void;
  setVoltageRange: (range: string) => void;
  setMapRange: (range: string) => void;
  setNtcMode: (mode: NtcMode) => void;
  setConnectionStatus: (status: ConnectionStatus) => void;
  toggleAutoRefresh: () => void;
}

const requestedState = new URLSearchParams(window.location.search).get("state");
const demoMode: DemoMode =
  requestedState === "empty" || requestedState === "error" || requestedState === "reconnecting"
    ? requestedState
    : "normal";

export const useDashboardStore = create<DashboardState>((set) => ({
  selectedDeviceId: "",
  deviceFilter: "all",
  searchQuery: "",
  voltageRange: "5 min",
  mapRange: "Live",
  ntcMode: "raw",
  connectionStatus: demoMode === "reconnecting" ? "reconnecting" : "connected",
  autoRefresh: true,
  demoMode,
  setSelectedDeviceId: (selectedDeviceId) => set({ selectedDeviceId }),
  setDeviceFilter: (deviceFilter) => set({ deviceFilter }),
  setSearchQuery: (searchQuery) => set({ searchQuery }),
  setVoltageRange: (voltageRange) => set({ voltageRange }),
  setMapRange: (mapRange) => set({ mapRange }),
  setNtcMode: (ntcMode) => set({ ntcMode }),
  setConnectionStatus: (connectionStatus) => set({ connectionStatus }),
  toggleAutoRefresh: () => set((state) => ({ autoRefresh: !state.autoRefresh })),
}));

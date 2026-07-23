import { useEffect } from "react";
import { useQueryClient } from "@tanstack/react-query";
import { telemetryQueryKeys } from "@/services/telemetry";
import { useDashboardStore } from "@/stores/dashboard-store";
import type { ApiRealtimeEvent } from "@/types/api";

export function useTelemetryStream(enabled: boolean) {
  const queryClient = useQueryClient();
  const selectedDeviceId = useDashboardStore((state) => state.selectedDeviceId);
  const autoRefresh = useDashboardStore((state) => state.autoRefresh);
  const demoMode = useDashboardStore((state) => state.demoMode);
  const setConnectionStatus = useDashboardStore((state) => state.setConnectionStatus);

  useEffect(() => {
    if (!enabled) return;
    const stream = new EventSource("/api/stream");

    const handleOnline = () => setConnectionStatus("reconnecting");
    const handleOffline = () => setConnectionStatus("offline");
    const handleEvent = (event: MessageEvent<string>) => {
      if (!autoRefresh) return;
      try {
        const realtime = JSON.parse(event.data) as ApiRealtimeEvent;
        void queryClient.invalidateQueries({ queryKey: telemetryQueryKeys.devices(demoMode) });
        if (realtime.sn === selectedDeviceId) {
          void queryClient.invalidateQueries({ queryKey: telemetryQueryKeys.bundle(selectedDeviceId, demoMode) });
        }
      } catch {
        setConnectionStatus("reconnecting");
      }
    };

    stream.onopen = () => setConnectionStatus("connected");
    stream.onerror = () => setConnectionStatus(navigator.onLine ? "reconnecting" : "offline");
    stream.addEventListener("telemetry", handleEvent as EventListener);
    stream.addEventListener("event", handleEvent as EventListener);
    window.addEventListener("online", handleOnline);
    window.addEventListener("offline", handleOffline);

    return () => {
      stream.close();
      stream.removeEventListener("telemetry", handleEvent as EventListener);
      stream.removeEventListener("event", handleEvent as EventListener);
      window.removeEventListener("online", handleOnline);
      window.removeEventListener("offline", handleOffline);
    };
  }, [autoRefresh, demoMode, enabled, queryClient, selectedDeviceId, setConnectionStatus]);
}

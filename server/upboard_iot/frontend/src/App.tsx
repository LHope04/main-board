import { useEffect } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { AuthLoadingScreen, LoginScreen } from "@/components/auth/LoginScreen";
import { AppShell } from "@/components/dashboard/AppShell";
import { DashboardContent } from "@/components/dashboard/DashboardContent";
import { DashboardLoadingState, DashboardStatePanel } from "@/components/dashboard/DashboardStates";
import { getSession, login, logout } from "@/services/api-client";
import { useTelemetryStream } from "@/hooks/use-telemetry-stream";
import { isMockDataSource, telemetryDataSource, telemetryQueryKeys } from "@/services/telemetry";
import { useDashboardStore } from "@/stores/dashboard-store";

function Dashboard({ username, onLogout, logoutPending }: { username: string; onLogout: () => void; logoutPending: boolean }) {
  const selectedDeviceId = useDashboardStore((state) => state.selectedDeviceId);
  const setSelectedDeviceId = useDashboardStore((state) => state.setSelectedDeviceId);
  const autoRefresh = useDashboardStore((state) => state.autoRefresh);
  const demoMode = useDashboardStore((state) => state.demoMode);

  useTelemetryStream(!isMockDataSource);

  const devicesQuery = useQuery({
    queryKey: telemetryQueryKeys.devices(demoMode),
    queryFn: () => telemetryDataSource.getDevices(demoMode),
    retry: 1,
    staleTime: 5_000,
  });

  const devices = devicesQuery.data ?? [];
  const selectedDevice = devices.find((device) => device.id === selectedDeviceId);

  useEffect(() => {
    if (devices.length > 0 && !selectedDevice) {
      const firstDevice = devices[0];
      if (firstDevice) setSelectedDeviceId(firstDevice.id);
    }
  }, [devices, selectedDevice, setSelectedDeviceId]);

  const bundleQuery = useQuery({
    queryKey: telemetryQueryKeys.bundle(selectedDevice?.id ?? "", demoMode),
    queryFn: () => {
      if (!selectedDevice) throw new Error("No device selected.");
      return telemetryDataSource.getDeviceBundle(selectedDevice, demoMode);
    },
    enabled: Boolean(selectedDevice),
    retry: 1,
    staleTime: 2_000,
    refetchInterval: autoRefresh ? (isMockDataSource ? 4_000 : 5_000) : false,
  });

  return (
    <AppShell devices={devices} devicesLoading={devicesQuery.isPending} username={username} onLogout={onLogout} logoutPending={logoutPending}>
      {devicesQuery.isError ? <DashboardStatePanel kind="error" onRetry={() => void devicesQuery.refetch()} /> : null}
      {devicesQuery.isSuccess && devices.length === 0 ? <DashboardStatePanel kind="empty" /> : null}
      {devicesQuery.isPending || (selectedDevice && bundleQuery.isPending) ? <DashboardLoadingState /> : null}
      {selectedDevice && bundleQuery.isError ? <DashboardStatePanel kind="error" onRetry={() => void bundleQuery.refetch()} /> : null}
      {selectedDevice && bundleQuery.data ? (
        <DashboardContent
          device={selectedDevice}
          devices={devices}
          bundle={bundleQuery.data}
          refreshing={bundleQuery.isFetching}
          onRefresh={() => void bundleQuery.refetch()}
        />
      ) : null}
    </AppShell>
  );
}

export default function App() {
  const queryClient = useQueryClient();
  const sessionQuery = useQuery({
    queryKey: ["session"],
    queryFn: getSession,
    enabled: !isMockDataSource,
    retry: false,
    staleTime: 60_000,
  });
  const loginMutation = useMutation({
    mutationFn: ({ username, password }: { username: string; password: string }) => login(username, password),
    onSuccess: (session) => {
      queryClient.setQueryData(["session"], { authenticated: true, username: session.username, role: session.role });
      void queryClient.invalidateQueries({ queryKey: ["devices"] });
    },
  });
  const logoutMutation = useMutation({
    mutationFn: logout,
    onSuccess: () => {
      queryClient.removeQueries({ queryKey: ["devices"] });
      queryClient.removeQueries({ queryKey: ["device-bundle"] });
      queryClient.setQueryData(["session"], { authenticated: false });
    },
  });

  useEffect(() => {
    const handleUnauthorized = () => queryClient.setQueryData(["session"], { authenticated: false });
    window.addEventListener("thermoride:unauthorized", handleUnauthorized);
    return () => window.removeEventListener("thermoride:unauthorized", handleUnauthorized);
  }, [queryClient]);

  if (isMockDataSource) return <Dashboard username="Mock Operator" onLogout={() => undefined} logoutPending={false} />;
  if (sessionQuery.isPending) return <AuthLoadingScreen />;
  if (sessionQuery.isError) return <LoginScreen loading={false} error="The API is unavailable. Confirm the FastAPI service is running, then retry." serviceUnavailable onLogin={() => undefined} onRetry={() => void sessionQuery.refetch()} />;
  if (!sessionQuery.data.authenticated) {
    return (
      <LoginScreen
        loading={loginMutation.isPending}
        error={loginMutation.isError ? "The username or password is incorrect." : null}
        onLogin={(username, password) => loginMutation.mutate({ username, password })}
      />
    );
  }
  return (
    <Dashboard
      username={sessionQuery.data.username ?? "Administrator"}
      onLogout={() => logoutMutation.mutate()}
      logoutPending={logoutMutation.isPending}
    />
  );
}

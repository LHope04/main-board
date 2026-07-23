import { DeviceSidebar } from "@/components/dashboard/DeviceSidebar";
import { IconSidebar } from "@/components/dashboard/IconSidebar";
import { TopNavigation } from "@/components/dashboard/TopNavigation";
import type { CoolingDevice } from "@/types/telemetry";

interface AppShellProps {
  devices: CoolingDevice[];
  devicesLoading: boolean;
  children: React.ReactNode;
  username: string;
  onLogout: () => void;
  logoutPending: boolean;
}

export function AppShell({ devices, devicesLoading, children, username, onLogout, logoutPending }: AppShellProps) {
  return (
    <div className="grid h-[100dvh] min-h-[720px] grid-cols-[68px_240px_minmax(0,1fr)] grid-rows-[68px_minmax(0,1fr)] gap-3.5 p-3.5">
      <IconSidebar />
      <TopNavigation username={username} onLogout={onLogout} logoutPending={logoutPending} />
      <DeviceSidebar devices={devices} loading={devicesLoading} />
      <main className="soft-scrollbar col-[3] row-[2] min-w-0 overflow-y-auto pr-1" aria-label="Cooling system dashboard">
        {children}
      </main>
    </div>
  );
}

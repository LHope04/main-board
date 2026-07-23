import { PackageOpen, Search } from "lucide-react";
import { DeviceListItem } from "@/components/dashboard/DeviceListItem";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Skeleton } from "@/components/ui/skeleton";
import { cn } from "@/lib/utils";
import { useDashboardStore } from "@/stores/dashboard-store";
import type { CoolingDevice } from "@/types/telemetry";

interface DeviceSidebarProps {
  devices: CoolingDevice[];
  loading: boolean;
}

const filters = ["all", "online", "offline"] as const;

export function DeviceSidebar({ devices, loading }: DeviceSidebarProps) {
  const selectedDeviceId = useDashboardStore((state) => state.selectedDeviceId);
  const setSelectedDeviceId = useDashboardStore((state) => state.setSelectedDeviceId);
  const deviceFilter = useDashboardStore((state) => state.deviceFilter);
  const setDeviceFilter = useDashboardStore((state) => state.setDeviceFilter);
  const searchQuery = useDashboardStore((state) => state.searchQuery);
  const setSearchQuery = useDashboardStore((state) => state.setSearchQuery);

  const visibleDevices = devices.filter((device) => {
    const matchesFilter = deviceFilter === "all" || device.status === deviceFilter;
    const query = searchQuery.trim().toLowerCase();
    const matchesSearch = !query || device.sn.toLowerCase().includes(query) || device.name.toLowerCase().includes(query);
    return matchesFilter && matchesSearch;
  });
  const onlineCount = devices.filter((device) => device.status === "online").length;
  const availability = devices.length > 0 ? (onlineCount / devices.length) * 100 : 0;

  return (
    <aside className="col-[2] row-[2] flex min-h-0 w-[240px] flex-col rounded-[20px] border border-white/80 bg-white/55 p-3.5 shadow-[0_12px_34px_rgba(44,56,49,0.035)] backdrop-blur-xl">
      <div className="flex items-end justify-between px-1.5 pt-1">
        <div>
          <h2 className="m-0 text-[16px] font-semibold tracking-[-0.02em]">Devices</h2>
          <p className="mt-1 text-[11px] text-[var(--muted)]">{devices.length} registered units</p>
        </div>
        <span className="rounded-full bg-black/[0.045] px-2 py-1 text-[10px] font-medium text-[var(--muted)]">{devices.length}</span>
      </div>

      <label className="relative mt-4 block">
        <span className="sr-only">Search SN or device</span>
        <Search className="absolute left-3 top-1/2 -translate-y-1/2 text-[#9ba09c]" size={14} />
        <Input
          value={searchQuery}
          onChange={(event) => setSearchQuery(event.target.value)}
          placeholder="Search SN or device"
          className="h-9 bg-white/85 pl-8 text-xs"
        />
      </label>

      <div className="mt-3 grid grid-cols-3 gap-1 rounded-[12px] bg-black/[0.04] p-1">
        {filters.map((filter) => (
          <Button
            key={filter}
            type="button"
            size="sm"
            variant="ghost"
            onClick={() => setDeviceFilter(filter)}
            className={cn("h-7 rounded-[8px] px-1 text-[10px] capitalize", deviceFilter === filter && "bg-white text-[var(--text)] shadow-[0_1px_4px_rgba(16,18,17,0.08)] hover:bg-white")}
          >
            {filter}
          </Button>
        ))}
      </div>

      <div className="soft-scrollbar mt-3 min-h-0 flex-1 space-y-1 overflow-y-auto pr-1">
        {loading ? Array.from({ length: 6 }, (_, index) => <Skeleton key={index} className="h-[91px] w-full rounded-[16px]" />) : null}
        {!loading && visibleDevices.map((device) => (
          <DeviceListItem
            key={device.id}
            device={device}
            selected={device.id === selectedDeviceId}
            onSelect={() => setSelectedDeviceId(device.id)}
          />
        ))}
        {!loading && visibleDevices.length === 0 ? (
          <div className="grid place-items-center px-3 py-12 text-center">
            <span className="grid size-10 place-items-center rounded-[13px] bg-black/[0.04] text-[var(--muted)]"><PackageOpen size={18} /></span>
            <p className="mb-0 mt-3 text-xs font-medium">No devices found</p>
            <p className="mt-1 text-[10px] leading-4 text-[var(--faint)]">Adjust the search or status filter.</p>
          </div>
        ) : null}
      </div>

      <div className="mt-3 rounded-[14px] border border-emerald-100 bg-emerald-50/75 px-3 py-2.5">
        <div className="flex items-center justify-between text-[10px] text-emerald-800">
          <span>Fleet availability</span>
          <strong>{availability.toFixed(1)}%</strong>
        </div>
        <div className="mt-2 h-1 overflow-hidden rounded-full bg-emerald-100"><div className="h-full rounded-full bg-[var(--accent)]" style={{ width: `${availability}%` }} /></div>
      </div>
    </aside>
  );
}

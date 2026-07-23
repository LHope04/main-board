import { ChevronRight } from "lucide-react";
import { StatusBadge } from "@/components/dashboard/StatusBadge";
import { cn, formatRelativeSeconds } from "@/lib/utils";
import type { CoolingDevice } from "@/types/telemetry";

interface DeviceListItemProps {
  device: CoolingDevice;
  selected: boolean;
  onSelect: () => void;
}

export function DeviceListItem({ device, selected, onSelect }: DeviceListItemProps) {
  return (
    <button
      type="button"
      aria-pressed={selected}
      onClick={onSelect}
      className={cn(
        "group relative w-full overflow-hidden rounded-[16px] border p-3.5 text-left transition duration-200 active:scale-[0.995]",
        selected
          ? "border-emerald-200/70 bg-[#eafff5] shadow-[0_8px_18px_rgba(39,111,78,0.08)]"
          : "border-transparent bg-transparent hover:border-[var(--border)] hover:bg-white/75",
      )}
    >
      {selected ? <span className="absolute inset-y-3 left-0 w-[3px] rounded-r-full bg-[var(--accent)]" /> : null}
      <div className="flex items-start gap-2.5">
        <span className={cn("mt-1.5 size-2 rounded-full", {
          "bg-[var(--accent)]": device.status === "online",
          "bg-[var(--warning)]": device.status === "delayed",
          "bg-[#a9adaa]": device.status === "offline",
          "bg-[var(--fault)]": device.status === "fault",
        })} />
        <div className="min-w-0 flex-1">
          <div className="truncate text-[12px] font-semibold text-[var(--text)]">{device.sn}</div>
          <div className="mt-1 truncate text-[11px] text-[var(--muted)]">{device.name}</div>
          <div className="mt-2 flex items-center justify-between gap-2">
            <StatusBadge status={device.status} compact />
            <span className="truncate text-[10px] text-[var(--faint)]">{formatRelativeSeconds(device.lastReportSeconds)}</span>
          </div>
        </div>
        <ChevronRight size={14} className={cn("mt-1 text-[#c3c7c4] transition", selected && "translate-x-0.5 text-emerald-600")} />
      </div>
    </button>
  );
}

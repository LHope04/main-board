import { Fan, Gauge, Power, RotateCw, Waves } from "lucide-react";
import { StatusBadge } from "@/components/dashboard/StatusBadge";
import { cn } from "@/lib/utils";
import type { ActuatorOutput } from "@/types/telemetry";

const icons = {
  boost: Gauge,
  load: Power,
  fan: Fan,
  pump: Waves,
  compressor: RotateCw,
};

interface ActuatorStatusItemProps {
  output: ActuatorOutput;
}

export function ActuatorStatusItem({ output }: ActuatorStatusItemProps) {
  const Icon = icons[output.id];
  return (
    <div className="group flex min-w-0 items-center gap-3 rounded-[15px] border border-[var(--border)] bg-[var(--surface-muted)]/75 p-3 transition duration-200 hover:border-[#d3d9d5] hover:bg-white">
      <span className={cn("grid size-9 shrink-0 place-items-center rounded-[11px]", {
        "bg-emerald-100 text-emerald-700": output.status === "active",
        "bg-zinc-200 text-zinc-500": output.status === "off",
        "bg-orange-100 text-orange-700": output.status === "warning",
        "bg-red-100 text-red-600": output.status === "fault",
        "bg-zinc-100 text-zinc-400": output.status === "unknown",
      })}><Icon size={16} strokeWidth={1.8} /></span>
      <div className="min-w-0 flex-1">
        <div className="truncate text-[10px] font-semibold tracking-[0.045em] text-[var(--text)]">{output.name}</div>
        <div className="mt-1 truncate text-[9px] text-[var(--faint)]">{output.description}</div>
      </div>
      <StatusBadge status={output.status} compact />
    </div>
  );
}

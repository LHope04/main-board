import { CalendarDays, RefreshCw } from "lucide-react";
import { Button } from "@/components/ui/button";
import { formatClock } from "@/lib/utils";

interface DashboardHeaderProps {
  lastUpdated: Date | null;
  refreshing: boolean;
  onRefresh: () => void;
}

export function DashboardHeader({ lastUpdated, refreshing, onRefresh }: DashboardHeaderProps) {
  return (
    <div className="flex items-end justify-between gap-5">
      <div>
        <h1 className="m-0 text-[27px] font-semibold tracking-[-0.04em] text-[var(--text)]">Cooling System Overview</h1>
        <p className="mb-0 mt-1.5 text-[12px] text-[var(--muted)]">Real-time telemetry and vehicle cooling system status</p>
      </div>
      <div className="flex items-center gap-2.5">
        <Button variant="outline" size="sm" className="h-9 bg-white/80 text-xs">
          <CalendarDays size={14} />
          Last 24 hours
        </Button>
        <div className="text-right">
          <div className="text-[10px] text-[var(--faint)]">Last updated</div>
          <div className="mt-0.5 text-[11px] font-medium text-[var(--muted)]">{lastUpdated ? `Today, ${formatClock(lastUpdated)}` : "No telemetry yet"}</div>
        </div>
        <Button aria-label="Refresh telemetry" variant="outline" size="iconSm" onClick={onRefresh} className="size-9 bg-white/80">
          <RefreshCw size={14} className={refreshing ? "animate-spin" : undefined} />
        </Button>
      </div>
    </div>
  );
}

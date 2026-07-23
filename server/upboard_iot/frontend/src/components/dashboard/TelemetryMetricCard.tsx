import type { LucideIcon } from "lucide-react";
import { cn } from "@/lib/utils";

interface TelemetryMetricCardProps {
  title: string;
  value: string;
  status: string;
  icon: LucideIcon;
  secondary?: string;
  sparkline?: number[];
  accent?: "green" | "purple" | "orange";
  className?: string;
}

function Sparkline({ values, accent = "green" }: { values: number[]; accent?: "green" | "purple" | "orange" }) {
  if (values.length === 0) return null;
  const min = Math.min(...values);
  const max = Math.max(...values);
  const range = max - min || 1;
  const points = values.map((value, index) => {
    const x = (index / Math.max(1, values.length - 1)) * 100;
    const y = 28 - ((value - min) / range) * 22;
    return `${x},${y}`;
  }).join(" ");
  const color = accent === "purple" ? "#B55CFF" : accent === "orange" ? "#FF9E4A" : "#28E29A";

  return (
    <svg viewBox="0 0 100 32" className="h-9 w-full overflow-visible" role="img" aria-label="Recent trend">
      <polyline points={points} fill="none" stroke={color} strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round" vectorEffect="non-scaling-stroke" />
    </svg>
  );
}

export function TelemetryMetricCard({ title, value, status, icon: Icon, secondary, sparkline, accent = "green", className }: TelemetryMetricCardProps) {
  return (
    <article className={cn("dashboard-card flex min-h-[142px] flex-col p-4 transition duration-200 hover:-translate-y-0.5", className)}>
      <div className="flex items-start justify-between gap-3">
        <div>
          <div className="text-[10px] font-medium text-[var(--muted)]">{title}</div>
          <div className="mt-2 text-[22px] font-semibold tracking-[-0.045em] text-[var(--text)]">{value}</div>
        </div>
        <span className="grid size-8 place-items-center rounded-[10px] bg-black/[0.035] text-[var(--muted)]"><Icon size={15} /></span>
      </div>
      <div className="mt-auto flex items-end justify-between gap-4">
        <div>
          <div className={cn("text-[10px] font-semibold", accent === "orange" ? "text-orange-600" : accent === "purple" ? "text-purple-600" : "text-emerald-700")}>{status}</div>
          {secondary ? <div className="mt-1 text-[10px] text-[var(--faint)]">{secondary}</div> : null}
        </div>
        {sparkline && sparkline.length > 0 ? <div className="w-[88px]"><Sparkline values={sparkline} accent={accent} /></div> : null}
      </div>
    </article>
  );
}

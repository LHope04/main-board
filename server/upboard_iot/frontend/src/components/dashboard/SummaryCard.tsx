import type { LucideIcon } from "lucide-react";
import { cn } from "@/lib/utils";

interface SummaryCardProps {
  label: string;
  value: string;
  description: string;
  icon: LucideIcon;
  dark?: boolean;
  progress?: number;
  trailing?: React.ReactNode;
}

export function SummaryCard({ label, value, description, icon: Icon, dark = false, progress, trailing }: SummaryCardProps) {
  return (
    <article className={cn("min-h-[126px] p-4.5 transition duration-200 hover:-translate-y-0.5", dark ? "dashboard-card-dark" : "dashboard-card")}>
      <div className="relative z-[1] flex h-full flex-col">
        <div className="flex items-start justify-between gap-3">
          <span className={cn("text-[11px] font-medium", dark ? "text-white/50" : "text-[var(--muted)]")}>{label}</span>
          <span className={cn("grid size-8 place-items-center rounded-[10px]", dark ? "bg-white/8 text-[var(--accent)]" : "bg-black/[0.035] text-[var(--muted)]")}><Icon size={15} /></span>
        </div>
        <div className={cn("mt-2 truncate text-[25px] font-semibold tracking-[-0.04em]", dark ? "text-white" : "text-[var(--text)]")}>{value}</div>
        <div className="mt-auto flex items-center justify-between gap-3">
          <span className={cn("truncate text-[10px]", dark ? "text-white/45" : "text-[var(--muted)]")}>{description}</span>
          {trailing}
        </div>
        {typeof progress === "number" ? (
          <div className="mt-3 h-1.5 overflow-hidden rounded-full bg-emerald-100">
            <div className="h-full rounded-full bg-[var(--accent)] transition-[width] duration-700" style={{ width: `${progress}%` }} />
          </div>
        ) : null}
      </div>
    </article>
  );
}

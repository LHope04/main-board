import { cn } from "@/lib/utils";

interface LiveIndicatorProps {
  label?: string;
  muted?: boolean;
}

export function LiveIndicator({ label = "Live", muted = false }: LiveIndicatorProps) {
  return (
    <span className={cn("inline-flex items-center gap-2 text-xs font-medium", muted ? "text-white/55" : "text-emerald-700")}>
      <span className={cn("size-2 rounded-full", muted ? "bg-white/40" : "live-dot bg-[var(--accent)]")} />
      {label}
    </span>
  );
}

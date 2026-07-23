import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";

interface TimeRangeSelectorProps {
  options: readonly string[];
  value: string;
  onChange: (value: string) => void;
  dark?: boolean;
  compact?: boolean;
}

export function TimeRangeSelector({ options, value, onChange, dark = false, compact = false }: TimeRangeSelectorProps) {
  return (
    <div className={cn("inline-flex items-center gap-1 rounded-[12px] p-1", dark ? "bg-white/[0.07]" : "bg-black/[0.045]")}>
      {options.map((option) => (
        <Button
          key={option}
          type="button"
          size="sm"
          variant="ghost"
          onClick={() => onChange(option)}
          className={cn(
            compact ? "h-7 px-2.5 text-[11px]" : "h-8 px-3 text-xs",
            dark
              ? value === option
                ? "bg-white text-[#111412] hover:bg-white"
                : "text-white/55 hover:bg-white/8 hover:text-white"
              : value === option
                ? "bg-white text-[var(--text)] shadow-[0_1px_4px_rgba(16,18,17,0.08)] hover:bg-white"
                : "text-[var(--muted)]",
          )}
        >
          {option}
        </Button>
      ))}
    </div>
  );
}

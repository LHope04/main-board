import { ChevronDown, Thermometer } from "lucide-react";
import { cn, formatRelativeSeconds } from "@/lib/utils";
import type { NtcChannel } from "@/types/telemetry";

interface NtcSensorRowProps {
  channel: NtcChannel;
  expanded: boolean;
  mode: "raw" | "temperature";
  onToggle: () => void;
}

function MiniHistory({ channel }: { channel: NtcChannel }) {
  const values = channel.history.map((point) => point.raw);
  if (values.length === 0) return <div className="mt-3 rounded-[14px] bg-[#f5f8f5] p-4 text-[10px] text-[var(--muted)]">No raw ADC history is available for this channel.</div>;
  const min = Math.min(...values);
  const max = Math.max(...values);
  const range = max - min || 1;
  const points = values.map((value, index) => {
    const x = (index / Math.max(1, values.length - 1)) * 100;
    const y = 44 - ((value - min) / range) * 34;
    return `${x},${y}`;
  }).join(" ");
  return (
    <div className="mt-3 rounded-[14px] bg-[#f5f8f5] p-3">
      <div className="flex items-center justify-between text-[9px] text-[var(--faint)]"><span>Recent raw ADC trend</span><span>{min} - {max}</span></div>
      <svg viewBox="0 0 100 48" preserveAspectRatio="none" className="mt-2 h-[64px] w-full" role="img" aria-label={`NTC ${channel.id} raw ADC history`}>
        <defs>
          <linearGradient id={`ntc-fill-${channel.id}`} x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor="#28e29a" stopOpacity="0.22" />
            <stop offset="1" stopColor="#28e29a" stopOpacity="0" />
          </linearGradient>
        </defs>
        <polygon points={`0,48 ${points} 100,48`} fill={`url(#ntc-fill-${channel.id})`} />
        <polyline points={points} fill="none" stroke="#23c984" strokeWidth="1.7" vectorEffect="non-scaling-stroke" />
      </svg>
    </div>
  );
}

export function NtcSensorRow({ channel, expanded, mode, onToggle }: NtcSensorRowProps) {
  const percent = channel.raw == null ? 0 : Math.max(0, Math.min(100, (channel.raw / 4095) * 100));
  const displayValue = mode === "raw" ? channel.raw?.toString() ?? "--" : channel.temperatureC == null ? "--" : `${channel.temperatureC.toFixed(1)}°C`;

  return (
    <button type="button" onClick={onToggle} className="w-full rounded-[15px] border border-transparent bg-[var(--surface-muted)]/70 p-3 text-left transition hover:border-[var(--border)] hover:bg-white">
      <div className="grid grid-cols-[38px_minmax(145px,1.35fr)_72px_86px_minmax(120px,1fr)_92px_18px] items-center gap-3">
        <span className="grid size-8 place-items-center rounded-[10px] bg-white text-[var(--muted)] shadow-[0_1px_3px_rgba(16,18,17,0.06)]"><Thermometer size={14} /></span>
        <div className="min-w-0">
          <div className="truncate text-[11px] font-semibold">NTC {channel.id}</div>
          <div className="mt-0.5 truncate text-[10px] text-[var(--muted)]">{channel.name}</div>
        </div>
        <div>
          <div className="text-[9px] text-[var(--faint)]">{mode === "raw" ? "Raw ADC" : "Temperature"}</div>
          <div className="mt-0.5 text-[12px] font-semibold">{displayValue}</div>
        </div>
        <div>
          <div className="text-[9px] text-[var(--faint)]">Reserved °C</div>
          <div className="mt-0.5 text-[11px] text-[var(--muted)]">Not calibrated</div>
        </div>
        <div>
          <div className="relative h-2 rounded-full bg-[#e3e7e3]">
            <div className="absolute inset-y-0 left-0 rounded-full bg-emerald-300/55" style={{ width: `${percent}%` }} />
            <span className="absolute top-1/2 size-3 -translate-y-1/2 rounded-full border-2 border-white bg-[var(--accent)] shadow-sm" style={{ left: `calc(${percent}% - 6px)` }} />
          </div>
          <div className="mt-1 flex justify-between text-[8px] text-[var(--faint)]"><span>0</span><span>4095</span></div>
        </div>
        <div className="text-right">
          <div className={cn("inline-flex items-center gap-1.5 text-[10px] font-medium", channel.status === "normal" ? "text-emerald-700" : channel.status === "warning" ? "text-orange-700" : "text-red-600")}><span className="size-1.5 rounded-full bg-current" />{channel.status === "normal" ? "Normal" : channel.status === "warning" ? "Warning" : "Fault"}</div>
          <div className="mt-1 text-[9px] text-[var(--faint)]">{formatRelativeSeconds(channel.updatedSeconds)}</div>
        </div>
        <ChevronDown size={14} className={cn("text-[var(--faint)] transition", expanded && "rotate-180 text-emerald-700")} />
      </div>
      {expanded ? <MiniHistory channel={channel} /> : null}
    </button>
  );
}

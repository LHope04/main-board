import { Activity, Check, Cpu, MapPin, Network, Zap } from "lucide-react";
import { cn } from "@/lib/utils";
import type { DeviceStatus, TelemetryData } from "@/types/telemetry";

interface SystemHealthCardProps {
  telemetry: TelemetryData;
  deviceStatus: DeviceStatus;
}

function Gauge({ value, label }: { value: number; label: string }) {
  const degrees = Math.max(0, Math.min(100, value)) * 1.8;
  return (
    <div className="relative h-[74px] w-[124px] shrink-0 overflow-hidden">
      <div className="absolute left-0 top-0 h-[124px] w-[124px] rounded-full bg-[conic-gradient(from_270deg,var(--accent)_0deg,var(--accent)_var(--gauge),rgba(255,255,255,0.09)_var(--gauge),rgba(255,255,255,0.09)_180deg,transparent_180deg)]" style={{ "--gauge": `${degrees}deg` } as React.CSSProperties}>
        <div className="absolute inset-[11px] rounded-full bg-[#111713]" />
      </div>
      <div className="absolute inset-x-0 bottom-0 text-center">
        <div className="text-[31px] font-semibold leading-none tracking-[-0.06em]">{value}%</div>
        <div className="mt-1.5 text-[9px] font-semibold tracking-[0.14em] text-[var(--accent)]">{label}</div>
      </div>
    </div>
  );
}

export function SystemHealthCard({ telemetry, deviceStatus }: SystemHealthCardProps) {
  const copy = deviceStatus === "fault"
    ? { title: "System requires attention", description: "A critical actuator fault is present in the latest telemetry.", badge: "Fault", gauge: "FAULT", badgeClass: "border-red-400/20 bg-red-400/10 text-red-200" }
    : deviceStatus === "offline"
      ? { title: "Live health unavailable", description: "The unit is offline. Values below are the last known telemetry.", badge: "Offline", gauge: "OFFLINE", badgeClass: "border-white/10 bg-white/7 text-white/55" }
      : deviceStatus === "delayed"
        ? { title: "Telemetry is delayed", description: "The last report is outside the normal live update window.", badge: "Delayed", gauge: "DELAYED", badgeClass: "border-orange-400/20 bg-orange-400/10 text-orange-200" }
        : { title: "System operating normally", description: "All critical cooling functions are within the expected range.", badge: "Healthy", gauge: "NORMAL", badgeClass: "border-emerald-400/20 bg-emerald-400/10 text-emerald-200" };
  const states = [
    { label: "Cooling Load", value: telemetry.coolingLoadPercent == null ? "Unavailable" : `${telemetry.coolingLoadPercent}%`, icon: Activity, tone: telemetry.coolingLoadPercent == null ? "muted" : "normal" },
    { label: "Power", value: telemetry.busVoltageV == null ? "Unavailable" : telemetry.busVoltageV >= 24 && telemetry.busVoltageV <= 24.9 ? "Stable" : "Check", icon: Zap, tone: telemetry.busVoltageV == null ? "muted" : telemetry.busVoltageV >= 24 && telemetry.busVoltageV <= 24.9 ? "normal" : "warning" },
    { label: "Network", value: telemetry.rssiDbm == null ? "Unavailable" : telemetry.rssiDbm >= -85 ? "Good" : "Weak", icon: Network, tone: telemetry.rssiDbm == null ? "muted" : telemetry.rssiDbm >= -85 ? "normal" : "warning" },
    { label: "GPS", value: telemetry.gps.valid ? "Valid" : "Last known", icon: MapPin, tone: telemetry.gps.valid ? "normal" : "warning" },
  ];

  const indicatorClass = (tone: string) => {
    if (deviceStatus === "offline") return "bg-white/25";
    if (deviceStatus === "fault") return "bg-red-400";
    if (deviceStatus === "delayed" || tone === "warning") return "bg-orange-400";
    if (tone === "muted") return "bg-white/25";
    return "bg-[var(--accent)]";
  };

  return (
    <article className="dashboard-card-dark min-h-[305px] p-5.5">
      <div className="relative z-[1] flex h-full flex-col">
        <div className="flex items-start justify-between gap-3">
          <div>
            <div className="flex items-center gap-2 text-[11px] text-white/45"><Cpu size={14} /> System Health</div>
            <h2 className="mb-0 mt-2 text-[21px] font-semibold tracking-[-0.03em]">{copy.title}</h2>
            <p className="mb-0 mt-1.5 text-[11px] text-white/42">{copy.description}</p>
          </div>
          <span className={cn("inline-flex items-center gap-1.5 rounded-full border px-2.5 py-1 text-[10px] font-semibold", copy.badgeClass)}><Check size={11} /> {copy.badge}</span>
        </div>

        <div className="mt-3 grid flex-1 grid-cols-[124px_minmax(0,1fr)] items-end gap-3">
          <Gauge value={telemetry.healthScore} label={copy.gauge} />
          <div className="grid min-w-0 grid-cols-2 gap-2">
            {states.map(({ label, value, icon: Icon, tone }) => (
              <div key={label} className={cn("min-w-0 rounded-[14px] border border-white/7 bg-white/[0.055] p-2.5 backdrop-blur-sm transition hover:bg-white/[0.075]")}>
                <div className="flex items-center justify-between text-white/38"><Icon size={13} /><span className={cn("size-1.5 rounded-full", indicatorClass(tone))} /></div>
                <div className="mt-1.5 truncate text-[10px] text-white/42">{label}</div>
                <div className="mt-0.5 truncate text-[12px] font-medium text-white/90">{value}</div>
              </div>
            ))}
          </div>
        </div>
      </div>
    </article>
  );
}

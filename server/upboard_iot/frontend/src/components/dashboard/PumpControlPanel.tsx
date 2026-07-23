import { useEffect, useState, type CSSProperties } from "react";
import { useMutation, useQueryClient } from "@tanstack/react-query";
import { Check, Droplets, Gauge, LoaderCircle, Power, TriangleAlert } from "lucide-react";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import { setPumpDuty } from "@/services/api-client";
import { isMockDataSource, telemetryQueryKeys } from "@/services/telemetry";
import { useDashboardStore } from "@/stores/dashboard-store";
import type { CoolingDevice, OutputStatus } from "@/types/telemetry";

const PRESETS = [0, 30, 50, 70, 100] as const;

interface PumpControlPanelProps {
  device: CoolingDevice;
  reportedDutyPercent: number | null;
  pumpStatus: OutputStatus;
}

export function PumpControlPanel({ device, reportedDutyPercent, pumpStatus }: PumpControlPanelProps) {
  const queryClient = useQueryClient();
  const demoMode = useDashboardStore((state) => state.demoMode);
  const [draftDuty, setDraftDuty] = useState(reportedDutyPercent ?? 0);
  const [lastRequestedDuty, setLastRequestedDuty] = useState<number | null>(null);
  const unavailable = device.status === "offline";

  useEffect(() => {
    setDraftDuty(reportedDutyPercent ?? 0);
    setLastRequestedDuty(null);
  }, [device.id]);

  useEffect(() => {
    if (lastRequestedDuty == null && reportedDutyPercent != null) {
      setDraftDuty(reportedDutyPercent);
    }
  }, [lastRequestedDuty, reportedDutyPercent]);

  const mutation = useMutation({
    mutationFn: async (dutyPct: number) => {
      if (isMockDataSource) {
        await new Promise<void>((resolve) => window.setTimeout(resolve, 450));
        return { ok: true, duty_pct: dutyPct };
      }
      return setPumpDuty(device.id, dutyPct);
    },
    onSuccess: (_, dutyPct) => {
      setLastRequestedDuty(dutyPct);
      void queryClient.invalidateQueries({ queryKey: telemetryQueryKeys.bundle(device.id, demoMode) });
    },
  });

  const confirmed = lastRequestedDuty != null && reportedDutyPercent === lastRequestedDuty;
  const reportedLabel = reportedDutyPercent == null ? "Unavailable" : `${reportedDutyPercent}%`;
  const draftLabel = draftDuty === 0 ? "Off" : `${draftDuty}%`;
  const statusText = mutation.isPending
    ? "Sending command to the selected unit"
    : mutation.isError
      ? mutation.error instanceof Error ? mutation.error.message : "The command could not be sent"
      : confirmed
        ? `Device confirmed ${reportedDutyPercent}% output`
        : lastRequestedDuty != null
          ? `Command accepted. Waiting for telemetry at ${lastRequestedDuty}%`
          : unavailable
            ? "The device is offline. Controls are disabled"
            : "Choose a duty cycle, then apply the command";

  const StatusIcon = mutation.isPending
    ? LoaderCircle
    : mutation.isError
      ? TriangleAlert
      : confirmed
        ? Check
        : Gauge;

  return (
    <article className="dashboard-card p-4.5" aria-labelledby="pump-control-title">
      <div className="flex items-start justify-between gap-3">
        <div>
          <h2 id="pump-control-title" className="m-0 text-[16px] font-semibold tracking-[-0.025em]">Pump Flow Control</h2>
          <p className="mb-0 mt-1 text-[10px] text-[var(--muted)]">Remote 100 Hz PWM adjustment for the selected cooling unit</p>
        </div>
        <span className="grid size-8 place-items-center rounded-[10px] bg-emerald-50 text-emerald-700"><Droplets size={15} /></span>
      </div>

      <div className="mt-4 grid gap-4 xl:grid-cols-[220px_minmax(0,1fr)_190px] xl:items-center">
        <div className="rounded-[16px] border border-[var(--border)] bg-[var(--surface-muted)]/75 p-4">
          <div className="text-[10px] font-medium text-[var(--muted)]">Reported output</div>
          <div className="mt-1 flex items-end gap-2">
            <strong className="text-[32px] font-semibold tracking-[-0.045em] text-[var(--ink)]">{reportedLabel}</strong>
            <span className={cn("mb-1.5 rounded-[7px] px-2 py-0.5 text-[9px] font-semibold", {
              "bg-emerald-100 text-emerald-700": pumpStatus === "active",
              "bg-zinc-200 text-zinc-600": pumpStatus === "off" || pumpStatus === "unknown",
              "bg-orange-100 text-orange-700": pumpStatus === "warning",
              "bg-red-100 text-red-600": pumpStatus === "fault",
            })}>{pumpStatus === "active" ? "Running" : pumpStatus === "off" ? "Stopped" : "Unknown"}</span>
          </div>
          <div className="mt-2 text-[9px] text-[var(--faint)]">Latest value returned by device telemetry</div>
        </div>

        <div className="min-w-0">
          <div className="flex items-center justify-between gap-3">
            <label htmlFor="pump-duty" className="text-[11px] font-semibold text-[var(--text)]">Target duty cycle</label>
            <strong className="text-[18px] font-semibold tracking-[-0.03em] text-[var(--ink)]">{draftLabel}</strong>
          </div>
          <input
            id="pump-duty"
            aria-label="Pump duty cycle"
            className="pump-range mt-3 w-full"
            type="range"
            min="0"
            max="100"
            step="5"
            value={draftDuty}
            disabled={unavailable || mutation.isPending}
            onChange={(event) => setDraftDuty(Number(event.target.value))}
            style={{ "--pump-fill": `${draftDuty}%` } as CSSProperties}
          />
          <div className="mt-3 grid grid-cols-5 gap-1.5" aria-label="Pump duty presets">
            {PRESETS.map((preset) => (
              <button
                key={preset}
                type="button"
                disabled={unavailable || mutation.isPending}
                onClick={() => setDraftDuty(preset)}
                className={cn("h-8 rounded-[9px] border text-[10px] font-semibold transition active:translate-y-px disabled:cursor-not-allowed disabled:opacity-45", {
                  "border-[var(--ink)] bg-[var(--ink)] text-white": draftDuty === preset,
                  "border-[var(--border)] bg-white text-[var(--muted)] hover:bg-[var(--surface-muted)] hover:text-[var(--text)]": draftDuty !== preset,
                })}
              >
                {preset === 0 ? "Off" : `${preset}%`}
              </button>
            ))}
          </div>
        </div>

        <div className="flex flex-col gap-2.5">
          <Button
            variant="accent"
            disabled={unavailable || mutation.isPending || draftDuty === reportedDutyPercent}
            onClick={() => mutation.mutate(draftDuty)}
          >
            {mutation.isPending ? <LoaderCircle className="animate-spin" size={15} /> : draftDuty === 0 ? <Power size={15} /> : <Droplets size={15} />}
            {mutation.isPending ? "Sending" : draftDuty === 0 ? "Stop pump" : "Apply flow"}
          </Button>
          <div className={cn("flex min-h-9 items-start gap-2 rounded-[11px] px-3 py-2 text-[9px] leading-4", {
            "bg-red-50 text-red-700": mutation.isError,
            "bg-emerald-50 text-emerald-700": confirmed,
            "bg-[var(--surface-muted)] text-[var(--muted)]": !mutation.isError && !confirmed,
          })} aria-live="polite">
            <StatusIcon className={cn("mt-0.5 shrink-0", { "animate-spin": mutation.isPending })} size={12} />
            <span>{statusText}</span>
          </div>
        </div>
      </div>
    </article>
  );
}

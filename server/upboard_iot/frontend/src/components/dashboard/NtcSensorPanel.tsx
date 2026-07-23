import { useState } from "react";
import { SlidersHorizontal } from "lucide-react";
import { NtcSensorRow } from "@/components/dashboard/NtcSensorRow";
import { Tabs, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { useDashboardStore } from "@/stores/dashboard-store";
import type { NtcChannel } from "@/types/telemetry";

interface NtcSensorPanelProps {
  channels: NtcChannel[];
}

export function NtcSensorPanel({ channels }: NtcSensorPanelProps) {
  const [expandedId, setExpandedId] = useState<number | null>(null);
  const ntcMode = useDashboardStore((state) => state.ntcMode);
  const setNtcMode = useDashboardStore((state) => state.setNtcMode);

  return (
    <article className="dashboard-card p-4.5">
      <div className="flex items-start justify-between gap-4">
        <div>
          <h2 className="m-0 text-[16px] font-semibold tracking-[-0.025em]">NTC Sensor Channels</h2>
          <p className="mb-0 mt-1 text-[10px] text-[var(--muted)]">Eight-channel sampler input with raw and calibrated views</p>
        </div>
        <div className="flex items-center gap-2">
          <span className="grid size-8 place-items-center rounded-[10px] bg-black/[0.035] text-[var(--muted)]"><SlidersHorizontal size={14} /></span>
          <Tabs value={ntcMode} onValueChange={(value) => setNtcMode(value as "raw" | "temperature")}>
            <TabsList>
              <TabsTrigger value="raw">Raw ADC</TabsTrigger>
              <TabsTrigger value="temperature">Temperature</TabsTrigger>
            </TabsList>
          </Tabs>
        </div>
      </div>
      <div className="mt-4 space-y-2">
        {channels.map((channel) => (
          <NtcSensorRow
            key={channel.id}
            channel={channel}
            mode={ntcMode}
            expanded={expandedId === channel.id}
            onToggle={() => setExpandedId((value) => value === channel.id ? null : channel.id)}
          />
        ))}
      </div>
    </article>
  );
}

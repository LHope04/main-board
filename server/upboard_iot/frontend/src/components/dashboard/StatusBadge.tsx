import { Badge } from "@/components/ui/badge";
import { deviceStatusLabel, outputStatusLabel } from "@/lib/status";
import type { DeviceStatus, OutputStatus } from "@/types/telemetry";

interface StatusBadgeProps {
  status: DeviceStatus | OutputStatus;
  compact?: boolean;
}

const badgeVariant = {
  online: "online",
  active: "online",
  delayed: "delayed",
  warning: "delayed",
  offline: "offline",
  off: "offline",
  fault: "fault",
  unknown: "offline",
} as const;

export function StatusBadge({ status, compact = false }: StatusBadgeProps) {
  const label = status in deviceStatusLabel
    ? deviceStatusLabel[status as DeviceStatus]
    : outputStatusLabel[status as OutputStatus];

  return (
    <Badge variant={badgeVariant[status]} className={compact ? "px-2 py-0.5 text-[10px]" : undefined}>
      <span className="size-1.5 rounded-full bg-current opacity-80" aria-hidden="true" />
      {label}
    </Badge>
  );
}

import type { DeviceStatus, OutputStatus } from "@/types/telemetry";

export const deviceStatusLabel: Record<DeviceStatus, string> = {
  online: "Online",
  delayed: "Delayed",
  offline: "Offline",
  fault: "Fault",
};

export const outputStatusLabel: Record<OutputStatus, string> = {
  active: "Active",
  off: "Off",
  warning: "Warning",
  fault: "Fault",
  unknown: "Unknown",
};

export function deriveDeviceStatus(lastReportSeconds: number, explicitFault = false): DeviceStatus {
  if (explicitFault) return "fault";
  if (lastReportSeconds <= 10) return "online";
  if (lastReportSeconds <= 60) return "delayed";
  return "offline";
}

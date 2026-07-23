export type DeviceStatus = "online" | "delayed" | "offline" | "fault";
export type OutputStatus = "active" | "off" | "warning" | "fault" | "unknown";
export type ConnectionStatus = "connected" | "reconnecting" | "offline";
export type DemoMode = "normal" | "empty" | "error" | "reconnecting";

export interface CoolingDevice {
  id: string;
  sn: string;
  name: string;
  status: DeviceStatus;
  lastReportSeconds: number;
  locationLabel: string;
}

export interface GpsData {
  latitude: number | null;
  longitude: number | null;
  accuracyM: number | null;
  valid: boolean;
  updatedSeconds: number;
}

export interface ActuatorOutput {
  id: "boost" | "load" | "fan" | "pump" | "compressor";
  name: string;
  status: OutputStatus;
  description: string;
}

export interface NtcChannel {
  id: number;
  name: string;
  raw: number | null;
  temperatureC: number | null;
  status: "normal" | "warning" | "fault";
  updatedSeconds: number;
  history: Array<{ timestamp: string; raw: number }>;
}

export interface TelemetryData {
  deviceId: string;
  capturedAt: string | null;
  healthScore: number;
  healthStatus: "normal" | "warning" | "fault";
  coolingLoadPercent: number | null;
  rssiDbm: number | null;
  busVoltageV: number | null;
  busCurrentA: number | null;
  vehicleInputV: number | null;
  vehicleCharging: boolean | null;
  gps: GpsData;
  outputs: ActuatorOutput[];
  ntcChannels: NtcChannel[];
}

export interface TelemetryHistoryPoint {
  timestamp: string;
  voltageV: number;
  currentA: number | null;
}

export interface GpsTrackPoint {
  timestamp: string;
  latitude: number;
  longitude: number;
  valid: boolean;
}

export interface DeviceTelemetryBundle {
  telemetry: TelemetryData;
  voltageHistory: TelemetryHistoryPoint[];
  gpsTrack: GpsTrackPoint[];
  rssiHistory: number[];
  voltageSparkline: number[];
  routeStats: {
    distanceKm: number;
    durationMinutes: number;
    averageSpeedKmh: number;
  } | null;
}

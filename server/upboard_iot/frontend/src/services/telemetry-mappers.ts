import { deriveDeviceStatus } from "@/lib/status";
import type { ApiDeviceRow, ApiEventRow, ApiGpsRow, ApiTelemetryRow } from "@/types/api";
import type {
  ActuatorOutput,
  CoolingDevice,
  DeviceTelemetryBundle,
  DeviceStatus,
  GpsTrackPoint,
  NtcChannel,
  OutputStatus,
  TelemetryHistoryPoint,
} from "@/types/telemetry";

const COOLING_REFERENCE_POWER_W = 120;
const NTC_NAMES = [
  "Coolant Outlet",
  "Coolant Return",
  "Compressor Shell",
  "Evaporator",
  "Condenser",
  "Battery",
  "Power Board",
  "Ambient",
] as const;

function clamp(value: number, minimum: number, maximum: number) {
  return Math.min(maximum, Math.max(minimum, value));
}

function secondsSince(timestamp: string | null | undefined, now = Date.now()) {
  if (!timestamp) return Number.MAX_SAFE_INTEGER;
  const milliseconds = Date.parse(timestamp);
  if (!Number.isFinite(milliseconds)) return Number.MAX_SAFE_INTEGER;
  return Math.max(0, Math.round((now - milliseconds) / 1000));
}

function nestedNumber(object: Record<string, unknown> | null, ...path: string[]) {
  let value: unknown = object;
  for (const key of path) {
    if (!value || typeof value !== "object") return null;
    value = (value as Record<string, unknown>)[key];
  }
  return typeof value === "number" && Number.isFinite(value) ? value : null;
}

export function mapRssiToDbm(value: number | null) {
  if (value == null || value === 99) return null;
  if (value >= 0 && value <= 31) return value * 2 - 113;
  return value;
}

export function mapDevice(row: ApiDeviceRow, now = Date.now()): CoolingDevice {
  const lastReportSeconds = secondsSince(row.last_seen_at ?? row.latest_received_at, now);
  const hasCoordinates = row.latest_latitude != null && row.latest_longitude != null;
  return {
    id: row.sn,
    sn: row.sn,
    name: row.name || row.model || "Unlabeled cooling unit",
    status: deriveDeviceStatus(lastReportSeconds),
    lastReportSeconds,
    locationLabel: hasCoordinates
      ? `${row.latest_latitude!.toFixed(4)}, ${row.latest_longitude!.toFixed(4)}`
      : "No recent GPS",
  };
}

function mapOutputStatus(value: boolean | null): OutputStatus {
  if (value == null) return "unknown";
  return value ? "active" : "off";
}

function mapOutputs(row: ApiTelemetryRow | null, deviceStatus: DeviceStatus): ActuatorOutput[] {
  const definitions: Array<[ActuatorOutput["id"], string, keyof ApiTelemetryRow, string, string]> = [
    ["boost", "BOOST CONVERTER", "boost_on", "24V boost stage enabled", "24V boost stage disabled"],
    ["load", "LOAD OUTPUT", "load_on", "Main load rail energized", "Main load rail disabled"],
    ["fan", "CONDENSER FAN", "fan_on", "Fan command active", "Fan command inactive"],
    ["pump", "COOLANT PUMP", "pump_on", "Pump command active", "Pump command inactive"],
    ["compressor", "COMPRESSOR", "compressor_on", "Compressor command active", "Compressor command inactive"],
  ];
  return definitions.map(([id, name, field, onDescription, offDescription]) => {
    const value = row?.[field];
    const booleanValue = typeof value === "boolean" ? value : null;
    if (deviceStatus === "offline") {
      return {
        id,
        name,
        status: "unknown",
        description: booleanValue == null
          ? "State unavailable before disconnect"
          : `Last known: ${booleanValue ? "active" : "inactive"} before disconnect`,
      };
    }
    return {
      id,
      name,
      status: mapOutputStatus(booleanValue),
      description: booleanValue == null ? "State unavailable" : booleanValue ? onDescription : offDescription,
    };
  });
}

function mapNtcChannels(latest: ApiTelemetryRow | null, history: ApiTelemetryRow[]): NtcChannel[] {
  return Array.from({ length: 8 }, (_, index) => {
    const channelHistory = history.flatMap((row) => {
      const raw = row.ntc_raw?.[index];
      return typeof raw === "number" ? [{ timestamp: row.received_at, raw }] : [];
    });
    const raw = latest?.ntc_raw?.[index] ?? channelHistory.at(-1)?.raw ?? null;
    return {
      id: index + 1,
      name: NTC_NAMES[index] ?? `Channel ${index + 1}`,
      raw,
      temperatureC: null,
      status: "normal",
      updatedSeconds: secondsSince(latest?.received_at),
      history: channelHistory,
    };
  });
}

function deriveCoolingLoadPercent(row: ApiTelemetryRow | null) {
  if (row?.bat24_v == null || row.bat24_i == null) return null;
  const powerW = Math.abs(row.bat24_v * row.bat24_i);
  return Math.round(clamp((powerW / COOLING_REFERENCE_POWER_W) * 100, 0, 100));
}

function hasRecentFault(events: ApiEventRow[]) {
  const cutoff = Date.now() - 24 * 60 * 60 * 1000;
  return events.some((event) => Date.parse(event.received_at) >= cutoff && ["error", "critical", "fault"].includes(event.level.toLowerCase()));
}

function deriveHealthScore(status: DeviceStatus, latest: ApiTelemetryRow | null, gpsValid: boolean, fault: boolean) {
  let score = 100;
  if (status === "offline") score -= 60;
  else if (status === "delayed") score -= 20;
  if (fault) score -= 35;
  if (latest?.bat24_v != null) {
    if (latest.bat24_v < 23 || latest.bat24_v > 25.5) score -= 20;
    else if (latest.bat24_v < 24 || latest.bat24_v > 24.9) score -= 5;
  }
  if (!gpsValid) score -= 5;
  return Math.round(clamp(score, 0, 100));
}

function degreesToRadians(value: number) {
  return value * (Math.PI / 180);
}

function pointDistanceKm(from: GpsTrackPoint, to: GpsTrackPoint) {
  const earthRadiusKm = 6371;
  const latitudeDelta = degreesToRadians(to.latitude - from.latitude);
  const longitudeDelta = degreesToRadians(to.longitude - from.longitude);
  const latitude1 = degreesToRadians(from.latitude);
  const latitude2 = degreesToRadians(to.latitude);
  const haversine = Math.sin(latitudeDelta / 2) ** 2
    + Math.cos(latitude1) * Math.cos(latitude2) * Math.sin(longitudeDelta / 2) ** 2;
  return earthRadiusKm * 2 * Math.atan2(Math.sqrt(haversine), Math.sqrt(1 - haversine));
}

export function deriveRouteStats(track: GpsTrackPoint[]): DeviceTelemetryBundle["routeStats"] {
  const validTrack = track.filter((point) => point.valid);
  if (validTrack.length < 2) return null;
  const distanceKm = validTrack.slice(1).reduce((total, point, index) => total + pointDistanceKm(validTrack[index]!, point), 0);
  const startedAt = Date.parse(validTrack[0]!.timestamp);
  const endedAt = Date.parse(validTrack.at(-1)!.timestamp);
  const durationMinutes = Math.max(0, (endedAt - startedAt) / 60_000);
  return {
    distanceKm,
    durationMinutes,
    averageSpeedKmh: durationMinutes > 0 ? distanceKm / (durationMinutes / 60) : 0,
  };
}

export function mapTelemetryBundle(
  device: CoolingDevice,
  latestResponse: ApiTelemetryRow | null,
  history: ApiTelemetryRow[],
  gpsRows: ApiGpsRow[],
  events: ApiEventRow[],
): DeviceTelemetryBundle {
  const latest = latestResponse ?? history.at(-1) ?? null;
  const gpsTrack: GpsTrackPoint[] = gpsRows.map((row) => ({
    timestamp: row.received_at,
    latitude: row.latitude,
    longitude: row.longitude,
    valid: row.gps_fix === true,
  }));
  const lastValidGps = [...gpsRows].reverse().find((row) => row.gps_fix === true) ?? gpsRows.at(-1) ?? null;
  const accuracyM = nestedNumber(lastValidGps?.raw_json ?? latest?.raw_json ?? null, "gps", "accuracy_m");
  const fault = hasRecentFault(events);
  const healthScore = deriveHealthScore(device.status, latest, lastValidGps?.gps_fix === true, fault);
  const voltageHistory: TelemetryHistoryPoint[] = history.flatMap((row) => row.bat24_v == null ? [] : [{
    timestamp: row.received_at,
    voltageV: row.bat24_v,
    currentA: row.bat24_i,
  }]);
  const rssiHistory = history.flatMap((row) => {
    const dbm = mapRssiToDbm(row.rssi);
    return dbm == null ? [] : [dbm];
  });

  return {
    telemetry: {
      deviceId: device.id,
      capturedAt: latest?.received_at ?? null,
      healthScore,
      healthStatus: fault || healthScore < 55 ? "fault" : healthScore < 80 ? "warning" : "normal",
      coolingLoadPercent: deriveCoolingLoadPercent(latest),
      rssiDbm: mapRssiToDbm(latest?.rssi ?? null),
      busVoltageV: latest?.bat24_v ?? null,
      busCurrentA: latest?.bat24_i ?? null,
      vehicleInputV: latest?.v12_v ?? null,
      vehicleCharging: latest?.v12_v == null ? null : latest.v12_v >= 13.2,
      gps: {
        latitude: lastValidGps?.latitude ?? null,
        longitude: lastValidGps?.longitude ?? null,
        accuracyM,
        valid: latest?.gps_fix === true,
        updatedSeconds: secondsSince(lastValidGps?.received_at),
      },
      outputs: mapOutputs(latest, device.status),
      ntcChannels: mapNtcChannels(latest, history),
    },
    voltageHistory,
    gpsTrack,
    rssiHistory,
    voltageSparkline: voltageHistory.slice(-18).map((point) => point.voltageV),
    routeStats: deriveRouteStats(gpsTrack),
  };
}
